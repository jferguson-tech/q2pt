@echo off
rem usage: build [x64|x86] [Release|Debug|RelWithDebInfo] [perf]
rem perf builds in the pt_perf graph of where the time goes, see the README
setlocal
set ARCH=%1
if "%ARCH%"=="" set ARCH=x64
set CFG=%2
if "%CFG%"=="" set CFG=RelWithDebInfo

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR echo Visual Studio not found & exit /b 1
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" %ARCH% >nul || exit /b 1

set "BDIR=%~dp0build\%ARCH%-%CFG%"
set PERF=OFF
if /i "%3"=="perf" set PERF=ON
cmake -S "%~dp0." -B "%BDIR%" -G Ninja -DCMAKE_BUILD_TYPE=%CFG% -DPT_PERF=%PERF% || exit /b 1
cmake --build "%BDIR%" -- -k 0
