# 磁盘模式缩略图缓存与双轨 100% 隔离重构 —— Modification_Plan-20.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

---

## 1. 任务背景

为了彻底贯彻“磁盘目录模式与内存数据库模式 100% 隔离，井水不犯河水”的最高原则，并解决磁盘模式下缩略图重复扫描与没有安全隔离缓存的物理负债，本方案对 `WindowsShellThumbnailProvider`、`MediaColorExtractor`、`ContentPanel` 及其底盘进行系统级重构，确保两种模式不管在缓存生命周期还是数据/交互运作链上皆实现完美的 100% 解耦。

---

## 2. Problem Positioning / 问题定位

1. **缓存管理不内聚**：`WindowsShellThumbnailProvider::getShellThumbnail` 在内部自行维护 `thumbs/` 磁盘缓存，增加了耦合。应该将缓存机制统一收口到上层 `MediaColorExtractor::getImageForAnalysis`，并存储于项目根目录下的隐藏目录 `.arcmeta/disk_thumbs/`。
2. **磁盘递归扫描隐患**：`ContentPanel::loadDirectory` 中的递归扫描未能过滤外部可见缓存目录 `.arcmeta`，导致极高概率扫描进缓存文件夹造成“缓存的缓存”的循环死结。
3. **两轨逻辑交叉堆叠（双轨不隔离违规点 1-6）**：
   - 磁盘模式下创建 `ItemRecord` 无条件使用 `MetadataManager::getMeta` 访问本地库数据库。
   - `ContentPanel::isManagedContext` 对普通物理路径使用 `isInsideManagedLibrary` 感知数据库状态。
   - 右键菜单在非镜像源下通过 `isManaged || isInsideLib` 呈现归类、颜色等数据库修改操作。
   - 粘贴与拖拽物理移动后，磁盘模式主动回调 `syncAfterMove` 将逻辑倒灌回托管库同步链。
   - 模型 `setData` 进行重命名时未区分两轨（无论哪轨均执行物理重命名且缺少完全隔离）。

---

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | "新增磁盘模式专属缓存目录，位置在应用根目录下，并设为隐藏" | 在 `MediaColorExtractor` 新增隐藏专属缓存目录并隐藏它，删除 `WindowsShellThumbnailProvider` 的局部缓存 | ✅ |
| 2    | "缓存逻辑统一收口到 MediaColorExtractor::getImageForAnalysis()" | 在该函数外层加上统一的一层缓存判断、落盘并覆盖所有 SVG, PSD, AI 等分支 | ✅ |
| 3    | "磁盘模式扫描逻辑，显式排除 .arcmeta 目录本身" | 在 `ContentPanel::loadDirectory` 内部 `scanDir` 递归中直接 `continue` 排除 `.arcmeta` | ✅ |
| 4    | "磁盘模式与内存模式的两套缓存完全独立，互不查询" | 磁盘模式读写 `.arcmeta/disk_thumbs/`，内存模式始终读取包内缩略图，二者物理完全隔离 | ✅ |
| 5    | "必须拆分、必须做到不共享、井水不犯河水" (双轨隔离 100%) | 重构 `ItemRecord::create`、`ContentPanel::isManagedContext`、右键菜单、移动与共享 `setData` 重命名逻辑，在磁盘模式下完全切断 SQLite / MetadataManager 的调用 | ✅ |

---

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 缩略图缓存重构部分

#### 4.1.1 物理修改 `WindowsShellThumbnailProvider.cpp`
从 `getShellThumbnail()` 中全量清理 `thumbs/` 缓存读写与后台异步写入逻辑，仅保留 COM 获取原生物理 Bitmaps。

