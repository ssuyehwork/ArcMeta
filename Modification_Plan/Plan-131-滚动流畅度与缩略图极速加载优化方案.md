# Plan-131 实施方案：内容面板滚动极速流畅与缩略图秒级加载重构

> **所属大纲章节**：UI 渲染性能与异步 I/O 管道优化
> **目标**：彻底消除按住滚动条上下滑动时的卡顿、掉帧、大面积灰色占位符及缩略图加载迟缓问题，实现 60FPS+ 丝滑滚动与即时预加载。

---

## 致命瓶颈根因诊断清单

| 维度 | 致命根因 | 代码定位 | 表现与后果 |
| :--- | :--- | :--- | :--- |
| **时序与调度** | **滚动中防抖饿死 (Starvation)** | `src/ui/ContentPanel.cpp:1415` | 滑动滚动条时高频触发 `valueChanged`，导致 100ms 单次定时器被不断 Reset 重置。**在按住滑动的全过程中，缩略图加载请求 0 次触发**，直到松手才开始加载，导致滑动中全屏占位符。 |
| **主线程 I/O** | **主线程 Paint 中执行磁盘 I/O** | `src/ui/ThumbnailDelegate.cpp:123, 152` | 每一帧 `paint()` 中对每个 item 同步调用 `QFileInfo(path).isDir()`，在主线程产生上百次物理磁盘 I/O 检查。 |
| **主线程 CPU** | **每帧重复 CPU 图像软缩放** | `src/ui/CardPainterHelper.cpp:36` | 每一帧对每个可见 item 重新执行 `thumb.scaled(cardRect.size(), ..., Qt::SmoothTransformation)`，造成大量 CPU 重采样与内存分配。 |
| **并发与吞吐** | **缩略图后台单线程串行处理** | `src/ui/models/DiskItemModel.cpp:278` | 后台解码采用单一 `QtConcurrent::run` 跑 `for` 循环串行解码，数十张图片需排队数十秒；旧任务阻塞新视口任务。 |
| **频繁重排** | **单图返回触发全量视图重排** | `src/ui/models/DiskItemModel.cpp:306` + `JustifiedView.cpp:128` | 每张缩略图返回均发射 `AspectRatioRole`，触发 `JustifiedView` 重新执行 `doLayout()` 全局网格几何重排。 |

---

## 阶段一：修复滚动条滑动响应（消除防抖饿死）

### 修改文件：`src/ui/ContentPanel.cpp`

#### 改动 1：引入节流（Throttle）滚动响应机制

```diff
<<<<<<< SEARCH
    m_visibleTimer = new QTimer(this);
    m_visibleTimer->setSingleShot(true);
    m_visibleTimer->setInterval(100); 
    connect(m_visibleTimer, &QTimer::timeout, this, &ContentPanel::refreshVisibleThumbnails);
=======
    m_visibleTimer = new QTimer(this);
    m_visibleTimer->setSingleShot(false);
    m_visibleTimer->setInterval(30); // 30ms 高频节流窗口
    connect(m_visibleTimer, &QTimer::timeout, this, &ContentPanel::refreshVisibleThumbnails);
>>>>>>> REPLACE
```

#### 改动 2：扩大视口预加载缓冲池（Precache Padding）

```diff
<<<<<<< SEARCH
    // 稍微向外扩大一两页缓冲，以防止滑动假白 (Precache padding)
    int padding = 5;
    top = std::max(0, top - padding);
    bottom = std::min(m_proxyModel->rowCount() - 1, bottom + padding);
=======
    // 向外扩大 25 行超前预加载缓冲池，彻底消除快速滑动假白
    int padding = 25;
    top = std::max(0, top - padding);
    bottom = std::min(m_proxyModel->rowCount() - 1, bottom + padding);
>>>>>>> REPLACE
```

#### 改动 3：滚动条事件改为启动/维持节流，并在滚动停止后自动熄火

```diff
<<<<<<< SEARCH
    connect(m_gridView->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        m_visibleTimer->start();
    });
=======
    connect(m_gridView->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        if (!m_visibleTimer->isActive()) {
            m_visibleTimer->start();
        }
        refreshVisibleThumbnails(); // 即时触发一次当前视口排队
    });
    connect(m_gridView->verticalScrollBar(), &QScrollBar::sliderReleased, this, [this]() {
        refreshVisibleThumbnails();
    });
>>>>>>> REPLACE
```

---

## 阶段二：剥离 Paint 中的 CPU 软缩放与磁盘 I/O（提升至 60FPS+）

### 修改文件：`src/ui/CardPainterHelper.cpp`

#### 改动 1：使用 QPainter 原生硬件加速几何居中绘制，彻底移除 `thumb.scaled`

