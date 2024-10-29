package main

import (
	"strings"
	"testing"
)

func TestValidateInput(t *testing.T) {
	tests := []struct {
		name                    string
		appID                   string
		site                    string
		clientToken             string
		arch                    string
		sessionSampleRate       int
		sessionReplaySampleRate int
		expectedError           string
	}{
		{
			name:                    "Valid input",
			appID:                   "test-app",
			site:                    "test-site",
			clientToken:             "test-token",
			arch:                    "amd64",
			sessionSampleRate:       50,
			sessionReplaySampleRate: 50,
			expectedError:           "",
		},
		{
			name:                    "Empty appID",
			appID:                   "",
			site:                    "test-site",
			clientToken:             "test-token",
			arch:                    "amd64",
			sessionSampleRate:       50,
			sessionReplaySampleRate: 50,
			expectedError:           "--appId is required",
		},
		{
			name:                    "Empty site",
			appID:                   "test-app",
			site:                    "",
			clientToken:             "test-token",
			arch:                    "amd64",
			sessionSampleRate:       50,
			sessionReplaySampleRate: 50,
			expectedError:           "--site is required",
		},
		{
			name:                    "Empty clientToken",
			appID:                   "test-app",
			site:                    "test-site",
			clientToken:             "",
			arch:                    "amd64",
			sessionSampleRate:       50,
			sessionReplaySampleRate: 50,
			expectedError:           "--clientToken is required",
		},
		{
			name:                    "Invalid sessionSampleRate (negative)",
			appID:                   "test-app",
			site:                    "test-site",
			clientToken:             "test-token",
			arch:                    "amd64",
			sessionSampleRate:       -1,
			sessionReplaySampleRate: 50,
			expectedError:           "sessionSampleRate is required and must be between 0 and 100",
		},
		{
			name:                    "Invalid sessionSampleRate (over 100)",
			appID:                   "test-app",
			site:                    "test-site",
			clientToken:             "test-token",
			arch:                    "amd64",
			sessionSampleRate:       101,
			sessionReplaySampleRate: 50,
			expectedError:           "sessionSampleRate is required and must be between 0 and 100",
		},
		{
			name:                    "Invalid sessionReplaySampleRate (negative)",
			appID:                   "test-app",
			site:                    "test-site",
			clientToken:             "test-token",
			arch:                    "amd64",
			sessionSampleRate:       50,
			sessionReplaySampleRate: -1,
			expectedError:           "sessionReplaySampleRate is required and must be between 0 and 100",
		},
		{
			name:                    "Invalid sessionReplaySampleRate (over 100)",
			appID:                   "test-app",
			site:                    "test-site",
			clientToken:             "test-token",
			arch:                    "amd64",
			sessionSampleRate:       50,
			sessionReplaySampleRate: 101,
			expectedError:           "sessionReplaySampleRate is required and must be between 0 and 100",
		},
		{
			name:                    "Invalid arch",
			appID:                   "test-app",
			site:                    "test-site",
			clientToken:             "test-token",
			arch:                    "x86",
			sessionSampleRate:       50,
			sessionReplaySampleRate: 50,
			expectedError:           "arch must be either 'amd64' or 'arm64'",
		},
		{
			name:                    "Valid input with arm64 arch",
			appID:                   "test-app",
			site:                    "test-site",
			clientToken:             "test-token",
			arch:                    "arm64",
			sessionSampleRate:       50,
			sessionReplaySampleRate: 50,
			expectedError:           "",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			err := validateInput(tt.appID, tt.site, tt.clientToken, tt.arch, tt.sessionSampleRate, tt.sessionReplaySampleRate)

			if tt.expectedError == "" {
				if err != nil {
					t.Errorf("Expected no error, but got: %v", err)
				}
			} else {
				if err == nil {
					t.Errorf("Expected error containing '%s', but got no error", tt.expectedError)
				} else if !strings.Contains(err.Error(), tt.expectedError) {
					t.Errorf("Expected error containing '%s', but got: %v", tt.expectedError, err)
				}
			}
		})
	}
}
