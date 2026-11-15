@echo off
setlocal
cd /d "%~dp0"

net session >nul 2>&1
if errorlevel 1 (
  echo [install] 需要管理员权限：请右键本文件 -^> 以管理员身份运行
  pause
  exit /b 1
)

set SRC=%~dp0dist\MemSweep.exe
set DST=%ProgramFiles%\MemSweep
if not exist "%SRC%" (
  echo [install] 未找到 %SRC%，请先运行 build.cmd
  pause
  exit /b 1
)

echo [install] 停止旧实例...
taskkill /IM MemSweep.exe /F >nul 2>&1

echo [install] 复制到 %DST% ...
if not exist "%DST%" mkdir "%DST%"
copy /y "%SRC%" "%DST%\MemSweep.exe" >nul

echo [install] 创建开机自启计划任务（管理员、无 UAC 弹窗）...
schtasks /Create /F /TN "MemSweep" /TR "\"%DST%\MemSweep.exe\" /tray" /SC ONLOGON /RL HIGHEST >nul
if errorlevel 1 (
  echo [install] 计划任务创建失败，请检查事件日志
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
echo   热键：Ctrl+Shift+M（立即清理）
echo   托盘：左键面板 / 右键菜单 / 中键秒清
echo   数据：%APPDATA%\MemSweep\（config.ini、log.txt）
echo   卸载：uninstall.cmd
echo.
pause
