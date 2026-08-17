# Plan-134 紧急修复方案：缩略图占位符死锁修复与秒级回显

> **故障根因诊断**：执行者引入的 `m_currentGen` 熔断拦截与 `setSingleShot(false)` 定时器产生了**“请求锁永久死锁”**，导致所有缩略图在后台被 100% 误杀丢弃，且防抖锁无法释放，全屏永久卡在灰色占位符。本方案彻底移除死锁逻辑，恢复秒级正常出图。

---

## 🚨 致命死锁根因（为什么现在全是占位符？）

1. **定时器无限疯狂自增**：`m_visibleTimer` 设置为非单次（`setSingleShot(false)`），导致每 30ms 永远不停地触发，`m_currentGen` 每秒递增 30 多次；
2. **死锁拦截丢弃了图片**：后台解码耗时约 5~15ms，刚解完准备交由主线程显示时，`m_currentGen` 已经被定时器自增了，结果在 `if (thisGen != m_currentGen) return;` 处被**全部当成过期任务误杀丢弃**！
3. **防抖锁永远无法释放**：由于直接 `return`，`m_requestedIcons.remove(path)` 根本没执行，所有图片路径被**永久锁死**在“正在请求中”状态，之后无论怎么滑动，系统都认为它们还在请求中，永远不再发出加载，全屏彻底瘫痪在占位符！

---

## 🛠️ 彻底修复实施代码（一步到位）

---

### 第一步：修复 `ContentPanel.cpp`（恢复单次节流定时器）

