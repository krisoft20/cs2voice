@echo off
setlocal
cd /d %~dp0
set PRESET=%1
if "%PRESET%"=="" set PRESET=debug
for %%p in (debug release vs ui dist check) do if "%PRESET%"=="%%p" goto known
echo unknown command %PRESET%
echo usage: build.cmd [debug ^| release ^| vs ^| ui ^| dist ^| check]
exit /b 1
:known

if "%PRESET%"=="ui" goto ui

set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
set VSDIR=
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSDIR=%%i
if "%VSDIR%"=="" (
    echo visual studio with the c++ desktop workload was not found
    exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
set VSCMAKE=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin
if exist "%VSCMAKE%\cmake.exe" set PATH=%VSCMAKE%;%PATH%
set VSNINJA=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja
if exist "%VSNINJA%\ninja.exe" set PATH=%VSNINJA%;%PATH%
set CXX=cl

if "%PRESET%"=="vs" (
    cmake --preset vs
    exit /b %errorlevel%
)
if "%PRESET%"=="dist" goto dist
if "%PRESET%"=="check" goto check
if not exist build\%PRESET%\build.ninja cmake --preset %PRESET%
if errorlevel 1 exit /b 1
cmake --build --preset %PRESET% %2 %3 %4
exit /b %errorlevel%

:dist
if not exist build\release\build.ninja cmake --preset release
if errorlevel 1 exit /b 1
cmake --build --preset release
if errorlevel 1 exit /b 1
if not exist dist mkdir dist
copy /y build\release\bin\Release\cs2voice.exe dist\cs2voice.exe >nul
for /f "tokens=4 delims=() " %%v in ('findstr /b /c:"project(cs2voice VERSION" CMakeLists.txt') do set VER=%%v
if not exist dist\symbols\%VER% mkdir dist\symbols\%VER%
copy /y build\release\bin\Release\cs2voice.pdb dist\symbols\%VER%\cs2voice.pdb >nul
powershell -NoProfile -Command "(Get-FileHash dist\cs2voice.exe -Algorithm SHA256).Hash.ToLower() + '  cs2voice.exe' | Set-Content -NoNewline -Encoding ascii dist\cs2voice.exe.sha256"
type dist\cs2voice.exe.sha256
echo.
exit /b %errorlevel%

:check
if not exist build\release\build.ninja cmake --preset release
cmake --build --preset release --target checks bench
if errorlevel 1 exit /b 1
build\release\bin\Release\checks.exe %2 %3
exit /b %errorlevel%

:ui
cd ui
if not exist node_modules call npm install
if errorlevel 1 exit /b 1
call npm run build
exit /b %errorlevel%