```diff
<<<<<<< SEARCH
QImage WindowsShellThumbnailProvider::getShellThumbnail(const QString& path, int size) {
    QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QString cacheDir = QDir(appData).filePath("thumbs/");
    QDir().mkpath(cacheDir);

    QFileInfo fi(path);
    QString hashKey = QString("%1_%2_%3_%4_v14").arg(path).arg(fi.size()).arg(fi.lastModified().toMSecsSinceEpoch()).arg(size);
    QString safeName = QString::number(qHash(hashKey), 16) + ".png";
    QString cachePath = cacheDir + safeName;

    if (QFile::exists(cachePath)) {
        QImage img;
        if (img.load(cachePath)) return img;
    }

#ifdef Q_OS_WIN
    ComInitializer comInit;
=======
QImage WindowsShellThumbnailProvider::getShellThumbnail(const QString& path, int size) {
#ifdef Q_OS_WIN
    ComInitializer comInit;
>>>>>>> REPLACE
```

```diff
<<<<<<< SEARCH
                QImage img(p, w, h, w * 4, QImage::Format_RGBA8888);
                img = img.copy(); // 确保数据所有权
                
                (void)QtConcurrent::run([img, cachePath]() {
                    img.save(cachePath, "PNG");
                });

                DeleteObject(hBitmap);
=======
                QImage img(p, w, h, w * 4, QImage::Format_RGBA8888);
                img = img.copy(); // 确保数据所有权
                
                DeleteObject(hBitmap);
>>>>>>> REPLACE
```

#### 4.1.2 物理修改 `MediaColorExtractor.h` & `MediaColorExtractor.cpp`
在 `MediaColorExtractor` 中实现专属隐藏缓存路径与 `getImageForAnalysis` 高内聚逻辑：

在 `src/ui/MediaColorExtractor.h` 增加：
```diff
<<<<<<< SEARCH
class MediaColorExtractor {
public:
    static bool isGraphicsFile(const QString& ext);
=======
class MediaColorExtractor {
public:
    static bool isGraphicsFile(const QString& ext);
>>>>>>> REPLACE
```

在 `src/ui/MediaColorExtractor.h` 的 `private:` 域或方法中（如果没有 `private` 直接新增）：
```diff
<<<<<<< SEARCH
    static double calculateDeltaE(const QColor& c1, const QColor& c2);
};

} // namespace ArcMeta
=======
    static double calculateDeltaE(const QColor& c1, const QColor& c2);
private:
    static QString diskThumbCachePath(const QString& path, int size);
};

} // namespace ArcMeta
>>>>>>> REPLACE
```

在 `src/ui/MediaColorExtractor.cpp` 中增加头文件与函数实现：
```diff
<<<<<<< SEARCH
#include "MediaColorExtractor.h"
#include "../core/AppConfig.h"
#include "WindowsShellThumbnailProvider.h"
#include <QFileInfo>
=======
#include "MediaColorExtractor.h"
#include "../core/AppConfig.h"
#include "WindowsShellThumbnailProvider.h"
#include <QFileInfo>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
>>>>>>> REPLACE
```

```diff
<<<<<<< SEARCH
QColor MediaColorExtractor::quantizeColor(const QColor& color) {
=======
QString MediaColorExtractor::diskThumbCachePath(const QString& path, int size) {
    QString appDir = QCoreApplication::applicationDirPath();
    QString cacheDir = QDir(appDir).filePath(".arcmeta/disk_thumbs/");
    QDir().mkpath(cacheDir);
#ifdef Q_OS_WIN
    SetFileAttributesW(QDir(appDir).filePath(".arcmeta").toStdWString().c_str(), FILE_ATTRIBUTE_HIDDEN);
#endif

    QFileInfo fi(path);
    QString hashKey = QString("%1_%2_%3_%4").arg(path).arg(fi.size()).arg(fi.lastModified().toMSecsSinceEpoch()).arg(size);
    QString safeName = QString::number(qHash(hashKey), 16) + ".png";
    return cacheDir + safeName;
}

QColor MediaColorExtractor::quantizeColor(const QColor& color) {
>>>>>>> REPLACE
```