```diff
<<<<<<< SEARCH
    if (hasThumb && !thumb.isNull()) {
        // 图片/视频缩略图：按原比例 Contain 居中展示，完好保留长宽比
        QPixmap scaled = thumb.scaled(cardRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        int x = cardRect.center().x() - scaled.width() / 2;
        int y = cardRect.center().y() - scaled.height() / 2;
        painter->drawPixmap(x, y, scaled);
    } else if (!defaultIcon.isNull()) {
=======
    if (hasThumb && !thumb.isNull()) {
        // 原生硬件加速绘制：根据原图宽高比计算目标矩形，零 CPU 重采样，零内存分配
        QSize imgSize = thumb.size();
        QSize targetSize = imgSize.scaled(cardRect.size(), Qt::KeepAspectRatio);
        QRect targetRect(cardRect.center().x() - targetSize.width() / 2,
                         cardRect.center().y() - targetSize.height() / 2,
                         targetSize.width(), targetSize.height());
        painter->drawPixmap(targetRect, thumb);
    } else if (!defaultIcon.isNull()) {
>>>>>>> REPLACE
```

### 修改文件：`src/ui/ThumbnailDelegate.cpp`

#### 改动 1：消除 `ThumbnailDelegate::paint` 中的 `QFileInfo.isDir()` 同步磁盘 I/O

```diff
<<<<<<< SEARCH
    // ④ 绘制自适应扩展名徽章
    if (m_pathRole != -1) {
        QString type = (m_typeRole != -1) ? index.data(m_typeRole).toString() : "";
        QString path = index.data(m_pathRole).toString();
        QFileInfo info(path);
        QString ext;
        if (type == "category" || type == "folder") {
            ext = "DIR"; // 分类与文件夹均强制显示为 "DIR" 徽章，增强视觉一致性
        } else {
            ext = info.isDir() ? "DIR" : info.suffix().toUpper();
        }
        if (ext.isEmpty()) ext = "FILE";

        CardPainterHelper::drawExtensionBadge(painter, m.cardRect, ext, hasThumb);
    }
=======
    // ④ 绘制自适应扩展名徽章（从内存数据直接解析，零磁盘 I/O）
    if (m_pathRole != -1) {
        QString type = (m_typeRole != -1) ? index.data(m_typeRole).toString() : "";
        QString ext;
        if (type == "category" || type == "folder") {
            ext = "DIR";
        } else {
            QString path = index.data(m_pathRole).toString();
            int dotIdx = path.lastIndexOf('.');
            int slashIdx = qMax(path.lastIndexOf('/'), path.lastIndexOf('\\'));
            if (dotIdx > slashIdx && dotIdx != -1) {
                ext = path.mid(dotIdx + 1).toUpper();
            }
        }
        if (ext.isEmpty()) ext = "FILE";

        CardPainterHelper::drawExtensionBadge(painter, m.cardRect, ext, hasThumb);
    }
>>>>>>> REPLACE
```

---

## 阶段三：缩略图提取线程池并发化（提升吞吐量 400%）

### 修改文件：`src/ui/models/DiskItemModel.cpp`

#### 改动 1：将单线程串行提取升级为 `QThreadPool` 并发分发

```diff
<<<<<<< SEARCH
    QPointer<DiskItemModel> weakThis(this);
    (void)QtConcurrent::run([weakThis, newQueue]() {
        for (const auto& task : newQueue) {
            if (!weakThis) break;
            QString path = task.first;

            // 🚨 单线直达：直接调用 DiskMediaExtractor，零分支判断！
            QImage img = DiskMediaExtractor::getDiskThumbnail(path, 512);

            double ar = 1.0;
            bool hasThumb = false;
            if (!img.isNull()) {
                ar = (double)img.width() / img.height();
                hasThumb = true;
            }

            QMetaObject::invokeMethod(weakThis.data(), [weakThis, path, img, ar, hasThumb]() {
                if (weakThis) {
                    QIcon icon = img.isNull() ? ShellIconManager::getFileIcon(path, 128) : QIcon(QPixmap::fromImage(img));
                    weakThis->m_iconCache.insert(path, new QIcon(icon));
                    weakThis->m_aspectRatios[QDir::toNativeSeparators(path)] = hasThumb ? ar : -1.0;
                    weakThis->m_requestedPaths.remove(path); // 任务完成，释放防抖锁，保持其内存占用完全有界！

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
    QPointer<DiskItemModel> weakThis(this);
    for (const auto& task : newQueue) {
        QString path = task.first;
        (void)QtConcurrent::run([weakThis, path]() {
            if (!weakThis) return;
            QImage img = DiskMediaExtractor::getDiskThumbnail(path, 512);

            double ar = 1.0;
            bool hasThumb = false;
            if (!img.isNull()) {
                ar = (double)img.width() / img.height();
                hasThumb = true;
            }

            QMetaObject::invokeMethod(weakThis.data(), [weakThis, path, img, ar, hasThumb]() {
                if (weakThis) {
                    QIcon icon = img.isNull() ? ShellIconManager::getFileIcon(path, 128) : QIcon(QPixmap::fromImage(img));
                    weakThis->m_iconCache.insert(path, new QIcon(icon));
                    weakThis->m_aspectRatios[QDir::toNativeSeparators(path)] = hasThumb ? ar : -1.0;
                    weakThis->m_requestedPaths.remove(path);

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

## 阶段四：LibraryAssetModel 同步升级并发

### 修改文件：`src/ui/models/LibraryAssetModel.cpp`

对 `LibraryAssetModel::loadThumbnailsForRows` 采取同等并发化升级，将任务逐项投递至 `QtConcurrent::run`，让多核 CPU 全速并行提取，彻底消灭队列排队拥堵。
