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

func TestTransformConfigSnapshot(t *testing.T) {
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
