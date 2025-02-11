Import-Module WebAdministration

<#
.SYNOPSIS
  Generates RUM configuration for the RUM Auto-Injection IIS module.

.EXAMPLE
  New-Datadog-RUMConfiguration -IISSite "Default Web Site" -ApplicationId "foo" -ClientToken "bar"

.EXAMPLE
  $RUMConfig = @{
    IISSite = "Default Web Site"
    ApplicationId = "foo"
    ClientToken = "bar"
    DatadogSite = "datadoghq.eu"
    SessionSampleRate = 100
    SessionReplaySampleRate = 100
  }

  New-Datadog-RUMConfiguration @RUMConfig
#>
function New-Datadog-RUMConfiguration {
  param (
    # Specifies the Windows IIS site name for which Datadog RUM should be configured
    [Parameter(Mandatory=$true)]
    [string]$IISSite,

    # RUM Application ID. Defined in the Datadog UI.
    [Parameter(Mandatory=$true)]
    [string]$ApplicationId,

    # Datadog Client Token. Defined in the Datadog UI.
    [Parameter(Mandatory=$true)]
    [string]$ClientToken,

    # Datadog site identifier for your organization.
    # Example: datadoghq.com or datadoghq.eu.
    [Parameter(Mandatory=$false)]
    [string]$DatadogSite,

    # Percentage of sessions to track (between 0-100)
    [Parameter(Mandatory=$false)]
    [ValidateRange(0, 100)]
    [int]$SessionSampleRate,

    # Percentage of tracked sessions to capture (between 0-100)
    [Parameter(Mandatory=$false)]
    [ValidateRange(0, 100)]
    [int]$SessionReplaySampleRate
  )

  # Create a hashtable to store the configuration
  $rumConfig = @{
    applicationId = $ApplicationId
    clientToken   = $ClientToken
  }

  if (-not ([string]::IsNullOrEmpty($DatadogSite))) {
    $rumConfig.add("site", $DatadogSite)
  }

  if ($PSBoundParameters.ContainsKey("SessionSampleRate")) {
    $rumConfig.add("sessionSampleRate", $SessionSampleRate)
  }

  if ($PSBoundParameters.ContainsKey("SessionReplaySampleRate")) {
    $rumConfig.add("sessionReplaySampleRate", $SessionReplaySampleRate)
  }

  $sectionPath = "system.webServer/datadogRum"

  # Create the `datadogRum` section if it does not already exist
  $psPath = "MACHINE/WEBROOT/APPHOST/" + $IISSite

  Start-WebCommitDelay

  if (-not (Get-WebConfiguration -Filter $sectionPath -PSPath $psPath)) {
    Add-WebConfiguration -PSPath $psPath -Filter $sectionPath
  } else {
    Write-Warning "A configuration file for exists for the site '$IISSite'. The existing file will be overwritten with new settings."
  }

  Set-WebConfigurationProperty -PSPath $psPath -Filter $sectionPath -Name "." -Value @{enabled="true";version="5"}

  foreach($key in $rumConfig.Keys) {
    $value = $rumConfig[$key]

    if (-not (Get-WebConfiguration -PSPath $psPath -Filter "$sectionPath/option[@name='$key']")) {
      Add-WebConfiguration -PSPath $psPath -Filter "$sectionPath" -AtIndex 0 -Value @{
        name = $key
        value = $value
      }
    } else {
      Set-WebConfigurationProperty -PSPath $psPath -Filter "$sectionPath/option[@name='$key']" -Name "value" -Value $value
    }
  }

  Stop-WebCommitDelay -Commit $True
}

