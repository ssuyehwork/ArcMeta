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
- **物理层数据库**：命名格式为 `Arcmeta_XXXX.db`（XXXX 为磁盘卷序列号）。例如，卷序列号为 `A1B2C3D4` 的磁盘，其数据库文件为 `Arcmeta_A1B2C3D4.db`。
- **逻辑层数据库**：命名为 `global.db`，存储跨盘分类及全局设置。

#### B. 隐藏属性 (Hidden Attributes)
必须通过 WinAPI 强制设置文件夹及文件的隐藏属性，确保系统环境的纯净。
- **逻辑实现**：
  ```cpp
  // 使用 WinAPI 设置隐藏属性
  #include <windows.h>
  SetFileAttributesW(L".arcmeta", FILE_ATTRIBUTE_HIDDEN);
  SetFileAttributesW(L".arcmeta/Arcmeta_XXXX.db", FILE_ATTRIBUTE_HIDDEN);
  ```

### 2.2 数据库结构设计 (Schema)

#### 物理层数据库 (`Arcmeta_XXXX.db`)
```sql
CREATE TABLE IF NOT EXISTS metadata (
    file_id TEXT PRIMARY KEY,        -- 128-bit File ID 或 Fallback ID
    path TEXT NOT NULL,              -- 归一化物理路径
    is_folder INTEGER DEFAULT 0,     -- 是否为文件夹
    rating INTEGER DEFAULT 0,        -- 评分
    color TEXT,                      -- 颜色标记
    tags TEXT,                       -- 标签 (逗号分隔)
    note TEXT,                       -- 备注
    url TEXT,                        -- 关联 URL
    ctime INTEGER,                   -- 创建时间
    mtime INTEGER,                   -- 修改时间
    atime INTEGER,                   -- 访问时间
    file_size INTEGER,               -- 文件大小
    palettes BLOB                    -- 调色盘数据
);
CREATE INDEX IF NOT EXISTS idx_path ON metadata(path);
```

### 2.3 内存同步机制 (The In-Memory Logic)
系统**仅在内存中**操作数据库，磁盘文件仅作为冷备份。
1. **启动加载**：通过卷序列号定位对应的 `Arcmeta_XXXX.db`，利用 `sqlite3_backup` API 全量克隆至 `:memory:` 连接中。
2. **退出持久化**：在程序关闭或定时器触发时，将内存库内容写回 `.arcmeta` 下的隐藏 DB 文件。

---

## 3. 方案二：彻底废除 ScanDialog 模块

### 3.1 物理移除清单
- `src/ui/ScanDialog.h` / `src/ui/ScanDialog.cpp`
- `src/ui/ScanController.h` / `src/ui/ScanController.cpp`

### 3.2 代码逻辑剥离点
1. **MainWindow.cpp**：
   - 彻底删除 `setupCustomTitleBarButtons()` 中关于 `m_btnScan` 的定义及信号槽连接。
   - 移除相关的按钮图标设置。
2. **CMakeLists.txt**：
   - 从 `SOURCES` 列表中同步移除上述四个文件的引用，确保构建系统纯净。

---

## 4. 迁移与开箱即用保障
由于用户已手动清理所有旧版 `.SCCH` 文件，`MetadataManager` 初始化时应：
1. **探测目录**：检测程序根目录下是否存在 `.arcmeta` 文件夹，若无则创建。
2. **创建库**：为每个当前挂载的磁盘创建对应的 `Arcmeta_XXXX.db` 并立即应用隐藏属性。
3. **初始化表**：执行 DDL 语句构建元数据表结构。

---
**资深程序员意见**：
更新后的命名规则 `Arcmeta_XXXX.db` 更加具品牌辨识度，且所有数据库集中于隐藏的 `.arcmeta` 目录下，既保证了数据的专业性，又避免了对用户文件系统的干扰。
