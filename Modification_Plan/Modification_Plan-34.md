# 慢速多媒体提图异步闭锁防抖与双轨缓存绝对隔离 —— Modification_Plan-34.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在 SQLite 内存模式下，点击选择 `.ai` 格式文件再点击选择其他格式项目时，会出现假死、卡顿现象（对应用户原话：“在内存模式下每当左键选中.ai格式项目后，再去选择其他格式项目时就会发生卡顿、假死等现象”）。

同时，系统在多媒体提图时，存在严重的重复提图与双轨缓存越界污染问题：
1. **重复提图漏洞**：系统没有优先复用 `.arc` 胶囊内现有的 `_thumbnail.png` 缩略图，导致慢速文件频繁被重复提图（对应用户原话：“明明有了缩略图，却还要重复提取……漏洞所在：它压根就没有去检查 .arc 容器里已经存在的 cx 抽象 - 736_thumbnail.png”）。
2. **写盘越界污染**：在执行“重新扫描该库”或平时解析时，托管库 `.arc` 资产的高清缩略图在写进胶囊后，由于写盘出口无隔离，会无差别地再次保存一份至专属于磁盘模式的 `disk_thumbs/` 缓存目录下，严重违反了“双轨隔离、井水不犯河水”的红线（对应用户原话：“在 `MediaColorExtractor.cpp` 的写盘出口处做最彻底的模式隔离分流……重新扫描该库会污染 `disk_thumbs`”）。

本方案旨在从架构源头上彻底掐灭重复提图的性能损耗，建立严格的防抖拦截和绝对隔离写盘闸门。

## 2. 问题定位
1. **防抖锁缺失**：内存模式的 `LibraryAssetModel` 声明了 `m_requestedIcons` 变量，但在 `loadThumbnailsForRows` 里从未引用。这导致 `.ai` 文件在后台提取未完成时，多次被重复投递至提图异步队列，爆满后台线程池、阻塞 UI 响应。
2. **双轨缓存不隔离与复用逻辑漏洞**：`MediaColorExtractor::getImageForAnalysis` 在入口处仅加载 `disk_thumbs` 缓存，未优先探查 `.arc` 胶囊容器同级的 `_thumbnail.png`；而在出口处又无差别地将所有提取的图片保存至 `disk_thumbs/`，造成严重的交叉污染和磁盘空间双重浪费。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 在内存模式下每当左键选中.ai格式项目后，再去选择其他格式项目时就会发生卡顿、假死等现象 | 在内存模式 `LibraryAssetModel::loadThumbnailsForRows` 函数中，对每个文件的加载逻辑加入 `m_requestedIcons` 闭锁防抖，彻底消除重复异步请求。 | ✅ 一致 |
| 2    | 明明有了缩略图，却还要重复提取……漏洞所在：它压根就没有去检查 .arc 容器里已经存在的 cx 抽象 - 736_thumbnail.png | 在 `MediaColorExtractor::getImageForAnalysis` 中，增加优先检查并读取 `.arc` 容器同级 `_thumbnail.png` 逻辑。 | ✅ 一致 |
| 3    | 重新扫描该库会污染 `disk_thumbs`……在 `MediaColorExtractor.cpp` 的写盘出口处做最彻底的模式隔离分流 | 在 `MediaColorExtractor::getImageForAnalysis` 的写盘出口处建立双轨隔离物理闸门，属于 `.arc` 资产的 100% 仅保存至胶囊内，磁盘模式资产的 100% 仅保存至 `disk_thumbs`，绝对不交叉。 | ✅ 一致 |

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 `LibraryAssetModel.cpp` 引入 `m_requestedIcons` 闭锁防抖

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

### 4.2 `MediaColorExtractor.cpp` 双轨缓存优先加载与绝对隔离落盘

对 `src/ui/MediaColorExtractor.cpp` 进行修改，建立完整的双轨路径闸门：

