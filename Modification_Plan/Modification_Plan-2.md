# 长期托管库极简重构与自动导入功能完好保持 —— Modification_Plan-2.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在 `Modification_Plan-1.md` 方案的设计基础之上，本方案作为该话题下独立递进的终极重构图纸，将重构焦点极致地收敛到**长期托管库 `ArcMeta.Library_[盘符]`** 的核心重构逻辑中。

为了消除由于物理路径移动/重命名等操作极易引发的索引丢失，本方案仅对长期托管库引入 **`.arc` 资产包封装架构**；同时，将托管库提拔为侧边栏的一等公民（`parentId = 0`），并全量补齐 `added_at` 毫秒时间戳和“添加日期”排序菜单。

最关键的是，本次重构在架构与物理层划设了绝对隔离红线，**对外部临时自定义监控文件夹（“新建自动导入” / `CustomMonitoredFolders`）的原地监控与原位索引逻辑不作任何改动，保证其 100% 完好不变**，杜绝破坏用户原本的物理文件夹结构。

---

## 2. 核心双模式分流边界界定【物理红线】

| 监控模式类型 | 适用对象 | 物理硬盘落盘规则 | 侧边栏分类与 UI 展示规则 |
|---|---|---|---|
| **模式 A：长期托管库**<br>*(正式资产库)* | **`ArcMeta.Library_[盘符]`**<br>*(如 `D:\ArcMeta.Library_D`)* | **全量执行重构新方案**：<br>1. 统一封装为 `<13位ID>.arc` 资产包<br>2. 内含 `原始文件.ext` + `_thumbnail.png`<br>3. 物理层平铺存放在 `Library_[盘符]` 根下，不执行任何改名/位移动作 | **一等公民分类树**：<br>1. `ArcMeta.Library_[盘符]` 为根分类 (`parentId = 0`)，废除顶层“我的分类”虚拟外壳包装<br>2. 导入的子文件夹在 SQLite 数据库中构建 1:1 逻辑分类树<br>3. 在 UI（内容面板等）上 100% 隐去 `.arc` 物理容器外壳 |
| **模式 B：临时自定义监控**<br>*(用户临时工作区)* | **“新建自动导入”**<br>*(如 `D:\Downloads\临时设计`)* | **100% 保持原有逻辑不变**：<br>1. 原地监控、原位索引、原位更新<br>2. **绝不创建 `.arc` 包**，绝不改变或干扰用户的物理文件夹结构 | **镜像分类树**：<br>1. 保留挂载在 “临时分类” 根节点下的既有形态<br>2. 散落文件直接归属“未分类” |

---

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|:---:|---|---|:---:|
| 1 | 只对ArcMeta.Library_[盘符]做重构，而“创建自动导入”的部分保持不变。 (对应用户原话) | 详见 4.1 及 4.3 节，双模式分流隔离，仅对 `ArcMeta.Library_[盘符]` 进行重构，自动导入 100% 保持既有逻辑完好不变 | ✅ |
| 2 | `ArcMeta.Library/[ID].arc` 1 个文件夹存储 1 个文件，放弃旧映射逻辑 (对应用户原话) | 详见 4.1 节，长期托管库下通过 `.arc` 极简平铺存放单个原始文件与高清缩略图 | ✅ |
| 3 | 不使用 `metadata.json`，全量使用 SQLite 数据库 (对应用户原话) | 详见 4.1 节，长期托管库物理层仅存储原始文件与缩略图，元数据 100% 写入数据库，丢弃旧的物理文件对账逻辑 | ✅ |
| 4 | 侧边栏废除“我的分类”外壳，`ArcMeta.Library_[盘符]` 为一等公民 (Root ID) (对应用户原话) | 详见 4.2 节，将 `ArcMeta.Library_[盘符]` 的逻辑 parentId 设为 `0`，直属侧边栏顶级分类 | ✅ |
| 5 | 拖拽文件夹 A (含 B、C) 创建次等子分类，拖拽单文件归属未分类 (对应用户原话) | 详见 4.3 节，分流器对长期托管库拖拽实现“物理平铺入库 + 数据库逻辑拉线建树” | ✅ |
| 6 | 内容面板只显示虚拟子分类和文件，UI 上 100% 隐去 `.arc` 文件夹 (对应用户原话) | 详见 4.6 节，重构 `loadCategory` 与 `scanDir` 递归拦截器，在视图层过滤所有带有 `.arc` 结尾的文件夹 | ✅ |
| 7 | 补齐“添加日期” (`added_at`) 字段与“按添加日期排序”菜单 (对应用户原话) | 详见 4.5 节，在数据库 `metadata` 表中增加 `added_at` 字段并扩展 `SortByAddedDate` 排序逻辑 | ✅ |

