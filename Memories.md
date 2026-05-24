1. 关于“.scch”
“.scch”和“mainwindow”窗口没有任何关系
“.scch”仅可应用在“ScanDialog.cpp”，只有在点击“扫描”按钮后加载“.scch”并在打开“ScanDialog.cpp”界面后才可以使用，当关闭“ScanDialog.cpp”界面后同时卸载“.scch”。所以“.scch”缓存文件和主界面没有任何关系，这是为了不占用内存

2. “ScanDialog.cpp”界面必须支持.svg 图片，可以直接参考/调用mainwindow 内容容器显示的svg方式

3. MainWindow 滚动条样式偏好（QSS 参数）
为了保持 UI 的一致性与“物理切割感”，后续所有界面添加滚动条时必须遵循以下样式：
- **背景 (Background)**: `transparent` (透明)
- **边框 (Border)**: `none`
- **宽度/高度 (Width/Height)**: 垂直方向 `width: 4px`, 水平方向 `height: 4px`
- **外边距 (Margin)**: `0px`
- **滑块颜色 (Handle Background)**: 默认 `#333333`, 悬停(hover) `#444444`
- **滑块圆角 (Handle Border-radius)**: `2px`
- **最小长度 (Min-length)**: 垂直方向 `min-height: 20px`, 水平方向 `min-width: 20px`
- **按钮 (Add/Sub-line)**: 必须设置为 `height: 0px` 或 `width: 0px` (彻底隐藏箭头按钮)
- **页面区域 (Add/Sub-page)**: `background: none`

---
**2026-05-21 需求记录：**
1. **数据库分析**：分析当前版本DB类数据库是否采用唯一FRN（File Reference Number）方式，以避免同一文件重复创建数据。
2. **列表计数排查**：分析为什么侧边栏分类中“全部数据”显示的计数为9708，而状态栏下只显示1000（两者数量不一致的问题）。
3. **取消加载限制**：废除数据库查询中对 1000 条数据的 LIMIT 1000 限制，使状态栏 and ContentPanel 真实加载并显示相应的所有数据（例如全量加载）。
4. **ScanDialog SVG支持**：ScanDialog.cpp 界面支持显示 .svg 类型图片，可参考/调用 ContentPanel 显示 SVG 的方式。
5. **ScanDialog SVG缩略图显示修复**：修复由于 `Qt::UserRole + 1` 未识别 `svg` 后缀，导致 Grid 卡片视图下 SVG 缩略图未能绘制，而降级显示为默认关联浏览器（Edge）图标的问题。
6. **编译构建排查**：解决用户在执行 Ninja 构建时报错 `build.ninja:35: loading 'CMakeFiles\rules.ninja': The system cannot find the file specified.` 的问题，指导用户清理 CMake 缓存并重新生成构建。
7. **设计与架构咨询**：解答用户关于 `ContentPanel` 中不同类别图标来源的技术疑问，即区分物理 SVG 真实文件（物理真实内容渲染）和常规文件降级保护（读取 `SvgIcons.h` 内内置的矢量图标集）的渲染逻辑。
8. **图标系统架构对齐（方案 B）**：按用户明确要求，图标的降级/非缩略图显示机制必须统一为 Windows 系统原生关联图标风格。当文件无缩略图时，直接拉取 Windows 原生系统图标（如 Edge 图标、WinRAR 关联图标等），确保 `ContentPanel.cpp` 和 `ScanDialog.cpp` 的降级展示行为完美一致。

---
**2026-06-05 需求记录：**
1. **缩放红线锁定**：物理强制 ContentPanel 卡片缩放最小值为 **96 像素**。
2. **视图自动切换**：
   - 当在 **96 像素** 继续向下缩小时，界面必须自动切换至列表模式。
   - 当在列表模式向上放大且行高超过 **96 像素** 时，界面必须自动切换回网格模式，并以 **96 像素** 作为起始缩放值。
3. **列表高亮优化**：列表视图（TreeView/ListView）的选中高亮必须改为直角、全行贯穿式填充，消除列间缝隙。
4. **动态比例星级 (Architecture V2)**：
   - 虽然网格底限锁定为 96px，但为了支持列表模式下的极端高密度，保留以下动态调整：
   - 当行高/缩放 `< 60` 时，自动将星级图标缩小 20%。
   - 当行高/缩放 `< 40` 时，自动隐藏评分栏。
