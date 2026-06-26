# “空格键”快速预览功能优化与回归修复 —— Analysis_Modification_Plan-109.md

## 1. 任务背景
用户反馈“空格键”预览功能存在多项视觉与逻辑缺陷：图片显示有锯齿（对应用户原话：“图片是高清的，但是预览出来的结果却含有锯齿”）、滚动条样式不符合规范（对应用户原话：“滚动条也被另起灶炉，完全没有按照Memories.md里的标准规范来实现”）、且缺乏对文件夹及不可预览属性的过滤（对应用户原话：“如果是这些不该被预览的属性，则不该打开预览窗口”）。此外，最新版本出现了 PNG 及 EPS 格式预览失效的严重回归问题（对应用户原话：“EPS完全无法预览了... PNG也没有结果”）。

## 2. 问题定位
1.  **图片锯齿 (Aliasing)**：虽然 `QuickLookWindow` 开启了 `SmoothPixmapTransform`，但在初次加载或窗口全屏化触发 `fitInView` 时，缩放比例的计算可能未能在高清 DPI 下正确映射，导致插值渲染出现锯齿。
2.  **非标滚动条**：`QuickLookWindow.cpp` 中的 QPlainTextEdit 滚动条样式采用硬编码 hex 值，未引用 `StyleLibrary.h` 中的 `BorderColor` (#333333) 常量，且圆角与宽度参数与 `Memories.md` 第 10 条规范不符。
3.  **属性过滤缺失**：`ContentPanel.cpp` 中的 `Space` 键拦截逻辑目前对文件夹及二进制文件拦截不彻底，导致不该预览的项触发了空白预览窗。
4.  **PNG/EPS 预览回归**：
    -   **PNG**：`renderImage` 逻辑中可能因为路径编码或后缀匹配精度问题导致 `QPixmap` 加载失败。
    -   **EPS**：`renderProfessionalImage` 依赖 `UiHelper::getShellThumbnail`，若 Windows Shell 引擎因某种原因（如缓存未命中或并发限制）返回空位图，代码缺乏降级加载机制，导致黑屏。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 图片是高清的，但是预览出来的结果却含有锯齿 | 强制开启 `SmoothPixmapTransform` 并优化 `fitInView` 后的采样 | ✅ |
| 2    | 滚动条也被另起灶炉，完全没有按照Memories.md里的标准规范来实现 | 将滚动条 QSS 重构为引用 `StyleLibrary` 常量，对齐 10px 宽度规范 | ✅ |
| 3    | 如果是这些不该被预览的属性，则不该打开预览窗口 | 在 `ContentPanel` 增加黑名单拦截，严禁文件夹及 .exe 等触发预览 | ✅ |
| 4    | EPS完全无法预览了... PNG也没有结果 | 修复 `standardImages` 匹配逻辑，并为专业格式增加 `renderImage` 降级逻辑 | ✅ |

## 4. 详细解决方案

### 4.1 QuickLookWindow 渲染链路加固 (修复 PNG/EPS)
- **PNG 修复**：在 `previewFile` 中，确保后缀提取使用 `QFileInfo::completeSuffix().toLower()`，以处理带点路径的异常。
- **EPS 降级策略**：修改 `renderProfessionalImage`，若 `getShellThumbnail` 返回空，则尝试调用 `renderImage`。虽然 Qt 原生不支持 EPS，但对于已转换或某些特殊环境下的专业格式，应保证有一次文件流读取尝试，而非直接中断。
- **抗锯齿优化**：在 `renderImage` 成功加载后，显式调用 `m_graphicsView->viewport()->update()`，并确保 `m_graphicsView` 的 `SmoothPixmapTransform` 渲染提示在 `fitInView` 执行后依然生效。

### 4.2 滚动条样式对齐 (对齐 Memories.md)
- 废除 `initUi` 中硬编码的样式字符串。
- 使用 `Style::qssColor(Style::BorderColor)` (#333333) 作为滚动条 Handle 的默认背景。
- 使用 `Style::qssColor(Style::BorderDark)` (#444444) 作为悬停态背景。
- 强制设置 `width: 10px` 和 `border-radius: 3px`（对应 `Memories.md` 第 10 条）。

### 4.3 ContentPanel 拦截逻辑完善 (拦截非预览项)
- 在 `eventFilter` 拦截 `Qt::Key_Space` 时，先行判断 `info.isDir()`，若是则直接 `return true` 拦截。
- 引入严苛的黑名单 `blackList`：包含 `exe, dll, sys, zip, rar, 7z, iso, msi`。
- 引入白名单 `whiteList`：仅允许图像类、专业设计类（PSD/AI/EPS/SVG）及文本类文件触发 `requestQuickLook`。

## 5. 修改边界声明【红线】

**本次方案涉及范围：**
- [ ] `src/ui/QuickLookWindow.cpp`: 渲染逻辑、UI 初始化样式、预览分流逻辑。
- [ ] `src/ui/ContentPanel.cpp`: 空格键事件拦截、后缀名黑白名单判断。

**明确禁止越界修改的范围：**
- [ ] 禁止修改 `UiHelper::getShellThumbnail` 的底层 Shell API 实现（Plan-28 核心逻辑）。
- [ ] 禁止修改 `StyleLibrary.h` 中的颜色常量。
- [ ] 禁止修改内容面板的拖拽或排序逻辑。

## 6. 实现准则与预警【核心】
1. **头文件依赖**：`QuickLookWindow.cpp` 必须包含 `#include "StyleLibrary.h"` 以获取颜色常量。
2. **QSS 注入安全**：在构建 QSS 字符串时，必须使用 `Style::qssColor()` 包装，防止因 `#` 缺失导致的渲染失败。
3. **性能预警**：`standardImages` 判定必须保持 `static const QSet` 形式，避免在每次空格触发时重新构造集合。
4. **上下文一致性**：方案中引用的 `renderImage` 逻辑必须包含之前 Plan-109 引入的 > 50MB 自动降级逻辑。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|-------------|----------------------|----------------|
| 快速预览 (QuickLook) | 严禁预览文件夹、安装包、系统库及压缩包 | ✅ (通过 ContentPanel 黑名单实现) |
| 快速预览 (QuickLook) | 加载全分辨率原图，启用 SmoothPixmapTransform | ✅ (renderImage 核心逻辑) |
| 滚动条样式 | 宽度 10px、圆角 3px、背景透明、Handle 对齐 #333333 | ✅ (QSS 重构) |
| “清除”按钮 | 一律使用 Qt 原生 setClearButtonEnabled(true) | ✅ (无改动，保持合规) |

## 8. 待确认事项（可选）
- 用户提到的“图片锯齿”在 `Facebook` 截图（PixPin_2026-06-26_14-42-24.png）中非常明显，这通常是因为原始图像分辨率远高于显示区域，导致了摩尔纹或插值劣化。方案将尝试通过开启 `QPainter::HighQualityAntialiasing` (若 Qt 版本支持) 进一步加固。
