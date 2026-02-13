if not exist c:\mnt\ goto nomntdir

:: Windows runners get re-used between jobs, so we must make sure to run our builds in a temporary location
mkdir \build
cd \build || exit /b 2
xcopy /e/s/h/q c:\mnt\*.* || exit /b 3

set "outdir=%cd%\build_out"
if exist %build_out% (rmdir /q/s %build_out%)

pushd injector_iis
invoke iis.build --out-dir %outdir% || exit /b 4
popd

dir %outdir%

if not exist %BUILD_OUTDIR% (mkdir %BUILD_OUTDIR%)
copy /y %outdir%\*.msi %BUILD_OUTDIR%	
copy /y %outdir%\unit_tests.exe %BUILD_OUTDIR%	

goto :EOF

:nomntdir
@echo directory not mounted, parameters incorrect
exit /b 1
goto :EOF
