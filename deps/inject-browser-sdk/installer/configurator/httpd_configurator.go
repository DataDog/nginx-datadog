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

const InstallPath = "/opt/datadog-httpd"

type HttpdConfigurator struct {
	Version    string
	ConfigPath string
	Command    string
}

func (h *HttpdConfigurator) VerifyRequirements() error {

	log.Debug("Verifying httpd requirements")

	IntegrationName = "httpd"

	commands := []string{"httpd", "apachectl", "apache2", "apache2ctl"}
	found := false

	for _, cmd := range commands {
		if commandExists(cmd) {
			log.Debug(fmt.Sprintf("%s command found", cmd))
			h.Command = cmd
			found = true
			break
		}
	}

	if !found {
		return NewInstallerError(
			HttpdError,
			fmt.Errorf(
				"unsuccessfully attempted to find the httpd front end binary, "+
					"tried %s. Please ensure one of those binaries is included in the $PATH",
				strings.Join(commands, ", "),
			),
		)
	}

	log.Debugf("Running '%s -V'", h.Command)
	cmd := exec.Command(h.Command, "-V")
	output, err := cmd.CombinedOutput()
	if err != nil {
		return NewInstallerError(HttpdError, fmt.Errorf("error running command '%s -V': %s", h.Command, string(output)))
	}

	log.Debugf("Successfully run '%s -V': %s", h.Command, string(output))

	versionRegex := regexp.MustCompile(`Server version: Apache/(\d+\.\d+\.\d+)`)
	matches := versionRegex.FindStringSubmatch(string(output))
	if len(matches) < 2 {
		return NewInstallerError(HttpdError, fmt.Errorf("failed to parse httpd version: %s", output))
	}

	h.Version = matches[1]

	log.Info("Detected httpd version: ", h.Version)
	IntegrationVersion = h.Version

	minSupportedVersion := "2.4.0"

	log.Debug("Minimum supported httpd version: ", minSupportedVersion)

	if compareVersions(h.Version, minSupportedVersion) < 0 {
		return NewInstallerError(HttpdError, fmt.Errorf("httpd version %s is not supported, must be higher than %s", h.Version, minSupportedVersion))
	}

	log.Info("The installed httpd version is supported")

	return nil
}

func (h *HttpdConfigurator) ModifyConfig(appID, site, clientToken, agentUri string, sessionSampleRate, sessionReplaySampleRate int, dryRun bool) error {

	log.Debug("Modifying httpd configuration")

	configPath, err := h.getDefaultConfigLocation()
	if err != nil {
		return NewInstallerError(HttpdError, fmt.Errorf("failed to get config location: %v", err))
	}

	h.ConfigPath = configPath
	log.Debug("Found config file at: ", h.ConfigPath)

	customConfPath := fmt.Sprintf("%s/datadog.conf", InstallPath)
	log.Info("Creating .conf file with RUM configuration in ", customConfPath)
	content := datadogConfigFile(agentUri, appID, clientToken, site, sessionSampleRate, sessionReplaySampleRate)

	os.WriteFile(customConfPath, []byte(content), 0644)

	if err := h.transformConfigHttpd(configPath, dryRun); err != nil {
		return err
	}

	return nil
}

func (h *HttpdConfigurator) ValidateConfig() error {

	if err := h.runConfigTest(h.ConfigPath); err != nil {
		return NewInstallerError(HttpdError, fmt.Errorf("config test failed: %w", err))
	}

	return nil
}

func (h *HttpdConfigurator) DownloadAndInstallModule(arch string, skipVerify bool) error {
	if err := h.createInstallDir(); err != nil {
		return err
	}

	moduleURL := fmt.Sprintf("https://rum-auto-instrumentation.s3.us-east-1.amazonaws.com/httpd/latest/mod_datadog-%s.zip", arch)

	moduleContent, err := downloadFile(moduleURL)
	if err != nil {
		return NewInstallerError(HttpdError, fmt.Errorf("failed to download module: %v", err))
	}

	log.Debug("Creating temp directory")

	tmpDir, err := os.MkdirTemp("", "httpd-module-verification")
	if err != nil {
		return NewInstallerError(HttpdError, fmt.Errorf("failed to create temp directory: %v", err))
	}

	log.Debug("Created temp directory: ", tmpDir)

	defer os.RemoveAll(tmpDir)

	moduleFile := filepath.Join(tmpDir, "httpd-rum.zip")
	signatureFile := filepath.Join(tmpDir, "httpd-rum.zip.asc")
	pubKeyFile := filepath.Join(tmpDir, "pubkey.gpg")

	if err := os.WriteFile(moduleFile, moduleContent, 0644); err != nil {
		return NewInstallerError(HttpdError, fmt.Errorf("failed to write module file: %v", err))
	}

	log.Debug("Downloaded and wrote module file: ", moduleURL)

	if !skipVerify {
		signatureURL := moduleURL + ".asc"
		signatureContent, err := downloadFile(signatureURL)
		if err != nil {
			return NewInstallerError(HttpdError, fmt.Errorf("failed to download signature: %v", err))
		}

		log.Debug("Downloaded signature file: ", signatureURL)

		if err := os.WriteFile(signatureFile, signatureContent, 0644); err != nil {
			return NewInstallerError(HttpdError, fmt.Errorf("failed to write signature file: %v", err))
		}

		log.Debug("Wrote signature file: ", signatureURL)

		pubKeyContent, err := downloadFile("https://keyserver.ubuntu.com/pks/lookup?op=get&search=0x2ed57717a223b2edf3281c427c74ee19de454d68")
		if err != nil {
			return NewInstallerError(HttpdError, fmt.Errorf("failed to download public key file: %v", err))
		}

		log.Debug("Downloaded public key file")

		if err := os.WriteFile(pubKeyFile, pubKeyContent, 0644); err != nil {
			return NewInstallerError(HttpdError, fmt.Errorf("failed to write public key file: %v", err))
		}

		if err := verifySignatureGPG(moduleFile, signatureFile, pubKeyFile); err != nil {
			return NewInstallerError(HttpdError, fmt.Errorf("failed to verify module signature. Bypass verification with --skipVerify: %v", err))
		}

		log.Debug("Verified module signature")
	}

	if err := unzipFile(moduleFile, InstallPath); err != nil {
		return NewInstallerError(HttpdError, fmt.Errorf("failed to unzip file: %v", err))
	}

	log.Infof("Module successfully downloaded, possibly verified, and installed in '%s'", InstallPath)
	return nil
}

