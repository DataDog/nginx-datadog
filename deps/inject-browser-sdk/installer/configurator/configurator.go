// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache 2.0 License. This product includes software developed at
// Datadog (https://www.datadoghq.com/).
//
// Copyright 2024-Present Datadog, Inc.

package main

import (
	"flag"
	"fmt"
	"os"
	"time"

	log "github.com/sirupsen/logrus"
)

var InstallerVersion = "0.2.0"
var IntegrationVersion = "unset"
var IntegrationName = "unset"

func validateInput(proxyKind, appID, site, clientToken, arch string, sessionSampleRate, sessionReplaySampleRate int) error {

	log.Debug("Validating input arguments")

	if proxyKind != "nginx" && proxyKind != "httpd" {
		return NewInstallerError(ArgumentError, fmt.Errorf("proxyKind must be either 'nginx' or 'httpd', found '%s'", proxyKind))
	}
	if appID == "" {
		return NewInstallerError(ArgumentError, fmt.Errorf("--appId is required"))
	}
	if site == "" {
		return NewInstallerError(ArgumentError, fmt.Errorf("--site is required"))
	}
	if clientToken == "" {
		return NewInstallerError(ArgumentError, fmt.Errorf("--clientToken is required"))
	}
	if sessionSampleRate < 0 || sessionSampleRate > 100 {
		return NewInstallerError(ArgumentError, fmt.Errorf("sessionSampleRate is required and must be between 0 and 100"))
	}
	if sessionReplaySampleRate < 0 || sessionReplaySampleRate > 100 {
		return NewInstallerError(ArgumentError, fmt.Errorf("sessionReplaySampleRate is required and must be between 0 and 100"))
	}
	if arch != "amd64" && arch != "arm64" {
		return NewInstallerError(ArgumentError, fmt.Errorf("arch must be either 'amd64' or 'arm64'"))
	}
	return nil
}

func handleError(err error, sender *TelemetrySender, appId string, dryRun bool) {
	log.Error(err)

	if !dryRun {
		var errorType ErrorType

		if installerErr, ok := err.(*InstallerError); ok {
			errorType = installerErr.ErrorType
		} else {
			errorType = UnexpectedError
		}

		if sender != nil {
			if err := sender.SendLog(LogMessage{
				Message: err.Error(),
				Level:   "ERROR",
				Tags: keyValuePairsAsTags(map[string]string{
					"app_id":              appId,
					"installer_version":   InstallerVersion,
					"integration_name":    IntegrationName,
					"integration_version": IntegrationVersion}),
			}); err != nil {
				log.Error("Error sending error log: ", err)
			}

			if err := sender.SendInstallationCount("error", errorType.String()); err != nil {
				log.Error("Error sending error count: ", err)
			}

			elapsed := time.Since(start)
			if err := sender.SendInstallationTime("error", errorType.String(), elapsed.Milliseconds()); err != nil {
				log.Error("Error sending error distribution: ", err)
			}
		}
	}

	os.Exit(1)
}

var start time.Time

func main() {
	start = time.Now()

	proxyKind := flag.String("proxyKind", "", "Proxy kind to install ('nginx' or 'httpd')")
	appID := flag.String("appId", "", "Application ID")
	site := flag.String("site", "", "Site")
	clientToken := flag.String("clientToken", "", "Client Token")
	sessionSampleRate := flag.Int("sessionSampleRate", -1, "Session Sample Rate (0-100)")
	sessionReplaySampleRate := flag.Int("sessionReplaySampleRate", -1, "Session Replay Sample Rate (0-100)")
	arch := flag.String("arch", "", "Architecture (amd64 or arm64)")
	agentUri := flag.String("agentUri", "http://localhost:8126", "Datadog Agent URI")
	skipVerify := flag.Bool("skipVerify", false, "Skip verifying downloads")
	verbose := flag.Bool("verbose", false, "Verbose output")
	dryRun := flag.Bool("dryRun", false, "Dry run (no changes made)")
	skipDownload := flag.Bool("skipDownload", false, "Skip the download of this installer and use a local binary instead")

	log.Info("Starting installer version ", InstallerVersion)

	flag.Parse()

	if *skipDownload {
		log.Info("Download was skipped, used local binary")
	}

	if *verbose {
		log.SetLevel(log.DebugLevel)
		log.Debug("Verbose output enabled")
	} else {
		log.SetLevel(log.InfoLevel)
	}

	sender, err := NewTelemetrySender(*agentUri, "rum-installer-"+*proxyKind, "prod")
	if err != nil {
		handleError(err, sender, "", *dryRun)
	}

	if *dryRun {
		log.Info("Dry run enabled. No changes will be made.")
	}

	if err := validateInput(*proxyKind, *appID, *site, *clientToken, *arch, *sessionSampleRate, *sessionReplaySampleRate); err != nil {
		handleError(err, sender, *appID, *dryRun)
	}

	var configurator ProxyConfigurator
	switch *proxyKind {
	case "nginx":
		configurator = &NginxConfigurator{}
	case "httpd":
		configurator = &HttpdConfigurator{}
	default:
		handleError(NewInstallerError(ArgumentError, fmt.Errorf("--proxyKind unexpected value")), sender, *appID, *dryRun)
	}

	if err := configurator.VerifyRequirements(); err != nil {
		handleError(err, sender, *appID, *dryRun)
	}

	// TODO: Re-enable once we again have signatures available
	if !*skipVerify {
		log.Debug("Temporarily skipping module signature verification")
		*skipVerify = true
	}

	if err := configurator.DownloadAndInstallModule(*arch, *skipVerify); err != nil {
		handleError(err, sender, *appID, *dryRun)
	}

	if err := configurator.ModifyConfig(*appID, *site, *clientToken, *agentUri, *sessionSampleRate, *sessionReplaySampleRate, *dryRun); err != nil {
		handleError(err, sender, *appID, *dryRun)
	}

	if !*dryRun {
		if err := configurator.ValidateConfig(); err != nil {
			handleError(err, sender, *appID, *dryRun)
		}

		log.Info("Datadog " + *proxyKind + " module has been successfully installed and configured. " + configurator.GetFinalConfigMessage())
	}

	if !*dryRun {
		elapsed := time.Since(start)
		log.Debug("Sending installation telemetry")

		if err := sender.SendInstallationCount("success", ""); err != nil {
			log.Error("Error sending installation success count: ", err)
		}

		if err := sender.SendInstallationTime("success", "", elapsed.Milliseconds()); err != nil {
			log.Error("Error sending installation success distribution: ", err)
		}
	}
}
