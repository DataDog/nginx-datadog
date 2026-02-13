if not exist c:\mnt\ goto nomntdir
if not exist c:\artifacts\ goto noartifactsdir

c:\artifacts\unit_tests.exe || exit /b 3
goto :EOF

:nomntdir
@echo mnt directory not mounted, parameters incorrect
exit /b 1
goto :EOF

:noartifactsdir
@echo artifacts directory not mounted, parameters incorrect
exit /b 2
goto :EOF
