// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache 2.0 License. This product includes software developed at
// Datadog (https://www.datadoghq.com/).
//
// Copyright 2024-Present Datadog, Inc.

package e2etests

import (
  "fmt"
  runneros "os"
  "path"
  "path/filepath"
  "runtime"
  "strings"
  "testing"

  "github.com/DataDog/datadog-agent/test/new-e2e/pkg/e2e"
  "github.com/DataDog/datadog-agent/test/new-e2e/pkg/environments"
  awsHostWindows "github.com/DataDog/datadog-agent/test/new-e2e/pkg/environments/aws/host/windows"
  "github.com/DataDog/datadog-agent/test/new-e2e/tests/windows"
  windowsCommon "github.com/DataDog/datadog-agent/test/new-e2e/tests/windows/common"
  commonAgent "github.com/DataDog/datadog-agent/test/new-e2e/tests/windows/common/agent"


  "github.com/stretchr/testify/require"
)

var (
  assetDir     string
)

type rumvmSuite struct {
  e2e.BaseSuite[environments.WindowsHost]

}

type rumInstallSuite struct {
  rumvmSuite
}

type rumUpgradeSuite struct {
  rumvmSuite
}
var (
  // this is currently hard-coded to a well known location/version.  Once releases have been made, need to update to
  // dynamically pick up the "last known good" version.
  oldVersionForUpgrade = "https://s3.amazonaws.com/ddagent-windows-unstable/inject-browser-sdk/upgrade/injector_iis-0.0.2.msi"
  oldVersionValidation = "0.0.2"
)
var sitesToTest = []windows.IISSiteDefinition{
  {
    Name: "default_site", // tests simple injection
    BindingPort: "*:8080:",
  },
  {
    Name: "large_site", // tests injection into large response
    BindingPort: "*:8081:",
  },
  {
    Name: "sitewithapps", // tests injection into a site with multiple applications
    BindingPort: "*:8082:",
    SiteDir:     path.Join("c:", "site1"),
    Applications: []windows.IISApplicationDefinition{
      {
        Name:         "/site1/app1",
        PhysicalPath: path.Join("c:", "app1"),
      },
      {
        Name:         "/site1/app2",
        PhysicalPath: path.Join("c:", "app2"),
      },
      {
        Name:         "/site1/app2/nested",
        PhysicalPath: path.Join("c:", "app2", "nested"),
      },
    },
  },
}
type injectionTest struct {
  url string
  injectionService string
  title string
}
var expected_results = []injectionTest {
  {
    url: "http://localhost:8080",
    injectionService: `"service":"my-web-application"`,
    title: "simple injection test, default_site",
  },
  {
    url: "http://localhost:8081",
    injectionService: `"service":"my-web-application"`,
    title: "large file injection test, large_site",
  },
  {
    url: "http://localhost:8082",
    injectionService: "",  // no injection set up on this site
    title: "no injection test, sitewithapps",
  },
  {
    url: "http://localhost:8082/site1/app1",
    injectionService: `"service":"app1"`,
    title: "simple app injection test, sitewithapps/app1",
  },
  {
    url: "http://localhost:8082/site1/app2",
    injectionService: `"service":"app2"`,
    title: "simple app injection test, sitewithapps/app2",
  },
  {
    url: "http://localhost:8082/site1/app2/nested",
    injectionService: `"service":"nested"`,
    title: "deep app injection test, sitewithapps/app2/nested", 
  },
}
func TestRUMInstallSuite(t *testing.T) {
  suiteParams := []e2e.SuiteOption{e2e.WithProvisioner(awsHostWindows.ProvisionerNoAgentNoFakeIntake())}

  e2e.Run(t, &rumInstallSuite{}, suiteParams...)
}
func TestRUMUpgradeSuite(t *testing.T) {
  suiteParams := []e2e.SuiteOption{e2e.WithProvisioner(awsHostWindows.ProvisionerNoAgentNoFakeIntake())}

  e2e.Run(t, &rumUpgradeSuite{}, suiteParams...)
}