---

## 4. 详细解决方案

### 4.1 重构 1：托管库 `.arc` 物理包平铺设计与 Base36 唯一 ID 机制
*   **平铺落盘结构**：
    对于长期托管库下的导入资产，一律平铺存储于托管库根目录下。每个资产独占一个以 13 位唯一 ID 命名的 `.arc` 文件夹（例如 `D:\ArcMeta.Library_D\m8crzbs3jb7dj.arc`）。其内部仅包含：
    *   `原始文件.ext`：用户导入的文件实体，不改名，不作改动。
    *   `_thumbnail.png`：由后台解析生成的 256x256 高清物理缩略图。
*   **Base36 唯一 ID 算法**：
    在 `src/util/ShellHelper.h` 和 `ShellHelper.cpp` 中升级唯一 ID 生成算法：基于当前毫秒级时间戳 + 原子单调递增计数器。在进行 Base36 转换后生成 13 位字母数字短串（形如 `m8crzbs3jb7dj`），并在多线程并发调用下保证绝对的原子不重复性。
*   **丢弃旧路径盲重对账**：
    托管库文件的物理路径和名字永远保持不变。重命名、移动分类、修改打标等动作，仅在 SQLite 数据库中更新对应记录，不需要物理移动任何磁盘位置。

### 4.2 重构 2：侧边栏“一等公民”分类树渲染改造
*   **物理剥离“我的分类”**：
    在 `src/ui/CategoryModel.cpp` 和 `src/ui/CategoryPanel.cpp` 中：
    *   废除向侧边栏顶级注入 `“我的分类”` 虚拟占位节点的动作。
    *   使所有托管库对应的分类节点（如 `ArcMeta.Library_C`、`ArcMeta.Library_D`）的 `parentId` 直接等于 `0`（或者顶级根分类标识），直属分类树最顶层。
*   **临时分类容器保留**：
    用户手动创建的自定义监控文件夹（临时工作区）统一保留在 `parentId` 对应“临时分类”容器下的既有渲染逻辑。

### 4.3 重构 3：智能拖拽与逻辑建树分流（`AssetImporter` / `ImportHelper`）
在用户向内容区或主界面拖拽/粘贴内容时，根据接收的目标容器类型执行物理分流：

