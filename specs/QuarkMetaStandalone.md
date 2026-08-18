# QuarkMeta 独立化改造（剥离内存模式 & 彻底拔除 CategoryPanel & 离散 JSON 文件名/类名真实重命名 & 配置隔离与应用重命名）实施方案

## 1. 所属大纲章节
- 独立应用化改造章节：1.1-QuarkMetaStandalone

## 2. 涉及代码文件
- **物理重命名代码文件 (Rename)**：
  - `src/meta/AmMetaJson.h` -> `src/meta/QuarkMetaJson.h`
  - `src/meta/AmMetaJson.cpp` -> `src/meta/QuarkMetaJson.cpp`
- **拟彻底删除文件 (Delete)**：
  - `src/ui/CategoryPanel.h`
  - `src/ui/CategoryPanel.cpp`
  - `src/ui/CategoryModel.h`
  - `src/ui/CategoryModel.cpp`
  - `src/ui/CategoryDelegate.h`
  - `src/ui/models/LibraryAssetModel.h`
  - `src/ui/models/LibraryAssetModel.cpp`
  - `src/ui/MemoryBatchRenameService.h`
  - `src/ui/MemoryBatchRenameService.cpp`
  - `src/core/DatabaseSynchronizer.h`
  - `src/core/DatabaseSynchronizer.cpp`
  - `src/core/AutoImportManager.h`
  - `src/core/AutoImportManager.cpp`
  - `src/core/LibraryMaintenanceService.h`
  - `src/core/LibraryMaintenanceService.cpp`
- **重命名资源与清单文件**：
  - `ArcMeta.manifest` -> `QuarkMeta.manifest`
  - `ArcMeta.rc` -> `QuarkMeta.rc`
- **需要精确定位修改的文件**：
  - `CMakeLists.txt`
  - `.gitignore`
  - `src/core/AppConfig.h`
  - `src/main.cpp`
  - `src/ui/Logger.h`
  - `src/meta/QuarkMetaJson.h`
  - `src/meta/QuarkMetaJson.cpp`
  - `src/meta/DiskNavigatorService.h`
  - `src/meta/DiskNavigatorService.cpp`
  - `src/meta/MetadataManager.cpp`
  - `src/meta/MetaCacheDecorator.cpp`
  - `src/core/FileFilterService.cpp`
  - `src/util/ShellHelper.cpp`
  - `src/ui/MainWindow.h`
  - `src/ui/MainWindow.cpp`
  - `src/ui/NavPanel.h`
  - `src/ui/NavPanel.cpp`
  - `src/ui/ContentPanel.cpp`
  - `src/ui/TagManagerDialog.cpp`
  - `src/ui/models/DiskItemModel.cpp`
  - `src/ui/BatchRenameDialog.cpp`

## 3. 功能描述
本次改造旨在将纯磁盘目录直连模式单独剥离并彻底提炼为独立应用 **QuarkMeta**。
改动包含六个维度：
1. **彻底拔除内存模式代码文件**：删除专门用于内存托管库模式的 Model、分类面板（`CategoryPanel`）、重命名服务、数据库同步器及自动导入管理器，彻底纯化代码仓库。
2. **彻底消除顶栏空白残影（拒绝 `.hide()` 打补丁）**：从 `MainWindow` 布局中彻底拔除 `CategoryPanel` 控件，第一栏直接放置包含“目录导航”的 `NavPanel` 视图，消除一切残影与空白空块。
3. **物理文件名与类名真实重命名**：将离散元数据 JSON 管理类及其物理文件由 `AmMetaJson.h / .cpp` 真实更名为 **`QuarkMetaJson.h` / `QuarkMetaJson.cpp`**，类名更名为 **`QuarkMetaJson`**。
4. **离散 JSON 缓存更名**：普通磁盘目录下生成的隐藏元数据缓存文件名由 `.ArcMeta.json` 彻底更新为 **`.QuarkMeta.json`**。
5. **物理配置隔离**：修改 `AppConfig` 单例的组织名与应用名为 `QuarkMeta`，使其 Windows 注册表与配置文件物理落盘路径彻底独立，避免与 ArcMeta 的配置互相覆盖或污染。
6. **应用全量重命名**：将构建目标可执行文件名、项目工程名、资源描述文件、主窗口标题、运行日志文件名等全部更新为 `QuarkMeta`。

