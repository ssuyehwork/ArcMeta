这份重构规划方案图纸（`Modification_Plan-1.md`）已为你全面梳理归纳完成。

方案涵盖了从 **`.arc` 物理资产包封装**、**侧边栏“一等公民”分类树**、**单文件/文件夹拖拽分流**、**多梯队缩略图生成**、到 **`added_at` 添加日期字段与排序**的全部重构落地细节，可以直接用于指导代码重构与批复：

---

# `.arc` 资产包封装架构与侧边栏一等公民分类系统极致重构 —— Modification_Plan-1.md

> 状态：待批准执行

## 1. 任务背景
针对现有版本中存在的“物理文件路径对账复杂、重命名/物理位移容易引发索引变灰与锁死、拖拽导入缺乏清晰分流规制、以及缺少添加日期排序”等一系列架构瓶颈，本方案旨在进行一次**从“物理文件追踪软件”向“专业数字资产管理系统（DAM）”的底层大重构**。

本次重构将全面采用 **`.arc` 资产包封装架构**，放弃旧有的复杂物理路径盲重对账逻辑；同时将 `ArcMeta.Library_[盘符]` 提拔为侧边栏分类树的直属一等公民，并在 UI 视图层彻底隐去 `.arc` 容器外壳，全量补齐“添加日期”元数据与排序体系。

---

## 2. 核心架构演进图谱

```text
[ 物理磁盘层 (D:\ArcMeta.Library_D\) ] ──> 极简平铺，绝对不深度嵌套
  ├── m8crzbs3jb7dj.arc/ ───> 原始文件.psd + _thumbnail.png (256x256)
  └── k9x2p1q4r8v5z.arc/ ───> 照片.jpg + _thumbnail.png (256x256)

[ 侧边栏分类树 (彻底废除“我的分类”外壳) ]
  ├── ArcMeta.Library_D (一等公民，parentId = 0)
  │   └── 文件夹 A (次等分类，parentId = Library_D)
  │       ├── 文件夹 B ───> [关联资产包：m8crzbs3jb7dj]
  │       └── 文件夹 C ───> [关联资产包：k9x2p1q4r8v5z]

[ SQLite 数据库 (唯一的真理源头) ]
  ├── metadata 表 ─────────> file_id ("m8crzbs3jb7dj") + added_at + 逻辑文件名 + 色彩 + 星级
  └── categories 表 ───────> id + parent_id + 逻辑分类名称 (重命名/移动 0.001ms 搞定)
```

---

## 3. 强制对照表

| 编号 | 用户原话 / 需求点 | 方案对应点 | 是否一致 |
|:---:|---|---|:---:|
| 1 | `ArcMeta.Library/[ID].arc` 1 个文件夹存储 1 个文件，放弃旧映射逻辑 | 详见 4.1 节，实现 `.arc` 物理封装与固定 ID 主键化，废除全量路径重对账 | ✅ |
| 2 | 不使用 `metadata.json`，全量使用 SQLite 数据库 | 详见 4.1 节，物理层仅存原文件+`_thumbnail.png`，元数据 100% 存在 SQLite DB | ✅ |
| 3 | 侧边栏废除“我的分类”外壳，`ArcMeta.Library_[盘符]` 为一等公民 (Root ID) | 详见 4.2 节，`ArcMeta.Library_[盘符]` 的 `parentId` 设为 `0`，直属侧边栏根节点 | ✅ |
| 4 | 拖拽文件夹 A (含 B、C) 创建次等子分类，拖拽单文件归属未分类 | 详见 4.3 节，实现 `AssetImporter` 自动化逻辑分流器 | ✅ |
| 5 | 内容面板只显示虚拟子分类和文件，UI 上 100% 隐去 `.arc` 文件夹 | 详见 4.6 节，`loadCategory` 与 `scanDir` 物理级过滤所有 `.arc` 目录 | ✅ |
| 6 | 补齐“添加日期” (`added_at`) 字段与“按添加日期排序”菜单 | 详见 4.5 节，全表与 Model 补齐 `added_at`，扩充 `SortByAddedDate` 枚举 | ✅ |

---

## 4. 详细重构解决方案

