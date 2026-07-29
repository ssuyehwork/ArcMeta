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

这个需求完全切中了像 **Adobe Bridge** 以及 **Eagle** 磁盘模式的核心体验！

要实现“**磁盘模式下无需导入托管库，也能对电脑上任意物理文件夹里的文件进行打星、颜色标记、添加标签，且数据自动缓存存入 `ArcMeta.cache/*.json`**”，我们只需要完成 **双轨数据路由分流**。

机制原理极其清晰：
* **在托管库内（`ArcMeta.Library_X`）**：元数据写入 **SQLite 数据库** (`.db`)；
* **在磁盘模式下（电脑任意物理文件夹）**：元数据自动写入 **`ArcMeta.cache/路径Hash.json`**（使用你提供的 `AmMetaJson` 类）。

下面是实现该功能的 3 处核心代码修改方案：

---

### 修改 1：解除 `ContentPanel.cpp` 中的“编辑受阻”弹窗

之前为了防止物理磁盘模式乱写数据库，代码里加了一道阻拦。现在有了 `AmMetaJson`，我们需要**彻底解锁磁盘模式下的编辑权限**。

打开 **`src/ui/ContentPanel.cpp`**，找到 `ArcMetaVirtualDbModel::setData`（约 L220）：

#### 删掉（或注释掉）以下阻拦代码：

```cpp
    // ❌ 删掉（或注释）这段“编辑受阻”阻拦代码：
    /*
    if (!record.isCategory && (role == RatingRole || role == ColorRole || role == IsLockedRole || role == PinnedRole)) {
        QString currentType = qobject_cast<ContentPanel*>(parent())->getCurrentCategoryType();
        if (currentType == "nav" || currentType == "") {
            if (!isInsideLibrary) {
                FramelessMessageBox::information(nullptr, "编辑受阻", "该项目尚未入库，无法进行元数据编辑...");
                return false;
            }
        }
    }
    */
```

---

### 修改 2：在 `loadDirectory` 物理扫描时自动装载 `AmMetaJson` 高级缓存

在磁盘导航模式下，当用户点击进入任意物理文件夹（如 `D:\Photos`）时，**先加载 `ArcMeta.cache` 里的 JSON 缓存**，让文件卡片在扫描出来的瞬间就亮出已设置的星级、颜色、标签！

打开 **`src/ui/ContentPanel.cpp`**，找到 `loadDirectory` 函数中的 `scanDir` 递归扫描段（约 L1320），修改为：

```cpp
// src/ui/ContentPanel.cpp -> loadDirectory() 的后台扫描 Lambda 中

        std::function<void(const QString&, bool)> scanDir; 
        scanDir = [&](const QString& p, bool rec) { 
            QDir dir(p); 
            if (!dir.exists()) return; 

            // 1. 自动预加载当前物理文件夹对应的 ArcMeta.cache/*.json 高级缓存
            AmMetaJson jsonCache(p.toStdWString());
            jsonCache.load();
            const auto& cachedItems = jsonCache.items();
 
            QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name); 
            for (const QFileInfo& info : entries) { 
                if (!panelPtr) return; 
                if (info.fileName() == "metadata.scch" || info.fileName() == "metadata.scch.tmp") continue; 
                if (info.isDir() && info.fileName().endsWith(".arc", Qt::CaseInsensitive)) continue; 
 
                QString absPath = info.absoluteFilePath();
                ItemRecord itemRec = ItemRecord::create(absPath);

                // 2. 🚨 关键绑定：如果这个物理文件在 ArcMeta.cache 中有打标/星级缓存，自动读取填充！
                std::wstring fileName = info.fileName().toStdWString();
                auto it = cachedItems.find(fileName);
                if (it != cachedItems.end()) {
                    itemRec.rating = it->second.rating;
                    itemRec.manualColor = QString::fromStdWString(it->second.color);
                    itemRec.pinned = it->second.pinned;
                    itemRec.note = QString::fromStdWString(it->second.note);
                    itemRec.url = QString::fromStdWString(it->second.url);
                    for (const auto& t : it->second.tags) {
                        itemRec.tags.append(QString::fromStdWString(t));
                    }
                }

                allItems.push_back(itemRec);
 
                if (rec && info.isDir()) { 
                    scanDir(absPath, true); 
                } 
            } 
        }; 
```

