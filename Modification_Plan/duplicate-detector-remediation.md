# 重复添加提示弹窗修复与 SHA-256 导入即时持久化三重闭环方案 —— duplicate-detector-remediation.md

## 一、 问题背景与核心痛点

1. **“重复添加提示”弹窗右侧卡片黑屏与 4180x4180 假数据问题**：
   - 新文件导入后，缩略图与尺寸提取任务异步投递至后台队列（`MediaExtractorPipeline`），但查重弹窗（`DuplicateConflictDialog`）在导入完成后瞬间弹出。
   - 弹窗构建卡片时由于后台尚未完成提取，`item.thumbnail` 为空且 `item.width <= 0`，卡片缺乏现场渲染兜底，导致右侧卡片背景纯黑，且触发了 `4180 x 4180` 的硬编码假分辨率保底。

2. **哈希值（`sha256`）未在导入时落盘**：
   - 新文件导入（拖拽、粘贴、自动监控）时，系统注册了资产记录，但没有在文件落盘的第一时间同步写入哈希，导致 SQLite 数据库中的 `sha256` 字段恒为空。
   - 查重服务（`DuplicateDetectorService`）因数据库哈希缺失，频繁现场读取磁盘物理文件临时计算哈希，引发磁盘 I/O 卡顿。

---

## 二、 精细化零脑补实施方案（三重防线 + UI保底）

### 2.1 `MetadataManager.h` 修改

**定位函数声明**（约第 155 行）：
将原声明：
```cpp
bool registerAsset(const std::string& folderId, const std::wstring& assetPath, int targetCatId);
```
**修改为**：
```cpp
bool registerAsset(const std::string& folderId, const std::wstring& assetPath, int targetCatId, const std::string& sha256 = "");
```

---

### 2.2 `MetadataManager.cpp` 修改

**定位函数实现**：`bool MetadataManager::registerAsset(...)`（约第 245 行）

1. **修改函数头**：
```cpp
bool MetadataManager::registerAsset(const std::string& initialFolderId, const std::wstring& assetPath, int targetCatId, const std::string& sha256) {
```
2. **在初始化 `RuntimeMeta rm` 结构体赋值处（约第 265 行）写入哈希**：
```cpp
rm.baseName = baseName; 
rm.ext = ext; 
rm.sha256 = sha256; // 补齐：将计算好的哈希填入内存结构体
rm.isManaged = true;
```

---

### 2.3 `AssetImporter.cpp` 修改（第一重防线：新文件导入即写入）

**定位函数**：`bool AssetImporter::importSingleFile(...)`（约第 170 行）

在文件拷贝/重命名成功后（即 `if (!copied) { ... return false; }` 之后），加入哈希提取并传入 `registerAsset`：

```cpp
    // 切断同步缩略图物理提取，改为在 importAssets 外部统一后台异步队列提取
    if (newlyImportedPaths) {
        newlyImportedPaths->append(destPath);
    }

    // 在文件落盘入库的第一时间，计算 SHA-256 哈希
    std::string sha256Hex;
    {
        QFile destFile(destPath);
        if (destFile.open(QIODevice::ReadOnly)) {
            QCryptographicHash hash(QCryptographicHash::Sha256);
            if (hash.addData(&destFile)) {
                sha256Hex = hash.result().toHex().toLower().toStdString();
            }
            destFile.close();
        }
    }
 
    // 提交 Base36 ID 和计算好的 SHA-256 一并入库
    std::wstring wDestPath = QDir::toNativeSeparators(destPath).toStdWString(); 
    bool registered = MetadataManager::instance().registerAsset(fileId.toStdString(), wDestPath, targetCatId, sha256Hex); 
    if (!registered) {
        QDir(containerDir).removeRecursively();
        return false;
    }
    return true;
```

---

### 2.4 `MediaExtractorPipeline.cpp` 修改（第二重防线：存量老文件后台扫描自愈）

**定位函数**：`void MediaExtractorPipeline::processItemDirect(const std::wstring& path)`（约第 185 行）

在执行特征提取时，加入老资产哈希自愈检测：

```cpp
    // 检查当前资产哈希是否为空（老数据自愈）
    RuntimeMeta currentMeta = MetadataManager::instance().getMeta(path);
    if (currentMeta.sha256.empty() && info.isFile()) {
        QFile f(qPath);
        if (f.open(QIODevice::ReadOnly)) {
            QCryptographicHash hash(QCryptographicHash::Sha256);
            if (hash.addData(&f)) {
                std::string shaHex = hash.result().toHex().toLower().toStdString();
                MetadataManager::instance().setSha256(path, shaHex, false);
            }
            f.close();
        }
    }
```

---

### 2.5 `DuplicateDetectorService.cpp` 修改（第三重防线：查重现场回写兜底）

**定位函数**：`DuplicateDetectorService::detectDuplicates(...)`（约第 55 行）

优先直接从内存读取 `meta.sha256`；若为空，则磁盘补算并**立即回写数据库持久化**：

```cpp
    // 只有大小相同时，才获取 SHA-256 哈希比对
    QString existSha = QString::fromStdString(meta.sha256).toLower();
    
    // 仅当老数据哈希确实为空时才做一次磁盘补救，并立即回写自愈
    if (existSha.isEmpty()) {
        QFile existFile(QString::fromStdWString(existPathW));
        if (existFile.open(QIODevice::ReadOnly)) {
            QCryptographicHash existHash(QCryptographicHash::Sha256);
            if (existHash.addData(&existFile)) {
                existSha = QString(existHash.result().toHex()).toLower();
                MetadataManager::instance().setSha256(existPathW, existSha.toStdString(), false);
            }
            existFile.close();
        }
    }
```

---

### 2.6 `DuplicateConflictDialog.cpp` 修改（UI 卡片现场保底渲染与尺寸矫正）

在 `createCard` 函数构建卡片时，增加即时兜底逻辑：若缩略图为空，现场调用磁盘渲染器生成预览图，并以真实宽高覆盖 `4180 x 4180` 硬编码假数据：

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

    // 1. 缩略图保底：若为空，现场调用 DiskMediaExtractor 实时渲染一张
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

    // 2. 真实尺寸保底：若 width/height <= 0，优先使用渲染图的真实宽高，消除 4180 假数据
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

## 三、 验证闭环

1. **新资产导入**：`AssetImporter` 写入第一秒哈希即落盘入库，查重比对为 **0ms 内存比对**。
2. **历史资产自愈**：后台流水线或查重服务扫到时一次性算好并存盘，下一次同样变为 **0ms 内存比对**。
3. **UI 弹窗展示**：告别右侧卡片黑屏与 `4180 x 4180` 假分辨率，呈现真实缩略图与精准分辨率。

---

> **文档状态**：方案已更新完毕并归档，待授权后续重构代码执行。