**修改文件**：[`src/ui/ContentPanel.cpp`](file:///G:/C++/ArcMeta/ArcMeta/src/ui/ContentPanel.cpp)

#### 改动 1：L584 定时器恢复为单次

```diff
<<<<<<< SEARCH
    m_visibleTimer = new QTimer(this);
    m_visibleTimer->setSingleShot(false);
    m_visibleTimer->setInterval(30); 
    connect(m_visibleTimer, &QTimer::timeout, this, &ContentPanel::refreshVisibleThumbnails);
=======
    m_visibleTimer = new QTimer(this);
    m_visibleTimer->setSingleShot(true);
    m_visibleTimer->setInterval(60); 
    connect(m_visibleTimer, &QTimer::timeout, this, &ContentPanel::refreshVisibleThumbnails);
>>>>>>> REPLACE
```

#### 改动 2：L1415 滚动条信号绑定

```diff
<<<<<<< SEARCH
    connect(m_gridView->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        // 🚨 严禁在 valueChanged 中直调 refreshVisibleThumbnails！由 50ms 节流定时器独占控制
        if (!m_visibleTimer->isActive()) {
            m_visibleTimer->start(50);
        }
    });
    connect(m_gridView->verticalScrollBar(), &QScrollBar::sliderReleased, this, [this]() {
        refreshVisibleThumbnails();
    });
=======
    connect(m_gridView->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        m_visibleTimer->start(60);
    });
    connect(m_gridView->verticalScrollBar(), &QScrollBar::sliderReleased, this, [this]() {
        refreshVisibleThumbnails();
    });
>>>>>>> REPLACE
```

---

### 第二步：彻底清除 `LibraryAssetModel.cpp` 中的死锁拦截

**修改文件**：[`src/ui/models/LibraryAssetModel.cpp`](file:///G:/C++/ArcMeta/ArcMeta/src/ui/models/LibraryAssetModel.cpp)

```diff
<<<<<<< SEARCH
void LibraryAssetModel::loadThumbnailsForRows(const QList<int>& rows) {
    uint64_t thisGen = ++m_currentGen;

    // 内存模式：穿透 .arc 搜寻高清缩略图与宽高比
    std::vector<std::pair<QString, QString>> newQueue;
    for (int r : rows) {
        if (r < 0 || r >= static_cast<int>(m_allRecords.size())) continue;
        const auto& rec = m_allRecords[r];
        if (rec.isCategory) continue;

        QString path = rec.path;
        bool isArcContainer = rec.isDir && rec.path.endsWith(".arc", Qt::CaseInsensitive);
        bool needLoad = !m_iconCache.contains(path);
        if ((UiHelper::isGraphicsFile(rec.suffix) || isArcContainer) && !m_aspectRatios.contains(QDir::toNativeSeparators(path))) {
            needLoad = true;
        }

        // 🚨 核心防爆锁：如果正在后台处理排队中，立刻 0 毫秒跳过！
        if (m_requestedIcons.contains(path)) {
            needLoad = false;
        }

        if (needLoad) {
            // 🚨 0 毫秒瞬间上锁！阻断高频重复开启渲染进程！
            m_requestedIcons.insert(path);
            newQueue.push_back({path, path});
        }
    }

    if (newQueue.empty()) return;

    QPointer<LibraryAssetModel> weakThis(this);
    for (const auto& task : newQueue) {
        QString path = task.first;
        (void)QtConcurrent::run([weakThis, path, thisGen]() {
            if (!weakThis || weakThis->m_currentGen.load() != thisGen) return;
            QFileInfo info(path);
            QString ext = info.suffix().toLower();

            QImage img;
            double ar = 1.0;
            bool hasThumb = false;

            bool isInsideArc = path.contains(".arc/", Qt::CaseInsensitive) || path.contains(".arc\\", Qt::CaseInsensitive);

            if (isInsideArc || ext == "svg" || ext == "psd" || ext == "psb" || ext == "ai" || ext == "eps" || UiHelper::isGraphicsFile(ext)) {
                img = CapsuleMediaExtractor::getCapsuleThumbnailReadOnly(path);
                if (!img.isNull()) {
                    ar = (double)img.width() / img.height();
                    hasThumb = true;
                }
            } else if ((ext == "arc" || path.endsWith(".arc", Qt::CaseInsensitive)) && info.isDir()) {
                QString cleanPath = path;
                if (cleanPath.endsWith("/") || cleanPath.endsWith("\\")) {
                    cleanPath = cleanPath.left(cleanPath.length() - 1);
                }
                QDir arcDir(cleanPath);
                QStringList thumbFiles = arcDir.entryList({"*_thumbnail.png"}, QDir::Files);
                if (!thumbFiles.isEmpty()) {
                    img = QImage(cleanPath + "/" + thumbFiles.first());
                    if (!img.isNull()) {
                        ar = (double)img.width() / img.height();
                        hasThumb = true;
                    }
                }
            }

            // 🚨 核心保命准则：图标提取（包括 ShellIconManager）100% 在子线程完成！
            QIcon icon;
            if (!img.isNull()) {
                icon = QIcon(QPixmap::fromImage(img));
            } else {
                QString iconTarget = path;
                if (ext == "arc" && info.isDir()) {
                    QString cleanPath = path;
                    if (cleanPath.endsWith("/") || cleanPath.endsWith("\\")) {
                        cleanPath = cleanPath.left(cleanPath.length() - 1);
                    }
                    QDir arcDir(cleanPath);
                    QStringList files = arcDir.entryList(QDir::Files | QDir::NoDotAndDotDot);
                    for (const QString& fn : files) {
                        if (fn.endsWith("_thumbnail.png", Qt::CaseInsensitive)) continue;
                        if (fn.compare("metadata.json", Qt::CaseInsensitive) == 0) continue;
                        iconTarget = cleanPath + "/" + fn;
                        break;
                    }
                }
                icon = ShellIconManager::getFileIcon(iconTarget, 128);
            }

            // 主线程只做 0.0001ms 纯内存赋值，零 I/O，绝不假死
            QMetaObject::invokeMethod(weakThis.data(), [weakThis, path, icon, ar, hasThumb, thisGen]() {
                if (weakThis && weakThis->m_currentGen.load() == thisGen) {
                    weakThis->m_iconCache.insert(path, new QIcon(icon));
                    weakThis->m_aspectRatios[QDir::toNativeSeparators(path)] = hasThumb ? ar : -1.0;
                    weakThis->m_requestedIcons.remove(path);

                    auto it = weakThis->m_pathToIndex.find(path);
                    if (it != weakThis->m_pathToIndex.end()) {
                        int rIdx = it->second;
                        if (weakThis->isSuspended()) {
                            weakThis->m_pendingUpdateRows.insert(rIdx);
                        } else {
                            // 仅通知卡片重绘，绝不发射 AspectRatioRole 触发全量网格重排！
                            emit weakThis->dataChanged(weakThis->index(rIdx, 0), weakThis->index(rIdx, 0), {Qt::DecorationRole, HasThumbnailRole});
                        }
                    }
                }
            });
        });
    }
}
=======
void LibraryAssetModel::loadThumbnailsForRows(const QList<int>& rows) {
    std::vector<std::pair<QString, QString>> newQueue;
    for (int r : rows) {
        if (r < 0 || r >= static_cast<int>(m_allRecords.size())) continue;
        const auto& rec = m_allRecords[r];
        if (rec.isCategory) continue;

        QString path = rec.path;
        bool isArcContainer = rec.isDir && rec.path.endsWith(".arc", Qt::CaseInsensitive);
        bool needLoad = !m_iconCache.contains(path);
        if ((UiHelper::isGraphicsFile(rec.suffix) || isArcContainer) && !m_aspectRatios.contains(QDir::toNativeSeparators(path))) {
            needLoad = true;
        }

        if (m_requestedIcons.contains(path)) {
            needLoad = false;
        }

        if (needLoad) {
            m_requestedIcons.insert(path);
            newQueue.push_back({path, path});
        }
    }

    if (newQueue.empty()) return;

    QPointer<LibraryAssetModel> weakThis(this);
    for (const auto& task : newQueue) {
        QString path = task.first;
        (void)QtConcurrent::run([weakThis, path]() {
            if (!weakThis) return;
            QFileInfo info(path);
            QString ext = info.suffix().toLower();

            QImage img;
            double ar = 1.0;
            bool hasThumb = false;

            bool isInsideArc = path.contains(".arc/", Qt::CaseInsensitive) || path.contains(".arc\\", Qt::CaseInsensitive);

            if (isInsideArc || ext == "svg" || ext == "psd" || ext == "psb" || ext == "ai" || ext == "eps" || UiHelper::isGraphicsFile(ext)) {
                img = CapsuleMediaExtractor::getCapsuleThumbnailReadOnly(path);
                if (!img.isNull()) {
                    ar = (double)img.width() / img.height();
                    hasThumb = true;
                }
            } else if ((ext == "arc" || path.endsWith(".arc", Qt::CaseInsensitive)) && info.isDir()) {
                QString cleanPath = path;
                if (cleanPath.endsWith("/") || cleanPath.endsWith("\\")) {
                    cleanPath = cleanPath.left(cleanPath.length() - 1);
                }
                QDir arcDir(cleanPath);
                QStringList thumbFiles = arcDir.entryList({"*_thumbnail.png"}, QDir::Files);
                if (!thumbFiles.isEmpty()) {
                    img = QImage(cleanPath + "/" + thumbFiles.first());
                    if (!img.isNull()) {
                        ar = (double)img.width() / img.height();
                        hasThumb = true;
                    }
                }
            }

            QIcon icon;
            if (!img.isNull()) {
                icon = QIcon(QPixmap::fromImage(img));
            } else {
                QString iconTarget = path;
                if (ext == "arc" && info.isDir()) {
                    QString cleanPath = path;
                    if (cleanPath.endsWith("/") || cleanPath.endsWith("\\")) {
                        cleanPath = cleanPath.left(cleanPath.length() - 1);
                    }
                    QDir arcDir(cleanPath);
                    QStringList files = arcDir.entryList(QDir::Files | QDir::NoDotAndDotDot);
                    for (const QString& fn : files) {
                        if (fn.endsWith("_thumbnail.png", Qt::CaseInsensitive)) continue;
                        if (fn.compare("metadata.json", Qt::CaseInsensitive) == 0) continue;
                        iconTarget = cleanPath + "/" + fn;
                        break;
                    }
                }
                icon = ShellIconManager::getFileIcon(iconTarget, 128);
            }

            QMetaObject::invokeMethod(weakThis.data(), [weakThis, path, icon, ar, hasThumb]() {
                if (weakThis) {
                    weakThis->m_iconCache.insert(path, new QIcon(icon));
                    weakThis->m_aspectRatios[QDir::toNativeSeparators(path)] = hasThumb ? ar : -1.0;
                    weakThis->m_requestedIcons.remove(path); // 🚨 无论何时，正常释放请求锁！

                    auto it = weakThis->m_pathToIndex.find(path);
                    if (it != weakThis->m_pathToIndex.end()) {
                        int rIdx = it->second;
                        if (weakThis->isSuspended()) {
                            weakThis->m_pendingUpdateRows.insert(rIdx);
                        } else {
                            emit weakThis->dataChanged(weakThis->index(rIdx, 0), weakThis->index(rIdx, 0), {Qt::DecorationRole, HasThumbnailRole});
                        }
                    }
                }
            });
        });
    }
}
>>>>>>> REPLACE
```

---

### 第三步：彻底清除 `DiskItemModel.cpp` 中的死锁拦截

**修改文件**：[`src/ui/models/DiskItemModel.cpp`](file:///G:/C++/ArcMeta/ArcMeta/src/ui/models/DiskItemModel.cpp)

移除 `DiskItemModel.cpp` L280-L296 中的 `thisGen` 校验，确保 `weakThis->m_requestedPaths.remove(path);` 100% 正常执行，锁正常释放。