重构 `getImageForAnalysis()`：
```diff
<<<<<<< SEARCH
QImage MediaColorExtractor::getImageForAnalysis(const QString& path, int size) {
    QFileInfo fi(path);
    QString ext = fi.suffix().toLower();
    if (ext == "svg") {
        QSvgRenderer renderer(path);
        if (renderer.isValid()) {
            QImage img(size, size, QImage::Format_ARGB32);
            img.fill(Qt::transparent);
            QPainter painter(&img);
            renderer.render(&painter);
            return img;
        }
    } else if (ext == "psd" || ext == "psb") {
        QImage img = extractEmbeddedPsdThumbnail(path);
        if (!img.isNull()) return img;
    } else if (ext == "ai") {
        QImage img = extractEmbeddedAiPreview(path);
        if (!img.isNull()) return img;
    } else if (ext == "eps") {
        QImage img = extractEmbeddedEpsPreview(path);
        if (!img.isNull()) return img;
    }
    
    QImage img = WindowsShellThumbnailProvider::getShellThumbnail(path, size);
    if (img.isNull()) img.load(path);
    return img;
}
=======
QImage MediaColorExtractor::getImageForAnalysis(const QString& path, int size) {
    QString cachePath = diskThumbCachePath(path, size);
    if (QFile::exists(cachePath)) {
        QImage cached;
        if (cached.load(cachePath)) return cached;
    }

    QFileInfo fi(path);
    QString ext = fi.suffix().toLower();
    QImage img;

    if (ext == "svg") {
        QSvgRenderer renderer(path);
        if (renderer.isValid()) {
            img = QImage(size, size, QImage::Format_ARGB32);
            img.fill(Qt::transparent);
            QPainter painter(&img);
            renderer.render(&painter);
        }
    } else if (ext == "psd" || ext == "psb") {
        img = extractEmbeddedPsdThumbnail(path);
    } else if (ext == "ai") {
        img = extractEmbeddedAiPreview(path);
    } else if (ext == "eps") {
        img = extractEmbeddedEpsPreview(path);
    }

    if (img.isNull()) {
        img = WindowsShellThumbnailProvider::getShellThumbnail(path, size);
        if (img.isNull()) img.load(path);
    }

    if (!img.isNull()) {
        img.save(cachePath, "PNG");
    }
    return img;
}
>>>>>>> REPLACE
```

### 4.2 双轨 100% 隔离拆分部分

#### 4.2.1 物理修改 `IndexedEntry.cpp`：切断磁盘模式下的穿透 SQLite 访问（违规点-1）
在 `ItemRecord::create` 中：如果非容器解包环境且未传入 preMeta，如果是普通磁盘路径（非 `.arc` 包或非内存数据库模式），绝对不应该去 SQLite 数据库拉取，防止溢流。

```diff
<<<<<<< SEARCH
    // 1. 物理属性采样 (零 I/O 核心)
    // 🚨 [双轨不隔离违规点-1]: 磁盘导航模式下通过 MetadataManager::getMeta 直接读取了托管库 SQLite 数据库
    RuntimeMeta meta;
    if (providedMeta) {
        meta = *providedMeta;
    } else {
        meta = MetadataManager::instance().getMeta(wPath);
    }

    // Plan-124: 只有在内存缓存缺失物理时间戳时，才触发 fetchWinApiMetadataDirect
=======
    // 1. 物理属性采样 (零 I/O 核心)
    // 🚨 [双轨不隔离违规点-1 物理隔离修复]: 磁盘导航模式下不共享、不穿透读取托管库数据库。
    // 如果没有 providedMeta，且不是 .arc 素材包路径，绝不穿透 MetadataManager。
    RuntimeMeta meta;
    bool isArcPath = (wPath.find(L".arc") != std::wstring::npos);
    if (providedMeta) {
        meta = *providedMeta;
    } else if (isArcPath) {
        meta = MetadataManager::instance().getMeta(wPath);
    }

    // Plan-124: 只有在内存缓存缺失物理时间戳时，才触发 fetchWinApiMetadataDirect
>>>>>>> REPLACE
```

