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

type HttpdConfigurator struct {
	Version     string
	ModulesPath string
	ConfigPath  string
	Command     string
}

func (h *HttpdConfigurator) VerifyRequirements() error {

	log.Debug("Verifying httpd requirements")

	IntegrationName = "httpd"

	if commandExists("httpd") {
		log.Debug("httpd command found")
		h.Command = "httpd"
	} else if commandExists("apachectl") {
		log.Debug("apachectl command found")
		h.Command = "apachectl"
	} else if commandExists("apache2") {
		log.Debug("apache2 command found")
		h.Command = "apache2"
	} else if commandExists("apache2ctl") {
		log.Debug("apache2ctl command found")
		h.Command = "apache2ctl"
	} else {
		return NewInstallerError(
			HttpdError,
			fmt.Errorf(
				"unsuccessfully attempted to find the httpd front end binary, "+
					"tried 'httpd', 'apachectl', 'apache2' and 'apache2ctl'. "+
					"Please ensure one of those binaries is included in the $PATH",
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

	customConfPath := "/opt/datadog-httpd/datadog.conf"
	log.Info("Creating .conf file with RUM configuration in ", customConfPath)
	content := fmt.Sprintf(`LoadModule datadog_module /opt/datadog-httpd/mod_datadog.so
DatadogTracing Off
DatadogRum On
DatadogRumSetting "applicationId" %s
DatadogRumSetting "clientToken" %s
DatadogRumSetting "site" %s
DatadogRumSetting "sessionSampleRate" %d
DatadogRumSetting "sessionReplaySampleRate" %d`,
		appID, clientToken, site, sessionSampleRate, sessionReplaySampleRate)

	os.WriteFile(customConfPath, []byte(content), 0644)

	if err := h.transformConfigHttpd(configPath, agentUri, appID, clientToken, site, sessionSampleRate, sessionReplaySampleRate); err != nil {
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
	if err := h.createModulesDir(); err != nil {
		return err
	}

	moduleURL := fmt.Sprintf("https://ddagent-windows-unstable.s3.amazonaws.com/inject-browser-sdk/httpd/de425e/httpd-rum-%s.zip", arch)

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

	// TODO
	// signatureFile := filepath.Join(tmpDir, "http-rum.zip.asc")
	// pubKeyFile := filepath.Join(tmpDir, "pubkey.gpg")

	if err := os.WriteFile(moduleFile, moduleContent, 0644); err != nil {
		return NewInstallerError(HttpdError, fmt.Errorf("failed to write module file: %v", err))
	}

	log.Debug("Downloaded and wrote module file: ", moduleURL)

	if !skipVerify {
		// TODO

		// signatureURL := moduleURL + ".asc"
		// signatureContent, err := downloadFile(signatureURL)
		// if err != nil {
		// 	return NewInstallerError(HttpdError, fmt.Errorf("failed to download signature: %v", err))
		// }

		// log.Debug("Downloaded signature file: ", signatureURL)

		// if err := os.WriteFile(signatureFile, signatureContent, 0644); err != nil {
		// 	return NewInstallerError(HttpdError, fmt.Errorf("failed to write signature file: %v", err))
		// }

		// log.Debug("Wrote signature file: ", signatureURL)

		// pubKeyContent, err := downloadFile("https://keyserver.ubuntu.com/pks/lookup?op=get&search=0x2ed57717a223b2edf3281c427c74ee19de454d68")
		// if err != nil {
		// 	return NewInstallerError(HttpdError, fmt.Errorf("failed to download public key file: %v", err))
		// }

		// log.Debug("Downloaded public key file")

		// if err := os.WriteFile(pubKeyFile, pubKeyContent, 0644); err != nil {
		// 	return NewInstallerError(HttpdError, fmt.Errorf("failed to write public key file: %v", err))
		// }

		// if err := verifySignatureGPG(moduleFile, signatureFile, pubKeyFile); err != nil {
		// 	return NewInstallerError(HttpdError, fmt.Errorf("failed to verify module signature: %v", err))
		// }

		// log.Debug("Verified module signature")
	}

	if err := unzipFile(moduleFile, h.ModulesPath); err != nil {
		return NewInstallerError(HttpdError, fmt.Errorf("failed to unzip file: %v", err))
	}

	log.Infof("Module successfully downloaded, possibly verified, and installed in '%s'", h.ModulesPath)
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

func (h *HttpdConfigurator) createModulesDir() error {
	modulesPath := "/opt/datadog-httpd"

	if err := os.MkdirAll(modulesPath, 0644); err == nil {
		h.ModulesPath = modulesPath
		log.Debug("Created httpd modules directory: ", modulesPath)

		return nil
	}

	return NewInstallerError(HttpdError, fmt.Errorf("could not create httpd modules directory: %s", modulesPath))
}

func (h *HttpdConfigurator) transformConfigHttpd(src string, agentUri string, appID string, clientToken string, site string, sessionSampleRate int, sessionReplaySampleRate int) error {
	configLine := "Include /opt/datadog-httpd/datadog.conf"

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
	newContent := string(content) + "\n" + configLine + "\n" // TODO: Is there a better way to do this?

	err = os.WriteFile(ddogFile, []byte(newContent), 0644)
	if err != nil {
		return fmt.Errorf("error writing new configuration file: %w", err)
	}

	defer os.Remove(ddogFile)

	if err := h.runConfigTest(ddogFile); err != nil {
		return NewInstallerError(HttpdError, fmt.Errorf("error running config test after modifications to the system: %w", err))
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
	log.Debugf("Testinf configuration: %s -t -f %s", h.Command, configFile)
	cmd := exec.Command(h.Command, "-t", "-f", configFile)
	output, err := cmd.CombinedOutput()
	if err != nil {
		return NewInstallerError(HttpdError, fmt.Errorf("config test failed: %w. Output: %s", err, output))
	}
	log.Info("Config validated successfully: ", configFile)

	return nil
}

// TODO: Remove
func runCommand(name string, args ...string) {
	log.Debug("Running command: ", name)
	cmd := exec.Command(name, args...)
	output, err := cmd.CombinedOutput()
	if err != nil {
		log.Error("Failed to run command: %v", err)
	}
	log.Debug("Command result: ", string(output))
}
