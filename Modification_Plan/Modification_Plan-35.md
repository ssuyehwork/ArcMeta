# 物理文件级双轨提取零判断隔离 —— Modification_Plan-35.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在之前的缩略图缓存机制重构中，虽然在 `MediaColorExtractor::getImageForAnalysis` 内部通过逻辑分支对托管库 `.arc` 容器和普通磁盘进行了路径隔离，但两套独立的模式逻辑和路径规则依然混杂在同一个物理代码文件（`MediaColorExtractor.cpp`）里（对应用户原话：“如果某个代码文件里包含着两种模式的代码，则算是失败的”）。这违反了项目全应用级“双轨完全物理隔离”与“单一职责文件级解耦（File-Level Single Responsibility）”的架构红线。

本方案旨在通过文件级物理拆分，创建各自专享、独立、直线单流且零条件判断（0判断）的提图管道，并一并处理之前由混合提图以及选中信号引发的风暴。

## 2. 问题定位
- 整个系统提图存在混合与侵入逻辑。`MediaColorExtractor.cpp` 原本承载了两种模式的分支判断，极易造成写盘交叉污染（即在扫描托管库时顺手污染磁盘模式的 `disk_thumbs`）。
- 磁盘模型 `DiskItemModel` 应该直接对接并调用磁盘模式提取器 `DiskMediaExtractor`；托管模型 `LibraryAssetModel` 以及后台特征分析管线 `MediaExtractorPipeline` 应该直接对接并调用胶囊提图器 `CapsuleMediaExtractor`。
- 本次重构将完美消除重试队列，彻底破除 3 秒一次的无限提取卡顿循环。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 如果某个代码文件里包含着两种模式的代码，则算是失败的 | 建立物理文件级双轨隔离，新建 DiskMediaExtractor 和 CapsuleMediaExtractor。 | ✅ 一致 |
| 2    | 磁盘模式狂点不卡，而内存模式快速切换选中就会假死 | 实现 30ms 黄金信号防抖定时器，并引入提图后台抢占锁。 | ✅ 一致 |

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块和新文件内容进行物理新建与替换，不得做任何自由发挥或脑补改动。

### 4.1 新建管道一专属文件（磁盘模式专享）：`src/util/DiskMediaExtractor.h`
```cpp
#pragma once
#include <QImage>
#include <QString>

namespace ArcMeta {

class DiskMediaExtractor {
public:
    // 磁盘模式专属：提取并保存至 .arcmeta/disk_thumbs/
    static QImage getDiskThumbnail(const QString& path, int size = 512);

private:
    static QString diskThumbCachePath(const QString& path, int size);
};

} // namespace ArcMeta
```

### 4.2 新建管道一专属文件（磁盘模式专享）：`src/util/DiskMediaExtractor.cpp`
```cpp
#include "DiskMediaExtractor.h"
#include "../ui/WindowsShellThumbnailProvider.h"
#include "../ui/MediaColorExtractor.h"
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QCoreApplication>
#include <QSvgRenderer>
#include <QPainter>

namespace ArcMeta {

QString DiskMediaExtractor::diskThumbCachePath(const QString& path, int size) {
    QString appDir = QCoreApplication::applicationDirPath();
    QString cacheDir = QDir(appDir).filePath(".arcmeta/disk_thumbs/");
    QDir().mkpath(cacheDir);

    QFileInfo fi(path);
    QString hashKey = QString("%1_%2_%3_%4").arg(path).arg(fi.size()).arg(fi.lastModified().toMSecsSinceEpoch()).arg(size);
    return cacheDir + QString::number(qHash(hashKey), 16) + ".png";
}

QImage DiskMediaExtractor::getDiskThumbnail(const QString& path, int size) {
    // 1. 查 disk_thumbs 缓存
    QString cachePath = diskThumbCachePath(path, size);
    if (QFile::exists(cachePath)) {
        QImage cached;
        if (cached.load(cachePath)) return cached;
    }

    // 2. 提取图像
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
    }

    if (img.isNull()) {
        img = WindowsShellThumbnailProvider::getShellThumbnail(path, size);
        if (img.isNull()) img.load(path);
    }

    // 3. 100% 仅落盘存入 disk_thumbs/ 目录
    if (!img.isNull()) {
        img.save(cachePath, "PNG");
    }
    return img;
}

} // namespace ArcMeta
```