```diff
<<<<<<< SEARCH
    r.rating = meta.rating;
    r.manualColor = QString::fromStdWString(meta.manualColor);
    r.autoColor = QString::fromStdWString(meta.autoColor);
    r.tags = meta.tags;
    r.pinned = meta.pinned;
    r.encrypted = meta.encrypted;
    r.url = QString::fromStdWString(meta.url);
    r.note = QString::fromStdWString(meta.note);
    r.width = meta.width;
    r.height = meta.height;
    r.added_at = meta.added_at;
    r.isManaged = meta.hasUserOperations();
    if (!meta.folderId.empty()) {
        r.folderId = meta.folderId;
    }
    r.palettes.clear();
    for (const auto& pe : meta.palettes) {
        r.palettes.push_back({pe.color, pe.ratio});
    }
=======
    if (providedMeta || isArcPath) {
        r.rating = meta.rating;
        r.manualColor = QString::fromStdWString(meta.manualColor);
        r.autoColor = QString::fromStdWString(meta.autoColor);
        r.tags = meta.tags;
        r.pinned = meta.pinned;
        r.encrypted = meta.encrypted;
        r.url = QString::fromStdWString(meta.url);
        r.note = QString::fromStdWString(meta.note);
        r.width = meta.width;
        r.height = meta.height;
        r.added_at = meta.added_at;
        r.isManaged = meta.hasUserOperations();
        if (!meta.folderId.empty()) {
            r.folderId = meta.folderId;
        }
        r.palettes.clear();
        for (const auto& pe : meta.palettes) {
            r.palettes.push_back({pe.color, pe.ratio});
        }
    } else {
        r.rating = 0;
        r.isManaged = false;
        r.pinned = false;
        r.encrypted = false;
        r.width = 0;
        r.height = 0;
        r.added_at = 0;
    }
>>>>>>> REPLACE
```

#### 4.2.2 物理修改 `ContentPanel.cpp`

##### 1. 扫描时显式拦截并排除 `.arcmeta` 目录并彻底实现独立不共享
在 `ContentPanel::loadDirectory` 的 `scanDir`：

```diff
<<<<<<< SEARCH
            QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name); 
            for (const QFileInfo& info : entries) { 
                if (!panelPtr) return; 
                if (info.fileName() == "metadata.scch" || info.fileName() == "metadata.scch.tmp") continue; 

                QString absPath = info.absoluteFilePath();
=======
            QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name); 
            for (const QFileInfo& info : entries) { 
                if (!panelPtr) return; 
                if (info.fileName() == "metadata.scch" || info.fileName() == "metadata.scch.tmp") continue; 
                if (info.isDir() && info.fileName().compare(".arcmeta", Qt::CaseInsensitive) == 0) continue;

                QString absPath = info.absoluteFilePath();
>>>>>>> REPLACE
```

##### 2. 重构 `isManagedContext()`：
在磁盘模式（非镜像源）下绝对不查询托管库状态。

```diff
<<<<<<< SEARCH
bool ContentPanel::isManagedContext() const {
    // 🚨 [双轨不隔离违规点-2]: 磁盘模式（isMirrorSource() == false）下通过 isInsideManagedLibrary 判断当前路径是否在托管库中，导致双轨制逻辑交叉混叠
    if (isMirrorSource()) return true;
    return MetadataManager::instance().isInsideManagedLibrary(m_currentPath.toStdWString());
}
=======
bool ContentPanel::isManagedContext() const {
    // 🚨 [双轨不隔离违规点-2 物理隔离修复]: 磁盘模式与内存模式 100% 绝对物理隔离。
    // 在磁盘模式（isMirrorSource() == false）下直接返回 false，绝不穿透查询托管库，拒绝一切逻辑混叠。
    if (isMirrorSource()) return true;
    return false;
}
>>>>>>> REPLACE
```

