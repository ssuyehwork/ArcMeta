@echo off
echo ========================================
echo 配置Windows崩溃转储文件自动生成
echo ========================================
echo.

REM 设置注册表键值以启用崩溃转储
reg add "HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps" /v DumpType /t REG_DWORD /d 2 /f
reg add "HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps" /v DumpCount /t REG_DWORD /d 10 /f
reg add "HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps" /v DumpFolder /t REG_EXPAND_SZ /d "%LOCALAPPDATA%\CrashDumps" /f

REM 为ArcMeta.exe创建专用转储设置
reg add "HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\ArcMeta.exe" /v DumpType /t REG_DWORD /d 2 /f
reg add "HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\ArcMeta.exe" /v DumpCount /t REG_DWORD /d 10 /f
reg add "HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\ArcMeta.exe" /v DumpFolder /t REG_EXPAND_SZ /d "%LOCALAPPDATA%\CrashDumps" /f

echo.
echo ✅ 崩溃转储配置完成！
echo 📁 转储文件将保存到: %LOCALAPPDATA%\CrashDumps
echo.
echo 接下来请运行ArcMeta.exe触发崩溃，然后使用WinDbg分析转储文件
echo.
pause
