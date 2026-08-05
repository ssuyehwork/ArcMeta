# AI 提图极速重构与无脏图净化 —— Modification_Plan-37.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在对多媒体提图模块的审计中，发现 AI (.ai) 格式的缩略图提取在 `MediaColorExtractor::extractEmbeddedAiPreview` 中存在重大的性能缺陷与安全漏洞：
1. **硬载入 15MB 导致的严重 I/O 阻塞**：不管文件实际大小和内嵌结构在多深的位置，一律无脑首行分配并强读 15MB 裸数据。当高频切换选中或滚动列表时，磁盘队列爆满、引发主线程或提图线程的严重假死卡顿（对应用户理解：“硬读取 15MB 导致的严重 I/O 阻塞”）。
2. **暴力幻数定位（通道5）导致的脏图截胡风险**：通道 5 采用裸字节内存查找 `\x89PNG` 与 `\xFF\xD8\xFF`。若 AI 中置入了任意位图，则系统会错误地把用户画板内部的设计素材图误判为本 AI 项目的整体预览，引发货不对板（对应用户理解：“暴力物理检索（通道5）存在极高概率解析脏图风险”）。

本方案旨在针对 AI 提图逻辑实施深度优化，彻底清退霸道加载与脏通道，引入先进的**渐进式分段流式读取（Sliding Header Buffering）**。

## 2. 问题定位
- 关键函数：`src/ui/MediaColorExtractor.cpp` 内的 `MediaColorExtractor::extractEmbeddedAiPreview`。
- 修改逻辑：
  1. 引入局部渐进读取逻辑：
     - 首次调用 `file.read(1 * 1024 * 1024)` 精准加载前 **1MB** 数据。
     - 在 1MB 范围内进行通道 1（%AI_Thumbnail）与通道 2（XMP Base64 标签）的检索。若成功命中，立即解析并返回。
     - 若 1MB 未命中，且文件总长度确实大于 1MB，则进行增量向后追加读取（最深读取至 **6MB**），再次尝试定位通道 1 & 2。
  2. 彻底、物理性地删除 **通道 5**（即暴力 `\x89PNG` 与 `\xFF\xD8\xFF` 幻数匹配），杜绝解析出 AI 画板内部置入材料的错误。
  3. 通道 3 (Ghostscript) 与通道 4 (Windows PDF) 以及通道 6 (Shell 兜底) 保持完好运行。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 直接一举读取文件前 15MB 的裸数据载入内存 | 改为渐进分段加载，首次仅读取 1MB，必要时增量增补至 6MB。 | ✅ 一致 |
