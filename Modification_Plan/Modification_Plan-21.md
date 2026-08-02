# 高清 AI 预览流解析重构与防虚标默认图标注入拦截 —— Modification_Plan-21.md

> 状态：已批准，执行中 / 已执行完成

## 1. 任务背景
在拖入 `.ai`（Adobe Illustrator）等复杂矢量设计文件时，系统面临两个高频问题：一是缩略图面临“重启后才能生成”的延迟，二是重启后即便成功补救生成的缩略图也是 Windows 默认给出的“Ai 软件通用图标”（对应用户原话：“Adobe Bridge 显示的是这个 AI 文件画面内容本身的缩略图，而 ArcMeta 显示的只是一个通用的 'Ai' 软件图标，不是文件内容”）。

本方案旨在：
1. 重构 `.ai` 文件的内嵌预览解析逻辑，彻底打破此前硬编码读取前 5MB（`5 * 1024 * 1024`）数据的物理空间屏障，改用低内存开销、不一次性吞噬内存的高效文件分块流游标扫描搜寻，实现对兼容性 JPEG 数据位置的高清定位解析。
2. 彻底拦截并切断 `WindowsShellThumbnailProvider::getShellThumbnail` 对 `.ai`、`.psd`、`.eps` 等矢量设计文件通用大图标的伪预览兜底。如果内嵌真正解析器提取失败，绝对禁止在 `.arc` 资产包或缓存目录下生成虚假的通用软件图标 `.png`，而是标记为无缩略图并返回（在 `HasThumbnailRole` 中直接返回 `false` 走 `defaultIcon` 干净绘制，对应用户原话），保证全应用画面渲染的极致品质。

## 2. 问题定位
1. **硬编码 5MB 限制漏洞**：在 `MediaColorExtractor.cpp` 的 `extractEmbeddedAiPreview` 中：
   ```cpp
   QByteArray data = file.read(5 * 1024 * 1024);
   ```
   该行代码强制将文件读取范围锁死在前 5MB。由于很多大型 `.ai` 文件包含庞大的矢量数据和极其复杂的图层结构，真实的兼容性高清 JPEG 数据可能保存在文件靠后的物理偏移区间，导致当场解析失败。
2. **通用的“虚假图标”生成与写入**：
   在 `MediaColorExtractor.cpp` 的 `getImageForAnalysis` 中：
   ```cpp
   if (img.isNull()) {
       img = WindowsShellThumbnailProvider::getShellThumbnail(path, size);
       if (img.isNull()) img.load(path);
   }
   ```
   一旦内嵌解析失败，兜底路径会通过 Shell 获取系统默认缩略图。由于 Windows 原生默认不支持 `.ai` 内容直接渲染，该接口在一段时间或重启索引完成后会成功返回“Ai 软件图标”。批量管道（`MediaExtractorPipeline`）或导入过程会误将此“虚假图标”保存为 `_thumbnail.png`，造成视觉混淆。
3. **在 `ContentPanel.cpp` 中的二次加载与判断越权**：
   在 `ContentPanel.cpp` 的缩略图加载模块 `loadThumbnailsForRows` 内部，针对 `.ai` 没有在提取失败或缺失时进行 `m_aspectRatios` 或 `HasThumbnailRole` 上的明确过滤与快速失败，导致残留了系统大图标。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | Adobe Bridge 显示的是这个 AI 文件画面内容本身的缩略图，而 ArcMeta 显示的只是一个通用的 'Ai' 软件图标，不是文件内容 (对应用户原话) | 4.2 节：通过 getImageForAnalysis 排除 ai/psd/eps 文件直接获取软件大图标，拦截其兜底行为 | ✅ 一致 |
| 2    | 是"没找到 JPEG 起始标记"、还是"找到了但解码失败"、还是这行日志根本没出现过 (对应用户原话) | 4.1 节：在流扫描寻找过程中精确注入日志，以直接确认究竟是哪个环节导致的提取失败 | ✅ 一致 |

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 重构 `src/ui/MediaColorExtractor.cpp` 的流式 AI 内嵌 JPEG 预览提取逻辑
重写 `MediaColorExtractor::extractEmbeddedAiPreview`。不一次性将大文件调入内存，而是采用高效的分块游标流式扫描：

