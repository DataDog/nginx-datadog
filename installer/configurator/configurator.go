package main

import (
	"flag"
	"fmt"
	"os"

	log "github.com/sirupsen/logrus"
)

func validateInput(appID, site, clientToken, arch string, sessionSampleRate, sessionReplaySampleRate int) error {

	log.Debug("Validating input arguments")

	if appID == "" {
		return fmt.Errorf("--appId is required")
	}
	if site == "" {
		return fmt.Errorf("--site is required")
	}
	if clientToken == "" {
		return fmt.Errorf("--clientToken is required")
	}
	if sessionSampleRate < 0 || sessionSampleRate > 100 {
		return fmt.Errorf("sessionSampleRate is required and must be between 0 and 100")
	}
	if sessionReplaySampleRate < 0 || sessionReplaySampleRate > 100 {
		return fmt.Errorf("sessionReplaySampleRate is required and must be between 0 and 100")
	}
	if arch != "amd64" && arch != "arm64" {
		return fmt.Errorf("arch must be either 'amd64' or 'arm64'")
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
	agentUrl := flag.String("agentUrl", "http://localhost:8126", "Datadog Agent URL")
	skipVerify := flag.Bool("skipVerify", false, "Skip verifying downloads")
	verbose := flag.Bool("verbose", false, "Verbose output")

	flag.Parse()

	if *verbose {
		log.SetLevel(log.DebugLevel)
		log.Debug("Verbose output enabled")
	} else {
		log.SetLevel(log.InfoLevel)
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

	if err := configurator.ModifyConfig(*appID, *site, *clientToken, *agentUrl, *sessionSampleRate, *sessionReplaySampleRate); err != nil {
		handleError(err)
	}

	if err := configurator.ValidateConfig(); err != nil {
		handleError(err)
	}

	log.Info("Datadog NGINX module has been successfully installed and configured. Please restart NGINX for the changes to take effect")
}
