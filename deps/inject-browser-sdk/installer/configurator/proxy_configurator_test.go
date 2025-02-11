// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache 2.0 License. This product includes software developed at
// Datadog (https://www.datadoghq.com/).
//
// Copyright 2024-Present Datadog, Inc.

package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/stretchr/testify/require"
	"gotest.tools/assert"
)

type FileInfo struct {
	Filename string
	IsError  bool
}

func TestTransformNginxConfigSnapshot(t *testing.T) {
	const modulesPath = "/opt/datadog-nginx"
	const agentUri = "http://localhost:8126"
	const appId = "ffffffff-ffff-ffff-ffff-ffffffffffff"
	const clientToken = "pubffffffffffffffffffffffffffffffff"
	const site = "datadoghq.com"
	const sessionSampleRate = 49
	const sessionReplaySampleRate = 51

	pattern := filepath.Join("testdata", "*.conf")
	files, err := filepath.Glob(pattern)
	require.NoError(t, err, "Failed to list test cases")

	// List all test cases
	// *.conf should contain a corresponding *.conf.snap file, created if not exists
	// *.err.conf should fail the transformation, no corresponding snapshot file
	var testCases []FileInfo
	for _, file := range files {
		isError := strings.HasSuffix(filepath.Base(file), ".err.conf")
		testCases = append(testCases, FileInfo{
			Filename: file,
			IsError:  isError,
		})
	}

	for _, tc := range testCases {
		t.Run(tc.Filename, func(t *testing.T) {
			input, err := os.ReadFile(tc.Filename)
			require.NoError(t, err, "Failed to read input file")

			// Create a temporary file for the output
			tmpFile, err := os.CreateTemp("", "nginx-config-*.conf")
			require.NoError(t, err, "Failed to create temporary file")
			defer os.Remove(tmpFile.Name())

			configurator := &NginxConfigurator{
				ModulesPath: modulesPath,
			}

			transformed, err := transformConfig(configurator, input, agentUri, appId, clientToken, site, sessionSampleRate, sessionReplaySampleRate, tmpFile.Name())
			if tc.IsError {
				require.Error(t, err, "Expected an error transforming the configuration, it succeeded instead")
				return
			}

			require.NoError(t, err, "Failed to transform config")

			snapshotFile := filepath.Join("testdata", filepath.Base(tc.Filename)+".snap")

			if _, err := os.Stat(snapshotFile); os.IsNotExist(err) {
				// If snapshot doesn't exist, create it for local use, fail the test
				err = os.WriteFile(snapshotFile, transformed, 0644)
				require.NoError(t, err, "Failed to create snapshot file")
				t.Logf("Created new snapshot: %s", snapshotFile)
				t.Fail()
			} else {
				// If snapshot exists, compare with it
				expected, err := os.ReadFile(snapshotFile)
				require.NoError(t, err, "Failed to read snapshot file")

				assert.Equal(t, string(expected), string(transformed), "Transformed config does not match snapshot")
			}
		})
	}
}

func TestHttpdConfigFile(t *testing.T) {
	tests := []struct {
		name                    string
		agentUri                string
		appID                   string
		clientToken             string
		site                    string
		sessionSampleRate       int
		sessionReplaySampleRate int
		expected                []string
	}{
		{
			name:                    "With local configuration",
			agentUri:                "http://localhost:8126",
			appID:                   "ffffffff-ffff-ffff-ffff-ffffffffffff",
			clientToken:             "pubffffffffffffffffffffffffffffffff",
			site:                    "datadoghq.com",
			sessionSampleRate:       98,
			sessionReplaySampleRate: 99,
			expected: []string{
				`LoadModule datadog_module /opt/datadog-httpd/mod_datadog.so`,
				"DatadogTracing Off",
				"DatadogRum On",
				`DatadogAgentUrl "http://localhost:8126"`,
				"<DatadogRumSettings>",
				`DatadogRumOption "applicationId" "ffffffff-ffff-ffff-ffff-ffffffffffff"`,
				`DatadogRumOption "clientToken" "pubffffffffffffffffffffffffffffffff"`,
				`DatadogRumOption "site" "datadoghq.com"`,
				`DatadogRumOption "sessionSampleRate" "98"`,
				`DatadogRumOption "sessionReplaySampleRate" "99"`,
				"</DatadogRumSettings>",
			},
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			res := datadogConfigFile(
				tt.agentUri,
				tt.appID,
				tt.clientToken,
				tt.site,
				tt.sessionSampleRate,
				tt.sessionReplaySampleRate,
			)

			resLines := strings.Split(strings.TrimSpace(res), "\n")

			if len(resLines) != len(tt.expected) {
				t.Errorf("generated %d lines, expected %d lines", len(resLines), len(tt.expected))
				return
			}

			for i := 0; i < len(resLines); i++ {
				resLine := strings.TrimSpace(resLines[i])
				expectedLine := strings.TrimSpace(tt.expected[i])
				if resLine != expectedLine {
					t.Errorf("line %d:\result:  %s\nexpected: %s", i, resLine, expectedLine)
				}
			}
		})
	}
}