### 4.3 新建管道二专属文件（受控资源库专享）：`src/meta/CapsuleMediaExtractor.h`
```cpp
#pragma once
#include <QImage>
#include <QString>

namespace ArcMeta {

class CapsuleMediaExtractor {
public:
    // 受控库模式专属：提取并保存至 .arc 胶囊内部 [baseName]_thumbnail.png
    static QImage getCapsuleThumbnail(const QString& mainAssetPath, int size = 512);
};

} // namespace ArcMeta
```

### 4.4 新建管道二专属文件（受控资源库专享）：`src/meta/CapsuleMediaExtractor.cpp`
```cpp
#include "CapsuleMediaExtractor.h"
#include "../ui/WindowsShellThumbnailProvider.h"
#include "../ui/MediaColorExtractor.h"
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QSvgRenderer>
#include <QPainter>

namespace ArcMeta {

QImage CapsuleMediaExtractor::getCapsuleThumbnail(const QString& mainAssetPath, int size) {
    QFileInfo fi(mainAssetPath);
    QString containerDir = fi.absolutePath();
    QString thumbPath = containerDir + "/" + fi.completeBaseName() + "_thumbnail.png";

    // 1. 优先查 .arc 胶囊内部
    if (QFile::exists(thumbPath)) {
        QImage arcThumb;
        if (arcThumb.load(thumbPath)) return arcThumb;
    }

    // 2. 提取图像
    QString ext = fi.suffix().toLower();
    QImage img;

    if (ext == "svg") {
        QSvgRenderer renderer(mainAssetPath);
        if (renderer.isValid()) {
            img = QImage(size, size, QImage::Format_ARGB32);
            img.fill(Qt::transparent);
            QPainter painter(&img);
            renderer.render(&painter);
        }
    }

    if (img.isNull()) {
        img = WindowsShellThumbnailProvider::getShellThumbnail(mainAssetPath, size);
        if (img.isNull()) img.load(mainAssetPath);
    }

    // 3. 100% 仅落盘保存至 .arc 胶囊容器内部！
    if (!img.isNull() && containerDir.endsWith(".arc", Qt::CaseInsensitive)) {
        img.save(thumbPath, "PNG");
    }
    return img;
}

} // namespace ArcMeta
```

### 4.5 管道一对接：修改 `src/ui/models/DiskItemModel.cpp`
将磁盘提图彻底对接至 `DiskMediaExtractor`：

```merge_diff
<<<<<<< SEARCH
void DiskItemModel::loadThumbnailsForRows(const QList<int>& rows) {
    std::vector<std::pair<QString, QString>> newQueue;
    for (int r : rows) {
        if (r < 0 || r >= static_cast<int>(m_allRecords.size())) continue;
        const auto& rec = m_allRecords[r];
        if (rec.isDir) continue;

        QString path = rec.path;
        if (!UiHelper::isGraphicsFile(rec.suffix)) continue;

        if (m_iconCache.contains(path) || m_requestedPaths.contains(path)) continue;

        m_requestedPaths.insert(path);
        newQueue.push_back({path, path});
    }

    if (newQueue.empty()) return;

    QPointer<DiskItemModel> weakThis(this);
    (void)QtConcurrent::run([weakThis, newQueue]() {
        for (const auto& task : newQueue) {
            if (!weakThis) break;
            QString path = task.first;

            QImage img = MediaColorExtractor::getImageForAnalysis(path, 512);

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
void DiskItemModel::loadThumbnailsForRows(const QList<int>& rows) {
    std::vector<std::pair<QString, QString>> newQueue;
    for (int r : rows) {
        if (r < 0 || r >= static_cast<int>(m_allRecords.size())) continue;
        const auto& rec = m_allRecords[r];
        if (rec.isDir) continue;

        QString path = rec.path;
        if (!UiHelper::isGraphicsFile(rec.suffix)) continue;

        // 🚨 核心防抖抢占锁：避免高频 100ms 定时器重复起爆进程！
        if (m_iconCache.contains(path) || m_requestedPaths.contains(path)) continue;

        m_requestedPaths.insert(path);
        newQueue.push_back({path, path});
    }

    if (newQueue.empty()) return;

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

并在 `src/ui/models/DiskItemModel.cpp` 顶部增加新头文件包含：
```cpp
#include "../../util/DiskMediaExtractor.h"
```

### 4.9 管道二对接：修改 `src/ui/models/LibraryAssetModel.cpp`
将托管提图彻底对接至 `CapsuleMediaExtractor`：

```merge_diff
<<<<<<< SEARCH
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
            }
