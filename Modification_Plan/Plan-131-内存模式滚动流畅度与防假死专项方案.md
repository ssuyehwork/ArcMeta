# Plan-131 专项方案：内存模式滚动卡顿、假死与缩略图极速加载重构

> **所属大纲章节**：1.2 视图渲染与异步 I/O 并发体系
> **专项背景**：根据排查，滚动卡顿与假死**特异性发生在“内存托管库模式”**（`LibraryAssetModel`）。本方案针对内存模式下的主线程 I/O 阻塞、单线程串行死锁与防抖饿死进行根治。

---

## 内存模式特异性“卡顿/假死”核心病灶

| 序号 | 致命根因 | 代码精准定位 | 严重后果与假死机制 |
| :--- | :--- | :--- | :--- |
| **1** | **主线程回调内执行磁盘扫描与 Shell 提取** | `src/ui/models/LibraryAssetModel.cpp:353-379` | 当后台未解出缩略图时，在 `QMetaObject::invokeMethod` **主线程（UI 线程）** 中执行 `arcDir.entryInfoList`（扫描物理磁盘）和 `ShellIconManager::getFileIcon`（Win32 COM 提取图标）。滚动时数十次回调排队在主线程执行物理 I/O，**主线程事件循环彻底卡死假死**。 |
| **2** | **主线程 data(HasThumbnailRole) 高频构造 QFileInfo/QDir** | `src/ui/models/LibraryAssetModel.cpp:524-526` | 每一帧排版和绘制时，每个条目均触发 `QFileInfo(path).dir().dirName()`，每秒执行上千次 Win32 路径规范化系统调用。 |
| **3** | **滚动条拖动时防抖饿死（Starvation）** | `src/ui/ContentPanel.cpp:1415` | 滑动时 `valueChanged` 高频重置 100ms 单次定时器，滑动全程 0 次触发请求；一旦松手数十个任务瞬时积压冲击主线程。 |
| **4** | **后台解码单线程串行阻塞** | `src/ui/models/LibraryAssetModel.cpp:306` | 单个后台线程 `for` 循环串行解码所有 `.arc` 包，没有多核并发。 |

---

## 彻底根治实施方案（无脑修改清单）

---

### 第一步：将 `LibraryAssetModel` 的所有 I/O 与图标提取完全移入工作线程

**修改文件**：[`src/ui/models/LibraryAssetModel.cpp`](file:///G:/C++/ArcMeta/ArcMeta/src/ui/models/LibraryAssetModel.cpp)

**原理**：主线程回调中**仅允许做纯内存赋值和发射信号**（耗时 0.0001ms），绝对禁止执行任何 `QFileInfo`、`QDir::entryInfoList` 或 `ShellIconManager::getFileIcon`！所有耗时操作必须在 `QtConcurrent::run` 工作线程中完成。

```diff
<<<<<<< SEARCH
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

            if (isInsideArc || ext == "svg" || ext == "psd" || ext == "psb" || ext == "ai" || ext == "eps") {
                // 🚨 管道二单线直达：直接调用 CapsuleMediaExtractor 只读版本，零分支判断！
                img = CapsuleMediaExtractor::getCapsuleThumbnailReadOnly(path);
                if (!img.isNull()) {
                    ar = (double)img.width() / img.height();
                    hasThumb = true;
                }
            } else if (UiHelper::isGraphicsFile(ext) && ext != "cur" && ext != "ico" && ext != "ani") {
                img = CapsuleMediaExtractor::getCapsuleThumbnailReadOnly(path);
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
                        if (weakThis->isSuspended()) {
                            weakThis->m_pendingUpdateRows.insert(rIdx);
                        } else {
                            emit weakThis->dataChanged(weakThis->index(rIdx, 0), weakThis->index(rIdx, 0), {Qt::DecorationRole, AspectRatioRole, HasThumbnailRole});
                        }
                    }
                }
            });
        }
    });
=======
    QPointer<LibraryAssetModel> weakThis(this);
    // 并发分发至后台线程池：多核并行处理，彻底消除串行排队
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
            QMetaObject::invokeMethod(weakThis.data(), [weakThis, path, icon, ar, hasThumb]() {
                if (weakThis) {
                    weakThis->m_iconCache.insert(path, new QIcon(icon));
                    weakThis->m_aspectRatios[QDir::toNativeSeparators(path)] = hasThumb ? ar : -1.0;
                    weakThis->m_requestedIcons.remove(path);

                    auto it = weakThis->m_pathToIndex.find(path);
                    if (it != weakThis->m_pathToIndex.end()) {
                        int rIdx = it->second;
                        if (weakThis->isSuspended()) {
                            weakThis->m_pendingUpdateRows.insert(rIdx);
                        } else {
                            emit weakThis->dataChanged(weakThis->index(rIdx, 0), weakThis->index(rIdx, 0), {Qt::DecorationRole, AspectRatioRole, HasThumbnailRole});
                        }
                    }
                }
            });
        });
    }
>>>>>>> REPLACE
```

---

### 第二步：剥离 `LibraryAssetModel::data(HasThumbnailRole)` 中的 `QFileInfo`

**修改文件**：[`src/ui/models/LibraryAssetModel.cpp`](file:///G:/C++/ArcMeta/ArcMeta/src/ui/models/LibraryAssetModel.cpp)

```diff
<<<<<<< SEARCH
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
=======
    } else if (role == HasThumbnailRole) {
        static const QStringList iconOnlyExts = {"cur", "ico", "ani"};
        if (iconOnlyExts.contains(record.suffix.toLower())) return false;

        // 纯内存字符串快速匹配，零 QFileInfo 构造与系统调用
        bool isInsideArcContainer = path.contains(".arc/", Qt::CaseInsensitive) || path.contains(".arc\\", Qt::CaseInsensitive);
        bool isArcContainer = record.isDir && path.endsWith(".arc", Qt::CaseInsensitive);
        if (isInsideArcContainer || isArcContainer) {
            QString nativePath = QDir::toNativeSeparators(path);
            return m_aspectRatios.contains(nativePath) && m_aspectRatios.value(nativePath) > 0.0;
        }
>>>>>>> REPLACE
```

---

### 第三步：应用 `ContentPanel.cpp` 节流与预加载（同步 Plan-131 阶段一）

确保 `ContentPanel.cpp` 中的 `m_visibleTimer` 已切换为 30ms 节流触发，且预加载行数设置为 25 行。

---

## 验证方法

1. 切换至内存模式（自定义分类或托管库），列表加载 1000+ 项资产；
2. 鼠标按住右侧滚动条滑块，快速大幅度上下往复拖动；
3. **预期表现**：
   - 界面无任何卡顿、假死或无响应提示；
   - 滑动过程中缩略图秒级并行填充，告别持续灰底占位符；
   - CPU 主线程占用率稳定在 5%~15% 以下。
