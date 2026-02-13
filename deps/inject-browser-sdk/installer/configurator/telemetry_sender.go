// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache 2.0 License. This product includes software developed at
// Datadog (https://www.datadoghq.com/).
//
// Copyright 2024-Present Datadog, Inc.

package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"net/http"
	"os"
	"os/exec"
	"runtime"
	"strings"
	"time"

	"github.com/google/uuid"
	log "github.com/sirupsen/logrus"
)

const (
	AgentEndpoint = "/telemetry/proxy/api/v2/apmtelemetry"
	APIVersion    = "v2"
	NA            = "unavailable"
)

type HTTPClient interface {
	Do(req *http.Request) (*http.Response, error)
}

type TelemetrySender struct {
	AgentEndpoint    string
	RuntimeID        string
	Application      Application
	Host             Host
	client           HTTPClient
	seqID            int
	telemetryEnabled bool
	metricsEnabled   bool
	logsEnabled      bool
}

type Application struct {
	ServiceName     string `json:"service_name"`
	Env             string `json:"env"`
	ServiceVersion  string `json:"service_version"`
	TracerVersion   string `json:"tracer_version"`
	LanguageName    string `json:"language_name"`
	LanguageVersion string `json:"language_version"`
}

type Host struct {
	Hostname      string `json:"hostname"`
	OS            string `json:"os"`
	OSVersion     string `json:"os_version"`
	Architecture  string `json:"architecture"`
	KernelName    string `json:"kernel_name"`
	KernelRelease string `json:"kernel_release"`
	KernelVersion string `json:"kernel_version"`
}

type Telemetry struct {
	APIVersion  string      `json:"api_version"`
	RequestType string      `json:"request_type"`
	TracerTime  int64       `json:"tracer_time"`
	RuntimeID   string      `json:"runtime_id"`
	SeqID       int         `json:"seq_id"`
	Application Application `json:"application"`
	Host        Host        `json:"host"`
	Payload     interface{} `json:"payload"`
}

type MetricData struct {
	Metric string    `json:"metric"`
	Points [][]int64 `json:"points"`
	Type   string    `json:"type"`
	Tags   []string  `json:"tags,omitempty"`
}

type GenerateMetricsPayload struct {
	Namespace string       `json:"namespace"`
	Series    []MetricData `json:"series"`
}

type LogMessage struct {
	Message string `json:"message"`
	Level   string `json:"level"`
	Count   int    `json:"count,omitempty"`
	Tags    string `json:"tags,omitempty"`
}

type LogsPayload struct {
	Logs []LogMessage `json:"logs"`
}

func NewTelemetrySender(agentUri, serviceName, env string) (*TelemetrySender, error) {
	app, err := initApplication(serviceName, env)
	if err != nil {
		return nil, NewInstallerError(TelemetryError, fmt.Errorf("failed to initialize application info: %w", err))
	}

	host := initHost()

	return &TelemetrySender{
		AgentEndpoint:    ensureProtocol(agentUri) + AgentEndpoint,
		RuntimeID:        uuid.New().String(),
		Application:      app,
		Host:             host,
		client:           &http.Client{},
		seqID:            0,
		telemetryEnabled: envBool("DD_INSTRUMENTATION_TELEMETRY_ENABLED"),
		metricsEnabled:   envBool("DD_TELEMETRY_METRICS_ENABLED"),
		logsEnabled:      envBool("DD_TELEMETRY_LOG_COLLECTION_ENABLED"),
	}, nil
}

func initApplication(serviceName, env string) (Application, error) {
	if serviceName == "" || env == "" {
		return Application{}, NewInstallerError(TelemetryError, fmt.Errorf("serviceName and env are required"))
	}

	return Application{
		ServiceName:     serviceName,
		Env:             env,
		ServiceVersion:  InstallerVersion,
		TracerVersion:   InstallerVersion,
		LanguageName:    "go",
		LanguageVersion: runtime.Version(),
	}, nil
}

func initHost() Host {
	hostname, _ := os.Hostname()
	kernelName, kernelRelease, kernelVersion := getKernelInfo()
	osVersion := getOSVersion()

	return Host{
		Hostname:      hostname,
		OS:            runtime.GOOS,
		OSVersion:     osVersion,
		Architecture:  runtime.GOARCH,
		KernelName:    kernelName,
		KernelRelease: kernelRelease,
		KernelVersion: kernelVersion,
	}
}

