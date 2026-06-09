# Analysis_Modification_Plan: 架构演进与冗余逻辑彻底清理方案

## 1. 核心目标
本方案旨在指导系统完成两大核心变革：
1. **架构范式转移**：从分布式二进制文件管理，彻底切换为“**SQLite 内存模式**”（One-Drive-One-DB + Global-DB）。
2. **UI 功能核平**：彻底废除并移除 `ScanDialog` 相关的所有界面与业务逻辑，简化系统入口。

---

## 2. 方案一：SQLite 内存模式详细设计

### 2.1 数据库存储与属性管理 (Storage & Attributes)

#### A. 存储位置与命名规则 (Storage & Naming)
所有数据库文件统一存放于程序根目录下的 `.arcmeta` 文件夹中。
- **物理层数据库**：命名格式为 `Arcmeta_XXXX.db`（XXXX 为磁盘卷序列号）。
- **逻辑层数据库**：命名为 `global.db`。

#### B. 隐藏属性 (Hidden Attributes)
必须通过 WinAPI 强制设置文件夹及文件的隐藏属性。

### 2.2 SQLite 项目集成指南 (Critical: 解决编译错误)

由于系统环境尚未包含 SQLite 开发库，出现“无法打开包括文件: sqlite3.h”错误。请按以下步骤集成：

1. **创建目录**：在项目中创建 `src/util/sqlite/` 文件夹。
2. **放置文件**：将从官网下载的 `sqlite3.h` 和 `sqlite3.c` 放入该文件夹。
3. **更新 CMakeLists.txt**：
   在 `set(SOURCES ...)` 列表中添加以下两行：
   ```cmake
   src/util/sqlite/sqlite3.h
   src/util/sqlite/sqlite3.c
   ```
4. **添加包含路径**：
   在 `CMakeLists.txt` 中添加：
   ```cmake
   include_directories(src/util/sqlite)
   ```
5. **代码中引用**：
   使用 `#include "sqlite3.h"`（注意是双引号，指向本地项目路径）。

---

## 3. 方案二：彻底废除 ScanDialog 模块

### 3.1 物理移除清单
- `src/ui/ScanDialog.h` / `src/ui/ScanDialog.cpp`
- `src/ui/ScanController.h` / `src/ui/ScanController.cpp`

### 3.2 代码逻辑剥离点
1. **MainWindow.cpp**：移除 `setupCustomTitleBarButtons()` 中关于 `m_btnScan` 的定义、信号槽及图标设置。
2. **CMakeLists.txt**：从 `SOURCES` 列表中同步移除上述四个文件的引用。

---

## 4. 架构影响评估
- **编译修复**：通过本地包含 `sqlite3.c/h`，彻底解决环境依赖缺失导致的编译失败。
- **性能红线**：所有查询基于 SQLite 索引，利用 `sqlite3_backup` 实现内存与磁盘的零延迟同步。

---
**资深程序员意见**：
编译错误是因为项目开始引用 SQLite 逻辑但尚未“喂入”源码。按照 2.2 节的操作，将 SQLite 源码直接作为项目的一部分编译，是目前 Windows 桌面开发中最稳健、最简单的集成方案。