### 4.1 重构 1：`.arc` 物理资产包封装规范与 13 位 Base36 ID 生成器
*   **物理存储规范**：
    在磁盘库根目录 `[盘符]:/ArcMeta.Library_[盘符]/` 下直接平铺存放所有 `.arc` 包：
    ```text
    D:\ArcMeta.Library_D\m8crzbs3jb7dj.arc\
        ├── 原始设计稿.psd               <-- 真实物理源文件
        └── _thumbnail.png               <-- 256x256 高清预渲染缩略图
    ```
*   **13 位 Base36 ID 算法**：
    在 `src/util/ShellHelper.h` 中提供高性能 13 位唯一 ID 生成器：结合毫秒时间戳 + 12 位原子计数器进行 Base36 编码，生成形如 `m8crzbs3jb7dj` 的永不重复短字符串。
*   **物理与逻辑解耦**：
    *   物理硬盘上的 `.arc` 文件夹和原文件**永远不执行改名/位移动作**；
    *   软件内重命名、改分类、改标签，只更新 SQLite 数据库记录，耗时 $0.001 \text{ ms}$，彻底杜绝文件锁死与 Windows `MAX_PATH` 260 字符溢出报错。

### 4.2 重构 2：侧边栏“一等公民”分类树重构
*   **物理剥离“我的分类”**：
    在 `CategoryModel` 和 `CategoryPanel` 中，废除顶层“我的分类”虚拟外壳包装。
*   **一等公民设定**：
    所有已创建/激活托管库的盘符（如 `ArcMeta.Library_C`、`ArcMeta.Library_D`、`ArcMeta.Library_Z`）直接作为 `parentId = 0` 的根分类节点挂载在侧边栏分类树顶层。

### 4.3 重构 3：智能拖拽/导入分流器（`AssetImporter`）
*   **规则 A：拖拽单文件 / 散落多文件**
    1. 计算目标盘符托管库路径 `[盘符]:/ArcMeta.Library_[盘符]/`；
    2. 为每个文件生成 13 位 ID（如 `m8crzbs3jb7dj`），创建 `m8crzbs3jb7dj.arc/` 容器并移入文件；
    3. 异步提取生成 `_thumbnail.png`；
    4. 写入 SQLite `metadata` 表，**分类 ID 设为未分类（`UNCATEGORIZED_CAT_ID` / `-2`，不绑定任何次等分类）**；
    5. 界面立刻在“全部数据”与“未分类”中展示该资产。
*   **规则 B：拖拽“文件夹 A”（内含子目录 B、C）**
    1. **逻辑建树**：在 SQLite `categories` 表中创建：
       * `文件夹 A`（`parentId = ArcMeta.Library_[盘符]`）
       * `文件夹 B`（`parentId = 文件夹 A`）
       * `文件夹 C`（`parentId = 文件夹 A`）
    2. **物理打包**：递归将 A、B、C 里的所有文件统一生成 `.arc` 容器，平铺存放在 `[盘符]:/ArcMeta.Library_[盘符]/` 目录下。
    3. **数据库关联**：在 `category_items` 表中将资产 `file_id` 与对应“文件夹 B”或“文件夹 C”的逻辑分类 `id` 拉线绑定。

### 4.4 重构 4：多梯队 `_thumbnail.png` 高效生成流水线
在 `MediaExtractorPipeline` 中，导入文件时按以下 5 梯队生成 $256 \times 256$ 像素的 `_thumbnail.png` 存入 `.arc` 容器：
1. **普通图片 (JPG/PNG/WEBP/GIF/SVG/CUR)**：Qt `QImageReader` / `QSvgRenderer` 直接缩放（`1 ~ 3 ms`）；
2. **专业设计稿 (AI/EPS/PSD/PSB/PDF)**：纯 C++ 二进制流扫描头信息，剥离内嵌的 JPEG/PNG 预览块（`2 ~ 5 ms`）；
3. **视频文件 (MP4/MKV/MOV/AVI)**：Win32 Shell / MediaFoundation 抓取 1 秒处关键帧（`5 ~ 10 ms`）；
4. **Office 文档 (DOCX/XLSX/PPTX)**：直接解压 Zip 包内 `docProps/thumbnail.jpeg`（`2 ms`）；
5. **代码/纯文本 (TXT/MD/AHK/CPP)**：渲染纯净的格式品牌矢量图标。

