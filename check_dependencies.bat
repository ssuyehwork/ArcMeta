@echo off
echo ========================================
echo 检查FERREX依赖项完整性
echo ========================================
echo.

set "PROGRAM_PATH=%~dp0FERREX\FERREX.exe"
set "QT_PATH=C:\Qt\6.10.2\msvc2022_64"

echo 📋 正在检查程序文件...
if exist "%PROGRAM_PATH%" (
    echo ✅ FERREX.exe 存在
) else (
    echo ❌ FERREX.exe 不存在！
    pause
    exit /b 1
)

echo.
echo 🔍 正在检查Qt运行时库...

REM 检查核心Qt DLL
set "QT_DLLS[0]=Qt6Core.dll"
set "QT_DLLS[1]=Qt6Widgets.dll"
set "QT_DLLS[2]=Qt6Gui.dll"
set "QT_DLLS[3]=Qt6Sql.dll"
set "QT_DLLS[4]=Qt6Svg.dll"
set "QT_DLLS[5]=Qt6Concurrent.dll"

for %%i in (0 1 2 3 4 5) do (
    call set "DLL=%%QT_DLLS[%%i]%%"
    if exist "%QT_PATH%\bin\!DLL!" (
        echo ✅ !DLL! 存在
    ) else (
        echo ❌ !DLL! 缺失！
    )
)

echo.
echo 🗂️ 正在检查平台插件...
if exist "%QT_PATH%\plugins\platforms\qwindows.dll" (
    echo ✅ qwindows.dll 存在
) else (
    echo ❌ qwindows.dll 缺失！
)

echo.
echo 💾 正在检查数据库驱动...
if exist "%QT_PATH%\plugins\sqldrivers\qsqlite.dll" (
    echo ✅ qsqlite.dll 存在
) else (
    echo ❌ qsqlite.dll 缺失！
)

echo.
echo 🔗 正在检查系统PATH...
echo %PATH% | findstr /i "qt" >nul
if %errorlevel% == 0 (
    echo ✅ Qt路径在PATH中
) else (
    echo ⚠️ Qt路径可能不在PATH中
)

echo.
echo 📊 正在检查Visual C++运行时...
where vcruntime140.dll >nul 2>&1
if %errorlevel% == 0 (
    echo ✅ Visual C++运行时存在
) else (
    echo ⚠️ Visual C++运行时可能缺失
)

echo.
echo ========================================
echo 依赖项检查完成！
echo ========================================
echo.
echo 📝 如果发现缺失项，请：
echo 1. 重新安装Qt 6.10.2 MSVC2022 64位版本
echo 2. 安装Visual C++ Redistributable 2022
echo 3. 确保PATH包含Qt bin目录
echo.
pause
