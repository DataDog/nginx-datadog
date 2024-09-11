package main

import (
	"fmt"
	"io"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strings"
)

type NginxConfigurator struct {
	Version     string
	ModulesPath string
}

// Runs 'nginx -v' and looks for the nginx version.
// Checks that the version is higher than a min version
func (n *NginxConfigurator) VerifyRequirements() error {
	cmd := exec.Command("nginx", "-v")
	output, err := cmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("nginx was not detected. Please ensure NGINX is included in the PATH")
	}

	versionRegex := regexp.MustCompile(`nginx version: nginx/(\d+\.\d+\.\d+)`)
	matches := versionRegex.FindStringSubmatch(string(output))
	if len(matches) < 2 {
		return fmt.Errorf("failed to parse nginx version: %s", output)
	}

	n.Version = matches[1]

	print("Detected NGINX version: ", n.Version, "\n")

	minSupportedVersion, err := getLowestSupportedModuleVersion()
	if err != nil {
		return fmt.Errorf("could not retrieve the supported versions for nginx-datadog")
	}

	if compareVersions(n.Version, minSupportedVersion) < 0 {
		return fmt.Errorf("nginx version %s is not supported, must be at least %s", n.Version, minSupportedVersion)
	}

	return nil
}

// Reads the config file
// 1. Checks if a line containing the module already exists
// 2. Creates a backup of the config file
// 3. Appends the load_module line to the beginning of the file
// 4. Inserts the RUM config as early as possible in the http section
func (n *NginxConfigurator) ModifyConfig(appID, site, clientToken, agentUrl string, sessionSampleRate, sessionReplaySampleRate int) error {
	configPath, err := n.getDefaultConfigLocation()
	if err != nil {
		return fmt.Errorf("failed to get config location: %v", err)
	}

	content, err := os.ReadFile(configPath)
	if err != nil {
		return fmt.Errorf("failed to read config file: %v", err)
	}

	// Check if Datadog module is already loaded
	if err := n.testNginxLoadsModule(n.ModulesPath + "/ngx_http_datadog_module.so"); err != nil {
		return fmt.Errorf("failed to load module: %v", err)
	}

	newConfigPath := configPath + ".datadog"
	fmt.Printf("Modifying NGINX configuration file based on '%s', into intermediate config file '%s'\n", configPath, newConfigPath)
	newContent, err := transformConfig(n, content, agentUrl, appID, clientToken, site, sessionSampleRate, sessionReplaySampleRate, newConfigPath)

	if err != nil {
		return fmt.Errorf("failed modifying config config: %v", err)
	}

	if err := os.WriteFile(newConfigPath, newContent, 0644); err != nil {
		return fmt.Errorf("failed to create intermediate config file file: %v", err)
	}

	if err := n.validateConfig(newConfigPath); err != nil {
		return fmt.Errorf("intermediate config '%s' is invalid: %v", newConfigPath, err)
	}

	if err := os.Remove(newConfigPath); err != nil {
		return fmt.Errorf("failed to remove intermediate config file: %v", err)
	}

	fmt.Printf("Removed intermediate file %s\n", newConfigPath)

	// Create backup
	backupPath := configPath + ".backup"
	if err := os.WriteFile(backupPath, content, 0644); err != nil {
		return fmt.Errorf("failed to create backup file: %v", err)
	}
	fmt.Printf("Backup config created: %s\n", backupPath)

	if err := os.WriteFile(configPath, newContent, 0644); err != nil {
		return fmt.Errorf("failed to create final config file file: %v", err)
	}

	return nil
}

// Runs 'nginx -t' to validate the configuration
func (n *NginxConfigurator) ValidateConfig() error {
	return n.validateConfig("")
}

// Downloads the latest nginx-datadog for the specified architecture
// Extracts the .so file and places it in the modules path
func (n *NginxConfigurator) DownloadAndInstallModule(arch string, skipVerify bool) error {
	if err := n.createModulesDir(); err != nil {
		return err
	}

	moduleURL := fmt.Sprintf("https://github.com/DataDog/nginx-datadog/releases/latest/download/ngx_http_datadog_module-%s-%s.so.tgz", arch, n.Version)

	moduleContent, err := downloadFile(moduleURL)
	if err != nil {
		return fmt.Errorf("failed to download module: %v", err)
	}

	tmpDir, err := os.MkdirTemp("", "nginx-module-verification")
	if err != nil {
		return fmt.Errorf("failed to create temp directory: %v", err)
	}
	defer os.RemoveAll(tmpDir)

	moduleFile := filepath.Join(tmpDir, "module.tgz")
	signatureFile := filepath.Join(tmpDir, "module.tgz.asc")
	publicKeyFile := filepath.Join(tmpDir, "pubkey.gpg")

	if err := os.WriteFile(moduleFile, moduleContent, 0644); err != nil {
		return fmt.Errorf("failed to write module file: %v", err)
	}

	if !skipVerify {
		signatureURL := moduleURL + ".asc"
		signatureContent, err := downloadFile(signatureURL)
		if err != nil {
			return fmt.Errorf("failed to download signature: %v", err)
		}

		publicKeyURL := "https://github.com/DataDog/nginx-datadog/releases/latest/download/pubkey.gpg"
		publicKeyContent, err := downloadFile(publicKeyURL)
		if err != nil {
			return fmt.Errorf("failed to download public key: %v", err)
		}

		if err := os.WriteFile(signatureFile, signatureContent, 0644); err != nil {
			return fmt.Errorf("failed to write signature file: %v", err)
		}
		if err := os.WriteFile(publicKeyFile, publicKeyContent, 0644); err != nil {
			return fmt.Errorf("failed to write public key file: %v", err)
		}

		if err := verifySignatureGPG(moduleFile, signatureFile, publicKeyFile); err != nil {
			return fmt.Errorf("failed to verify module signature: %v", err)
		}
	}

	if err := extractTarGzFile(moduleFile, n.ModulesPath); err != nil {
		return fmt.Errorf("failed to extract tgz file: %v", err)
	}

	fmt.Printf("Module successfully downloaded, possibly verified, and installed in %s\n", n.ModulesPath)
	return nil
}

