# Unless explicitly stated otherwise all files in this repository are licensed
# under the Apache 2.0 License. This product includes software developed at
# Datadog (https://www.datadoghq.com/).
#
# Copyright 2024-Present Datadog, Inc.

@{

# Script module or binary module file associated with this manifest.
RootModule = 'Datadog.RUM.psm1'

# Version number of this module.
ModuleVersion = '0.1.0'

# ID used to uniquely identify this module
GUID = 'd20a3223-92e5-473f-b0c5-8a039e6cc9c2'

# Author of this module
Author = 'Datadog Inc'

# Company or vendor of this module
CompanyName = 'Datadog Inc'

# Copyright statement for this module
Copyright = 'Copyright 2024-Present Datadog, Inc.'

# Description of the functionality provided by this module
Description = 'Manage Datadog RUM configuration for IIS Auto-Instrumentation'

# Minimum version of the Windows PowerShell engine required by this module
PowerShellVersion = '5.0'

# Modules that must be imported into the global environment prior to importing this module
RequiredModules = 'WebAdministration'

# Functions to export from this module, for best performance, do not use wildcards and do not delete the entry, use an empty array if there are no functions to export.
FunctionsToExport = 'New-Datadog-RUMConfiguration'

}

