@echo off
setlocal
cd /d "%~dp0"

:: CI can pass ZIG=<path-to-zig.exe>; default is the pinned toolchain next to the repo.
if "%ZIG%"=="" set ZIG=%~dp0..\tools\zig-x86_64-windows-0.17.1\zig.exe
if not exist "%ZIG%" goto :nozig

set MODE=%1
if "%MODE%"=="" set MODE=release

if not exist dist mkdir dist
if not exist obj mkdir obj

if /I "%MODE%"=="test" goto :testmode
if /I "%MODE%"=="dev" goto :devmode
if /I "%MODE%"=="arm64" goto :arm64mode

set RC=res\app.rc
set RES=obj\app.res
set OUT=dist\MemSweep.exe
set DEVFLAG=
set TARGET=x86_64-windows-gnu
echo [build] RELEASE build - requireAdministrator
goto :compile

:devmode
set RC=res\app_dev.rc
set RES=obj\app_dev.res
set OUT=dist\MemSweepDev.exe
set DEVFLAG=-DMS_DEV
set TARGET=x86_64-windows-gnu
echo [build] DEV build - asInvoker, testing only
goto :compile

:arm64mode
set RC=res\app.rc
set RES=obj\app_arm64.res
set OUT=dist\MemSweep-arm64.exe
set DEVFLAG=
set TARGET=aarch64-windows-gnu
echo [build] ARM64 build - requireAdministrator
goto :compile

:compile
echo [build] compiling resources...
"%ZIG%" rc "%RC%" "%RES%"
if errorlevel 1 goto :fail

echo [build] compiling+linking...
"%ZIG%" c++ -target %TARGET% -municode -DUNICODE -D_UNICODE -Oz -fno-exceptions -fno-rtti -fno-ident -ffunction-sections -fdata-sections -fvisibility=hidden -fvisibility-inlines-hidden -Wall -Wextra -Wshadow -Wformat=2 -Wundef -Wpointer-arith -Werror -I src src\main.cpp src\core.cpp src\ui.cpp "%RES%" %DEVFLAG% -o "%OUT%" -lntdll -lshell32 -lgdi32 -ladvapi32 -lcomctl32 -ldwmapi -lgdiplus -lole32 -loleaut32 -luuid -limm32 -Wl,--subsystem,windows -Wl,--gc-sections -s
if errorlevel 1 goto :fail

echo [build] done: %OUT%
for %%F in ("%OUT%") do echo         size: %%~zF bytes
exit /b 0

:testmode
echo [test] build dev + run self tests
call "%~f0" dev
if errorlevel 1 goto :fail
dist\MemSweepDev.exe /selftest
if errorlevel 1 goto :fail
dist\MemSweepDev.exe /idlecheck
if errorlevel 1 goto :fail
dist\MemSweepDev.exe /uipng
if errorlevel 1 goto :fail
dist\MemSweepDev.exe /checkpng
if errorlevel 1 goto :fail
echo [test] PASS
exit /b 0

:nozig
echo [build] zig not found: %ZIG%
echo         set ZIG=<path-to-zig.exe> or place the pinned toolchain at ..\tools
exit /b 1
:fail
echo [build] FAILED
exit /b 1