### 4.5 重构 5：数据库 `added_at` 字段扩展与“按添加日期排序”
*   **SQLite 数据库升级**：在 `metadata` 表中增加 `added_at INTEGER DEFAULT 0` 字段（记录首次导入 ArcMeta 的毫秒时间戳）。
*   **数据模型同步**：在 `RuntimeMeta` 和 `ItemRecord` 中补齐 `long long addedAt` 字段。
*   **排序枚举扩展**：
    *   在 `ContentPanel.h` 的 `SortType` 枚举中增加 `SortByAddedDate`；
    *   在 `FilterProxyModel::lessThan` 比较引擎中增加 `case ContentPanel::SortByAddedDate: return leftRec.addedAt < rightRec.addedAt;`；
    *   在 `ContentPanel` 和 `MainWindow` 的右键“排序”二级菜单中加入 **“添加日期”** 选项。

### 4.6 重构 6：内容面板（`ContentPanel`）物理 `.arc` 容器全过滤
*   **分类模式（`loadCategory`）**：
    *   仅查询 `categories` 表拉取**虚拟子分类**；
    *   仅查询 `category_items` 关联表拉取 `.arc` 内部的**真实物理文件**（`m8crzbs3jb7dj.arc/原始文件.psd`）；
    *   物理 `.arc` 容器文件夹本身**绝对不进入 UI 列表**。
*   **磁盘模式（`loadDirectory`）**：
    *   在 `scanDir` 递归扫描中拦截：`if (info.isDir() && info.fileName().endsWith(".arc", Qt::CaseInsensitive)) continue;`；
    *   即便在磁盘导航模式下，用户也永远看不到任何 `.arc` 杂质容器。

---

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] `src/meta/DatabaseManager.cpp`（扩展 `metadata` 表 `added_at` 字段及索引）
- [ ] `src/core/ModelContract.h` / `src/meta/MetadataDefs.h`（增加 `addedAt` 时间戳字段）
- [ ] `src/meta/MetadataManager.h` / `src/meta/MetadataManager.cpp`（重构 `registerItem` 入库写入 `added_at`，清洗旧路径对账逻辑）
- [ ] `src/meta/CategoryRepo.h` / `src/meta/CategoryRepo.cpp`（清空 `syncPhysicalDirectoryCascade` 旧对账逻辑，配合一等公民 Root 分类树）
- [ ] `src/ui/CategoryModel.cpp` / `src/ui/CategoryPanel.cpp`（废除“我的分类”顶层节点，实现 `ArcMeta.Library_[盘符]` 一等公民树渲染）
- [ ] `src/ui/ContentPanel.h` / `src/ui/ContentPanel.cpp`（扩充 `SortByAddedDate` 排序，实现 `.arc` 目录过滤）
- [ ] `src/ui/CardPainterHelper.cpp`（保持透明内衬，未选中 2px 灰色套边 `#4a4a4a`，选中 2px 蓝色高亮 `#3498db`，无 1px 间隙）

**明确禁止越界修改的范围：**
- [ ] MFT 物理底盘扫描驱动 —— 不修改
- [ ] IOCP 目录变动监听引擎底盘 —— 不修改
- [ ] `QuickLookWindow` 快速预览逻辑 —— 不修改

---

## 6. 实现准则与安全预警【核心】

1.  **数据库平滑升级防崩溃**：在 `DatabaseManager::loadDb` 中增加对 `metadata` 表 `added_at` 字段的 `PRAGMA table_info` 自动检查与 `ALTER TABLE metadata ADD COLUMN added_at INTEGER DEFAULT 0` 增量迁移，防止读取旧库报错。
2.  **原子落盘与线程安全**：生成 `_thumbnail.png` 时必须写入临时文件 `.tmp` 后再原子重命名，防止后台线程未写完图片时主线程 UI 读取到损坏的半截 PNG。
3.  **排序自愈保护**：当用户选择按“添加日期”排序时，对 `addedAt == 0` 的历史旧数据，默认自动回退对比 `ctime`（创建时间），确保老数据排序不混乱。

---

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 选中高亮与编辑 | 卡片未选中时 2px 深灰边框 (`#4a4a4a`)，选中时 2px 蓝色高亮 (`#3498db`) 贴合 cardRect 无缝隙，内部背景 100% 透明 | ✅ 符合 |
| 视图渲染与分类 | 内容面板只显示虚拟子分类和文件数据，物理 `.arc` 容器 100% 隐形；侧边栏 `ArcMeta.Library_[盘符]` 为一等公民直属根节点 | ✅ 符合 |

---