| 2    | 直接物理暴力搜寻 \x89PNG 或 \xFF\xD8\xFF | 彻底物理性删除此暴力搜寻脏通道（通道5），完全杜绝偷梁换柱风险。 | ✅ 一致 |

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 修改 `src/ui/MediaColorExtractor.cpp` 实现渐进分段读取并清退脏通道
```merge_diff
<<<<<<< SEARCH
QImage MediaColorExtractor::extractEmbeddedAiPreview(const QString& filePath, int targetSize) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return QImage();

    QByteArray rawData = file.read(15 * 1024 * 1024);
    file.close();

    if (rawData.isEmpty()) return QImage();

    // =========================================================================
    // 通道 1：解析 PostScript %AI7_Thumbnail ~ %AI10_Thumbnail 256色索引调色板
    // =========================================================================
    int thumbHeaderIdx = rawData.indexOf("%AI7_Thumbnail:");
    if (thumbHeaderIdx == -1) thumbHeaderIdx = rawData.indexOf("%AI8_Thumbnail:");
    if (thumbHeaderIdx == -1) thumbHeaderIdx = rawData.indexOf("%AI9_Thumbnail:");
    if (thumbHeaderIdx == -1) thumbHeaderIdx = rawData.indexOf("%AI10_Thumbnail:");

    if (thumbHeaderIdx != -1) {
        int lineEnd = rawData.indexOf('\n', thumbHeaderIdx);
        if (lineEnd != -1) {
            QByteArray headerLine = rawData.mid(thumbHeaderIdx, lineEnd - thumbHeaderIdx);
            QString headerStr = QString::fromLatin1(headerLine);
            QStringList parts = headerStr.section(':', 1).trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            if (parts.size() >= 2) {
                int width = parts[0].toInt();
                int height = parts[1].toInt();

                int blockEnd = rawData.indexOf("%AI7_ThumbnailEnd", lineEnd);
                if (blockEnd == -1) blockEnd = rawData.indexOf("%AI9_ThumbnailEnd", lineEnd);
                if (blockEnd == -1) blockEnd = rawData.indexOf("%%EndData", lineEnd);

                if (blockEnd != -1 && width > 0 && height > 0) {
                    QByteArray hexData;
                    hexData.reserve(blockEnd - lineEnd);
                    
                    int cur = lineEnd;
                    while (cur < blockEnd) {
                        int nextLine = rawData.indexOf('\n', cur);
                        if (nextLine == -1) break;
                        QByteArray line = rawData.mid(cur, nextLine - cur).trimmed();
                        if (line.startsWith("%")) {
                            hexData.append(line.mid(1).trimmed());
                        }
                        cur = nextLine + 1;
                    }

                    QByteArray binaryData = QByteArray::fromHex(hexData);
                    const int paletteSize = 256 * 3;

                    if (binaryData.size() >= paletteSize + width * height) {
                        QList<QRgb> colorTable;
                        colorTable.reserve(256);
                        const uchar* palPtr = reinterpret_cast<const uchar*>(binaryData.constData());
                        
                        for (int i = 0; i < 256; ++i) {
                            // 🚨 核心修复：按 (B, G, R) 顺序解析 PostScript 调色板，防止红变蓝！
                            colorTable.append(qRgb(palPtr[i * 3 + 2], palPtr[i * 3 + 1], palPtr[i * 3]));
                        }

                        QImage img(width, height, QImage::Format_Indexed8);
                        img.setColorTable(colorTable);
                        const uchar* pixelPtr = palPtr + paletteSize;
                        for (int y = 0; y < height; ++y) {
                            memcpy(img.scanLine(y), pixelPtr + y * width, width);
                        }
                        if (!img.isNull()) {
                            qDebug() << "[MediaColorExtractor][AI] 二进制游标提取 %AI_Thumbnail 成功：" << filePath;
                            return img.convertToFormat(QImage::Format_ARGB32);
                        }
                    }
                }
            }
        }
    }

    // =========================================================================
    // 通道 2：解析 Adobe XMP 元数据中的 Base64 预览图 (<xmpGImg:image>)
    // =========================================================================
    int xmpStart = rawData.indexOf("<xmpGImg:image>");
    if (xmpStart != -1) {
        xmpStart += 15;
        int xmpEnd = rawData.indexOf("</xmpGImg:image>", xmpStart);
        if (xmpEnd != -1) {
            QByteArray base64Data = rawData.mid(xmpStart, xmpEnd - xmpStart).trimmed();
            base64Data.replace("\n", "").replace("\r", "").replace(" ", "");
            QByteArray jpgBytes = QByteArray::fromBase64(base64Data);
            QImage img;
            if (img.loadFromData(jpgBytes)) {
                qDebug() << "[MediaColorExtractor][AI] 成功解包 Adobe XMP 内嵌 Base64 预览图：" << filePath;
                return img;
            }
        }
    }

    // 通道 3：Ghostscript 矢量引擎
    QImage gsImg = renderWithGhostscript(filePath, targetSize);
    if (!gsImg.isNull()) {
        return gsImg;
    }

    // 通道 4：Windows 原生系统 PDF 引擎
    QImage pdfRenderImg = renderPdfAiFirstPage(filePath, targetSize);
    if (!pdfRenderImg.isNull()) {
        return pdfRenderImg;
    }

    // =========================================================================
    // 通道 5：检索 PDF 规范下的 JPEG / PNG 裸数据流 (\xFF\xD8\xFF)
    // =========================================================================
    int pngStart = rawData.indexOf("\x89PNG\r\n\x1a\n");
    if (pngStart != -1) {
        int pngEnd = rawData.indexOf("IEND", pngStart);
        if (pngEnd != -1) {
            QByteArray pngData = rawData.mid(pngStart, (pngEnd + 8) - pngStart);
            QImage img;
            if (img.loadFromData(pngData, "PNG") && img.width() >= 32) {
                qDebug() << "[MediaColorExtractor][AI] 成功提取 PNG 裸数据流：" << filePath;
                return img;
            }
        }
    }

    int jpgStart = rawData.indexOf("\xFF\xD8\xFF");
    if (jpgStart != -1) {
        int jpgEnd = rawData.indexOf("\xFF\xD9", jpgStart);
        if (jpgEnd != -1) {
            QByteArray jpgData = rawData.mid(jpgStart, (jpgEnd + 2) - jpgStart);
            QImage img;
            if (img.loadFromData(jpgData, "JPEG") && img.width() >= 32) {
                qDebug() << "[MediaColorExtractor][AI] 成功提取 JPEG 裸数据流：" << filePath;
                return img;
            }
        }
    }

    // 通道 6：Windows Shell 严格缩略图兜底
    return WindowsShellThumbnailProvider::getShellThumbnail(filePath, targetSize);
}
=======
QImage MediaColorExtractor::extractEmbeddedAiPreview(const QString& filePath, int targetSize) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return QImage();

    // 🚨 1. 极致优化：渐进式分段读取（Sliding Header Buffering），首阶段精准读取前 1MB 
    QByteArray rawData = file.read(1 * 1024 * 1024);
    qint64 totalSize = file.size();

    auto tryExtractOfficialPreview = [&](const QByteArray& data) -> QImage {
        if (data.isEmpty()) return QImage();

        // =========================================================================
        // 通道 1：解析 PostScript %AI7_Thumbnail ~ %AI10_Thumbnail 256色索引调色板
        // =========================================================================
        int thumbHeaderIdx = data.indexOf("%AI7_Thumbnail:");
        if (thumbHeaderIdx == -1) thumbHeaderIdx = data.indexOf("%AI8_Thumbnail:");
        if (thumbHeaderIdx == -1) thumbHeaderIdx = data.indexOf("%AI9_Thumbnail:");
        if (thumbHeaderIdx == -1) thumbHeaderIdx = data.indexOf("%AI10_Thumbnail:");

        if (thumbHeaderIdx != -1) {
            int lineEnd = data.indexOf('\n', thumbHeaderIdx);
            if (lineEnd != -1) {
                QByteArray headerLine = data.mid(thumbHeaderIdx, lineEnd - thumbHeaderIdx);
                QString headerStr = QString::fromLatin1(headerLine);
                QStringList parts = headerStr.section(':', 1).trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                if (parts.size() >= 2) {
                    int width = parts[0].toInt();
                    int height = parts[1].toInt();

                    int blockEnd = data.indexOf("%AI7_ThumbnailEnd", lineEnd);
                    if (blockEnd == -1) blockEnd = data.indexOf("%AI9_ThumbnailEnd", lineEnd);
                    if (blockEnd == -1) blockEnd = data.indexOf("%%EndData", lineEnd);

                    if (blockEnd != -1 && width > 0 && height > 0) {
                        QByteArray hexData;
                        hexData.reserve(blockEnd - lineEnd);
                        
                        int cur = lineEnd;
                        while (cur < blockEnd) {
                            int nextLine = data.indexOf('\n', cur);
                            if (nextLine == -1) break;
                            QByteArray line = data.mid(cur, nextLine - cur).trimmed();
                            if (line.startsWith("%")) {
                                hexData.append(line.mid(1).trimmed());
                            }
                            cur = nextLine + 1;
                        }

                        QByteArray binaryData = QByteArray::fromHex(hexData);
                        const int paletteSize = 256 * 3;

                        if (binaryData.size() >= paletteSize + width * height) {
                            QList<QRgb> colorTable;
                            colorTable.reserve(256);
                            const uchar* palPtr = reinterpret_cast<const uchar*>(binaryData.constData());
                            
                            for (int i = 0; i < 256; ++i) {
                                colorTable.append(qRgb(palPtr[i * 3 + 2], palPtr[i * 3 + 1], palPtr[i * 3]));
                            }

                            QImage img(width, height, QImage::Format_Indexed8);
                            img.setColorTable(colorTable);
                            const uchar* pixelPtr = palPtr + paletteSize;
                            for (int y = 0; y < height; ++y) {
                                memcpy(img.scanLine(y), pixelPtr + y * width, width);
                            }
                            if (!img.isNull()) {
                                qDebug() << "[MediaColorExtractor][AI] 二进制游标提取 %AI_Thumbnail 成功：" << filePath;
                                return img.convertToFormat(QImage::Format_ARGB32);
                            }
                        }
                    }
                }
            }
        }

        // =========================================================================
        // 通道 2：解析 Adobe XMP 元数据中的 Base64 预览图 (<xmpGImg:image>)
        // =========================================================================
        int xmpStart = data.indexOf("<xmpGImg:image>");
        if (xmpStart != -1) {
            xmpStart += 15;
            int xmpEnd = data.indexOf("</xmpGImg:image>", xmpStart);
            if (xmpEnd != -1) {
                QByteArray base64Data = data.mid(xmpStart, xmpEnd - xmpStart).trimmed();
                base64Data.replace("\n", "").replace("\r", "").replace(" ", "");
                QByteArray jpgBytes = QByteArray::fromBase64(base64Data);
                QImage img;
                if (img.loadFromData(jpgBytes)) {
                    qDebug() << "[MediaColorExtractor][AI] 成功解包 Adobe XMP 内嵌 Base64 预览图：" << filePath;
                    return img;
                }
            }
        }
        return QImage();
    };

    // 🚨 2. 第一阶段探测（1MB）
    QImage officialImg = tryExtractOfficialPreview(rawData);
    if (!officialImg.isNull()) {
        file.close();
        return officialImg;
    }

    // 🚨 3. 若 1MB 未命中且文件较大，则增量追加读取至 6MB，作第二阶段探测
    if (totalSize > 1 * 1024 * 1024) {
        qint64 targetReadSize = qMin(totalSize, static_cast<qint64>(6 * 1024 * 1024));
        qint64 remaining = targetReadSize - rawData.size();
        if (remaining > 0) {
            rawData.append(file.read(remaining));
            officialImg = tryExtractOfficialPreview(rawData);
            if (!officialImg.isNull()) {
                file.close();
                return officialImg;
            }
        }
    }
    file.close();

    // =========================================================================
    // 正轨 B：矢量级渲染与系统接口（Ghostscript / 原生 Windows PDF 引擎 / Shell 兜底）
    // 🚨 100% 物理性清退通道 5（暴力搜寻裸数据流），彻底杜绝偷梁换柱！
    // =========================================================================

    // 通道 3：Ghostscript 矢量引擎
    QImage gsImg = renderWithGhostscript(filePath, targetSize);
    if (!gsImg.isNull()) {
        return gsImg;
    }

    // 通道 4：Windows 原生系统 PDF 引擎
    QImage pdfRenderImg = renderPdfAiFirstPage(filePath, targetSize);
    if (!pdfRenderImg.isNull()) {
        return pdfRenderImg;
    }

    // 通道 6：Windows Shell 严格缩略图兜底
    return WindowsShellThumbnailProvider::getShellThumbnail(filePath, targetSize);
}
>>>>>>> REPLACE
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/file: `src/ui/MediaColorExtractor.cpp` —— 修改 `MediaColorExtractor::extractEmbeddedAiPreview` 以执行渐进分段探测并彻底移除暴力检索脏图逻辑。

**明确禁止越界修改的范围：**
- [ ] 其他格式（PSD、EPS、SVG 等）的生成核心算法 —— 不修改。

## 6. 实现准则与预警【核心】
1. **零脏图原则**：方案 100% 物理删除了对 JPEG、PNG 幻数的暴力搜索分支，AI 预览图绝对保持画板排版的高内聚一致性。
2. **I/O 轻量分级**：探测第一档限幅为 1MB，第二档最终增补至 6MB。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 双轨隔离 | 托管库模式和普通磁盘导航模式独立运行，互不交叉。 | ✅ 符合（本性能优化仅针对公共提图底层，不干涉业务路由） |

## 8. 待确认事项（可选）
无。