func (n *HttpdConfigurator) GetFinalConfigMessage() string {
	return "Please gracefully restart httpd (or hard restart) for the changes to take effect"
}

func (h *HttpdConfigurator) getDefaultConfigLocation() (string, error) {
	log.Debugf("Getting config location running '%s -V'", h.Command)

	cmd := exec.Command(h.Command, "-V")
	output, err := cmd.CombinedOutput()
	if err != nil {
		return "", NewInstallerError(HttpdError, fmt.Errorf("failed to execute '%s -V': %v. Output:\n%s", h.Command, err, output))
	}

	pattern := regexp.MustCompile(`SERVER_CONFIG_FILE="([^"]+)"`)
	matches := pattern.FindStringSubmatch(string(output))

	if len(matches) < 2 {
		return "", NewInstallerError(HttpdError, fmt.Errorf("failed to find SERVER_CONFIG_FILE in: %s", output))
	}

	configPath := matches[1]

	// Absolute path
	if strings.HasPrefix(matches[1], "/") {
		return configPath, nil
	}

	log.Debugf("Detected relative path for config '%s', looking for root path", configPath)

	pattern = regexp.MustCompile(`HTTPD_ROOT="([^"]+)"`)
	matches = pattern.FindStringSubmatch(string(output))

	if len(matches) < 2 {
		return "", NewInstallerError(HttpdError, fmt.Errorf("failed to find HTTPD_ROOT in: %s", output))
	}

	log.Debug("Found root path: ", matches[1])

	return filepath.Join(matches[1], configPath), nil
}

func (h *HttpdConfigurator) createInstallDir() error {
	if err := os.MkdirAll(InstallPath, 0644); err == nil {
		log.Debug("Created installation directory: ", InstallPath)

		return nil
	}

	return NewInstallerError(HttpdError, fmt.Errorf("could not create httpd installation directory: %s", InstallPath))
}

func (h *HttpdConfigurator) transformConfigHttpd(src string, dryRun bool) error {
	configLine := fmt.Sprintf("Include %s/datadog.conf", InstallPath)

	err := h.runConfigTest(src)
	if err != nil {
		return NewInstallerError(HttpdError, fmt.Errorf("error running config test before modifications to the system, can't continue until fixed: %w", err))
	}

	content, err := os.ReadFile(src)
	if err != nil {
		return NewInstallerError(HttpdError, fmt.Errorf("error reading file: %w", err))
	}

	if strings.Contains(string(content), configLine) {
		return NewInstallerError(HttpdError, fmt.Errorf("datadog module is already installed, can't perform installation"))
	}

	ddogFile := src + ".datadog"
	log.Debug("Writing the modified config file in ", ddogFile)
	newContent := fmt.Sprintf("%s\n%s\n", string(content), configLine)

	err = os.WriteFile(ddogFile, []byte(newContent), 0644)
	if err != nil {
		return fmt.Errorf("error writing new configuration file: %w", err)
	}

	defer os.Remove(ddogFile)

	if err := h.runConfigTest(ddogFile); err != nil {
		return NewInstallerError(HttpdError, fmt.Errorf("error running config test after modifications to the system: %w", err))
	}

	if dryRun {
		log.Info("Dry run enabled, skipping config file modifications.")
		return nil
	}

	backupFile := src + ".bak"
	log.Debug("Backing up original configuration to ", backupFile)

	err = os.Rename(src, backupFile)
	if err != nil {
		return fmt.Errorf("error moving to backup file: %w", err)
	}

	log.Info("Backed up original configuration to ", backupFile)

	err = os.Rename(ddogFile, src)
	if err != nil {
		return fmt.Errorf("error moving datadog modified file '%s' to original configuration path '%s': %w", ddogFile, src, err)
	}

	log.Info("Configuration updated successfully")

	return nil
}

func (h *HttpdConfigurator) runConfigTest(configFile string) error {
	log.Debugf("Testing configuration: %s -t -f %s", h.Command, configFile)
	cmd := exec.Command(h.Command, "-t", "-f", configFile)
	output, err := cmd.CombinedOutput()
	if err != nil {
		return NewInstallerError(HttpdError, fmt.Errorf("config test failed: %w. Output: %s", err, output))
	}
	log.Info("Config validated successfully: ", configFile)

	return nil
}

func datadogConfigFile(agentUri, appID, clientToken, site string, sessionSampleRate, sessionReplaySampleRate int) string {
	return fmt.Sprintf(`LoadModule datadog_module %s/mod_datadog.so
DatadogTracing Off
DatadogRum On
DatadogAgentUrl "%s"
<DatadogRumSettings>
	DatadogRumOption "applicationId" "%s"
	DatadogRumOption "clientToken" "%s"
	DatadogRumOption "site" "%s"
	DatadogRumOption "sessionSampleRate" "%d"
	DatadogRumOption "sessionReplaySampleRate" "%d"
</DatadogRumSettings>`,
		InstallPath, agentUri, appID, clientToken, site, sessionSampleRate, sessionReplaySampleRate)
}
