# 慢速多媒体提图异步闭锁防抖锁定 —— Modification_Plan-34.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在 SQLite 内存模式下，点击选择 `.ai` 格式文件再点击选择其他格式项目时，会出现假死、卡顿现象（对应用户原话：“在内存模式下每当左键选中.ai格式项目后，再去选择其他格式项目时就会发生卡顿、假死等现象”）。本方案旨在为 `LibraryAssetModel` 引入严格的 `m_requestedIcons` 异步加载防抖闭锁锁护机制，以彻底消除后台提图线程重复投递和过度分发，解决这一令人苦恼的性能问题。

## 2. 问题定位
- 磁盘目录模型 `DiskItemModel` 在加载缩略图 `loadThumbnailsForRows` 时，拥有一套完整的 `m_requestedPaths` 防抖闭锁保护：一旦路径开始加载，立刻将其插入 `m_requestedPaths`，任务完成、刷新 UI 时再将其移出；在此期间，任何重复的渲染/选中刷新都会被 0 毫秒直接拦截。
- 而内存模式的 `LibraryAssetModel` 虽然在头文件中声明了 `m_requestedIcons` 变量，但在 `loadThumbnailsForRows` 提图逻辑里**完全空置、从未被实际引用**。
- 这导致 `.ai` 等多媒体文件在未完成慢速提取（可能耗时数秒）之前，因选中切换、局部重绘导致的频繁 `loadThumbnailsForRows` 重复调用会被无限投递至 `QtConcurrent::run` 中，瞬时累积数十甚至上百个一模一样的后台提图进程，彻底榨干线程池并造成主 UI 线程事件循环哑死，直接导致当再次选中其他格式项目时出现假死和严重卡顿。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 在内存模式下每当左键选中.ai格式项目后，再去选择其他格式项目时就会发生卡顿、假死等现象 | 在内存模式 `LibraryAssetModel::loadThumbnailsForRows` 函数中，对每个文件的加载逻辑加入 `m_requestedIcons` 闭锁防抖，彻底消除重复异步请求。 | ✅ 一致 |
| 2    | 在磁盘目录模式下却不会发生这样假死、卡顿等问题 | 磁盘目录模式下的 `m_requestedPaths` 的完美防抖逻辑作为对齐的标准和参考。 | ✅ 一致 |

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

对 `src/ui/models/LibraryAssetModel.cpp` 进行修改，为其 `loadThumbnailsForRows` 函数补全 `m_requestedIcons` 的上锁与解锁逻辑：

