// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache 2.0 License. This product includes software developed at
// Datadog (https://www.datadoghq.com/).
//
// Copyright 2024-Present Datadog, Inc.

package main

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strings"

	log "github.com/sirupsen/logrus"
)

type NginxConfigurator struct {
	Version     string
	ModulesPath string
}

// Runs 'nginx -v' and looks for the nginx version.
// Checks that the version is higher than a min version
func (n *NginxConfigurator) VerifyRequirements() error {

	log.Debug("Verifying NGINX requirements")

	IntegrationName = "nginx"

	cmd := exec.Command("nginx", "-v")
	output, err := cmd.CombinedOutput()
	if err != nil {
		return NewInstallerError(NginxError, fmt.Errorf("'nginx' command was not detected. Please ensure NGINX is included in the PATH"))
	}

	log.Debug("Successfully run 'nginx -v': ", string(output))

	versionRegex := regexp.MustCompile(`nginx version: nginx/(\d+\.\d+\.\d+)`)
	matches := versionRegex.FindStringSubmatch(string(output))
	if len(matches) < 2 {
		return NewInstallerError(NginxError, fmt.Errorf("failed to parse NGINX version: %s", output))
	}

	n.Version = matches[1]

	log.Info("Detected NGINX version: ", n.Version)
	IntegrationVersion = n.Version

	return nil
}