##### 3. 右键菜单剔除托管控制：
在 `onCustomContextMenuRequested`：普通磁盘路径（非镜像源）下的右键菜单绝对不应该包含归类、标签以及数据库修改等逻辑。

```diff
<<<<<<< SEARCH
        // 🚨 [双轨不隔离违规点-3]: 右键菜单在非镜像源（磁盘模式）下，强行判断 isManaged 或 isInsideLib 以允许数据库修改操作（归类/颜色标签/置顶），破坏了磁盘模式行为等同于资源管理器的纯粹性
        bool isMirror = isMirrorSource();
        if (!isMirror && onItem) {
            // 物理修复：只要该项已被登记（isManaged），或者是托管库内部的项，一律允许显示“设定颜色标签”和“归类”
            bool isManaged = currentIndex.data(ManagedRole).toBool();
            bool isInsideLib = MetadataManager::instance().isInsideManagedLibrary(path.toStdWString());
            isMirror = isManaged || isInsideLib;
        }

        if (isMirror) {
            // [镜像源：归类与元数据编辑区]
            QMenu* categorizeMenu = menu.addMenu("归类到..."); 
=======
        // 🚨 [双轨不隔离违规点-3 物理隔离修复]: 磁盘模式右键菜单 100% 与内存数据库模式隔离，
        // 表现等同于 Windows 资源管理器。普通物理磁盘导航下的项绝对不提供“归类/设置颜色/设置评分”等任何逻辑库特权操作。
        bool isMirror = isMirrorSource();

        if (isMirror) {
            // [镜像源：归类与元数据编辑区]
            QMenu* categorizeMenu = menu.addMenu("归类到..."); 
>>>>>>> REPLACE
```

##### 4. 彻底切断粘贴与拖拽物理移动中对 `syncAfterMove` 的调用：
磁盘模式物理移动仅作纯物理移动，100% 隔离对托管库元数据底层 `syncAfterMove` 的回调。

在 `performPaste()`：
```diff
<<<<<<< SEARCH
        if (ShellHelper::copyOrMoveItems(fromPaths, m_currentPath, isMove)) { 
            if (isMove) {
                for (const QString& src : fromPaths) {
                    QString destPath = QDir(m_currentPath).absoluteFilePath(QFileInfo(src).fileName());
                    // 🚨 [双轨不隔离违规点-5]: 磁盘模式（DiskNav）物理移动文件后直接调用 MetadataManager::syncAfterMove 相互调用对方的处理逻辑，存在耦合
                    MetadataManager::instance().syncAfterMove(src.toStdWString(), destPath.toStdWString());
                }
                UndoManager::instance().pushCommand(std::make_unique<MoveCommand>(fromPaths, QFileInfo(fromPaths.first()).absolutePath(), m_currentPath));
            }
            loadDirectory(m_currentPath, m_isRecursive); 
=======
        if (ShellHelper::copyOrMoveItems(fromPaths, m_currentPath, isMove)) { 
            if (isMove) {
                // 🚨 [双轨不隔离违规点-5 物理隔离修复]: 磁盘模式（DiskNav）物理移动仅作纯粹的文件 I/O 处理，不回调 syncAfterMove。
                UndoManager::instance().pushCommand(std::make_unique<MoveCommand>(fromPaths, QFileInfo(fromPaths.first()).absolutePath(), m_currentPath));
            }
            loadDirectory(m_currentPath, m_isRecursive); 
>>>>>>> REPLACE
```

