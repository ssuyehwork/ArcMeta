@echo off
echo ========================================
echo 启动FERREX并捕获调试信息
echo ========================================
echo.

REM 创建日志目录
if not exist "debug_logs" mkdir debug_logs

REM 设置环境变量以启用更多调试信息
set QT_DEBUG_PLUGINS=1
set QT_LOGGING_RULES="*=true"
set QT_ASSUME_STDERR_HAS_CONSOLE=1

echo 🚀 正在启动FERREX.exe...
echo 📋 调试信息将输出到控制台
echo 📍 如果程序崩溃，转储文件将保存到: %LOCALAPPDATA%\CrashDumps
echo.

REM 启动程序并捕获输出
FERREX\FERREX.exe 2>&1 | tee debug_logs\startup_log_%date:~0,4%%date:~5,2%%date:~8,2%_%time:~0,2%%time:~3,2%%time:~6,2%.txt

echo.
echo ⚠️ 程序已退出
echo 📄 请检查 debug_logs 目录中的日志文件
echo 📄 如果发生崩溃，请检查 %LOCALAPPDATA%\CrashDumps 目录中的 .dmp 文件
echo.
pause