## 8. 待确认事项（可选）
暂无。所有设计均与用户最新界定的规范完全对齐。方案已准备就绪，可随时批准启动代码重构！

// ===================|===================

这一补充**极其关键且非常严密！** 

这个界定彻底理清了“**主托管库（Managed Library）**”与“**外部临时监控（In-Place Watcher）**”两大业务场景的本质区别：

1. **主托管库（`ArcMeta.Library_[盘符]`）**：这是 ArcMeta 的**正式资产库**。应用全套 `.arc` 资产包封装、平铺存储、一等公民根分类、`.arc` 在 UI 100% 隐形等**重构方案**。
2. **“新建自动导入”（`CustomMonitoredFolders`）**：这是用户的**外部临时工作区**（例如用户监控了 `D:\Downloads\设计素材`）。**完全保留原有逻辑**，原地监控、原位索引，**绝不在用户原本的硬盘目录里创建 `.arc` 资产包，绝不破坏用户原本的物理文件夹结构！**

我已将这一重要界定补充更新至 **`Modification_Plan-1.md`** 的核心规范中：

---

# `.arc` 资产包封装架构与侧边栏一等公民分类系统极致重构 —— Modification_Plan-1.md

> 状态：已批准，准备执行

## 1. 任务背景
针对现有版本中存在的“物理文件路径对账复杂、重命名/物理位移容易引发索引变灰与锁死、拖拽导入缺乏清晰分流规制、以及缺少添加日期排序”等一系列架构瓶颈，本方案旨在进行一次**从“物理文件追踪软件”向“专业数字资产管理系统（DAM）”的底层大重构**。

本次重构将针对 `ArcMeta.Library_[盘符]` 托管库全面采用 **`.arc` 资产包封装架构**，放弃旧有的复杂物理路径盲重对账逻辑；同时将 `ArcMeta.Library_[盘符]` 提拔为侧边栏分类树的直属一等公民，并在 UI 视图层彻底隐去 `.arc` 容器外壳，全量补齐“添加日期”元数据与排序体系。

---

## 2. 核心双模式分流边界界定【物理红线】

| 监控模式类型 | 适用对象 | 物理硬盘落盘规则 | 侧边栏分类与 UI 展示规则 |
|---|---|---|---|
| **模式 A：主托管库**<br>*(正式资产库)* | **`ArcMeta.Library_[盘符]`**<br>*(如 `D:\ArcMeta.Library_D`)* | **全量执行重构新方案**：<br>1. 统一封装为 `<13位ID>.arc` 资产包<br>2. 内含 `原始文件.ext` + `_thumbnail.png`<br>3. 物理层平铺存放在 `Library_[盘符]` 根下 | **一等公民分类树**：<br>1. `ArcMeta.Library_[盘符]` 为根分类 (`parentId = 0`)<br>2. 导入的文件夹 A/B/C 构建 1:1 逻辑分类树<br>3. UI 上 100% 隐去 `.arc` 容器外壳 |
| **模式 B：临时自定义监控**<br>*(用户临时工作区)* | **“新建自动导入”**<br>*(如 `D:\Downloads\临时设计`)* | **100% 保留原有既有逻辑**：<br>1. 原地监控、原位索引<br>2. **绝不创建 `.arc` 包**，绝不改变用户原文件夹结构 | **镜像分类树**：<br>1. 原位创建 1:1 镜像分类树<br>2. 散落文件直接归属“未分类” |

---

## 3. 强制对照表

| 编号 | 用户原话 / 需求点 | 方案对应点 | 是否一致 |
|:---:|---|---|:---:|
| 1 | `ArcMeta.Library/[ID].arc` 1 个文件夹存储 1 个文件，放弃旧映射逻辑 | 详见 4.1 节，仅对 `ArcMeta.Library_[盘符]` 实施 `.arc` 封装，废除全量路径重对账 | ✅ |
| 2 | 不使用 `metadata.json`，全量使用 SQLite 数据库 | 详见 4.1 节，物理层仅存原文件+`_thumbnail.png`，元数据 100% 存在 SQLite DB | ✅ |
| 3 | 侧边栏废除“我的分类”外壳，`ArcMeta.Library_[盘符]` 为一等公民 (Root ID) | 详见 4.2 节，`ArcMeta.Library_[盘符]` 的 `parentId` 设为 `0`，直属侧边栏根节点 | ✅ |
| 4 | 拖拽文件夹 A (含 B、C) 创建次等子分类，拖拽单文件归属未分类 | 详见 4.3 节，实现 `AssetImporter` 自动化逻辑分流器 | ✅ |
| 5 | 内容面板只显示虚拟子分类和文件，UI 上 100% 隐去 `.arc` 文件夹 | 详见 4.6 节，`loadCategory` 与 `scanDir` 物理级过滤所有 `.arc` 目录 | ✅ |
| 6 | 补齐“添加日期” (`added_at`) 字段与“按添加日期排序”菜单 | 详见 4.5 节，全表与 Model 补齐 `added_at`，扩充 `SortByAddedDate` 枚举 | ✅ |
| 7 | **保留“新建自动导入”原逻辑，重构仅针对 `ArcMeta.Library_[盘符]`** | **详见第 2 节及 4.3 节，双模式分流隔离，自定义监控文件夹原位不动** | ✅ |