```cpp
QImage MediaColorExtractor::extractEmbeddedAiPreview(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "[MediaColorExtractor][AI] 文件打开失败：" << filePath;
        return QImage();
    }

    // 采用高效的流式分块搜寻游标，消除 5MB 的硬编码限制，支持无限大文件的内嵌预览检索
    const qint64 chunkSize = 1024 * 1024; // 1MB chunk size
    QByteArray buffer;
    qint64 startOffset = -1;

    while (!file.atEnd()) {
        qint64 currentChunkOffset = file.pos();
        QByteArray chunk = file.read(chunkSize);
        if (chunk.isEmpty()) break;

        // 如果之前有残留 buffer（重叠），拼接以防标记被分块截断
        if (!buffer.isEmpty()) {
            buffer = buffer.right(2) + chunk;
            currentChunkOffset -= 2;
        } else {
            buffer = chunk;
        }

        int idx = buffer.indexOf("\xFF\xD8\xFF");
        if (idx != -1) {
            startOffset = currentChunkOffset + idx;
            break;
        }
    }

    if (startOffset == -1) {
        qWarning() << "[MediaColorExtractor][AI] 未找到 JPEG 起始标记(FFD8FF)，该文件可能未内嵌兼容性预览：" << filePath;
        return QImage();
    }

    // 从 startOffset 开始寻找 \xFF\xD9 结束标记
    if (!file.seek(startOffset)) {
        qWarning() << "[MediaColorExtractor][AI] 重定向文件指针失败：" << filePath;
        return QImage();
    }

    QByteArray imgData;
    buffer.clear();
    bool foundEnd = false;

    while (!file.atEnd() && imgData.size() < 50 * 1024 * 1024) { // 安全上限：最多提取 50MB 的 JPEG 数据
        qint64 currentChunkOffset = file.pos();
        QByteArray chunk = file.read(chunkSize);
        if (chunk.isEmpty()) break;

        if (!buffer.isEmpty()) {
            buffer = buffer.right(1) + chunk;
            currentChunkOffset -= 1;
        } else {
            buffer = chunk;
        }

        int idx = buffer.indexOf("\xFF\xD9");
        if (idx != -1) {
            qint64 endOffset = currentChunkOffset + idx + 2;
            qint64 totalLen = endOffset - startOffset;
            if (totalLen > 0) {
                if (file.seek(startOffset)) {
                    imgData = file.read(totalLen);
                    foundEnd = true;
                }
            }
            break;
        }
    }

    file.close();

    if (!foundEnd || imgData.isEmpty()) {
        qWarning() << "[MediaColorExtractor][AI] 找到起始标记但未找到 JPEG 结束标记(FFD9)或数据不完整：" << filePath;
        return QImage();
    }

    QImage img;
    if (!img.loadFromData(imgData)) {
        qWarning() << "[MediaColorExtractor][AI] 已提取出 JPEG 字节流但解码失败，数据长度：" << imgData.size() << "：" << filePath;
        return QImage();
    }

    qDebug() << "[MediaColorExtractor][AI] 内嵌预览提取成功：" << filePath;
    return img;
}
```

### 4.2 拦截 `MediaColorExtractor::getImageForAnalysis` 中对设计文件的默认系统图标兜底行为
修改 `MediaColorExtractor::getImageForAnalysis`。针对 `psd`、`psb`、`ai`、`eps` 等设计类文件，如果在各自的物理提取器中解码失败，**100% 绝对拦截并切断其调用 Shell 默认图标的兜底行为**，防止生成虚假的“软件图标占位符”：

