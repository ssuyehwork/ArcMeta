# 内存模式下受控资产包卡片解包名称与缩略图穿透修正 —— Modification_Plan-22.md

> 状态：已批准，已执行完成

## 1. 任务背景
在内存数据库模式（侧边栏分类模式）下，系统资产以 `.arc` 文件夹容器存放。但在实际加载和显示时，内容面板卡片显示为外壳文件夹名 `00msbqcswm003.arc` 和默认黄色文件夹图标，没有穿透显示真实的素材名称（如 `cx 抽象 - 736.eps`）及对应的高清缩略图（如 `cx 抽象 - 736_thumbnail.png`），产生了严重的“货不对板”现象。本方案旨在彻底理清和打通“载入 → 解包 → 模型传递 → 视图异步渲染”这一整条 MVC 链路，实现完美高内聚的自愈映射。

## 2. 问题定位
1. **数据在传输链路中被二次覆盖（自愈失效）**：
   在 `ItemRecord::create` 中虽然对 `.arc` 进行了穿透，但在加载、重算或重命名后，`filename` 常在其他调用处被外部误覆盖为物理路径的文件名。需要对其设置高内聚的路径规范化，从而在源头上将尾部斜杠去除，确保后续的后缀、容器名匹配以及穿透解包 100% 成功。
2. **加载路径判定失误（导致缩略图缺失）**：
   `LibraryAssetModel::loadThumbnailsForRows` 提取后缀采用的是 `ext = info.suffix().toLower()`。对于 `.arc` 资产文件夹（如 `G:/ArcMeta.Library_G/00msbqcswm003.arc/`），若路径末尾带斜杠或被识别为普通 Directory，其 `suffix()` 返回值将为空字符串 `""`。导致其进入最后一个 `else` 兜底分支，未能触发读取 `*_thumbnail.png` 的逻辑。
3. **模型判定打脸（导致不渲染缩略图）**：
   `LibraryAssetModel::data` 的 `HasThumbnailRole` 只判定 `isInsideArcContainer = pInfo.dir().dirName().endsWith(".arc")`（判定当前显示项是否在 `.arc` 的“内部”）。但目前在内容面板中显示的卡片条目本身就是 `.arc` 文件夹，其父级是 `Library_G`。因此它在该角色下被判为 `false`。
   而在 `DecorationRole` 中，系统又因 `isInsideArcContainer` 为 `false` 且不属于图像后缀，进而使用普通文件的 Shell 图标（黄色文件夹），直接屏蔽了缩略图异步刷新加载成功的 QIcon，从而将缩略图盖住。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 内容面板卡片里显示的缩略图应该是“cx 抽象 - 736_thumbnail.png”（对应用户原话） | 修正 `LibraryAssetModel::data` 的 `HasThumbnailRole`、`DecorationRole` 判断及 `loadThumbnailsForRows` 对 `.arc` 特有后缀和带/不带斜杠物理路径的穿透载入 | ✅ |
| 2    | 卡片下方显示的名称应该是“cx 抽象 - 736.eps”，而不该是“00msbqcswm003.arc”（对应用户原话） | 修正 `ItemRecord::create` 中对容器资产的解包名，并在 `LibraryAssetModel::data` 的 `DisplayRole` 下安全剥离并强防护返回 `filename`，从根本上杜绝中途二次覆盖 | ✅ |

## 4. 详细解决方案

### 修改点 1：`src/core/ItemRecord.cpp`
在进入 `create` 方法之初，统一剥除斜杠或反斜杠尾部路径，进行高内聚的路径规范化，确保在后续的各子流程中不存在由物理路径尾部斜杠导致的不匹配行为。

```cpp
ItemRecord ItemRecord::create(const QString& path, const RuntimeMeta* providedMeta, bool isFromMemory) {
    ItemRecord r;
    std::wstring wPath = MetadataManager::normalizePath(path.toStdWString());
    QString nPath = QString::fromStdWString(wPath);
    if (nPath.endsWith("/") || nPath.endsWith("\\")) {
        nPath = nPath.left(nPath.length() - 1);
        wPath = nPath.toStdWString();
    }

    // 1. 物理属性采样 (零 I/O 核心)
```

### 修改点 2：`src/ui/models/LibraryAssetModel.cpp`
在 `LibraryAssetModel::data()` 的起始部分，统一对 `path` 进行路径尾部斜杠、反斜杠剥除规范化处理。确保整个模型在向视图汇报及判定卡片各种角色时的绝对一致性。

```cpp
QVariant LibraryAssetModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= static_cast<int>(m_allRecords.size())) return QVariant();

    const auto& record = m_allRecords[index.row()];
    QString path = record.path;
    if (path.endsWith("/") || path.endsWith("\\")) {
        path = path.left(path.length() - 1);
    }

    // 分类节点及子分类专用大分支（对应用户原话：“LibraryAssetModel 只处理内存数据库模式条目（包含 isCategory 分支）”）
```