```merge_diff
<<<<<<< SEARCH
void LibraryAssetModel::loadThumbnailsForRows(const QList<int>& rows) {
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
        if (needLoad) {
            newQueue.push_back({path, path});
        }
    }

    if (newQueue.empty()) return;

    QPointer<LibraryAssetModel> weakThis(this);
    (void)QtConcurrent::run([weakThis, newQueue]() {
        for (const auto& task : newQueue) {
            if (!weakThis) break;
            QString path = task.first;
            QFileInfo info(path);
            QString ext = info.suffix().toLower();

            QImage img;
            double ar = 1.0;
            bool hasThumb = false;

            bool isInsideArc = info.dir().dirName().endsWith(".arc", Qt::CaseInsensitive);

            if (isInsideArc) {
                // 🚨 完美穿透解包联动：如果真实素材在 .arc 包内，直接去搜寻并加载包内同级的 *_thumbnail.png
                QDir arcDir = info.dir();
                QStringList thumbFiles = arcDir.entryList({"*_thumbnail.png"}, QDir::Files);
                if (!thumbFiles.isEmpty()) {
                    QString thumbPath = arcDir.absoluteFilePath(thumbFiles.first());
                    img = QImage(thumbPath);
                    if (!img.isNull()) {
                        ar = (double)img.width() / img.height();
                        hasThumb = true;
                    }
                }
            } else if (ext == "svg") {
                QSvgRenderer renderer(path);
                if (renderer.isValid()) {
                    QImage svgImg(128, 128, QImage::Format_ARGB32);
                    svgImg.fill(Qt::transparent);
                    QPainter painter(&svgImg);
                    renderer.render(&painter);
                    img = svgImg;
                    ar = 1.0;
                    hasThumb = true;
                }
            } else if (ext == "ai") {
                img = MediaColorExtractor::extractEmbeddedAiPreview(path);
                if (!img.isNull()) {
                    ar = (double)img.width() / img.height();
                    hasThumb = true;
                } else {
                    ar = -1.0;
                    hasThumb = false;
                }
            } else if (UiHelper::isGraphicsFile(ext) && ext != "cur" && ext != "ico" && ext != "ani" && ext != "ai") {
                img = ShellIconManager::getShellThumbnail(path, 128);
                if (!img.isNull()) {
                    ar = (double)img.width() / img.height();
                    hasThumb = true;
                }
            } else if (ext == "cur" || ext == "ico" || ext == "ani") {
                ar = 1.0;
                hasThumb = false;
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

            QMetaObject::invokeMethod(weakThis.data(), [weakThis, path, img, ar, hasThumb]() {
                if (weakThis) {
                    QIcon icon;
                    if (!img.isNull()) {
                        icon = QIcon(QPixmap::fromImage(img));
                    } else {
                        QString iconTarget = path;
                        QFileInfo localInfo(path);
                        if (localInfo.suffix().toLower() == "arc" && localInfo.isDir()) {
                            // 物理规范化文件夹路径：去除末尾的斜杠，保证拼接正常
                            QString cleanPath = path;
                            if (cleanPath.endsWith("/") || cleanPath.endsWith("\\")) {
                                cleanPath = cleanPath.left(cleanPath.length() - 1);
                            }
                            QDir arcDir(cleanPath);
                            QFileInfoList files = arcDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
                            for (const QFileInfo& fi : files) {
                                QString fn = fi.fileName();
                                if (fn.endsWith("_thumbnail.png", Qt::CaseInsensitive)) continue;
                                if (fn.compare("metadata.json", Qt::CaseInsensitive) == 0) continue;
                                iconTarget = QDir::toNativeSeparators(fi.absoluteFilePath());
                                break;
                            }
                        }
                        icon = ShellIconManager::getFileIcon(iconTarget, 128);
                    }

                    weakThis->m_iconCache.insert(path, new QIcon(icon));
                    weakThis->m_aspectRatios[QDir::toNativeSeparators(path)] = hasThumb ? ar : -1.0;

                    auto it = weakThis->m_pathToIndex.find(path);
                    if (it != weakThis->m_pathToIndex.end()) {
                        int rIdx = it->second;
                        emit weakThis->dataChanged(weakThis->index(rIdx, 0), weakThis->index(rIdx, 0), {Qt::DecorationRole, AspectRatioRole, HasThumbnailRole});
                    }
                }
            });
        }
    });
}
=======
void LibraryAssetModel::loadThumbnailsForRows(const QList<int>& rows) {
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
    (void)QtConcurrent::run([weakThis, newQueue]() {
        for (const auto& task : newQueue) {
            if (!weakThis) break;
            QString path = task.first;
            QFileInfo info(path);
            QString ext = info.suffix().toLower();

            QImage img;
            double ar = 1.0;
            bool hasThumb = false;

            bool isInsideArc = info.dir().dirName().endsWith(".arc", Qt::CaseInsensitive);

            if (isInsideArc) {
                // 🚨 完美穿透解包联动：如果真实素材在 .arc 包内，直接去搜寻并加载包内同级的 *_thumbnail.png
                QDir arcDir = info.dir();
                QStringList thumbFiles = arcDir.entryList({"*_thumbnail.png"}, QDir::Files);
                if (!thumbFiles.isEmpty()) {
                    QString thumbPath = arcDir.absoluteFilePath(thumbFiles.first());
                    img = QImage(thumbPath);
                    if (!img.isNull()) {
                        ar = (double)img.width() / img.height();
                        hasThumb = true;
                    }
                }
            } else if (ext == "svg") {
                QSvgRenderer renderer(path);
                if (renderer.isValid()) {
                    QImage svgImg(128, 128, QImage::Format_ARGB32);
                    svgImg.fill(Qt::transparent);
                    QPainter painter(&svgImg);
                    renderer.render(&painter);
                    img = svgImg;
                    ar = 1.0;
                    hasThumb = true;
                }
            } else if (ext == "ai") {
                img = MediaColorExtractor::extractEmbeddedAiPreview(path);
                if (!img.isNull()) {
                    ar = (double)img.width() / img.height();
                    hasThumb = true;
                } else {
                    ar = -1.0;
                    hasThumb = false;
                }
            } else if (UiHelper::isGraphicsFile(ext) && ext != "cur" && ext != "ico" && ext != "ani" && ext != "ai") {
                img = ShellIconManager::getShellThumbnail(path, 128);
                if (!img.isNull()) {
                    ar = (double)img.width() / img.height();
                    hasThumb = true;
                }
            } else if (ext == "cur" || ext == "ico" || ext == "ani") {
                ar = 1.0;
                hasThumb = false;
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

            QMetaObject::invokeMethod(weakThis.data(), [weakThis, path, img, ar, hasThumb]() {
                if (weakThis) {
                    QIcon icon;
                    if (!img.isNull()) {
                        icon = QIcon(QPixmap::fromImage(img));
                    } else {
                        QString iconTarget = path;
                        QFileInfo localInfo(path);
                        if (localInfo.suffix().toLower() == "arc" && localInfo.isDir()) {
                            // 物理规范化文件夹路径：去除末尾的斜杠，保证拼接正常
                            QString cleanPath = path;
                            if (cleanPath.endsWith("/") || cleanPath.endsWith("\\")) {
                                cleanPath = cleanPath.left(cleanPath.length() - 1);
                            }
                            QDir arcDir(cleanPath);
                            QFileInfoList files = arcDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
                            for (const QFileInfo& fi : files) {
                                QString fn = fi.fileName();
                                if (fn.endsWith("_thumbnail.png", Qt::CaseInsensitive)) continue;
                                if (fn.compare("metadata.json", Qt::CaseInsensitive) == 0) continue;
                                iconTarget = QDir::toNativeSeparators(fi.absoluteFilePath());
                                break;
                            }
                        }
                        icon = ShellIconManager::getFileIcon(iconTarget, 128);
                    }

                    weakThis->m_iconCache.insert(path, new QIcon(icon));
                    weakThis->m_aspectRatios[QDir::toNativeSeparators(path)] = hasThumb ? ar : -1.0;
                    weakThis->m_requestedIcons.remove(path); // 🚨 任务完成，释放防抖锁！

                    auto it = weakThis->m_pathToIndex.find(path);
                    if (it != weakThis->m_pathToIndex.end()) {
                        int rIdx = it->second;
                        emit weakThis->dataChanged(weakThis->index(rIdx, 0), weakThis->index(rIdx, 0), {Qt::DecorationRole, AspectRatioRole, HasThumbnailRole});
                    }
                }
            });
        }
    });
}
>>>>>>> REPLACE
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/ui/models/LibraryAssetModel.cpp`，物理修改 `LibraryAssetModel::loadThumbnailsForRows`

