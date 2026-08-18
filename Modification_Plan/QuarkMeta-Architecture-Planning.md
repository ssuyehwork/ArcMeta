# QuarkMeta 独立化设计理念与规划规范 (QuarkMeta Architecture and Planning)

## 0. 设计理念与架构重构总则
QuarkMeta 为纯磁盘目录直连模式独立应用。通过彻底剔除原 ArcMeta 内存托管库/镜像数据库及全量内存索引相关代码，构建轻量、高效的实时磁盘 I/O 浏览与管理体验。

---

## 1. 界面面板与三栏式视图布局规范 (UI Panel & Three-Column View Specification)

### 1.1 三栏式侧边与主体结构布局 (Three-Column Layout Architecture)
在 QuarkMeta 纯磁盘直连模式下，系统彻底废除原有的“侧边栏分类面板”（`CategoryPanel`），严禁采用任何 `.hide()` 等打补丁隐藏方式留下空白残影顶栏。界面架构调整为如下从左至右的无痕三栏结构：

```
+------------------------+------------------------+------------------------------------+
|  第一栏：目录导航      |  第二栏：收藏夹        |  第三栏：内容展示与元数据区        |
|  (DirNav / Left)       |  (Favorites / Middle)  |  (Content & Meta / Right)          |
+------------------------+------------------------+------------------------------------+
|  - 此电脑              |  - 快捷访问路径 1      |  - 缩略图/列表视图                 |
|  - C:\, D:\, E:\...    |  - 快捷访问路径 2      |  - 文件元数据与筛选面板            |
|  - 桌面 / 文档...      |  - 常用文件/文件夹     |                                    |
+------------------------+------------------------+------------------------------------+
```

#### A. 磁盘目录模式下：
1. **第一栏（原分类面板位置）：目录导航栏（Dir Tree Navigation）**
   - 彻底移除 `CategoryPanel` 控件。将原中间栏的目录树（此电脑、本地盘符、桌面等系统常用目录树）直接放置于最左侧第一栏，顶格占满全高空间。
   - 彻底清除原 ArcMeta 的分类标题栏（如“文件夹”及两侧图标按钮）与分类树，专职负责本地磁盘目录结构的折叠与展开导航。

2. **第二栏（中间栏）：收藏夹独占栏（Dedicated Favorites Bar）**
   - 移出目录导航树后，中间第二栏由“收藏夹”（Favorites）独立占满整栏（从顶部延伸至底部）。
   - 提供更宽广、更清晰的常用文件/文件夹快捷访问区域，支持拖拽添加与快捷键导航。

3. **第三栏（右侧）：内容面板与辅助面板区（Content & Inspector Area）**
   - 包含主视图 `ContentPanel`（网格/列表视图）、右侧 `MetaPanel`（元数据编辑与属性）以及 `FilterPanel`（条件筛选）。

---

## 2. 磁盘模式离散 JSON 元数据缓存与代码类名物理重命名规范 (QuarkMetaJson Specification)

### 2.1 物理文件名与类名重命名规范
- **原物理文件名**：`src/meta/AmMetaJson.h` / `src/meta/AmMetaJson.cpp`
- **新物理文件名**：`src/meta/QuarkMetaJson.h` / `src/meta/QuarkMetaJson.cpp`
- **原 C++ 类名**：`AmMetaJson`
- **新 C++ 类名**：`QuarkMetaJson`
- **原生成离散 JSON 文件名**：`.ArcMeta.json`
- **新生成离散 JSON 文件名**：`.QuarkMeta.json`

### 2.2 处理原则
在 QuarkMeta 磁盘直连模式下：
1. 涉及磁盘离散元数据 JSON 的读写管理类统一重命名为 **`QuarkMetaJson`**，对应头文件为 **`QuarkMetaJson.h`**。
2. 用户对普通物理文件夹或文件进行标注时，落盘的离散元数据缓存文件名统一使用 **`.QuarkMeta.json`**，防止在磁盘上残留旧应用标识。
3. `.gitignore` 中原 `*.ArcMeta.json` 规则同步更新为 `*.QuarkMeta.json`。

---

## 3. 独立化应用改造与配置隔离规范 (Standalone Application & Isolation Specification)

### 3.1 依赖清场与文件彻底拔除规范 (Code Deprecation)
彻底拔除并删除专门服务于内存托管模式的代码文件及 UI 控件：
- 分类面板组件：`CategoryPanel.h / .cpp`，`CategoryModel.h / .cpp`，`CategoryDelegate.h`
- 内存资产模型：`LibraryAssetModel.h / .cpp`
- 内存重命名服务：`MemoryBatchRenameService.h / .cpp`
- 数据库与同步器：`DatabaseSynchronizer.h / .cpp`
- 自动导入与维护服务：`AutoImportManager.h / .cpp`，`LibraryMaintenanceService.h / .cpp`

### 3.2 配置与日志隔离规范 (Config & Log Isolation)
- **物理配置落盘**：`AppConfig` 的 `QSettings` 实例化强制改为 `m_settings("QuarkMeta", "QuarkMeta")`，使得注册表/INI文件存储于专属于 QuarkMeta 的全新路径，绝对禁止与 ArcMeta 共享或覆盖配置。
- **运行日志隔离**：全局运行与调试日志由 `arcmeta_debug.log` 重命名为 `quarkmeta_debug.log`。
- **可执行文件与项目重命名**：构建目标、可执行文件及 Windows 资源清单统一更名为 `QuarkMeta.exe` / `QuarkMeta.manifest` / `QuarkMeta.rc`。
