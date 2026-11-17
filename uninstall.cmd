@echo off
rem This file is UTF-8; switch the console to UTF-8 so Chinese text is readable.
chcp 65001 >nul
setlocal
cd /d "%~dp0"

net session >nul 2>&1
if errorlevel 1 (
  echo [uninstall] 需要管理员权限：请右键本文件 -^> 以管理员身份运行
  echo [uninstall] Administrator rights required: right-click this file, Run as administrator
  pause
  exit /b 1
)

echo [uninstall] 退出 MemSweep...
"%ProgramFiles%\MemSweep\MemSweep.exe" /exit >nul 2>&1
timeout /t 2 /nobreak >nul
taskkill /IM MemSweep.exe /F >nul 2>&1

echo [uninstall] 删除计划任务...
schtasks /Delete /F /TN "MemSweep" >nul 2>&1

echo [uninstall] 删除程序目录...
if exist "%ProgramFiles%\MemSweep" rmdir /s /q "%ProgramFiles%\MemSweep"

echo [uninstall] 保留配置与日志：%APPDATA%\MemSweep（如需彻底清除请手动删除）
echo [uninstall] Settings and logs kept in %APPDATA%\MemSweep (delete the folder to remove them)
echo [uninstall] 完成。
echo [uninstall] Done.
echo.
pause