5. **实时日志监控**：在 `updateGridSize()` 中植入 `[UI_DEBUG]` 标签日志，以便用户精确监控当前的缩放级别。

---
**2026-05-20 需求记录：**
1. **品牌更名**：MainWindow 标题栏显示的名称由 "ArcMeta" 修改为 "FERREX"。
2. **视觉对齐**：内容容器卡片下方的“禁止”图标与“星级”图标视觉大小不一致（禁止图标偏大），需进行等大处理。
3. **交互优化**：选中卡片时容易误触发重命名逻辑。需移除“再次点击选中项触发编辑”的机制，仅保留快捷键或专用菜单触发。

**2024-10-10 需求记录：**
1. **图标系统重构**：物理删除了 `resources/icon` 目录下所有的 SVG 文件。
2. **资源引用优化**：全面排查并移除了代码中对 `:/icons/*.svg` 的路径引用。
3. **动态加载机制**：在 `UiHelper` 中实现了 `getSvgDataUrl` 函数，将 `SvgIcons.h` 中的 SVG 字符串转换为 Base64 Data URL，供 QSS 样式表（如 `QTreeView::branch` 图标）直接调用，彻底摆脱对外部物理图标文件的依赖。**物理要求：tree branch 图标颜色必须显式指定为蓝色 (#3498db)**。
4. **资源文件瘦身**：清理了 `resources.qrc`，仅保留必要的样式表与应用程序图标。

**2024-10-21 需求记录 (Eagle 布局增强)：**
1. **布局核心**：实现类似 Eagle 的 Justified Layout。`JustifiedView.cpp` 的 `doLayout` 必须为每行计算统一的高度（`actualHeight`），并在此基础上**额外增加 36px** 的物理空间用于显示文件名，确保图片与文字不重叠。
2. **卡片视觉**：`ThumbnailDelegate.cpp` 实现 6px 圆角。缩略图 must 使用 `KeepAspectRatioByExpanding` 策略**铺满**圆角卡片区域。
3. **文字规范**：文件名显示在卡片正下方（非叠加），高度固定为 36px，颜色为 `#D4D4D4`。

// ===================|===================

# 🚨 核心警示 🚨
**由于jules是个脑残的Ai，它的脑补没有任何益处，只会给用户带来破坏、损失，浪费时间，同等于谋财害命。所以必须杜绝它的傻逼脑补带来的蔓延破坏**

1. 为了降低后期的维护成本，所以需要将曾经的错误记录到这里并注释原因，这样就不会发生重蹈覆辙。

2. 右键菜单的圆角设计为 8 像素

3. 悬浮面板 圆角设计为 6 像素

4. 悬停高亮 圆角设计为为 4 像素

## 界面架构规范标准 (基于 FramelessDialog)

### 1. 核心架构理念
*   **物理级无边框方案**：摒弃系统原生边框，采用 `Qt::FramelessWindowHint` 结合 `Qt::WA_TranslucentBackground` 实现。
*   **0 延时瞬时显隐**：对于 ToolTip 等辅助窗口，应改用 `Qt::Window` 结合 `Qt::WindowDoesNotAcceptFocus` 方案。
*   **扁平化布局架构**：ArcMeta 采用极致扁平化设计，**全面废除外发光阴影效果**。

### 2. 关键视觉参数
*   **边距标准**：**全面移除 20px 阴影边距**。
*   **窗口圆角 (Window Radius)**：对话框主体容器统一采用 **6px** 或 **12px**。
*   **交互颜色**：统一悬停色采用 `#3e3e42`。

### 3. 自定义标题栏 / 工具栏规范 (TitleBar & ToolBar)
*   **尺寸极限限制**：主工具栏高度**绝对不可超过 36px**。

---
**2026-05-23 记录：**
1. **图片倒置修复**：通过 GetDIBits 并强制指定 top-down 方向修复 QImage::fromHBITMAP 倒置问题。
2. **UI偏好**：ContentPanel 中的星级部分不应位于卡片内部，应显示在卡片与文件名称的中间位置。