// Reads the config file
// 1. Checks if a line containing the module already exists
// 2. Creates a backup of the config file
// 3. Appends the load_module line to the beginning of the file
// 4. Inserts the RUM config as early as possible in the http section
func (n *NginxConfigurator) ModifyConfig(appID, site, clientToken, agentUri string, sessionSampleRate, sessionReplaySampleRate int, dryRun bool) error {

	log.Debug("Modifying NGINX configuration")

	configPath, err := n.getDefaultConfigLocation()
	if err != nil {
		return NewInstallerError(NginxError, fmt.Errorf("failed to get config location: %v", err))
	}

	log.Debug("Found config file at: ", configPath)

	content, err := os.ReadFile(configPath)
	if err != nil {
		return NewInstallerError(NginxError, fmt.Errorf("failed to read config file: %v", err))
	}

	log.Debug("Successfully read config file")

	// Check if Datadog module is already loaded
	if err := n.testNginxLoadsModule(n.ModulesPath + "/ngx_http_datadog_module.so"); err != nil {
		return NewInstallerError(NginxError, fmt.Errorf("datadog module could not be loaded, likely because the module is already in use: %v", err))
	}

	newConfigPath := configPath + ".datadog"
	log.Info(fmt.Sprintf("Modifying NGINX configuration file based on '%s', into intermediate config file '%s'", configPath, newConfigPath))
	newContent, err := transformConfig(n, content, agentUri, appID, clientToken, site, sessionSampleRate, sessionReplaySampleRate, newConfigPath)

	if err != nil {
		return NewInstallerError(NginxError, fmt.Errorf("failed modifying config config: %v", err))
	}

	if err := os.WriteFile(newConfigPath, newContent, 0644); err != nil {
		return NewInstallerError(NginxError, fmt.Errorf("failed to create intermediate config file file: %v", err))
	}

	log.Debug("Created intermediate config file: ", newConfigPath)

	if err := n.validateConfig(newConfigPath); err != nil {
		return NewInstallerError(NginxError, fmt.Errorf("intermediate config '%s' is invalid: %v", newConfigPath, err))
	}

	log.Debug("Intermediate config is valid: ", newConfigPath)

	if dryRun {
		log.Info("Dry run enabled, skipping config file modifications. The config to be used in a non dry run is: ", newConfigPath)
		return nil
	}

	backupPath := configPath + ".backup"
	if err := os.Rename(configPath, backupPath); err != nil {
		return NewInstallerError(NginxError, fmt.Errorf("failed to move file '%s' to '%s': %v", configPath, backupPath, err))
	}

	log.Info(fmt.Sprintf("Moved config '%s' into backup file '%s'", configPath, backupPath))

	if err := os.Rename(newConfigPath, configPath); err != nil {
		return NewInstallerError(NginxError, fmt.Errorf("failed to move '%s' into final config file '%s': %v", newConfigPath, configPath, err))
	}

	log.Debug("Created final config file: ", configPath)

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

	baseURL := "https://rum-auto-instrumentation.s3.us-east-1.amazonaws.com/nginx/latest/"
	moduleURL := baseURL + fmt.Sprintf("ngx_http_datadog_module-%s-%s.so.tgz", arch, n.Version)

	moduleContent, err := downloadFile(moduleURL)
	if err != nil {
		return NewInstallerError(NginxError, fmt.Errorf("failed to download module %s. "+
			"This is likely because this NGINX version (%s) is not supported: %v", moduleURL, n.Version, err))
	}

	tmpDir, err := os.MkdirTemp("", "nginx-module-verification")
	if err != nil {
		return NewInstallerError(NginxError, fmt.Errorf("failed to create temp directory: %v", err))
	}

	log.Debug("Created temp directory: ", tmpDir)

	defer os.RemoveAll(tmpDir)

	moduleFile := filepath.Join(tmpDir, "module.tgz")
	signatureFile := filepath.Join(tmpDir, "module.tgz.asc")
	publicKeyFile := filepath.Join(tmpDir, "pubkey.gpg")

	if err := os.WriteFile(moduleFile, moduleContent, 0644); err != nil {
		return NewInstallerError(NginxError, fmt.Errorf("failed to write module file: %v", err))
	}

	log.Debug("Downloaded module file: ", moduleURL)

	if !skipVerify {
		signatureURL := moduleURL + ".asc"
		signatureContent, err := downloadFile(signatureURL)
		if err != nil {
			return NewInstallerError(NginxError, fmt.Errorf("failed to download signature: %v", err))
		}

		log.Debug("Downloaded signature file: ", signatureURL)

		publicKeyURL := baseURL + "pubkey.gpg"
		publicKeyContent, err := downloadFile(publicKeyURL)
		if err != nil {
			return NewInstallerError(NginxError, fmt.Errorf("failed to download public key: %v", err))
		}

		log.Debug("Downloaded public key file: ", publicKeyURL)

		if err := os.WriteFile(signatureFile, signatureContent, 0644); err != nil {
			return NewInstallerError(NginxError, fmt.Errorf("failed to write signature file: %v", err))
		}

		log.Debug("Wrote signature file: ", signatureURL)

		if err := os.WriteFile(publicKeyFile, publicKeyContent, 0644); err != nil {
			return NewInstallerError(NginxError, fmt.Errorf("failed to write public key file: %v", err))

		}
		log.Debug("Wrote public key file: ", publicKeyURL)

		if err := verifySignatureGPG(moduleFile, signatureFile, publicKeyFile); err != nil {
			return NewInstallerError(NginxError, fmt.Errorf("failed to verify module signature. Bypass verification with --skipVerify: %v", err))
		}

		log.Debug("Verified module signature")
	}

	if err := extractTarGzFile(moduleFile, n.ModulesPath); err != nil {
		return NewInstallerError(NginxError, fmt.Errorf("failed to extract tgz file: %v", err))
	}

	log.Info(fmt.Sprintf("Module successfully downloaded, possibly verified, and installed in %s", n.ModulesPath))
	return nil
}

func (n *NginxConfigurator) validateConfig(configPath string) error {
	var cmd *exec.Cmd

	if configPath != "" {
		cmd = exec.Command("nginx", "-c", configPath, "-t")
	} else {
		cmd = exec.Command("nginx", "-t")
	}

	configInfo := "default location"
	if configPath != "" {
		configInfo = configPath
	}

	output, err := cmd.CombinedOutput()
	if err != nil {
		return NewInstallerError(NginxError, fmt.Errorf("NGINX configuration validation failed for config at '%s' with output: %s. %v", configInfo, output, err))
	}

	log.Debug("Successfully validated nginx configuration at ", configInfo)

	return nil
}

func (n *NginxConfigurator) GetFinalConfigMessage() string {
	return "Please reload NGINX or restart the service for the changes to take effect"
}

