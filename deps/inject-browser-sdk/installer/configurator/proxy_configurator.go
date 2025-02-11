// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache 2.0 License. This product includes software developed at
// Datadog (https://www.datadoghq.com/).
//
// Copyright 2024-Present Datadog, Inc.

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
	// Returns a message to display to the user after the installation, usually indicating to restart the server
	GetFinalConfigMessage() string
}