*   **长期托管库分流（目标为 `ArcMeta.Library_[盘符]`）**：
    *   **拖入单文件/散落文件**：
        1. 调用 13 位 Base36 生成器，创建 `D:\ArcMeta.Library_D\<ID>.arc\`。
        2. 将物理原文件拷贝或移动入该容器。
        3. 提取高清 `_thumbnail.png`，写入该容器下。
        4. 写入 SQLite `metadata` 数据库，其 `category_id` 为 “未分类” （`-2`），其 `added_at` 记为当前毫秒时间戳。
    *   **拖入“文件夹 A”（内含子目录 B、C）**：
        1. 在 `categories` 数据库中创建逻辑节点 `文件夹 A`（`parentId = 托管库分类ID`）、`文件夹 B`（`parentId = A 的分类ID`）、`文件夹 C`（`parentId = A 的分类ID`）。
        2. 将 A、B、C 下的所有物理资产按单文件方式，全部重构在托管库根下平铺生成各自的 `<ID>.arc` 容器。
        3. 在 `category_items` 数据库关联表中，将这些 `file_id` 分别与 `B` 或 `C` 的逻辑分类 `id` 建立拉线映射。
*   **临时自定义监控文件夹（“新建自动导入”）**：
    *   **100% 保持既有逻辑**，原位原地执行监控。绝不在用户外部物理目录下创建任何 `.arc` 文件夹，保持用户物理结构 100% 完好。

### 4.4 重构 4：托管库 `_thumbnail.png` 高效安全提取管线
导入文件入库时，在 `MediaExtractorPipeline` 后台服务中按如下 5 级梯队，异步生成 $256 \times 256$ 像素的高品质 `_thumbnail.png`：
1. **图片格式**：通过 Qt `QImageReader` 平滑降采样。
2. **专业设计稿 (PSD/AI/EPS/PDF)**：流式提取内含的预览缩略图块。
3. **视频格式**：采用 Shell/MediaFoundation 抓取第 1 秒首帧。
4. **Office 文档**：解压 Zip 并解出 `docProps/thumbnail.jpeg`。
5. **文本代码 (TXT/MD/MD/AHK)**：渲染其专有的、高对比度矢量卡片占位标。
*为保证线程安全，提取缩略图时必须先写到同一目录下的临时文件 `.tmp`，写入成功后再原子重命名为 `_thumbnail.png`，防止 UI 渲染线程读取到半截损坏的文件。*

### 4.5 重构 5：数据库 `added_at` 毫秒时间戳扩展与排序集成
*   **SQLite 升级与平滑自愈 DDL**：
    在 `src/meta/DatabaseManager.cpp` 的 `loadDb` 流程中，加入平滑迁移 SQL：
    ```sql
    ALTER TABLE metadata ADD COLUMN added_at INTEGER DEFAULT 0;
    CREATE INDEX IF NOT EXISTS idx_metadata_added_at ON metadata(added_at);
    ```
    *如果读取到的是没有 `added_at` 字段的老数据库，将自动触发此 DDL 升级并创建索引，彻底防止由于缺字段导致老库加载报错崩溃。*
*   **模型同步**：
    在 `src/core/ModelContract.h` 的 `ItemRecord` 和 `RuntimeMeta` 结构中补齐 `long long addedAt`。
*   **排序枚举与 UI 菜单扩展**：
    *   在 `src/ui/ContentPanel.h` 的 `SortType` 枚举中扩充 `SortByAddedDate`。
    *   在 `FilterProxyModel::lessThan` 比较层，遇到 `ContentPanel::SortByAddedDate` 时：
        *   比较双方的 `addedAt`。
        *   *安全保护退回：若 `addedAt == 0`，则自动回退去对比系统文件的物理创建时间（`ctime`），保证老旧资产排序稳定性。*
    *   在 `ContentPanel` 和 `MainWindow` 的右键“排序”子菜单中，插入 “添加日期” 这一中文字样选项。

### 4.6 重构 6：内容面板 100% 物理 `.arc` 文件夹隐形过滤
*   **分类模式（`loadCategory`）**：
    1. 通过 `categories` 表查询得到逻辑次等子分类，予以绘制展示。
    2. 通过 `category_items` 映射表提取物理文件，在卡片上还原其真实的逻辑名称与图片。
    3. 屏蔽并物理拦截所有含有 `.arc` 的容器路径直接进入 UI 数据列表。
*   **磁盘树扫描（`loadDirectory` / `scanDir`）**：
    在 `scanDir` 递归或文件列出过滤环节：
    ```cpp
    if (info.isDir() && info.fileName().endsWith(".arc", Qt::CaseInsensitive)) {
        continue;
    }
    ```
    使得即便处于物理磁盘视图模式下，用户的列表也绝对保持干净，100% 隐去任何 `.arc` 杂质文件夹。

---

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] `src/meta/DatabaseManager.cpp`（实现 `added_at` 增量 DDL 平滑升级）
- [ ] `src/core/ModelContract.h` / `src/meta/MetadataDefs.h`（加入 `addedAt` 数据结构定义）
- [ ] `src/meta/MetadataManager.cpp` / `src/meta/MetadataManager.h`（入库对账逻辑重构，添加 `added_at` 赋值，在导入托管库时走新方案）
- [ ] `src/meta/CategoryRepo.cpp` / `src/meta/CategoryRepo.h`（清空主托管库旧对账，适配顶级 `parentId = 0`）
- [ ] `src/ui/CategoryModel.cpp` / `src/ui/CategoryPanel.cpp`（删除“我的分类”多余包装，把 `ArcMeta.Library_[盘符]` 的 parentId 设为顶级，渲染直属一等公民）
- [ ] `src/ui/ContentPanel.h` / `src/ui/ContentPanel.cpp`（添加按添加日期排序、在 `scanDir` 物理扫描中拦截隐藏 `.arc` 容器，渲染逻辑文件）
- [ ] `src/ui/CardPainterHelper.cpp`（确保卡片未选中时 2px 深灰边框 `#4a4a4a`，选中时 2px 蓝色高亮 `#3498db` 贴合 cardRect 无缝隙，背景透明）