在 `LibraryAssetModel::loadThumbnailsForRows()` 中兼容带或不带斜杠的路径后缀：

```cpp
            } else if ((ext == "arc" || path.endsWith(".arc", Qt::CaseInsensitive) || path.endsWith(".arc/", Qt::CaseInsensitive) || path.endsWith(".arc\\", Qt::CaseInsensitive)) && info.isDir()) {
                // 物理规范化文件夹路径：去除末尾的斜杠，保证拼接正常
                QString cleanPath = path;
                if (cleanPath.endsWith("/") || cleanPath.endsWith("\\")) {
                    cleanPath = cleanPath.left(cleanPath.length() - 1);
                }
                QDir arcDir(cleanPath);
                QStringList thumbFiles = arcDir.entryList({"*_thumbnail.png"}, QDir::Files);
                if (!thumbFiles.isEmpty()) {
                    QString thumbPath = cleanPath + "/" + thumbFiles.first();
                    img = QImage(thumbPath);
                    if (!img.isNull()) {
                        ar = (double)img.width() / img.height();
                        hasThumb = true;
                    }
                }
            }
```

在 `LibraryAssetModel::data()` 的 `HasThumbnailRole` 和 `DecorationRole` 中，同步对卡片本身的容器属性穿透判断予以打通和完善：

```cpp
    } else if (role == HasThumbnailRole) {
        static const QStringList iconOnlyExts = {"cur", "ico", "ani"};
        if (iconOnlyExts.contains(record.suffix.toLower())) return false;

        QFileInfo pInfo(path);
        bool isInsideArcContainer = pInfo.dir().dirName().endsWith(".arc", Qt::CaseInsensitive);
        bool isArcContainer = record.isDir && path.endsWith(".arc", Qt::CaseInsensitive);
        if (isInsideArcContainer || isArcContainer) {
            QString nativePath = QDir::toNativeSeparators(path);
            return m_aspectRatios.contains(nativePath) && m_aspectRatios.value(nativePath) > 0.0;
        }

        if (record.suffix.toLower() == "ai") {
            QString nativePath = QDir::toNativeSeparators(path);
            if (m_aspectRatios.contains(nativePath)) {
                return m_aspectRatios.value(nativePath) > 0.0;
            }
            return false;
        }
        if (UiHelper::isGraphicsFile(record.suffix)) return true;
        if (record.width > 0 && record.height > 0) return true;
        return m_aspectRatios.contains(QDir::toNativeSeparators(path)) && m_aspectRatios.value(QDir::toNativeSeparators(path)) > 0.0;
    } else if (role == Qt::DecorationRole && index.column() == 0) {
        QString cacheKey = path;
        QIcon* cached = m_iconCache.object(cacheKey);
        if (cached) return *cached;

        QFileInfo info(path);
        QString ext = info.suffix().toLower();
        bool isGraphic = UiHelper::isGraphicsFile(ext) || ext == "svg";
        
        // .arc 资产包容器内部文件：判断父目录是否为 .arc 容器，等待异步加载
        bool isInsideArcContainer = info.dir().dirName().endsWith(".arc", Qt::CaseInsensitive);
        bool isArcContainer = record.isDir && path.endsWith(".arc", Qt::CaseInsensitive);

        if (isGraphic || isInsideArcContainer || isArcContainer) return QIcon(); 
        return ShellIconManager::getFileIcon(path, 128);
    }
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/core/ItemRecord.cpp` (修正穿透解包名称及后缀的强保护自愈)
- [ ] 模块/文件：`src/ui/models/LibraryAssetModel.cpp` (修正缩略图异步解包检测、HasThumbnailRole和DecorationRole卡片穿透状态返回)

**明确禁止越界修改的范围：**
- [ ] 磁盘模式 `src/ui/models/DiskItemModel.cpp` —— 不修改

## 6. 实现准则与预警【核心】
1. 方案完全自包含，精准拦截 `.arc` 并读取对应的 `*_thumbnail.png` 缩略图文件，不会引起系统级普通文件夹的扫描崩溃。
2. 必须保证 `ItemRecord` 解包得到的主素材文件名在 `LibraryAssetModel::data` 中高优先级传递，不可将其再次覆盖回 `00msbqcswm003.arc` 这样的机器哈希名。
3. 本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 托管卡片解包名称及缩略图穿透 | 点击分类加载数据时，内容面板中呈现的受控资产包卡片其名称穿透显示为包内的真实素材文件名，缩略图也穿透并读取包内的高清缩略图 | ✅ 符合 |

## 8. 待确认事项（可选）
（无）
