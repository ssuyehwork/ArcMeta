@echo off
echo ========================================
echo 启动 ArcMeta 并捕获调试信息
echo ========================================
echo.

REM 创建日志目录
if not exist "debug_logs" mkdir debug_logs

REM 设置环境变量以启用更多调试信息
set QT_DEBUG_PLUGINS=1
set QT_LOGGING_RULES="*=true"
set QT_ASSUME_STDERR_HAS_CONSOLE=1

echo 🚀 正在启动 ArcMeta.exe...
echo 📋 调试信息将输出到控制台
echo 📍 如果程序崩溃，转储文件将保存到: %%LOCALAPPDATA%%\CrashDumps
echo.

REM 启动程序
REM 2026-06-xx 物理修复：根据 CMake 输出路径，ArcMeta.exe 位于 ArcMeta/ 目录下
ArcMeta\ArcMeta.exe

echo.
echo ⚠️ 程序已退出
echo 📄 请检查 arcmeta_debug.log 以获取详细运行轨迹
echo 📄 如果发生崩溃，请检查 %%LOCALAPPDATA%%\CrashDumps 目录中的 .dmp 文件
echo.
pause