**明确禁止越界修改的范围：**
- [ ] “新建自动导入”临时自定义监控文件夹（`CustomMonitoredFolders`）原地监控与原位索引核心代码逻辑 —— 绝对不修改
- [ ] MFT 磁盘物理高速扫描引擎及 IO 通道 —— 绝对不修改
- [ ] IOCP 目录变动监听引擎底层 —— 绝对不修改
- [ ] `QuickLookWindow` 快速预览界面及交互逻辑 —— 绝对不修改

---

## 6. 实现准则与预警【核心】

1.  **双模式严格分流与防污染**：
    在入库对账、移动或注册操作中，必须首先进行路径比对，判定是否处于 `ArcMeta.Library_[盘符]` 托管库内。只有属于托管库目录下的数据才执行 `.arc` 包解析和封装。对于用户外部自定义监控文件夹，必须走原位原地索引，绝对不能污染和修改用户的原文件。
2.  **原子写入防损坏**：
    在多线程写入 `_thumbnail.png` 过程中，先使用唯一的 `.tmp` 后缀写入，写完后调用 `QFile::rename` 进行原子覆盖，防止 UI 主线程在后台未绘制完成时读取受损。
3.  **排序退回保护机制**：
    由于增加了 `added_at` 时间戳，导入的新资产会有这个值，但原有旧记录在此前可能为 `0`。在 `FilterProxyModel::lessThan` 排序过程中，若遇到两个或其中一方 `addedAt == 0`，则自动退回到比较文件的 `ctime`（创建时间）属性，确保全站排序时旧数据不会发生严重的漂移或卡死。
4.  **考古对齐与卡片高亮精确绘制**：
    在 `CardPainterHelper::drawCard` 中，重绘卡片边框时：
    *   卡片未选中时，绘制 2px 的 `#4a4a4a` 深灰边框。
    *   选中时，绘制 2px 的 `#3498db` 蓝色高亮边框，且必须紧密贴合 `cardRect`，内部保持 100% 透明背景，绝不产生多余的 1px 间隙。

---

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 选中高亮与编辑 | 卡片未选中时 2px 深灰边框 (`#4a4a4a`)，选中时 2px 蓝色高亮 (`#3498db`) 贴合 cardRect 无缝隙，内部背景 100% 透明 | ✅ 符合 |
| 视图渲染与分类 | 内容面板只显示虚拟子分类和文件数据，物理 `.arc` 容器 100% 隐形；侧边栏 `ArcMeta.Library_[盘符]` 为一等公民直属根节点；自定义监控保持原位 | ✅ 符合 |

---

## 8. 待确认事项（可选）
暂无。重构边界极为清晰，方案已正式冻结，准备执行。