```merge_diff
<<<<<<< SEARCH
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
        img = extractEmbeddedAiPreview(path, size);
    } else if (ext == "eps") {
        img = extractEmbeddedEpsPreview(path, size);
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
=======
QImage MediaColorExtractor::getImageForAnalysis(const QString& path, int size) {
    QFileInfo fi(path);
    QString containerDir = fi.absolutePath();
    bool isManagedArc = containerDir.endsWith(".arc", Qt::CaseInsensitive);

    // 🚨 1. 【托管库模式】：只去探查 .arc 胶囊内的 [baseName]_thumbnail.png，绝对不去查 disk_thumbs！
    if (isManagedArc) {
        QString thumbPath = containerDir + "/" + fi.completeBaseName() + "_thumbnail.png";
        if (QFile::exists(thumbPath)) {
            QImage arcThumb;
            if (arcThumb.load(thumbPath)) return arcThumb; // 0 毫秒直接返回胶囊内缩略图！
        }
    } else {
        // 🚨 2. 【磁盘导航模式】：只去探查 .arcmeta/disk_thumbs/[hash].png 缓存！
        QString cachePath = diskThumbCachePath(path, size);
        if (QFile::exists(cachePath)) {
            QImage cached;
            if (cached.load(cachePath)) return cached;
        }
    }

    // 3. 缓存均未命中，调起解包/提图引擎提取图像...
    QString ext = fi.suffix().toLower();
    QImage img;

    if (ext == "svg") {
        QSvgRenderer renderer(path);
        if (renderer.isValid()) {
            img = QImage(size, size, QImage::Format_ARGB32);
            img.fill(Qt::transparent);
            QPainter painter(&img);
            renderer.render(&img);
        }
    } else if (ext == "psd" || ext == "psb") {
        img = extractEmbeddedPsdThumbnail(path);
    } else if (ext == "ai") {
        img = extractEmbeddedAiPreview(path, size);
    } else if (ext == "eps") {
        img = extractEmbeddedEpsPreview(path, size);
    }

    if (img.isNull()) {
        img = WindowsShellThumbnailProvider::getShellThumbnail(path, size);
        if (img.isNull()) img.load(path);
    }

    // 🚨 4. 【双轨隔离物理闸门】：根据模式精准分流落盘，绝对不交叉！
    if (!img.isNull()) {
        if (isManagedArc) {
            // A. 托管库模式：100% 只保存到资产自身的 .arc 胶囊内部！绝对不触碰 disk_thumbs！
            QString thumbPath = containerDir + "/" + fi.completeBaseName() + "_thumbnail.png";
            if (!QFile::exists(thumbPath)) {
                img.save(thumbPath, "PNG");
                qDebug() << "[MediaColorExtractor] 托管库缩略图精准落盘至胶囊:" << thumbPath;
            }
        } else {
            // B. 磁盘导航模式：100% 只保存到程序根目录的 disk_thumbs 集中缓存中！
            QString cachePath = diskThumbCachePath(path, size);
            img.save(cachePath, "PNG");
            qDebug() << "[MediaColorExtractor] 磁盘模式缩略图精准落盘至 disk_thumbs:" << cachePath;
        }
    }

    return img;
}
>>>>>>> REPLACE
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/ui/models/LibraryAssetModel.cpp`，物理修改 `LibraryAssetModel::loadThumbnailsForRows`
- [ ] 模块/文件：`src/ui/MediaColorExtractor.cpp`，物理修改 `MediaColorExtractor::getImageForAnalysis`

**明确禁止越界修改的范围：**
- [ ] 磁盘模式 `DiskItemModel`——不修改
- [ ] 后台抽图队列 `MediaExtractorPipeline`——不修改

## 6. 实现准则与预警【核心】
1. 方案完全不引入任何新的不必要头文件。
2. 所使用的防抖拦截容器 `m_requestedIcons` 已由头文件 `LibraryAssetModel.h` 完整声明，调用安全、编译必过。
3. 对胶囊容器 `_thumbnail.png` 检查时采用完全符合 Windows 平台（大小写不敏感）特征的 `endsWith(".arc", Qt::CaseInsensitive)` 规范，极其稳定，杜绝由于路径尾部斜杠等原因产生的匹配失效。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 慢速提图防闪烁 | 针对图形文件（图像、SVG等），在异步加载缩略图期间，`data()` 接口必须返回空图标 (`QIcon()`)。 | ✅ 符合（本方案不更改 `data` 的原机制） |
| UI 异步加载与防闪烁 | 在内容面板（`ContentPanel`）进行异步数据扫描前，禁止先行调用 `m_model->clear()`。 | ✅ 符合（本方案不涉及 `clear()` 和多余刷新） |
| 双轨隔离 | 托管库下进行逻辑处理；磁盘模式下进行物理处理并同步缓存，绝对不可在同一代码块中混淆两者。 | ✅ 符合（本方案完美对齐双轨隔离规范，在 `getImageForAnalysis` 进出口处建立完全严密的物理闸门，将托管库资产与磁盘缓存彻底斩断交叉，杜绝污染） |

## 8. 待确认事项（可选）
无。
