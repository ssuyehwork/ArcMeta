# ArcMeta崩溃转储文件分析指南

## 🎯 配置完成步骤

### 1. 启用崩溃转储
- 运行 `configure_crash_dumps.bat` 以管理员权限
- 这将配置Windows自动生成崩溃转储文件
- 转储文件保存位置：`%LOCALAPPDATA%\CrashDumps\`

### 2. 启动调试模式
- 运行 `launch_with_debug.bat`
- 这将启用Qt调试插件和详细日志
- 所有输出将保存到 `debug_logs` 目录

## 🔍 分析转储文件

### 使用WinDbg分析
1. 安装Windows SDK获取WinDbg
2. 打开WinDbg: `windbg -z "转储文件路径.dmp"`
3. 设置符号路径：
   ```
   .sympath+ C:\Qt\6.10.2\msvc2022_64\bin
   .sympath+ http://msdl.microsoft.com/download/symbols
   .reload
   ```
4. 查看崩溃信息：
   ```
   !analyze -v
   kv
   lm
   ```

### 常见崩溃原因排查
- **缺失DLL**: 使用 `dependency walker` 检查
- **Qt插件问题**: 检查 `platforms` 目录
- **数据库连接失败**: 检查SQLite驱动
- **内存访问违例**: 查看堆栈跟踪中的指针操作

## 📋 需要检查的关键文件

1. **Qt运行时库**:
   - `Qt6Widgets.dll`
   - `Qt6Core.dll`
   - `Qt6Gui.dll`

2. **平台插件**:
   - `platforms\qwindows.dll`

3. **数据库驱动**:
   - `sqldrivers\qsqlite.dll`

## 🚨 紧急修复方案

如果发现是依赖项问题：
1. 使用Qt Maintenance Tool重新安装
2. 检查PATH环境变量
3. 确保所有必需的DLL都在程序目录

## 📊 调试日志分析

查看 `debug_logs\startup_log_*.txt` 文件：
- 查找 "ERROR" 或 "FATAL" 关键字
- 检查插件加载失败信息
- 分析数据库初始化错误