func getKernelInfo() (name, release, version string) {
	cmd := exec.Command("uname", "-srv")
	output, err := cmd.Output()
	if err != nil {
		return NA, NA, NA
	}

	parts := strings.SplitN(strings.TrimSpace(string(output)), " ", 3)
	if len(parts) < 3 {
		return NA, NA, NA
	}

	return parts[0], parts[1], parts[2]
}

func getOSVersion() string {
	cmd := exec.Command("cat", "/etc/os-release")
	output, err := cmd.Output()
	if err != nil {
		return NA
	}

	lines := strings.Split(string(output), "\n")
	for _, line := range lines {
		if strings.HasPrefix(line, "VERSION_ID=") {
			return strings.Trim(strings.TrimPrefix(line, "VERSION_ID="), "\"")
		}
	}

	return NA
}

func (ts *TelemetrySender) SendInstallationCount(status, errorType string) error {
	if !ts.telemetryEnabled || !ts.metricsEnabled {
		log.Debug("Telemetry disabled for metrics, skipping telemetry")
		return nil
	}

	payload := GenerateMetricsPayload{
		Namespace: "rum",
		Series: []MetricData{
			{
				Metric: "injection.installation",
				Points: [][]int64{{time.Now().Unix(), 1}},
				Type:   "count",
				Tags: []string{
					"integration_name:" + IntegrationName,
					"integration_version:" + IntegrationVersion,
					"installer_version:" + InstallerVersion,
					"status:" + status,
					"reason:" + errorType,
				},
			},
		},
	}

	return ts.sendTelemetry("generate-metrics", payload)
}

func (ts *TelemetrySender) SendInstallationTime(status, errorType string, executionTime int64) error {
	if !ts.telemetryEnabled || !ts.metricsEnabled {
		log.Debug("Telemetry disabled for metrics, skipping telemetry")
		return nil
	}

	payload := GenerateMetricsPayload{
		Namespace: "rum",
		Series: []MetricData{
			{
				Metric: "injection.installation.duration",
				Points: [][]int64{{time.Now().Unix(), executionTime}},
				Type:   "distribution",
				Tags: []string{
					"integration_name:" + IntegrationName,
					"integration_version:" + IntegrationVersion,
					"installer_version:" + InstallerVersion,
					"status:" + status,
					"reason:" + errorType,
				},
			},
		},
	}

	return ts.sendTelemetry("generate-metrics", payload)
}

func (ts *TelemetrySender) SendLog(logMsg LogMessage) error {
	if !ts.telemetryEnabled || !ts.logsEnabled {
		log.Debug("Telemetry disabled for logs, skipping telemetry")
		return nil
	}

	payload := LogsPayload{
		Logs: []LogMessage{logMsg},
	}
	return ts.sendTelemetry("logs", payload)
}

func (ts *TelemetrySender) sendTelemetry(requestType string, payload interface{}) error {
	ts.seqID++
	telemetry := Telemetry{
		APIVersion:  APIVersion,
		RequestType: requestType,
		TracerTime:  time.Now().Unix(),
		RuntimeID:   ts.RuntimeID,
		SeqID:       ts.seqID,
		Application: ts.Application,
		Host:        ts.Host,
		Payload:     payload,
	}

	jsonData, err := json.Marshal(telemetry)
	if err != nil {
		return NewInstallerError(TelemetryError, fmt.Errorf("error marshaling telemetry: %w", err))
	}

	req, err := http.NewRequest("POST", ts.AgentEndpoint, bytes.NewBuffer(jsonData))
	if err != nil {
		return NewInstallerError(TelemetryError, fmt.Errorf("error creating request: %w", err))
	}

	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("DD-Telemetry-API-Version", APIVersion)
	req.Header.Set("DD-Telemetry-Request-Type", requestType)

	resp, err := ts.client.Do(req)
	if err != nil {
		return NewInstallerError(TelemetryError, fmt.Errorf("error sending request: %w", err))
	}

	defer resp.Body.Close()

	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return NewInstallerError(TelemetryError, fmt.Errorf("unexpected status code: %d", resp.StatusCode))
	}

	return nil
}
