package main

// ProxyConfigurator interface defines the methods that any proxy configurator should implement
type ProxyConfigurator interface {
	// Verifies that the proxy is installed and compatible
	VerifyRequirements() error
	// Downloads and installs necessary files for the injection
	DownloadAndInstallModule(arch string, skipVerify bool) error
	// Does whatever config modification is necessary to inject the RUM agent
	ModifyConfig(appID, site, clientToken, agentUri string, sessionSampleRate, sessionReplaySampleRate int, dryRun bool) error
	// Validates the proxy still works after the config modifications
	ValidateConfig() error
}