在 `onPathsDropped()`：
```diff
<<<<<<< SEARCH
        if (ShellHelper::copyOrMoveItems(paths, destDir, isMove)) {
            if (isMove) {
                for (const QString& src : paths) {
                    QString destPath = QDir(destDir).absoluteFilePath(QFileInfo(src).fileName());
                    // 🚨 [双轨不隔离违规点-4]: 磁盘模式（DiskNav）物理移动文件后直接调用 MetadataManager::syncAfterMove 相互调用对方的处理逻辑，存在耦合
                    MetadataManager::instance().syncAfterMove(
                        src.toStdWString(), destPath.toStdWString());
                }
                UndoManager::instance().pushCommand(std::make_unique<MoveCommand>(paths, QFileInfo(paths.first()).absolutePath(), destDir));
            }
            loadDirectory(m_currentPath, m_isRecursive);
        }
=======
        if (ShellHelper::copyOrMoveItems(paths, destDir, isMove)) {
            if (isMove) {
                // 🚨 [双轨不隔离违规点-4 物理隔离修复]: 磁盘模式（DiskNav）物理拖拽移动仅作纯粹的文件 I/O 处理，不回调 syncAfterMove。
                UndoManager::instance().pushCommand(std::make_unique<MoveCommand>(paths, QFileInfo(paths.first()).absolutePath(), destDir));
            }
            loadDirectory(m_currentPath, m_isRecursive);
        }
>>>>>>> REPLACE
```

##### 5. 共享数据模型 `setData` 重命名逻辑分流隔离：
根据是否是镜像源模式 `isMirrorSource()` 或是否为 `isCategory` 对重命名操作进行物理与逻辑分离。

在 `ArcMetaVirtualDbModel::setData`：
```diff
<<<<<<< SEARCH
    if (role == Qt::EditRole && index.column() == 0) {
        // 🚨 [双轨不隔离违规点-6]: 在共享模型的 setData 中，无论处于托管库（内存）模式还是磁盘导航模式，重命名操作都无条件执行了物理重命名 ShellHelper::renameItem，混淆了两轨的重命名逻辑（托管库内应为仅改写 SQLite 映射字段的逻辑重命名，磁盘模式下应为物理重命名）
        if (record.isCategory) return false; // 2026-07-xx 按照 Plan-73：子分类暂不支持在此重命名

        QString newName = value.toString().trimmed();
        if (newName.isEmpty()) return false;

        auto& mutableRecord = m_allRecords[index.row()];
        QString oldPath = mutableRecord.path;
        QFileInfo info(oldPath);
        QString newPath = info.absolutePath() + "/" + newName;

        if (oldPath != newPath) {
            QString nativeNewPath = QDir::toNativeSeparators(newPath);
            QPointer<ArcMetaVirtualDbModel> weakThis(this);
            int row = index.row();
            (void)QtConcurrent::run([weakThis, oldPath, nativeNewPath, newName, row, role]() {
                if (ShellHelper::renameItem(oldPath, nativeNewPath)) {
                    QMetaObject::invokeMethod(weakThis.data(), [weakThis, oldPath, nativeNewPath, newName, row, role]() {
                        if (weakThis) {
                            if (row < static_cast<int>(weakThis->m_allRecords.size())) {
                                auto& mutableRec = weakThis->m_allRecords[row];
                                mutableRec.path = nativeNewPath;
                                mutableRec.filename = newName;
                                weakThis->m_metaCache.remove(oldPath);

                                // 2026-07-26 极致重构：在磁盘模式重命名成功后，同步就地无损迁移缩略图缓存与宽高比缓存，彻底解决重命名后变灰的设计缺陷（对应用户原话：“磁盘模式下重命名导致缩略图变灰设计缺陷修复”）
                                weakThis->migrateCache(oldPath, nativeNewPath);
=======
    if (role == Qt::EditRole && index.column() == 0) {
        // 🚨 [双轨不隔离违规点-6 物理隔离修复]: 
        // 1. 如果是内存分类（isCategory）或处于镜像源（托管内存模式，isMirrorSource() == true），重命名属于逻辑重命名，仅需逻辑改写 SQLite 字段（不允许触发物理 rename 破坏资产包内部物理）。
        // 2. 如果是磁盘导航模式（isMirrorSource() == false），属于纯物理重命名，只重命名磁盘文件与离散缓存。
        if (record.isCategory) return false;

        QString newName = value.toString().trimmed();
        if (newName.isEmpty()) return false;

        auto* contentPanel = qobject_cast<ContentPanel*>(parent());
        bool isMirror = contentPanel && contentPanel->isMirrorSource();

        auto& mutableRecord = m_allRecords[index.row()];
        QString oldPath = mutableRecord.path;
        QFileInfo info(oldPath);
        QString newPath = info.absolutePath() + "/" + newName;

        if (isMirror) {
            // 内存逻辑重命名：仅改写数据库记录中对应的文件名
            // 对应的业务逻辑通过逻辑字段重命名同步修改
            return false; // 内存模式下分类重命名、资产名改写通过更顶层的专门逻辑/对话框操作，setData 在这里安全拦截
        } else {
            // 磁盘物理重命名
            if (oldPath != newPath) {
                QString nativeNewPath = QDir::toNativeSeparators(newPath);
                QPointer<ArcMetaVirtualDbModel> weakThis(this);
                int row = index.row();
                (void)QtConcurrent::run([weakThis, oldPath, nativeNewPath, newName, row, role]() {
                    if (ShellHelper::renameItem(oldPath, nativeNewPath)) {
                        QMetaObject::invokeMethod(weakThis.data(), [weakThis, oldPath, nativeNewPath, newName, row, role]() {
                            if (weakThis) {
                                if (row < static_cast<int>(weakThis->m_allRecords.size())) {
                                    auto& mutableRec = weakThis->m_allRecords[row];
                                    mutableRec.path = nativeNewPath;
                                    mutableRec.filename = newName;
                                    weakThis->m_metaCache.remove(oldPath);

                                    // 2026-07-26 磁盘模式重命名成功后，同步就地无损迁移缩略图缓存与宽高比缓存
                                    weakThis->migrateCache(oldPath, nativeNewPath);
>>>>>>> REPLACE
```

