package main

import (
	"flag"
	"fmt"
	"os"

	log "github.com/sirupsen/logrus"
)

var InstallerVersion = "0.1.1"

func validateInput(appID, site, clientToken, arch string, sessionSampleRate, sessionReplaySampleRate int) error {

	log.Debug("Validating input arguments")

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

func handleError(err error) {
	// TODO: Send telemetry

	log.Error(err)

	os.Exit(1)
}

func main() {
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

	log.Info("Starting installer version ", InstallerVersion)

	if *dryRun {
		log.Info("Dry run enabled. No changes will be made.")
	}

	if err := validateInput(*appID, *site, *clientToken, *arch, *sessionSampleRate, *sessionReplaySampleRate); err != nil {
		handleError(err)
	}

	var configurator ProxyConfigurator = &NginxConfigurator{}

	if err := configurator.VerifyRequirements(); err != nil {
		handleError(err)
	}

	if err := configurator.DownloadAndInstallModule(*arch, *skipVerify); err != nil {
		handleError(err)
	}

	if err := configurator.ModifyConfig(*appID, *site, *clientToken, *agentUri, *sessionSampleRate, *sessionReplaySampleRate, *dryRun); err != nil {
		handleError(err)
	}

	if !*dryRun {
		if err := configurator.ValidateConfig(); err != nil {
			handleError(err)
		}

		log.Info("Datadog NGINX module has been successfully installed and configured. Please reload NGINX or restart the service for the changes to take effect")
	}
}