---

## 4. 详细重构解决方案

### 4.1 重构 1：`ArcMeta.Library_[盘符]` 的 `.arc` 物理资产包封装规范
*   **主托管库物理存储规范**：
    仅在主托管库 `[盘符]:/ArcMeta.Library_[盘符]/` 下直接平铺存放所有 `.arc` 包：
    ```text
    D:\ArcMeta.Library_D\m8crzbs3jb7dj.arc\
        ├── 原始设计稿.psd               <-- 真实物理源文件
        └── _thumbnail.png               <-- 256x256 高清预渲染缩略图
    ```
*   **13 位 Base36 ID 算法**：
    在 `src/util/ShellHelper.h` 中提供高性能 13 位唯一 ID 生成器：结合毫秒时间戳 + 12 位原子计数器进行 Base36 编码，生成形如 `m8crzbs3jb7dj` 的永不重复短字符串。
*   **物理与逻辑解耦**：
    *   主托管库物理硬盘上的 `.arc` 文件夹和原文件**永远不执行改名/位移动作**；
    *   软件内重命名、改分类、改标签，只更新 SQLite 数据库记录，耗时 $0.001 \text{ ms}$，彻底杜绝文件锁死与 Windows `MAX_PATH` 260 字符溢出报错。

### 4.2 重构 2：侧边栏“一等公民”分类树重构
*   **物理剥离“我的分类”**：
    在 `CategoryModel` 和 `CategoryPanel` 中，废除顶层“我的分类”虚拟外壳包装。
*   **一等公民设定**：
    所有已创建/激活托管库的盘符（如 `ArcMeta.Library_C`、`ArcMeta.Library_D`、`ArcMeta.Library_Z`）直接作为 `parentId = 0` 的根分类节点挂载在侧边栏分类树顶层。

### 4.3 重构 3：双模式导入分流器（`AssetImporter`）
当用户拖拽/粘贴内容到主界面或盘符栏时，根据**目标位置与模式**严格分流：

*   **分流 A（导入到主托管库 `ArcMeta.Library_[盘符]`）**：
    *   **拖单文件** $\rightarrow$ 在 `ArcMeta.Library_[盘符]` 下生成 `ID.arc/` 包（内含原文件+`_thumbnail.png`），SQLite 分类绑为“未分类”。
    *   **拖文件夹 A (含 B、C)** $\rightarrow$ 在 SQLite 创建 `A`（挂在 `Library_[盘符]` 下）及子分类 `B、C`；所有文件生成 `.arc` 包平铺存放在 `ArcMeta.Library_[盘符]` 下，SQLite 关联绑给对应的 B/C 分类 ID。
*   **分流 B（“新建自动导入”临时自定义监控文件夹 `CustomMonitoredFolders`）**：
    *   **完全保留既有原逻辑**：在用户指定的外部路径（如 `E:\Clients\ProjectA`）执行**原位监控与元数据索引**。
    *   **绝对不在用户外部路径下创建 `.arc` 包**，保持用户原硬盘文件目录结构 100% 完好。