```cpp
    if (img.isNull()) {
        // 🚨 极致物理重构：针对 psd/psb/ai/eps 这四类矢量和设计层文件，若内嵌数据流解析失败，
        // 绝对禁止采用系统关联的软件大图标（如 Ai 大图标）进行虚假兜底！
        static const QStringList rawDesignExts = {"psd", "psb", "ai", "eps"};
        if (rawDesignExts.contains(ext)) {
            qWarning() << "[MediaColorExtractor][BLOCK] 已强制拦截该文件的系统默认图标兜底，标记为无真实内容预览：" << path;
            return QImage(); // 干净地返回空图
        }

        img = WindowsShellThumbnailProvider::getShellThumbnail(path, size);
        if (img.isNull()) img.load(path);
    }

    if (!img.isNull()) {
        img.save(cachePath, "PNG");
    }
    return img;
}
```

### 4.3 修正 `ContentPanel.cpp` 在提取失败时的 `m_aspectRatios` 宽高比缓存映射
在 `ContentPanel.cpp` 中的 `loadThumbnailsForRows` 里：
1. 针对 `.ai` 文件，若其内嵌提取失败且返回空图，必须在主线程中将对应的 `m_aspectRatios[path]` 赋值为 `-1.0`（而不是像现在这样跳过更新），从而彻底向界面及 Delegate 释出“没有真实预览”的致命信号，促使其干净地绘制文字卡片或图标。

```cpp
                    } else if (ext == "ai") {
                        // 纯 C++ 提取 .ai 文件中内嵌的高清 JPEG 预览图 (耗时仅 1~2ms，零依赖)
                        img = MediaColorExtractor::extractEmbeddedAiPreview(path);
                        if (!img.isNull()) {
                            ar = (double)img.width() / img.height();
                            hasThumb = true;
                        } else {
                            ar = -1.0; // 🚨 解析失败，强制标记为 -1.0 告知界面 Delegate 没有内容缩略图！
                            hasThumb = false;
                        }
                    } else if (UiHelper::isGraphicsFile(ext) && ext != "cur" && ext != "ico" && ext != "ani" && ext != "ai") {
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/ui/MediaColorExtractor.cpp`
  - 替换 `extractEmbeddedAiPreview`（改写流式查找）
  - 替换 `getImageForAnalysis`（拦截通用大图标兜底）
- [ ] 模块/文件：`src/ui/ContentPanel.cpp`
  - 替换 `loadThumbnailsForRows`（在 `.ai` 解析失败时赋予宽高比 `ar = -1.0` 显式信号）

**明确机制拦截不修改的文件：**
- [ ] 模块/文件：`WindowsShellThumbnailProvider.cpp` —— 不作任何物理改动，其底层的 SHCreateItemFromIDList 保持原样供非设计类格式正常读取，从调用上游直接进行设计文件的干净拦截。

## 6. 实现准则与预警【核心】
1. **防止无限循环和内存占用**：通过 `chunkSize = 1024 * 1024` 的 1MB 自适应缓冲与 2 字节重叠重连算法，既保证了搜索时不漏掉处于拼缝处的 `\xFF\xD8\xFF` 起始标记，又杜绝了超大型 AI 文件在读取时导致内存飙升崩溃。
2. **强制确认日志注入**：在寻找 JPEG 数据时，如果起始位置正确提取，日志一律输出 `[MediaColorExtractor][AI] 内嵌预览提取成功`；如果最终返回空图，则向终端抛出 `[MediaColorExtractor][BLOCK] 已强制拦截该文件的系统默认图标兜底...` 的警告，方便通过日志精确定位。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|--------------------------------------------|----------------|
| 输入框清除功能 | 一律使用 Qt 原生 `setClearButtonEnabled(true)`。 | ✅ 符合。本方案不触及输入框。 |
| 双轨标记落盘路由 | 托管库写入 SQLite 数据库，磁盘导航模式 100% 独立，写入离散缓存且不溢流本地库。 | ✅ 符合。本方案不改变任何落盘和隔离路由逻辑。 |

## 8. 待确认事项（可选）
- **无**。
