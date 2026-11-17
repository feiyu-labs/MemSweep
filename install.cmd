@echo off
rem This file is UTF-8; switch the console to UTF-8 so Chinese text is readable.
chcp 65001 >nul
setlocal
cd /d "%~dp0"

net session >nul 2>&1
if errorlevel 1 (
  echo [install] 需要管理员权限：请右键本文件 -^> 以管理员身份运行
  echo [install] Administrator rights required: right-click this file, Run as administrator
  pause
  exit /b 1
)

rem Release zip: MemSweep.exe next to install.cmd; dev tree: dist\MemSweep.exe
set SRC=%~dp0MemSweep.exe
if not exist "%SRC%" set SRC=%~dp0dist\MemSweep.exe
set DST=%ProgramFiles%\MemSweep
if not exist "%SRC%" (
  echo [install] 未找到 MemSweep.exe（应与 install.cmd 同目录，或位于 dist\）
  echo [install] MemSweep.exe not found; it belongs next to install.cmd, or in dist\
  pause
  exit /b 1
)

echo [install] 停止旧实例...
if exist "%DST%\MemSweep.exe" (
  "%DST%\MemSweep.exe" /exit >nul 2>&1
  timeout /t 2 /nobreak >nul
)
taskkill /IM MemSweep.exe /F >nul 2>&1

echo [install] 复制到 %DST% ...
if not exist "%DST%" mkdir "%DST%"
copy /y "%SRC%" "%DST%\MemSweep.exe" >nul

echo [install] 创建开机自启计划任务（管理员、无 UAC 弹窗）...
schtasks /Create /F /TN "MemSweep" /TR "\"%DST%\MemSweep.exe\" /tray" /SC ONLOGON /RL HIGHEST >nul
if errorlevel 1 (
  echo [install] 计划任务创建失败，请检查事件日志
  echo [install] Could not create the scheduled task; check the event log
  pause
  exit /b 1
)

echo [install] 修正电源策略与执行时限（笔记本电池供电也启动，不限 3 天）...
powershell -NoProfile -ExecutionPolicy Bypass -Command "$s = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -StartWhenAvailable -ExecutionTimeLimit ([TimeSpan]::Zero) -MultipleInstances IgnoreNew; Set-ScheduledTask -TaskName 'MemSweep' -Settings $s | Out-Null" >nul 2>&1

echo [install] 创建桌面快捷方式...
powershell -NoProfile -Command "$ws=New-Object -ComObject WScript.Shell; $l=$ws.CreateShortcut([Environment]::GetFolderPath('Desktop')+'\MemSweep.lnk'); $l.TargetPath='%DST%\MemSweep.exe'; $l.Arguments='/show'; $l.WorkingDirectory='%DST%'; $l.IconLocation='%DST%\MemSweep.exe,0'; $l.Save()" >nul 2>&1

echo [install] 启动...
start "" "%DST%\MemSweep.exe" /tray

echo.
echo [install] 完成：MemSweep 已在托盘运行
echo   热键：默认 Ctrl+Shift+M（可在设置里修改）
echo   托盘：左键面板 / 右键菜单 / 中键秒清
echo   数据：%APPDATA%\MemSweep\（config.ini、log.txt）
echo   卸载：uninstall.cmd
echo.
echo [install] Done: MemSweep is running in the tray
echo   Hotkey: Ctrl+Shift+M by default (change it in Settings)
echo   Tray: left-click panel / right-click menu / middle-click clean
echo   Data: %APPDATA%\MemSweep\ (config.ini, log.txt)
echo   Uninstall: uninstall.cmd
echo.
pause