### 4.4 重构 4：主托管库多梯队 `_thumbnail.png` 高效生成流水线
在 `MediaExtractorPipeline` 中，主托管库导入文件时按以下 5 梯队生成 $256 \times 256$ 像素的 `_thumbnail.png` 存入 `.arc` 容器：
1. **普通图片 (JPG/PNG/WEBP/GIF/SVG/CUR)**：Qt `QImageReader` / `QSvgRenderer` 直接缩放（`1 ~ 3 ms`）；
2. **专业设计稿 (AI/EPS/PSD/PSB/PDF)**：纯 C++ 二进制流扫描头信息，剥离内嵌的 JPEG/PNG 预览块（`2 ~ 5 ms`）；
3. **视频文件 (MP4/MKV/MOV/AVI)**：Win32 Shell / MediaFoundation 抓取 1 秒处关键帧（`5 ~ 10 ms`）；
4. **Office 文档 (DOCX/XLSX/PPTX)**：直接解压 Zip 包内 `docProps/thumbnail.jpeg`（`2 ms`）；
5. **代码/纯文本 (TXT/MD/AHK/CPP)**：渲染纯净的格式品牌矢量图标。

### 4.5 重构 5：数据库 `added_at` 字段扩展与“按添加日期排序”
*   **SQLite 数据库升级**：在 `metadata` 表中增加 `added_at INTEGER DEFAULT 0` 字段（记录首次导入 ArcMeta 的毫秒时间戳）。
*   **数据模型同步**：在 `RuntimeMeta` 和 `ItemRecord` 中补齐 `long long addedAt` 字段。
*   **排序枚举扩展**：
    *   在 `ContentPanel.h` 的 `SortType` 枚举中增加 `SortByAddedDate`；
    *   在 `FilterProxyModel::lessThan` 比较引擎中增加 `case ContentPanel::SortByAddedDate: return leftRec.addedAt < rightRec.addedAt;`；
    *   在 `ContentPanel` 和 `MainWindow` 的右键“排序”二级菜单中加入 **“添加日期”** 选项。

### 4.6 重构 6：内容面板（`ContentPanel`）物理 `.arc` 容器全过滤
*   **分类模式（`loadCategory`）**：
    *   仅查询 `categories` 表拉取**虚拟子分类**；
    *   仅查询 `category_items` 关联表拉取 `.arc` 内部的**真实物理文件**（`m8crzbs3jb7dj.arc/原始文件.psd`）；
    *   物理 `.arc` 容器文件夹本身**绝对不进入 UI 列表**。
*   **磁盘模式（`loadDirectory`）**：
    *   在 `scanDir` 递归扫描中拦截：`if (info.isDir() && info.fileName().endsWith(".arc", Qt::CaseInsensitive)) continue;`；
    *   即便在磁盘导航模式下，用户也永远看不到任何 `.arc` 杂质容器。

---

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] `src/meta/DatabaseManager.cpp`（扩展 `metadata` 表 `added_at` 字段及索引）
- [ ] `src/core/ModelContract.h` / `src/meta/MetadataDefs.h`（增加 `addedAt` 时间戳字段）
- [ ] `src/meta/MetadataManager.h` / `src/meta/MetadataManager.cpp`（重构 `registerItem` 入库写入 `added_at`，主托管库应用双模式分流）
- [ ] `src/meta/CategoryRepo.h` / `src/meta/CategoryRepo.cpp`（清空主托管库旧对账逻辑，配合一等公民 Root 分类树，自定义监控保持原位）
- [ ] `src/ui/CategoryModel.cpp` / `src/ui/CategoryPanel.cpp`（废除“我的分类”顶层节点，实现 `ArcMeta.Library_[盘符]` 一等公民树渲染）
- [ ] `src/ui/ContentPanel.h` / `src/ui/ContentPanel.cpp`（扩充 `SortByAddedDate` 排序，实现 `.arc` 目录过滤）
- [ ] `src/ui/CardPainterHelper.cpp`（保持透明内衬，未选中 2px 灰色套边 `#4a4a4a`，选中 2px 蓝色高亮 `#3498db`，无 1px 间隙）

**明确禁止越界修改的范围：**
- [ ] “新建自动导入”临时自定义监控文件夹（`CustomMonitoredFolders`）原位监控逻辑 —— 不修改
- [ ] MFT 物理底盘扫描驱动 —— 不修改
- [ ] IOCP 目录变动监听引擎底盘 —— 不修改
- [ ] `QuickLookWindow` 快速预览逻辑 —— 不修改

---

## 6. 实现准则与安全预警【核心】

