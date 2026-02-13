// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache 2.0 License. This product includes software developed at
// Datadog (https://www.datadoghq.com/).
//
// Copyright 2024-Present Datadog, Inc.

package main

import (
	"encoding/json"
	"net/http"
	"os"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/mock"
)

const (
	unset   = "unset"
	success = "success"
)

type MockHTTPClient struct {
	mock.Mock
}

func (m *MockHTTPClient) Do(req *http.Request) (*http.Response, error) {
	args := m.Called(req)
	return args.Get(0).(*http.Response), args.Error(1)
}

func getSenderAndClient() (*TelemetrySender, *MockHTTPClient) {
	mockClient := new(MockHTTPClient)

	sender := &TelemetrySender{
		AgentEndpoint:    "http://localhost:8126/telemetry/proxy/api/v2/apmtelemetry",
		Application:      Application{ServiceName: "test-service", Env: "test-env"},
		Host:             Host{Hostname: "test-host"},
		client:           mockClient,
		seqID:            0,
		telemetryEnabled: true,
		metricsEnabled:   true,
		logsEnabled:      true,
	}

	mockClient.On("Do", mock.AnythingOfType("*http.Request")).Return(&http.Response{
		StatusCode: http.StatusOK,
		Body:       http.NoBody,
	}, nil)

	return sender, mockClient
}

func getFirstCallTelemetry(mockClient *MockHTTPClient) Telemetry {
	call := mockClient.Calls[0]
	req := call.Arguments[0].(*http.Request)

	var actualPayload Telemetry
	json.NewDecoder(req.Body).Decode(&actualPayload)

	return actualPayload
}

func TestNewTelemetrySender(t *testing.T) {
	os.Setenv("DD_INSTRUMENTATION_TELEMETRY_ENABLED", "true")
	os.Setenv("DD_TELEMETRY_METRICS_ENABLED", "true")
	os.Setenv("DD_TELEMETRY_LOG_COLLECTION_ENABLED", "true")

	sender, err := NewTelemetrySender("http://test-host:8126", "test-service", "test-env")
	assert.NoError(t, err)
	assert.NotNil(t, sender)
	assert.Equal(t, "http://test-host:8126/telemetry/proxy/api/v2/apmtelemetry", sender.AgentEndpoint)
	assert.Equal(t, "test-service", sender.Application.ServiceName)
	assert.Equal(t, "test-env", sender.Application.Env)
	assert.True(t, sender.telemetryEnabled)
	assert.True(t, sender.metricsEnabled)
	assert.True(t, sender.logsEnabled)
}

func TestSendInstallationCount(t *testing.T) {
	sender, mockClient := getSenderAndClient()
	sender.SendInstallationCount(success, unset)
	telemetry := getFirstCallTelemetry(mockClient)

	payloadMap := telemetry.Payload.(map[string]interface{})
	series := payloadMap["series"].([]interface{})
	metric := series[0].(map[string]interface{})
	tags := metric["tags"].([]interface{})

	assert.Contains(t, tags, "integration_name:"+unset)
	assert.Contains(t, tags, "integration_version:"+unset)
	assert.Contains(t, tags, "installer_version:"+InstallerVersion)
	assert.Contains(t, tags, "status:"+success)
	assert.Contains(t, tags, "reason:"+unset)
}

func TestSendInstallationTime(t *testing.T) {
	const duration = 1000

	sender, mockClient := getSenderAndClient()
	sender.SendInstallationTime(success, unset, duration)
	telemetry := getFirstCallTelemetry(mockClient)

	payloadMap := telemetry.Payload.(map[string]interface{})
	series := payloadMap["series"].([]interface{})
	metric := series[0].(map[string]interface{})
	points := metric["points"].([]interface{})
	firstPoint := points[0].([]interface{})
	tags := metric["tags"].([]interface{})

	assert.Equal(t, "injection.installation.duration", metric["metric"])
	assert.Equal(t, "distribution", metric["type"])
	assert.Equal(t, float64(duration), firstPoint[1])

	assert.Contains(t, tags, "integration_name:"+unset)
	assert.Contains(t, tags, "integration_version:"+unset)
	assert.Contains(t, tags, "installer_version:"+InstallerVersion)
	assert.Contains(t, tags, "status:"+success)
	assert.Contains(t, tags, "reason:"+unset)
}

func TestSendLog(t *testing.T) {
	const msgStr = "Test log message"
	const msgLvl = "error"
	const msgTags = "tag1:value1,tag2:value2"

	logMsg := LogMessage{
		Message: msgStr,
		Level:   msgLvl,
		Tags:    msgTags,
	}

	sender, mockClient := getSenderAndClient()
	sender.SendLog(logMsg)
	telemetry := getFirstCallTelemetry(mockClient)

	payloadMap := telemetry.Payload.(map[string]interface{})
	logs := payloadMap["logs"].([]interface{})
	log := logs[0].(map[string]interface{})

	assert.Equal(t, msgStr, log["message"])
	assert.Equal(t, msgLvl, log["level"])
	assert.Equal(t, msgTags, log["tags"])
}
