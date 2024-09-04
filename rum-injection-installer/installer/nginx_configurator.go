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
func (n *NginxConfigurator) Verify() error {
	cmd := exec.Command("nginx", "-v")
	output, err := cmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("nginx is not installed")
	}

	versionRegex := regexp.MustCompile(`nginx version: nginx/(\d+\.\d+\.\d+)`)
	matches := versionRegex.FindStringSubmatch(string(output))
	if len(matches) < 2 {
		return fmt.Errorf("failed to parse nginx version: %s", output)
	}

	n.Version = matches[1]

	print("Detected NGINX version: ", n.Version, "\n")

	const minSupportedVersion = "1.22.0"
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
	configPath, err := n.getConfigLocation()
	if err != nil {
		return fmt.Errorf("failed to get config location: %v", err)
	}

	content, err := os.ReadFile(configPath)
	if err != nil {
		return fmt.Errorf("failed to read config file: %v", err)
	}

	// Check if Datadog module is already loaded
	if strings.Contains(string(content), "ngx_http_datadog_module.so") {
		return fmt.Errorf("datadog module is already loaded in the configuration")
	}

	// Create backup
	backupPath := configPath + ".backup"
	if err := os.WriteFile(backupPath, content, 0644); err != nil {
		return fmt.Errorf("failed to create backup file: %v", err)
	}
	fmt.Printf("Backup created: %s\n", backupPath)

	var newContent strings.Builder
	newContent.WriteString(fmt.Sprintf("load_module %s/ngx_http_datadog_module.so;\n", n.ModulesPath))

	// Find http and bracket { on possibly multiple lines
	httpRegex := regexp.MustCompile(`(?m)^[\s]*http[\s]*\{`)

	httpBlockStart := httpRegex.FindIndex(content)
	if httpBlockStart == nil {
		return fmt.Errorf("could not find http block in nginx config")
	}

	newContent.Write(content[:httpBlockStart[0]])

	newContent.Write(content[httpBlockStart[0]:httpBlockStart[1]])
	newContent.WriteString("\n")

	datadogConfig := fmt.Sprintf(`    datadog_agent_url %s;
    datadog_disable;
    datadog_rum on;
    datadog_rum_config "v5" {
        "applicationId" "%s";
        "clientToken" "%s";
        "site" "%s";
        "sessionSampleRate" %d;
        "sessionReplaySampleRate" %d;
    }
`, agentUrl, appID, clientToken, site, sessionSampleRate, sessionReplaySampleRate)
	newContent.WriteString(datadogConfig)

	newContent.Write(content[httpBlockStart[1]:])

	// Write the modified content back to the file
	if err := os.WriteFile(configPath, []byte(newContent.String()), 0644); err != nil {
		return fmt.Errorf("failed to write modified config: %v", err)
	}

	fmt.Printf("Successfully modified NGINX configuration file: %s\n", configPath)
	return nil
}

// Runs 'nginx -t' to validate the configuration
func (n *NginxConfigurator) ValidateConfig() error {
	cmd := exec.Command("nginx", "-t")
	output, err := cmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("nginx configuration validation failed with output:\n%s", output)
	}
	return nil
}

// Downloads the latest nginx-datadog for the specified architecture
// Extracts the .so file and places it in the modules path
func (n *NginxConfigurator) DownloadAndInstallModule(arch string) error {
	if err := n.getModulesPath(); err != nil {
		return err
	}

	moduleURL := fmt.Sprintf("https://github.com/DataDog/nginx-datadog/releases/latest/download/ngx_http_datadog_module-%s-%s.so.tgz", arch, n.Version)

	moduleContent, err := downloadFile(moduleURL)
	if err != nil {
		return fmt.Errorf("failed to download module: %v", err)
	}

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
	if err := os.WriteFile(signatureFile, signatureContent, 0644); err != nil {
		return fmt.Errorf("failed to write signature file: %v", err)
	}
	if err := os.WriteFile(publicKeyFile, publicKeyContent, 0644); err != nil {
		return fmt.Errorf("failed to write public key file: %v", err)
	}

	if err := verifySignatureGPG(moduleFile, signatureFile, publicKeyFile); err != nil {
		return fmt.Errorf("failed to verify module signature: %v", err)
	}

	if err := extractTarGzFile(moduleFile, n.ModulesPath); err != nil {
		return fmt.Errorf("failed to extract tgz file: %v", err)
	}

	fmt.Printf("Module successfully downloaded, verified, and installed in %s\n", n.ModulesPath)
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
func (n *NginxConfigurator) getConfigLocation() (string, error) {
	cmd := exec.Command("nginx", "-T")
	output, err := cmd.CombinedOutput()
	if err != nil {
		return "", fmt.Errorf("failed to execute nginx -T: %v", err)
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

	return "", fmt.Errorf("could not find configuration file in nginx -T output")
}

// Tries to find a modules path by different means, exits if one method works
// 1. Run 'nginx -V' and look for --modules-path if nginx was built using it
// 2. Check common paths, see if the directory exists
// 3. With the output from 'nginx -V' uses the prefix to guess the modules path
func (n *NginxConfigurator) getModulesPath() error {
	cmd := exec.Command("nginx", "-V")
	output, err := cmd.CombinedOutput()
	if err == nil {
		modulePathRegex := regexp.MustCompile(`--modules-path=(\S+)`)
		matches := modulePathRegex.FindStringSubmatch(string(output))
		if len(matches) > 1 {
			path := matches[1]
			if _, err := os.Stat(path); err == nil {
				n.ModulesPath = path
				return nil
			}
		}
	}

	commonPaths := []string{
		"/usr/lib/nginx/modules",
		"/usr/local/lib/nginx/modules",
		"/opt/nginx/modules",
	}

	for _, path := range commonPaths {
		if _, err := os.Stat(path); err == nil {
			n.ModulesPath = path
			return nil
		}
	}

	prefixRegex := regexp.MustCompile(`--prefix=(\S+)`)
	matches := prefixRegex.FindStringSubmatch(string(output))
	if len(matches) >= 2 {
		modulesPath := filepath.Join(matches[1], "modules")

		if err := os.MkdirAll(modulesPath, 0755); err == nil {
			n.ModulesPath = modulesPath
			return nil
		}
	}

	return fmt.Errorf("could not guess an appropriate nginx modules path")
}