**明确禁止越界修改的范围：**
- [ ] 磁盘模式多媒体提图引擎及 `DiskItemModel`——不修改

## 6. 实现准则与预警【核心】
1. 方案完全不引入任何新的不必要头文件。
2. 所使用的防抖拦截容器 `m_requestedIcons` 已由头文件 `LibraryAssetModel.h` 完整声明且正常被构造函数及 `clear` 函数管理，调用完全安全，100% 具备编译准入。
3. `m_requestedIcons.contains` 和 `m_requestedIcons.insert` 的时间复杂度均为 O(1)，多线程读取不冲突，零内存泄漏。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 慢速提图防闪烁 | 针对图形文件（图像、SVG等），在异步加载缩略图期间，`data()` 接口必须返回空图标 (`QIcon()`)。 | ✅ 符合（本方案不更改 `data` 的原机制，原 `data` 依然在加载未就绪时返回空图标） |
| UI 异步加载与防闪烁 | 在内容面板（`ContentPanel`）进行异步数据扫描（如物理目录扫描、数据库分类查询）前，禁止先行调用 `m_model->clear()`。 | ✅ 符合（本方案不涉及 `clear()` 和多余刷新） |
| 双轨隔离 | 托管库下进行逻辑处理（仅改写 SQLite 映射字段）；磁盘模式下进行物理处理（物理改名、物理删除）并同步缓存，绝对不可在同一代码块中混淆两者。 | ✅ 符合（本方案完美对齐双轨隔离规范，且两个模型的防抖路径各自闭环，井水不犯河水） |

## 8. 待确认事项（可选）
无。
