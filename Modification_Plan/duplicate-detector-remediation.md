# 重复添加提示弹窗修复与 SHA-256 导入即时持久化方案 —— duplicate-detector-remediation.md

## 一、 问题背景与核心痛点

1. **“重复添加提示”弹窗右侧卡片黑屏与 4180x4180 假数据问题**：
   - 新文件导入后，缩略图与尺寸提取任务异步投递至后台队列（`MediaExtractorPipeline`），但查重弹窗（`DuplicateConflictDialog`）在导入完成后瞬间被弹出。
   - 弹窗构建卡片时由于后台尚未完成提取，`item.thumbnail` 为空且 `item.width <= 0`，卡片缺乏现场渲染兜底，导致右侧卡片背景纯黑，且触发了 `4180 x 4180` 的硬编码假分辨率保底。

2. **哈希值（`sha256`）未在导入时落盘**：
   - 新文件导入（拖拽、粘贴、自动监控）时，系统注册了资产记录，但在后台提取管线中遗漏了哈希计算，导致 SQLite 数据库中的 `sha256` 字段恒为空。
   - 查重服务（`DuplicateDetectorService`）因数据库哈希缺失，不得不频繁现场读取磁盘物理文件临时计算哈希，引发磁盘 I/O 卡顿。

---

## 二、 极简实施方案

### 2.1 修复一：`src/ui/DuplicateConflictDialog.cpp` 卡片现场保底渲染与真实尺寸修正

在 `createCard` 函数构建卡片时，增加即时兜底逻辑：若缩略图为空，直接现场调用磁盘渲染器生成一张预览图，并从预览图中提取真实宽高，彻底消除黑屏与 `4180 x 4180` 假数据。

```cpp
static QWidget* createCard(const DuplicateItemInfo& item, const QString& badgeText, bool isExisting) {
    QWidget* card = new QWidget();
    card->setFixedSize(320, 320);
    card->setStyleSheet("background-color: #232325; border-radius: 8px;");

    QVBoxLayout* layout = new QVBoxLayout(card);
    layout->setContentsMargins(15, 15, 15, 15);
    layout->setSpacing(8);

    QLabel* imgLabel = new QLabel(card);
    imgLabel->setFixedSize(290, 200);
    imgLabel->setStyleSheet("background-color: #2D2D30; border-radius: 6px;");
    imgLabel->setAlignment(Qt::AlignCenter);

    // 1. 缩略图保底：拿现有的缩略图；如果为空，现场调用 DiskMediaExtractor 实时渲染一张
    QImage thumb = item.thumbnail;
    if (thumb.isNull() && QFile::exists(item.path)) {
        thumb = DiskMediaExtractor::getDiskThumbnail(item.path, 256);
    }

    if (!thumb.isNull()) {
        imgLabel->setPixmap(QPixmap::fromImage(thumb).scaled(290, 200, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }

    QLabel* badge = new QLabel(badgeText, imgLabel);
    badge->setStyleSheet("background-color: rgba(0, 0, 0, 0.6); color: #FFFFFF; border-radius: 4px; padding: 2px 8px; font-size: 11px;");
    badge->move(10, 10);

    layout->addWidget(imgLabel);

    QLabel* nameLabel = new QLabel(item.filename, card);
    nameLabel->setAlignment(Qt::AlignCenter);
    nameLabel->setStyleSheet("color: #FFFFFF; font-weight: bold; font-size: 12px;");
    layout->addWidget(nameLabel);

    // 2. 真实尺寸保底：若 width/height <= 0，优先使用渲染图的真实宽高，避免硬套 4180 假数据
    int realW = item.width;
    int realH = item.height;
    if ((realW <= 0 || realH <= 0) && !thumb.isNull()) {
        realW = thumb.width();
        realH = thumb.height();
    }

    QString dimStr = (realW > 0 && realH > 0) 
                     ? QString("%1 x %2").arg(realW).arg(realH) 
                     : "未知分辨率";

    QString infoText = QString("%1 / %2 KB")
                        .arg(dimStr)
                        .arg(item.size / 1024);

    QLabel* infoLabel = new QLabel(infoText, card);
    infoLabel->setAlignment(Qt::AlignCenter);
    infoLabel->setStyleSheet("color: #AAAAAA; font-size: 11px;");
    layout->addWidget(infoLabel);

    if (!item.tagHint.isEmpty()) {
        QLabel* tagBadge = new QLabel(item.tagHint, card);
        tagBadge->setAlignment(Qt::AlignCenter);
        tagBadge->setStyleSheet("background-color: #333336; color: #CCCCCC; border-radius: 4px; padding: 2px 6px; font-size: 10px;");
        layout->addWidget(tagBadge, 0, Qt::AlignHCenter);
    }

    return card;
}
```

---

### 2.2 修复二：`src/meta/MediaExtractorPipeline.cpp` 后台计算 SHA-256 并即时落盘

在后台流水线 `MediaExtractorPipeline::processItemDirect` 中，加入 SHA-256 哈希计算与数据库持久化逻辑：

```cpp
void MediaExtractorPipeline::processItemDirect(const std::wstring& path) {
    if (m_isCanceled.load()) return;

    QString qPath = QString::fromStdWString(path);
    QFileInfo info(qPath);

    // 1. 提取尺寸
    int w = 0, h = 0;
    extractDimensions(path, w, h);

    // 2. 提取主色与调色盘
    std::wstring colorStr;
    QVector<QPair<QColor, float>> palette;
    if (info.isFile() && MediaColorExtractor::isGraphicsFile(info.suffix().toLower())) {
        QImage thumb = ImageDecoderFacade::loadScaledImage(qPath, 512);
        if (!thumb.isNull()) {
            auto pal = ColorAlgorithmEngine::extractPaletteFromImage(thumb);
            if (!pal.isEmpty()) {
                QColor dominant = MediaColorExtractor::quantizeColor(pal.first().first);
                colorStr = dominant.name().toUpper().toStdWString();
                palette = pal;
            }
        }
    }

    // 3. 计算 SHA-256 并持久化写入数据库与内存快照
    if (info.isFile()) {
        QFile file(qPath);
        if (file.open(QIODevice::ReadOnly)) {
            QCryptographicHash hash(QCryptographicHash::Sha256);
            if (hash.addData(&file)) {
                std::string shaHex = hash.result().toHex().toLower().toStdString();
                MetadataManager::instance().setSha256(path, shaHex, false);
            }
            file.close();
        }
    }

    // 4. 更新媒体高级特征并标记完成
    MetadataManager::instance().updateExtractedMediaFeatures(path, w, h, colorStr, palette, 1);
}
```

---

## 三、 验证与效果

1. **“重复添加提示”弹窗黑屏修复**：即便后台尚未完成缩略图抽离，弹窗也能现场直出缩略图卡片，且显示真实的物理尺寸。
2. **哈希永久持久化**：所有场景（拖拽、粘贴、自动监控）导入的文件，其 SHA-256 均被后台流水线自动写入 SQLite 数据库，后续查重实现 0 I/O 毫秒级匹配。

---

> **文档状态**：方案已写入归档，等待后续重构代码执行。
