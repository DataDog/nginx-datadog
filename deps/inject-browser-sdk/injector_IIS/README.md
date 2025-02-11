# RUM SDK Injector for Windows IIS

A Native-Code module for IIS 7.0 and above, designed to inject the RUM Browser SDK on the fly into HTML pages.



## Build/installation steps
1. Configure the project
```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release .
```

To cross-compile from a Linux host for Windows:
```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-x86_64-pc-windows-msvc.cmake .
```
2. Compile

```sh
cmake --build build -j
```

## Installation
1. On your target host, copy `injector_IIS.dll` and `injectbrowsersdk.dll` into `%windir%\System32\inetsrv`
    - both files can be found in the output directory from your build in step 2
2. Set the required system environment variables: `DD_APPLICATION_ID` and `DD_CLIENT_TOKEN`
    - the available configuration environment variables are listed in `src/ruminjector.h`
3. Open `IIS manager` > `Modules` > `Configure Native Modules` and add `injector_ISS.dll` as a module (you can give it whatever name you like)
4. Start your IIS server and inspect the resulting webpage to validate that the Datadog RUM script was injected
    - If any problems occur, for Debug builds you can check the windows event logs for details

## Upgrade steps
1. Stop the IIS server
2. Update DLL
3. Start the IIS server

## Uninstall steps
1. Stop the IIS server
2. Remove the DLLs
3. Open `%windir%\system32\inetsrv\config\applicationhost.config` and remove the module from  module the `<globalModules>` & `<modules>` sections\
4. Start the IIS server
