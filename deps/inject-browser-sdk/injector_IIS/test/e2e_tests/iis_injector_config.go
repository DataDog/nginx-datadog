// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache 2.0 License. This product includes software developed at
// Datadog (https://www.datadoghq.com/).
//
// Copyright 2024-Present Datadog, Inc.

package e2etests

const (
	remoteInstallerPath   = "C:\\Users\\Administrator\\Desktop"
	installerName         = "injector_IIS.msi"
	defaultModuleName     = "DDIISInjectorModule"
	defaultBinPath        = "C:\\Program Files\\Datadog\\Datadog RUM\\injector_IIS.dll"
	configSchemaPath      = "C:\\Windows\\System32\\inetsrv\\Config\\schema\\RUM_schema.xml"
	defaultConfigDir      = "C:\\ProgramData\\Datadog RUM\\"
	defaultConfigName     = "web.config"
	defaultConfigLocation = "Registry::"
)

var expectedBinAccess = []basicFileAccess{
	{
		FileSystemRights:  "FullControl",
		AccessControlType: "Allow",
		IdentityReference: "NT AUTHORITY\\SYSTEM",
		IsInherited:       "True",
		InheritanceFlags:  "None",
		PropagationFlags:  "None",
	},
	{
		FileSystemRights:  "FullControl",
		AccessControlType: "Allow",
		IdentityReference: "BUILTIN\\Administrators",
		IsInherited:       "True",
		InheritanceFlags:  "None",
		PropagationFlags:  "None",
	},
	{
		FileSystemRights:  "ReadAndExecute, Synchronize",
		AccessControlType: "Allow",
		IdentityReference: "BUILTIN\\Users",
		IsInherited:       "True",
		InheritanceFlags:  "None",
		PropagationFlags:  "None",
	},
	{
		FileSystemRights:  "ReadAndExecute, Synchronize",
		AccessControlType: "Allow",
		IdentityReference: "APPLICATION PACKAGE AUTHORITY\\ALL APPLICATION PACKAGES",
		IsInherited:       "True",
		InheritanceFlags:  "None",
		PropagationFlags:  "None",
	},
	{
		FileSystemRights:  "ReadAndExecute, Synchronize",
		AccessControlType: "Allow",
		IdentityReference: "APPLICATION PACKAGE AUTHORITY\\ALL RESTRICTED APPLICATION PACKAGES",
		IsInherited:       "True",
		InheritanceFlags:  "None",
		PropagationFlags:  "None",
	},
}

var expectedConfigAccess = []basicFileAccess{
	{
		FileSystemRights:  "FullControl",
		AccessControlType: "Allow",
		IdentityReference: "NT AUTHORITY\\SYSTEM",
		IsInherited:       "True",
		InheritanceFlags:  "None",
		PropagationFlags:  "None",
	},
	{
		FileSystemRights:  "FullControl",
		AccessControlType: "Allow",
		IdentityReference: "BUILTIN\\Administrators",
		IsInherited:       "True",
		InheritanceFlags:  "None",
		PropagationFlags:  "None",
	},
	{
		FileSystemRights:  "ReadAndExecute, Synchronize",
		AccessControlType: "Allow",
		IdentityReference: "BUILTIN\\Users",
		IsInherited:       "True",
		InheritanceFlags:  "None",
		PropagationFlags:  "None",
	},
}

