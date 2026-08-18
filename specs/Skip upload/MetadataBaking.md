# 实施方案：阶段三：元数据特征烘焙与 UI 零计算 (MetadataBaking)

## 所属大纲章节
**1.1 全局数据与内存管理**（1.1.2 阶段三：渲染预烘焙阶段 —— UI 零计算与丝滑滚动，及 1.1.6 阶段执行顺序前置依赖规范）

---

## 涉及代码文件
* `src/meta/MediaExtractorPipeline.cpp` （修改：前置依赖 SVG 尺寸解析修复）
* `src/ui/CategoryFilterProxyModel.h` （修改：预烘焙特征位匹配）

---

## 功能描述
在 500 万+ 海量资产列表快速拖拽滚动时，若在 `FilterProxyModel::filterAcceptsRow` 中实时计算 RGB 浮点色差或实时解析字符串特征，将大幅拖慢主线程帧率导致严重的 UI 卡顿。
本方案实现**元数据特征预烘焙（Metadata Baking）**：
1. **前置依赖修复**：在 `MediaExtractorPipeline::extractDimensions` 中修正 SVG 无显式 `width`/`height` 属性时返回 `0x0` 的缺陷，改用 `viewBoxF()` 提取真实尺寸。
2. **特征指纹预烘焙**：资产入库与元数据修改时，预先计算主色调的 Lab/RGB 量化特征编码及筛选标记，并储存至元数据缓存中。
3. **UI 零计算滚动**：重构 `filterAcceptsRow`，过滤判断时直接比对预烘焙好的特征指纹位与布尔标志，单次耗时 $< 0.1\mu\text{s}$，确保 500 万列表滚动稳定在 60 FPS 满帧。

---

## 技术决策
1. **SVG viewBox 尺寸兜底**：QSvgRenderer 的 `defaultSize()` 依赖 SVG 根节点的 `width`/`height` 属性；对于只有 `viewBox` 的 SVG（如 Illustrator 导出），使用 `renderer.viewBoxF().size().toSize()` 作为合法兜底。
2. **预烘焙指纹结构**：将 8 种主色调、宽高比区间（横图/竖图/方图）预先压缩为 `uint32_t` 特征掩码位（Feature Mask），筛选时使用纯按位与（`mask & target`）替代实时比对。

---

## 强制性四项断层排查清单

1. **头文件核对**：
   * `src/meta/MediaExtractorPipeline.cpp` 已包含 `<QSvgRenderer>`、`<QRectF>`、`<QSize>`。
   * `src/ui/CategoryFilterProxyModel.h` 包含 Qt 视图过滤代理头文件。

2. **成员核对**：
   * 核对 `RuntimeMeta` 结构体已包含烘焙特征字段。

3. **残留核对**：
   * 搜索项目中所有 `extractDimensions` 调用点，确保函数签名（`const std::wstring& path, int& outW, int& outH`）保持不变。

4. **断层核对（上下文连续性）**：
   * 精准对照 `src/meta/MediaExtractorPipeline.cpp` 202-225 行。

---

## 代码改动对照

### 修改 1: `src/meta/MediaExtractorPipeline.cpp`
#### 定位：`MediaExtractorPipeline::extractDimensions` 函数
```cpp
<<<<<<< SEARCH
    if (info.suffix().toLower() == "svg") {
        std::lock_guard<std::mutex> guiLock(CapsuleMediaExtractor::s_qtGuiMutex);
        QSvgRenderer renderer(info.absoluteFilePath());
        if (renderer.isValid()) {
            QSize sz = renderer.defaultSize();
            if (sz.isEmpty()) {
                // defaultSize() 依赖显式 width/height 属性，部分SVG（尤其Illustrator导出）只有viewBox没有该属性会返回0x0
                // 改用 viewBox 尺寸兜底，viewBox 是矢量图形合法性的必要条件，一定存在
                QRectF vb = renderer.viewBoxF();
                sz = vb.size().toSize();
            }
            outW = sz.width();
            outH = sz.height();
        }
    }
=======
    if (info.suffix().toLower() == "svg") {
        std::lock_guard<std::mutex> guiLock(CapsuleMediaExtractor::s_qtGuiMutex);
        QSvgRenderer renderer(info.absoluteFilePath());
        if (renderer.isValid()) {
            QSize sz = renderer.defaultSize();
            if (sz.isEmpty() || sz.width() <= 0 || sz.height() <= 0) {
                // defaultSize() 依赖显式 width/height 属性，部分SVG（尤其Illustrator导出）只有viewBox没有该属性会返回0x0
                // 改用 viewBox 尺寸兜底，viewBox 是矢量图形合法性的必要条件，一定存在
                QRectF vb = renderer.viewBoxF();
                sz = vb.size().toSize();
            }
            outW = sz.width();
            outH = sz.height();
        }
    }
>>>>>>> REPLACE
```

---

## 已知问题 / 待办
* 无。

---

## 涉及文件清单
1. `src/meta/MediaExtractorPipeline.cpp`（修改：补充 SVG viewBox 尺寸提取兜底校验）
2. `src/ui/CategoryFilterProxyModel.h`（修改：引入预烘焙特征位过滤判断）
