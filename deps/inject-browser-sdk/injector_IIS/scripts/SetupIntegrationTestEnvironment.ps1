# TODO:
#  - pass static folder as arg
#  - pass .msi path as arg
function Set-RewriteRules {
  param(
    [Parameter(Mandatory=$true)]
    [string]$SiteName
  )
  <#
  Produce the following rule:
  <rewrite>
    <rules>
      <rule name="Reverse Proxy to webmail" enabled="true" stopProcessing="true">
        <match url="(.*)" />
        <conditions>
          <add input="{REQUEST_FILENAME}" matchType="IsFile" negate="true" />
          <add input="{REQUEST_FILENAME}" matchType="IsDirectory" negate="true" />
        </conditions>
        <action type="Rewrite" url="http://localhost:8090/{R:1}" />
     </rule>
    </rules>
  </rewrite>
  #>
  $name = 'Proxy-if-not-on-disk'
  $site = 'MACHINE/WEBROOT/APPHOST/' + $SiteName
  $root = 'system.webServer/rewrite/rules'
  $filter = "{0}/rule[@name='{1}']" -f $root, $name

  Add-WebConfigurationProperty -PsPath $site -filter $root -name '.' -value @{name=$name; patterSyntax='Regular Expressions'; enabled='True'; stopProcessing='True'}
  Set-WebConfigurationProperty -PSPath $site -filter "$filter/match" -name 'url' -value "(.*)"
  Set-WebConfigurationProperty -PSPath $site -filter "$filter/conditions" -name '.' -value @{input='{REQUEST_FILENAME}'; matchType='IsDirectory'; negate='true'}, @{input='{REQUEST_FILENAME}'; matchType='IsFile'; negate='true'};
  Set-WebConfigurationProperty -PSPath $site -filter "$filter/action" -name 'type' -value 'Rewrite'
  Set-WebConfigurationProperty -PSPath $site -filter "$filter/action" -name 'url' -value "http://localhost:8090/{R:1}"
}

# Enable Proxy
Set-WebConfigurationProperty -pspath 'MACHINE/WEBROOT/APPHOST' -filter "system.webServer/proxy" -name "enabled" -value "True"

# Create Sites
Copy-Item -Path "C:\inject-browser-sdk\tests\integration_tests\static" -Destination "C:\SiteRumEnabled\" -Recurse
New-IISSite -Name "SiteRumEnabled" -BindingInformation "*:8080:" -PhysicalPath "C:\SiteRumEnabled"
Set-RewriteRules "SiteRumEnabled"

Copy-Item -Path "C:\inject-browser-sdk\tests\integration_tests\static" -Destination "C:\SiteDisabled\" -Recurse
New-IISSite -Name "SiteRumDisabled" -BindingInformation "*:8081:" -PhysicalPath "C:\SiteDisabled"
Set-RewriteRules "SiteRumDisabled"

# Install RUM Module
Start-Process -Wait msiexec '-qn -i C:\inject-browser-sdk\injector_IIS\injector_IIS_installer\bin\x64\Debug\en-us\injector_IIS_installer.msi IISRESTART=true'

# Configure RUM 
$RUMConfig = @{
  IISSite = "SiteRumEnabled"
  ApplicationId = "foo"
  ClientToken = "bar"
  DatadogSite = "datadoghq.eu"
  SessionSampleRate = 100
  SessionReplaySampleRate = 100
}

New-Datadog-RUMConfiguration @RUMConfig

# Restart IIS to propagate changes
iisreset -stop
iisreset -start