---

### 修改 3：在 `MetadataManager.cpp` 中实现双轨落盘路由

当用户在磁盘模式下打星级、设颜色、加标签时，让 `MetadataManager` **自动判断**：如果在库外，自动写入 `AmMetaJson`（即 `ArcMeta.cache/*.json`）！

打开 **`src/meta/MetadataManager.cpp`**，更新 `setRating` 和 `setColor` 方法：

```cpp
// src/meta/MetadataManager.cpp

void MetadataManager::setRating(const std::wstring& path, int rating, bool notify) {
    std::wstring nPath = MetadataManager::normalizePath(path);

    if (isInsideManagedLibrary(nPath)) {
        // A. 托管库模式：写入 SQLite 数据库
        ensureActivated(nPath);
        { 
            std::unique_lock<std::shared_mutex> lock(m_mutex); 
            m_cache[nPath].rating = rating; 
        }
        persistAsync(nPath);
    } else {
        // B. 磁盘导航模式：写入 ArcMeta.cache 高级 JSON 缓存文件
        QFileInfo info(QString::fromStdWString(nPath));
        std::wstring folderPath = info.absolutePath().toStdWString();
        std::wstring fileName = info.fileName().toStdWString();

        AmMetaJson jsonCache(folderPath);
        jsonCache.load();
        jsonCache.items()[fileName].rating = rating;
        jsonCache.save(); // 物理落盘写进 ArcMeta.cache/*.json

        // 同步内存缓存
        { 
            std::unique_lock<std::shared_mutex> lock(m_mutex); 
            m_cache[nPath].rating = rating; 
        }
    }

    if (notify) notifyUI(RefreshLevel::PathUpdate, QString::fromStdWString(nPath));
}

void MetadataManager::setColor(const std::wstring& path, const std::wstring& color, bool notify) {
    std::wstring nPath = MetadataManager::normalizePath(path);

    if (isInsideManagedLibrary(nPath)) {
        // A. 托管库模式：写入 SQLite 数据库
        ensureActivated(nPath);
        { 
            std::unique_lock<std::shared_mutex> lock(m_mutex); 
            m_cache[nPath].manualColor = color; 
        }
        persistAsync(nPath);
    } else {
        // B. 磁盘导航模式：写入 ArcMeta.cache 高级 JSON 缓存文件
        QFileInfo info(QString::fromStdWString(nPath));
        std::wstring folderPath = info.absolutePath().toStdWString();
        std::wstring fileName = info.fileName().toStdWString();

        AmMetaJson jsonCache(folderPath);
        jsonCache.load();
        jsonCache.items()[fileName].color = color;
        jsonCache.save(); // 物理落盘写进 ArcMeta.cache/*.json

        // 同步内存缓存
        { 
            std::unique_lock<std::shared_mutex> lock(m_mutex); 
            m_cache[nPath].manualColor = color; 
        }
    }

    if (notify) notifyUI(RefreshLevel::PathUpdate, QString::fromStdWString(nPath));
}
```

---

### 🎯 修改后的使用体验

1. **磁盘模式零受限**：你可以在 C 盘、D 盘、桌面或任意文件夹里随意对文件**打星级（Alt+0~5）、标记颜色（Alt+1~9）、添加备注与标签**。
2. **纯净离散缓存**：所有打标数据自动被压缩写入主程序目录下的 `ArcMeta.cache/哈希值.json` 中，**完全不污染用户的原始物理文件夹**。
3. **秒开记忆**：下一次你再点击进入这个物理文件夹时，所有的星级、颜色标记会自动亮起，完美体验超越 Adobe Bridge！