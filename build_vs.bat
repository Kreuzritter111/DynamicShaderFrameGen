@echo off
setlocal
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL exit /b 1
call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if /I "%~1"=="--dll-only" goto dll
cmake -S tests/present_smoke -B build/present-smoke -G Ninja -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b 1
cmake --build build/present-smoke --parallel 2
if errorlevel 1 exit /b 1
ctest --test-dir build/present-smoke --output-on-failure --no-tests=error
if errorlevel 1 exit /b 1
if /I "%~1"=="--present-only" exit /b 0
:dll
cmake --preset windows-release
if errorlevel 1 exit /b 1
cmake --build --preset windows-release --parallel 2
if errorlevel 1 exit /b 1
ctest --preset windows-release --no-tests=error
exit /b %errorlevel%