func (n *NginxConfigurator) validateConfig(configPath string) error {
	var cmd *exec.Cmd

	if configPath != "" {
		cmd = exec.Command("nginx", "-c", configPath, "-t")
	} else {
		cmd = exec.Command("nginx", "-t")
	}

	output, err := cmd.CombinedOutput()
	if err != nil {
		configInfo := "default location"
		if configPath != "" {
			configInfo = configPath
		}
		return fmt.Errorf("nginx configuration validation failed for config at '%s' with output:\n%s", configInfo, output)
	}
	return nil

}

// Insert load_module directive at the beginning of the file
// Find http and bracket { on possibly multiple lines
// Insert the datadog configuration after the http block
// Return the modified content
func transformConfig(n *NginxConfigurator, content []byte, agentUrl string, appID string, clientToken string, site string, sessionSampleRate int, sessionReplaySampleRate int, destFile string) ([]byte, error) {
	var newContent strings.Builder
	newContent.WriteString(fmt.Sprintf("load_module %s/ngx_http_datadog_module.so;\n", n.ModulesPath))

	httpRegex := regexp.MustCompile(`(?m)^[\s]*http[\s]*\{`)

	httpBlockStart := httpRegex.FindIndex(content)
	if httpBlockStart == nil {
		return nil, fmt.Errorf("could not find http block in nginx config")
	}

	newContent.Write(content[:httpBlockStart[0]])

	newContent.Write(content[httpBlockStart[0]:httpBlockStart[1]])
	newContent.WriteString("\n")

	datadogConfig := fmt.Sprintf(`    datadog_agent_url %s;

    # Disable APM Tracing. Remove the next line to enable APM Tracing.
    datadog_disable;

    # Enable RUM Injection
    datadog_rum on;

    datadog_rum_config "v5" {
        "applicationId" "%s";
        "clientToken" "%s";
        "site" "%s";
        "sessionSampleRate" "%d";
        "sessionReplaySampleRate" "%d";
    }
`, agentUrl, appID, clientToken, site, sessionSampleRate, sessionReplaySampleRate)
	newContent.WriteString(datadogConfig)

	newContent.Write(content[httpBlockStart[1]:])

	return []byte(newContent.String()), nil
}

func (n *NginxConfigurator) testNginxLoadsModule(modulePath string) error {
	// Ensure test without changes succeeds
	if err := n.validateConfig(""); err != nil {
		return fmt.Errorf("nginx configuration test failed with the current config: %s", err)
	}

	// Test again loading the module
	cmd := exec.Command("nginx", "-g", fmt.Sprintf("load_module %s;", modulePath), "-t")
	output, err := cmd.CombinedOutput()
	if err != nil {
		if strings.Contains(string(output), "already loaded") {
			return fmt.Errorf("datadog module is already loaded, can't perform installation: %s\n%s", err, string(output))
		}
		return fmt.Errorf("nginx configuration test with module failed with unexpected error: %s\n%s", err, string(output))
	}

	return nil
}

func verifySignatureGPG(moduleFile, signatureFile, publicKeyFile string) error {
	importCmd := exec.Command("gpg", "--import", publicKeyFile)
	if output, err := importCmd.CombinedOutput(); err != nil {
		return fmt.Errorf("failed to import public key: %v\nOutput: %s", err, output)
	}

	verifyCmd := exec.Command("gpg", "--verify", signatureFile, moduleFile)
	if output, err := verifyCmd.CombinedOutput(); err != nil {
		return fmt.Errorf("signature verification failed: %v\nOutput: %s", err, output)
	}

	return nil
}

// Helper function to download a file
func downloadFile(url string) ([]byte, error) {
	resp, err := http.Get(url)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	return io.ReadAll(resp.Body)
}

// Gets the configuration file from 'nginx -T'
func (n *NginxConfigurator) getDefaultConfigLocation() (string, error) {
	cmd := exec.Command("nginx", "-T")
	output, err := cmd.CombinedOutput()
	if err != nil {
		return "", fmt.Errorf("failed to execute nginx -T: %v. output:\n%s", err, output)
	}

	lines := strings.Split(string(output), "\n")
	for _, line := range lines {
		if strings.HasPrefix(line, "# configuration file ") {
			configFile := strings.TrimPrefix(line, "# configuration file ")
			configFile = strings.TrimSpace(configFile)
			configFile = strings.TrimSuffix(configFile, ":")
			return strings.TrimSpace(configFile), nil
		}
	}

	return "", fmt.Errorf("could not find configuration file in nginx -T output:\n%s", output)
}

func (n *NginxConfigurator) createModulesDir() error {
	modulesPath := "/datadog"

	if err := os.MkdirAll(modulesPath, 0644); err == nil {
		n.ModulesPath = modulesPath
		return nil
	}

	return fmt.Errorf("could not create nginx modules directory: %s", modulesPath)
}