func (v *rumvmSuite) SetupSuite() {
  t := v.T()

  currDir, _ := runneros.Getwd()
  assetDir = filepath.Join(currDir, "test_assets")

  // this creates the VM.
  v.BaseSuite.SetupSuite()

  // get the remote host
  vm := v.Env().RemoteHost

  // configure the assets dir
  for idx := range sitesToTest {
    sitesToTest[idx].AssetsDir = filepath.Join(assetDir, sitesToTest[idx].Name)
  }

  err := windows.InstallIIS(vm)
  require.NoError(t, err)
  // HEADSUP the paths are windows, but this will execute in linux. So fix the paths
  t.Log("IIS Installed, continuing")

  err = windows.CreateIISSite(vm, sitesToTest)

  // NOTE(@dmehala): The agent code is wrong for application
  for _, site := range sitesToTest {
    // copy the configs, if they exist, for the applications
    for _, app := range site.Applications {
      localConfigFilePath := filepath.Join(assetDir, site.Name, app.Name, "web.config")
      if _, err := runneros.Stat(localConfigFilePath); err != nil {
        v.T().Logf("Didn't find %s %v, skipping", filepath.Join(assetDir, "web.config"), err)
      } else {
        v.T().Logf("Found %s", filepath.Join(assetDir, "web.config"))
        remoteFilePath := filepath.Join(app.PhysicalPath, "web.config")
        vm.CopyFile(localConfigFilePath, remoteFilePath)
      }
    }
  }

  v.Require().NoError(err)
  
  v.T().Log("Restarting IIS server")
  vm.Execute("iisreset /restart")
}

// requireRun is an additional wrapper so that the test fails immediately on failure 
// of a subtest function, rather than continuing on.
func (v *rumvmSuite) requireRun(desc string, f func ()) {
  if !v.Run(desc, f) {
    v.FailNow("Failed to successfully run subtest %s", desc)
  }
}
func (v *rumInstallSuite) TestInjectorIISInstall() {

  v.requireRun("Install", v.testInstall)
  v.requireRun("Test injection", v.testSuccessfulInjectionAll)
  v.requireRun("Uninstall", v.testUninstall)

}
func (v *rumUpgradeSuite) TestInjectorIISUpgrade() {
  
  v.requireRun("Install old version for upgrade", v.installOldVersionForUpgrade)
  v.requireRun("Upgrade", v.testInstall)
  v.requireRun("Uninstall", v.testUninstall)
}

func (v *rumvmSuite) testInstall() {
  vm := v.Env().RemoteHost

  // Copy the injector installer to the VM
  remoteFilePath := filepath.Join(remoteInstallerPath, installerName)
  iname := filepath.Join(assetDir, installerName)
  v.T().Logf("Attempting to copy %s to %s", iname, remoteFilePath)
  if _, err := runneros.Stat(iname); err != nil {
    v.T().Logf("Didn't find %s %v", iname, err)
  } else {
    v.T().Logf("Found %s", iname)
  }
  vm.CopyFile(iname, remoteFilePath)
  v.doTestInstall(remoteFilePath, "0.1.0", false)
}

func (v *rumvmSuite) installOldVersionForUpgrade() {
  vm := v.Env().RemoteHost

  // Run the installer
  if runtime.GOOS != "windows" {
    // because oldVersionForUpgrade is constructed at runtime, we have to convert it to a windows path if the test runner is linux
    oldVersionForUpgrade = strings.ReplaceAll(oldVersionForUpgrade, "/", "\\")
  }
  outputDir, err := v.CreateTestOutputDir()
  v.Require().NoError(err)
  err = windowsCommon.InstallMSI(vm, oldVersionForUpgrade, "IISRESTART=true", filepath.Join(outputDir, "install.log"))
  v.Require().NoError(err)
  // Verify installer put the injector DLL in the right place
  v.T().Logf("Checking for injector DLL at '%s'\n", defaultBinPath)
  res := strings.TrimSpace(vm.MustExecute(fmt.Sprintf("Test-Path -Path '%s'", defaultBinPath)))
  v.Require().Equal("True", res)

  // Verify the injector DLL has the right version
  err = windowsCommon.VerifyVersion(vm, defaultBinPath, oldVersionValidation)
  if err != nil {
    v.T().Logf("Failed to get version on %s: %v", defaultBinPath, err)
  }
  v.Require().NoError(err)

  // Verify the injector DLL has the right permissions
  v.T().Logf("Checking injector DLL permissions at '%s'\n", defaultBinPath)
  accessMatches, err := isBasicFileAccessEqual(vm, defaultBinPath, expectedBinAccess)
  v.Require().NoError(err)
  v.Require().True(accessMatches)
}