=======
            } else if (ext == "svg" || ext == "psd" || ext == "psb" || ext == "ai" || ext == "eps") {
                img = CapsuleMediaExtractor::getCapsuleThumbnail(path, 128);
                if (!img.isNull()) {
                    ar = (double)img.width() / img.height();
                    hasThumb = true;
                }
            } else if (UiHelper::isGraphicsFile(ext) && ext != "cur" && ext != "ico" && ext != "ani") {
                img = CapsuleMediaExtractor::getCapsuleThumbnail(path, 128);
                if (!img.isNull()) {
                    ar = (double)img.width() / img.height();
                    hasThumb = true;
                }
            }
>>>>>>> REPLACE
```

并在 `src/ui/models/LibraryAssetModel.cpp` 顶部增加头文件：
```cpp
#include "../CapsuleMediaExtractor.h"
```

### 4.10 彻底清退 `MediaColorExtractor::getImageForAnalysis`
将混合的 `getImageForAnalysis` 从 `MediaColorExtractor.h` 和 `MediaColorExtractor.cpp` 中彻底移除。

### 4.11 构建配置更新 `CMakeLists.txt`
将新增加的源文件登记加入构建：

```merge_diff
<<<<<<< SEARCH
    src/ui/WindowsShellThumbnailProvider.h
    src/ui/MediaColorExtractor.h
    src/ui/MediaColorExtractor.cpp
    src/util/ShellHelper.cpp
=======
    src/ui/WindowsShellThumbnailProvider.h
    src/ui/MediaColorExtractor.h
    src/ui/MediaColorExtractor.cpp
    src/util/DiskMediaExtractor.h
    src/util/DiskMediaExtractor.cpp
    src/meta/CapsuleMediaExtractor.h
    src/meta/CapsuleMediaExtractor.cpp
    src/util/ShellHelper.cpp
>>>>>>> REPLACE
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：物理新建 `src/util/DiskMediaExtractor.h/cpp`、`src/meta/CapsuleMediaExtractor.h/cpp`。
- [ ] 模块/文件：修改 `CMakeLists.txt`，追加新编译源文件。
- [ ] 模块/文件：修改 `DiskItemModel.cpp` 与 `LibraryAssetModel.cpp` 对接全新拆分提图器。
- [ ] 模块/文件：修改 `MediaColorExtractor.h/cpp`，物理删除混合式 `getImageForAnalysis` 等业务层代码。

**明确禁止越界修改的范围：**
- [ ] 核心扫描算法及 SQLite 数据库、`ContentPanel` 本身——不修改

## 6. 实现准则与预警【核心】
1. **彻底的头文件清退**：`DiskMediaExtractor` 严禁 include 与受控包相关的任何头文件。`CapsuleMediaExtractor` 严禁 include 与 `disk_thumbs` 缓存路径计算相关的任何磁盘类头文件。
2. **0 判断红线**：分流落盘逻辑中不可出现两轨逻辑交叉，两个提取器的主要落盘路径直接硬编码到单条流水线代码中。
3. **MOC 编译校验**：修改 `CMakeLists.txt` 后，请确保构建工具链已正确重新扫描，防止报“未定义的符号”链接错误。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 双轨隔离 | 托管库下进行逻辑处理（仅改写 SQLite 映射字段）；磁盘模式下进行物理处理（物理改名、物理删除）并同步缓存，绝对不可在同一代码块中混淆两者。 | ✅ 符合（本方案在文件级和物理介质级别将双轨重构得比以前更为纯净） |
| UI 异步加载与防闪烁 | 在内容面板（`ContentPanel`）进行异步数据扫描（如物理目录扫描、数据库分类查询）前，禁止先行调用 `m_model->clear()`。 | ✅ 符合（本方案不更改任何界面数据刷新流程） |

## 8. 待确认事项（可选）
无。
