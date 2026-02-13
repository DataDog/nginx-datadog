// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache 2.0 License. This product includes software developed at
// Datadog (https://www.datadoghq.com/).
//
// Copyright 2024-Present Datadog, Inc.

package e2etests

import (
	"encoding/json"
	"fmt"
	"strings"

	"github.com/DataDog/datadog-agent/test/new-e2e/pkg/components"
)

type basicFileAccess struct {
	FileSystemRights  string `json:"FileSystemRights"`
	RegistryRights    string `json:"RegistryRights"`
	AccessControlType string `json:"AccessControlType"`
	IdentityReference string `json:"IdentityReference"`
	IsInherited       string `json:"IsInherited"`
	InheritanceFlags  string `json:"InheritanceFlags"`
	PropagationFlags  string `json:"PropagationFlags"`
}

func getBasicFileAccess(VM *components.RemoteHost, path string) ([]basicFileAccess, error) {
	command := `(Get-Acl "%s").Access | Select-Object @{Name='FileSystemRights'; Expression={$_.FileSystemRights.ToString()}}, 
@{Name='RegistryRights'; Expression={$_.RegistryRights.ToString()}}, 
@{Name='AccessControlType'; Expression={$_.AccessControlType.ToString()}}, 
@{Name='IdentityReference'; Expression={$_.IdentityReference.Value}}, 
@{Name='IsInherited'; Expression={$_.IsInherited.ToString()}}, 
@{Name='InheritanceFlags'; Expression={$_.InheritanceFlags.ToString()}}, 
@{Name='PropagationFlags'; Expression={$_.PropagationFlags.ToString()}} | ConvertTo-Json`

	output := VM.MustExecute(fmt.Sprintf(command, path))
	var basicFileAccesses []basicFileAccess
	err := json.Unmarshal([]byte(output), &basicFileAccesses)
	if err != nil {
		return nil, err
	}
	return basicFileAccesses, nil
}

func isBasicFileAccessEqual(VM *components.RemoteHost, path string, expectedAccess []basicFileAccess) (bool, error) {
	actualAccess, err := getBasicFileAccess(VM, path)
	if err != nil {
		return false, err
	}
	return compareBasicFileAccess(expectedAccess, actualAccess)
}

func compareBasicFileAccess(expectedAccess, actualAccess []basicFileAccess) (bool, error) {
	if len(actualAccess) != len(expectedAccess) {
		return false, fmt.Errorf("expected %d access entries, got %d", len(expectedAccess), len(actualAccess))
	}

	actualAccessMap := make(map[string]basicFileAccess)
	for _, access := range actualAccess {
		actualAccessMap[access.IdentityReference] = access
	}

	expectedAccessMap := make(map[string]basicFileAccess)
	for _, access := range expectedAccess {
		expectedAccessMap[access.IdentityReference] = access
	}

	for identityReference, expectedAccess := range expectedAccessMap {
		actualAccess, ok := actualAccessMap[identityReference]
		if !ok {
			return false, fmt.Errorf("expected access entry for %s, got none", identityReference)
		}
		if actualAccess != expectedAccess {
			return false, fmt.Errorf("expected access entry for %s to be %v, got %v", identityReference, expectedAccess, actualAccess)
		}
	}
	return true, nil
}

func getBinarySignature(VM *components.RemoteHost, path string) string {
	command := `(Get-AuthenticodeSignature "%s").Status`
	res := VM.MustExecute(fmt.Sprintf(command, path))
	return strings.TrimSpace(res)
}