func (v *rumvmSuite) doTestInstall(remoteFilePath string, expectedVersion string, skipSigCheck bool) {
  vm := v.Env().RemoteHost

  // Run the installer
  if runtime.GOOS != "windows" {
    // because remoteFilePath is constructed at runtime, we have to convert it to a windows path if the test runner is linux
    remoteFilePath = strings.Replace(remoteFilePath, "/", "\\", -1)
  }
  outputDir, err := v.CreateTestOutputDir()
  v.Require().NoError(err)
  err = windowsCommon.InstallMSI(vm, remoteFilePath, "IISRESTART=true", filepath.Join(outputDir, "install.log"))
  v.Require().NoError(err)
  // Verify installer put the injector DLL in the right place
  v.T().Logf("Checking for injector DLL at '%s'\n", defaultBinPath)
  res := strings.TrimSpace(vm.MustExecute(fmt.Sprintf("Test-Path -Path '%s'", defaultBinPath)))
  v.Require().Equal("True", res)

  // Verify the injector DLL has the right version
  err = windowsCommon.VerifyVersion(vm, defaultBinPath, expectedVersion)
  if err != nil {
    v.T().Logf("Failed to get version on %s: %v", defaultBinPath, err)
  }
  v.Require().NoError(err)

  // Verify the injector DLL has the right permissions
  v.T().Logf("Checking injector DLL permissions at '%s'\n", defaultBinPath)
  accessMatches, err := isBasicFileAccessEqual(vm, defaultBinPath, expectedBinAccess)
  v.Require().NoError(err)
  v.Require().True(accessMatches)

  if !skipSigCheck {
    sigcheck := []string{remoteFilePath}
    signed := commonAgent.TestValidDatadogCodeSignatures(v.T(), vm, sigcheck)
    v.Require().True(signed)
  }
  
  // Verify the installer put the injector config in the right place
  v.T().Logf("Checking for injector config schema at '%s'\n", configSchemaPath)
  res = strings.TrimSpace(vm.MustExecute(fmt.Sprintf("Test-Path -Path '%s'", configSchemaPath)))
  v.Assert().Equal("True", res)

  // Verify the injector config has the right permissions
  // v.T().Logf("Checking injector config permissions at '%s'\n", exampleConfigPath)
  // accessMatches, err = isBasicFileAccessEqual(vm, exampleConfigPath, expectedConfigAccess)
  // v.Require().NoError(err)
  // v.Require().True(accessMatches)
}

func (v *rumvmSuite) testSuccessfulInjectionAll() {
  v.testSuccessfulInjection(expected_results)
}
func (v *rumvmSuite) testSuccessfulInjectionOld() {
  v.testSuccessfulInjection(expected_results[0:2])
}

func (v *rumvmSuite) testSuccessfulInjection(tests []injectionTest) {
  vm := v.Env().RemoteHost

  for _, test := range tests {

    v.Run(test.title, func() {

      v.T().Logf("Testing injection for %s", test.url)
      response := vm.MustExecute("(iwr -UseBasicParsing " + test.url + ").Content")
      
      responseHeaders := vm.MustExecute("(iwr -UseBasicParsing " + test.url + ").Headers")
      
      if test.injectionService != "" {
        v.Assert().Contains(responseHeaders, "x-datadog-rum-injected")
        v.Assert().Contains(response, test.injectionService)
      } else {
        v.Assert().NotContains(responseHeaders, "x-datadog-rum-injected")
        v.Assert().NotContains(response, `"service":`)
      }
    })
  }
}

func (v *rumvmSuite) testUninstall() {
  vm := v.Env().RemoteHost

  // Uninstall the injector
  outputDir, err := v.CreateTestOutputDir()
  v.Require().NoError(err)

  remotePath := filepath.Join(remoteInstallerPath, installerName)
  err = windowsCommon.UninstallMSI(vm, remotePath, "IISRESTART=true", filepath.Join(outputDir,"uninstall.log"))
  v.Require().NoError(err)
  
  // Verify the injector DLL is gone
  res := strings.TrimSpace(vm.MustExecute(fmt.Sprintf("Test-Path -Path '%s'", defaultBinPath)))
  v.Assert().Equal("False", res)
}