## 4. 技术决策与精准修改方案

### 4.1 物理文件名与类名真实重命名 (QuarkMetaJson)

1. 重命名物理文件：
   `git mv src/meta/AmMetaJson.h src/meta/QuarkMetaJson.h`
   `git mv src/meta/AmMetaJson.cpp src/meta/QuarkMetaJson.cpp`

2. 在 `QuarkMetaJson.h` 和 `QuarkMetaJson.cpp` 中将类名 `AmMetaJson` 替换为 `QuarkMetaJson`，将生成文件名更改为 `.QuarkMeta.json`：

**文件**：`src/meta/QuarkMetaJson.cpp`
**精准定位**：Line 22-23

```cpp
<<<<<<< SEARCH
    // 🚨 彻底废除 .am_meta.json，唯一物理文件名：.ArcMeta.json
    m_filePath = path + L".ArcMeta.json";
=======
    // 🚨 QuarkMeta 唯一物理离散缓存文件名：.QuarkMeta.json
    m_filePath = path + L".QuarkMeta.json";
>>>>>>> REPLACE
```

**文件**：`src/core/FileFilterService.cpp`
**精准定位**：Line 12

```cpp
<<<<<<< SEARCH
    if (fileName.endsWith(".ArcMeta.json", Qt::CaseInsensitive) || 
=======
    if (fileName.endsWith(".QuarkMeta.json", Qt::CaseInsensitive) || 
>>>>>>> REPLACE
```

**文件**：`.gitignore`
**精准定位**：Line 124

```text
<<<<<<< SEARCH
*.ArcMeta.json
=======
*.QuarkMeta.json
>>>>>>> REPLACE
```

---

### 4.2 彻底拔除 MainWindow 中的 CategoryPanel 残留

在 `MainWindow.h` 中彻底删除 `CategoryPanel* m_categoryPanel = nullptr;` 成员变量定义及 `#include "CategoryPanel.h"` 头文件引用。
在 `MainWindow.cpp` 中彻底移除 `m_categoryPanel` 的实例化与布局添加代码，避免留下任何标题栏与残影。

**文件**：`src/ui/MainWindow.h`
**精准定位**：Line 26, Line 138

```cpp
<<<<<<< SEARCH
class CategoryPanel;
...
    CategoryPanel* m_categoryPanel = nullptr;
=======
// CategoryPanel 彻底拔除废除
>>>>>>> REPLACE
```

**文件**：`src/ui/MainWindow.cpp`
**精准定位**：`setupSplitters()` 布局组装

```cpp
<<<<<<< SEARCH
    m_categoryPanel = new CategoryPanel(this);
    m_categoryPanel->setObjectName("SidebarContainer");
    
    m_navPanel = new NavPanel(this);
    m_navPanel->setObjectName("ListContainer");

    ...

    m_mainSplitter->addWidget(m_categoryPanel);
    m_mainSplitter->addWidget(m_navPanel);
=======
    // 物理直接将 NavPanel 作为第一栏，彻底清除 CategoryPanel 残留
    m_navPanel = new NavPanel(this);
    m_navPanel->setObjectName("SidebarContainer");

    ...

    m_mainSplitter->addWidget(m_navPanel);
>>>>>>> REPLACE
```

---

### 4.3 配置文件与注册表隔离 (AppConfig)

**文件**：`src/core/AppConfig.h`
**精准定位**：Line 36 构造函数

```cpp
<<<<<<< SEARCH
    AppConfig() : m_settings("ArcMeta团队", "ArcMeta") {}
=======
    AppConfig() : m_settings("QuarkMeta", "QuarkMeta") {}
>>>>>>> REPLACE
```