// Insert load_module directive at the beginning of the file
// Find http and bracket { on possibly multiple lines
// Insert the datadog configuration after the http block
// Return the modified content
func transformConfig(n *NginxConfigurator, content []byte, agentUri string, appID string, clientToken string, site string, sessionSampleRate int, sessionReplaySampleRate int, destFile string) ([]byte, error) {
	var newContent strings.Builder
	newContent.WriteString(fmt.Sprintf("load_module %s/ngx_http_datadog_module.so;\n", n.ModulesPath))

	httpRegex := regexp.MustCompile(`(?m)^[\s]*http[\s]*\{`)

	httpBlockStart := httpRegex.FindIndex(content)
	if httpBlockStart == nil {
		return nil, NewInstallerError(NginxError, fmt.Errorf("could not find http block in nginx config"))
	}

	newContent.Write(content[:httpBlockStart[0]])

	newContent.Write(content[httpBlockStart[0]:httpBlockStart[1]])
	newContent.WriteString("\n")

	datadogConfig := fmt.Sprintf(`    datadog_agent_url %s;

    # Disable APM Tracing. Remove the next line to enable APM Tracing.
    datadog_tracing off;

    # Enable RUM Injection
    datadog_rum on;

    datadog_rum_config "v5" {
        "applicationId" "%s";
        "clientToken" "%s";
        "site" "%s";
        "sessionSampleRate" "%d";
        "sessionReplaySampleRate" "%d";
    }
`, agentUri, appID, clientToken, site, sessionSampleRate, sessionReplaySampleRate)
	newContent.WriteString(datadogConfig)

	newContent.Write(content[httpBlockStart[1]:])

	return []byte(newContent.String()), nil
}

func (n *NginxConfigurator) testNginxLoadsModule(modulePath string) error {
	// Ensure test without changes succeeds
	if err := n.validateConfig(""); err != nil {
		return NewInstallerError(NginxError, fmt.Errorf("NGINX configuration test failed with the current config: %s", err))
	}

	log.Debug("NGINX default configuration test succeeded before modifications")

	// Test again loading the module
	cmd := exec.Command("nginx", "-g", fmt.Sprintf("load_module %s;", modulePath), "-t")
	output, err := cmd.CombinedOutput()
	if err != nil {
		if strings.Contains(string(output), "already loaded") {
			return NewInstallerError(NginxError, fmt.Errorf("datadog module is already loaded, can't perform installation: %s\n%s", err, string(output)))
		}
		return NewInstallerError(NginxError, fmt.Errorf("NGINX configuration test with module failed with unexpected error: %s\n%s", err, string(output)))
	}

	log.Debug("Successfully validated default configuration with module loaded")

	return nil
}

// Gets the configuration file from 'nginx -T'
func (n *NginxConfigurator) getDefaultConfigLocation() (string, error) {
	log.Debug("Getting default config location running 'nginx -T'")

	cmd := exec.Command("nginx", "-T")
	output, err := cmd.CombinedOutput()
	if err != nil {
		return "", NewInstallerError(NginxError, fmt.Errorf("failed to execute nginx -T: %v. output:\n%s", err, output))
	}

	lines := strings.Split(string(output), "\n")
	for _, line := range lines {
		if strings.HasPrefix(line, "# configuration file ") {
			configFile := strings.TrimPrefix(line, "# configuration file ")
			configFile = strings.TrimSpace(configFile)
			configFile = strings.TrimSuffix(configFile, ":")

			log.Debug("Found default config file: ", configFile)

			return strings.TrimSpace(configFile), nil
		}
	}

	return "", NewInstallerError(NginxError, fmt.Errorf("could not find configuration file in nginx -T output:\n%s", output))
}

func (n *NginxConfigurator) createModulesDir() error {
	modulesPath := "/opt/datadog-nginx"

	if err := os.MkdirAll(modulesPath, 0644); err == nil {
		n.ModulesPath = modulesPath
		log.Debug("Created nginx modules directory: ", modulesPath)

		return nil
	}

	return NewInstallerError(NginxError, fmt.Errorf("could not create nginx modules directory: %s", modulesPath))
}