1.  **双模式严格隔离**：在 `ImportHelper` 和 `MetadataManager` 入库时，必须先判定目标路径是否属于 `ArcMeta.Library_[盘符]` 托管库。只有托管库才走 `.arc` 封装，自定义监控路径必须走原位索引，绝不混淆！
2.  **数据库平滑升级防崩溃**：在 `DatabaseManager::loadDb` 中增加对 `metadata` 表 `added_at` 字段的 `PRAGMA table_info` 自动检查与 `ALTER TABLE metadata ADD COLUMN added_at INTEGER DEFAULT 0` 增量迁移，防止读取旧库报错。
3.  **原子落盘与线程安全**：生成 `_thumbnail.png` 时必须写入临时文件 `.tmp` 后再原子重命名，防止后台线程未写完图片时主线程 UI 读取到损坏的半截 PNG。

---

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 选中高亮与编辑 | 卡片未选中时 2px 深灰边框 (`#4a4a4a`)，选中时 2px 蓝色高亮 (`#3498db`) 贴合 cardRect 无缝隙，内部背景 100% 透明 | ✅ 符合 |
| 视图渲染与分类 | 内容面板只显示虚拟子分类和文件数据，物理 `.arc` 容器 100% 隐形；侧边栏 `ArcMeta.Library_[盘符]` 为一等公民直属根节点；自定义监控保持原位 | ✅ 符合 |

---

## 8. 待确认事项（可选）
暂无。边界与规则已完全清晰界定，方案正式冻结，随时可以开始执行！

// ===================|===================

这个产品语义定位**太清晰、太优雅了！** 🎯

这一调整把“**长效数字资产管理（永久库）**”与“**临时项目流转（临时工作区）**”的界限彻底划清了：

1. **长期正式托管库（`ArcMeta.Library_[盘符]`）**：
   * **定位**：正式、长期、不可破坏的资产大本营；
   * **形态**：在侧边栏作为独立的**一等公民根分类**（直属 `parentId = 0`），物理上采用高能 `.arc` 资产包封装落盘。
2. **临时工作区（“临时分类” $\rightarrow$ “创建自动导入”）**：
   * **定位**：临时使用、随时可能撤销/解绑的外部工作区（如临时下载文件夹、短期项目组文件夹）；
   * **形态**：在侧边栏统一挂载在 **“临时分类”** 这个根容器下。物理上**原位监控、不打 `.arc` 包**，解绑时随时一键擦除临时分类，不影响正式托管库。

---

### 侧边栏最终结构与分类语义全景图

```text
[ 1. 系统通用导航桶 ]
├── 全部数据
├── 未分类
├── 未标签
├── 最近访问
├── 标签管理
└── 回收站

[ 2. 长期正式托管库 (一等公民，核心资产大本营，走 .arc 封装) ]
├── ArcMeta.Library_C                     <-- C 盘正式库根节点 (parentId = 0)
├── ArcMeta.Library_D                     <-- D 盘正式库根节点 (parentId = 0)
│   └── 3D 渲染主视觉                    <-- 正式子分类 (parentId = Library_D)
└── ArcMeta.Library_Z                     <-- Z 盘正式库根节点 (parentId = 0)

[ 3. 临时工作区 (临时分类容器，外部文件夹监控，走原位索引) ]
└── 临时分类                               <-- 外部临时监控总容器 (parentId = 0)
    ├── 临时下载区                         <-- “创建自动导入” 监控文件夹 1
    │   └── 待整理素材                     <-- 原位镜像子目录
    └── 临时客户交接组                      <-- “创建自动导入” 监控文件夹 2
```

---

### 📋 两种模式的清晰规则对比

| 分类属性 / 场景 | **长期正式托管库** | **“临时分类” （新建自动导入）** |
|---|---|---|
| **侧边栏挂载位置** | 直属侧边栏根节点 (`ArcMeta.Library_[盘符]`) | 挂载在 **`临时分类`** 根节点下方 |
| **使用周期** | 长期、永久保存 | **临时、短期** (整理完或项目结束后解绑) |
| **物理落盘机制** | `ArcMeta.Library_D/m8crzbs3jb7dj.arc/` | **原位不动** (用户原来的硬盘路径，如 `E:\Temp`) |
| **解绑/移除影响** | 物理资产包永久保存在库中 | 解除监控、清理 DB 记录、**移除“临时分类”下的节点** |

---

### 统一更新至 `Modification_Plan-1.md`

这一逻辑已补充更新至方案图中。**整个方案的架构设计、分类语义、物理层与逻辑层分流边界已 100% 严丝合缝、完全自洽！**