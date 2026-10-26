@echo off
setlocal
cd /d "%~dp0"
"C:\devkitPro\msys2\usr\bin\bash.exe" -c "export DEVKITPRO=/opt/devkitpro; export DEVKITARM=/opt/devkitpro/devkitARM; make"
exit /b %ERRORLEVEL%