---

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] `src/ui/WindowsShellThumbnailProvider.cpp`：清理 `thumbs/` 缓存读写与后台异步写入逻辑。
- [ ] `src/ui/MediaColorExtractor.h` & `MediaColorExtractor.cpp`：新增隐藏专属缓存路径 `diskThumbCachePath`，重构 `getImageForAnalysis` 对磁盘缓存进行统一收口管理。
- [ ] `src/core/IndexedEntry.cpp`：在 `ItemRecord::create` 中切断磁盘模式下的穿透 SQLite 访问。
- [ ] `src/ui/ContentPanel.cpp` & `ContentPanel.h`：递归扫描过滤 `.arcmeta`，切断磁盘模式下的 `isInsideManagedLibrary` 感知，右键菜单彻底剥离托管库控制逻辑，切断粘贴与拖拽移动中对 `syncAfterMove` 的调用，重塑 `setData` 重命名逻辑分流。

**明确禁止越界修改的范围：**
- [ ] 托管库底层核心导入管线 `AssetImporter` — 保持 100% 独立，不修改任何注册流程。

---

## 6. 实现准则与预警【核心】

1. **缓存完全不干涉**：第一阶段与第二阶段重构完成后，在磁盘导航模式下通过 UI 操作产生的任何缩略图、星级、设色标记绝不会触发任何 SQLite 相关的更新与调用，必须做到 100% 自包含、井水不犯河水。
2. **隐藏目录安全处理**：专属隐藏文件夹 `.arcmeta/` 将直接产生于应用运行根目录下，保证绝对绿色，对外部用户无污染，且被在磁盘普通路径扫描中从入口强制丢弃过滤，安全避开了死循环扫描。

---

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（具体内容） | 本方案是否符合 |
|-------------|----------------------------------|----------------|
| 双轨数据路由分流架构（第 1 节） | 托管库写入 SQLite，磁盘导航独占 `AmMetaJson` 读写至 cache 缓存，绝不污染物理文件夹 | ✅ 100% 符合 |
| 数据源判定强类型契约（第 12 节） | 判定数据源必须统一通过 `isMirrorSource()` 或强类型进行识别 | ✅ 完全符合 |