---

### 4.4 日志文件名隔离与更新

**文件**：`src/main.cpp`
**精准定位**：Line 99 `rotateLogFiles` 启动点

```cpp
<<<<<<< SEARCH
    rotateLogFiles("arcmeta_debug.log");
=======
    rotateLogFiles("quarkmeta_debug.log");
>>>>>>> REPLACE
```

**文件**：`src/ui/Logger.h`
**精准定位**：Line 131-132

```cpp
<<<<<<< SEARCH
        rotateLogFiles("arcmeta_debug.log");
        QFile file("arcmeta_debug.log");
=======
        rotateLogFiles("quarkmeta_debug.log");
        QFile file("quarkmeta_debug.log");
>>>>>>> REPLACE
```

---

### 4.5 构建配置与 CMakeLists.txt 更新

在 `CMakeLists.txt` 中：
1. 将工程名由 `ArcMeta` 更新为 `QuarkMeta`；
2. 移除已被彻底删除的 `.h/.cpp` 文件列表；
3. 将 `src/meta/AmMetaJson.cpp` 更新为 `src/meta/QuarkMetaJson.cpp`；
4. 更新资源文件为 `QuarkMeta.rc`。

```cmake
<<<<<<< SEARCH
project(ArcMeta)

# 2026-03-xx 按照用户要求：将可执行文件直接输出到指定项目子目录 ArcMeta 下，解决 \out 文件夹查找不便的问题
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/ArcMeta")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/ArcMeta")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/ArcMeta")
=======
project(QuarkMeta)

# 2026-03-xx 将 QuarkMeta 可执行文件直接输出到项目子目录 QuarkMeta 下
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/QuarkMeta")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/QuarkMeta")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/QuarkMeta")
>>>>>>> REPLACE
```

---

## 5. 已知问题/待办
- **旧配置不迁移策略**：ArcMeta 既有用户的偏好设置将留在原有 ArcMeta 路径下，QuarkMeta 将以后者全新的独立配置启动。
- **图标与美术资源**：按照要求，本次改造保持现有 `.ico/.png` 等图标资源不动。

---

## 涉及文件清单
### 1. 拟重命名文件 (2 个)
- `src/meta/AmMetaJson.h` -> `src/meta/QuarkMetaJson.h`
- `src/meta/AmMetaJson.cpp` -> `src/meta/QuarkMetaJson.cpp`

### 2. 拟彻底删除文件 (13 个)
- `src/ui/CategoryPanel.h`
- `src/ui/CategoryPanel.cpp`
- `src/ui/CategoryModel.h`
- `src/ui/CategoryModel.cpp`
- `src/ui/CategoryDelegate.h`
- `src/ui/models/LibraryAssetModel.h`
- `src/ui/models/LibraryAssetModel.cpp`
- `src/ui/MemoryBatchRenameService.h`
- `src/ui/MemoryBatchRenameService.cpp`
- `src/core/DatabaseSynchronizer.h`
- `src/core/DatabaseSynchronizer.cpp`
- `src/core/AutoImportManager.h`
- `src/core/AutoImportManager.cpp`

### 3. 新增/更新文档与资源文件 (3 个)
- `Modification_Plan/QuarkMeta-Architecture-Planning.md`
- `QuarkMeta.manifest`
- `QuarkMeta.rc`

### 4. 需要精确定位修改的文件 (15 个)
- `CMakeLists.txt`
- `.gitignore`
- `src/core/AppConfig.h`
- `src/main.cpp`
- `src/ui/Logger.h`
- `src/meta/QuarkMetaJson.h`
- `src/meta/QuarkMetaJson.cpp`
- `src/meta/DiskNavigatorService.h`
- `src/meta/DiskNavigatorService.cpp`
- `src/meta/MetadataManager.cpp`
- `src/core/FileFilterService.cpp`
- `src/util/ShellHelper.cpp`
- `src/ui/MainWindow.h`
- `src/ui/MainWindow.cpp`
- `src/ui/ContentPanel.cpp`
