package main

import (
	"flag"
	"fmt"
	"os"
)

func validateInput(appID, site, clientToken, arch, agentUrl string, sessionSampleRate, sessionReplaySampleRate int) error {
	if appID == "" || site == "" || clientToken == "" || agentUrl == "" {
		return fmt.Errorf("appID, site, clientToken, and agentUrl are required")
	}
	if sessionSampleRate < 0 || sessionSampleRate > 100 {
		return fmt.Errorf("sessionSampleRate must be between 0 and 100")
	}
	if sessionReplaySampleRate < 0 || sessionReplaySampleRate > 100 {
		return fmt.Errorf("sessionReplaySampleRate must be between 0 and 100")
	}
	if arch != "amd64" && arch != "arm64" {
		return fmt.Errorf("arch must be either 'amd64' or 'arm64'")
	}
	return nil
}

func handleError(err error) {
	// TODO: Send telemetry

	fmt.Println("Error:", err)

	os.Exit(1)
}

func main() {
	appID := flag.String("appId", "", "Application ID")
	site := flag.String("site", "", "Site")
	clientToken := flag.String("clientToken", "", "Client Token")
	sessionSampleRate := flag.Int("sessionSampleRate", 0, "Session Sample Rate (0-100)")
	sessionReplaySampleRate := flag.Int("sessionReplaySampleRate", 0, "Session Replay Sample Rate (0-100)")
	arch := flag.String("arch", "", "Architecture (amd64 or arm64)")
	agentUrl := flag.String("agentUrl", "", "Datadog Agent URL")

	flag.Parse()

	if err := validateInput(*appID, *site, *clientToken, *arch, *agentUrl, *sessionSampleRate, *sessionReplaySampleRate); err != nil {
		handleError(err)
	}

	var configurator ProxyConfigurator = &NginxConfigurator{}

	if err := configurator.Verify(); err != nil {
		handleError(err)
	}

	if err := configurator.DownloadAndInstallModule(*arch); err != nil {
		handleError(err)
	}

	if err := configurator.ModifyConfig(*appID, *site, *clientToken, *agentUrl, *sessionSampleRate, *sessionReplaySampleRate); err != nil {
		handleError(err)
	}

	if err := configurator.ValidateConfig(); err != nil {
		handleError(err)
	}

	fmt.Println("Configuration completed successfully")
}
