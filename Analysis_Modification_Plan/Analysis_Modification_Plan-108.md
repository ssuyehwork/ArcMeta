# 缩略图加载视觉抖动治理 —— Analysis_Modification_Plan-108.md

## 1. 任务背景
在内容面板加载图形文件时，单元格会先显示系统图标，后台加载完缩略图后再替换，产生了明显的“图标 -> 缩略图”（对应用户原话：“两阶段切换”）视觉抖动。本方案旨在消除该抖动，实现占位符到缩略图的平滑过渡。

## 2. 问题定位
- **模块**：`src/ui/ContentPanel.cpp` (FerrexVirtualDbModel::data), `src/ui/ThumbnailDelegate.cpp`
- **根因分析**：`FerrexVirtualDbModel::data()` 在处理 `Qt::DecorationRole` 时，若缩略图缓存未命中，会立即启动异步提取任务并返回 `UiHelper::getFileIcon(path, 128)` 作为临时占位。二段式（对应用户原话：“二阶段”）渲染导致抖动。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 第一步：区分"是否为图形文件" | 方案 4.1：逻辑分支区分文件类型 | ✅ |
| 2    | 对于图形文件...返回 QIcon()（空图标） | 方案 4.1：图形文件异步期间返回空图标 | ✅ |
| 3    | 对于非图形文件...直接返回 UiHelper::getFileIcon() | 方案 4.1：非图形文件维持原逻辑 | ✅ |
| 4    | 第二步：修改占位返回值 | 方案 4.1：物理修改 data() 返回逻辑 | ✅ |
| 5    | 第三步：ThumbnailDelegate 配合处理空图标 | 方案 4.2：Delegate 识别空图标 | ✅ |
| 6    | 绘制一个轻量的灰色圆角矩形占位背景（纯色，无图标） | 方案 4.2：绘制灰色圆角矩形占位 | ✅ |

## 4. 详细解决方案

### 4.1 修改 `FerrexVirtualDbModel::data()` (src/ui/ContentPanel.cpp)
- **位点**：约 200 行 `Qt::DecorationRole` 分支。
- **操作**：执行第一步（对应用户原话：“第一步”），提取后缀名 `QString ext = record.suffix.toLower();`。
- **逻辑**：
    - 若 `!UiHelper::isGraphicsFile(ext)`（对应用户原话：“对于非图形文件...直接返回 UiHelper::getFileIcon()，不启动异步任务”），则直接返回系统图标。
    - 若是图形文件且缓存未命中，启动后台任务后执行第二步（对应用户原话：“第二步”），执行 `return QIcon();`（对应用户原话：“return QIcon(); // 图形文件等待缩略图，返回空”）。

### 4.2 修改 `ThumbnailDelegate::paint()` (src/ui/ThumbnailDelegate.cpp)
- **位点**：约 74 行 `paint` 函数内部。
- **操作**：执行第三步（对应用户原话：“第三步”）。
- **逻辑**：判定 `Qt::DecorationRole` 返回的图标。若图标为空且该项为图形文件，则在 `m.cardRect` 区域内**绘制一个（对应用户原话：“一个”）轻量的灰色圆角矩形占位背景（纯色，无图标）**（对应用户原话：“绘制一个轻量的灰色圆角矩形占位背景（纯色，无图标）”）。建议使用颜色 `#3a3a3a`。

## 5. 修改边界声明【红线】
**本次方案涉及范围：**
- [ ] 模块/文件：`src/ui/ContentPanel.cpp` (FerrexVirtualDbModel::data)
- [ ] 模块/文件：`src/ui/ThumbnailDelegate.cpp` (ThumbnailDelegate::paint)

**明确禁止越界修改的范围：**
- [ ] 禁止修改异步提取线程池逻辑。
- [ ] 禁止修改缓存查找机制。
- [ ] 禁止修改 `ThumbnailDelegate` 中除 `paint()` 空图标处理之外的任何逻辑。

## 6. 实现准则与预警【核心】
1. **考古对齐**：方案利用了 `ThumbnailDelegate` 已有的圆角矩形裁剪机制，确保占位块圆角与缩略图圆角完全对齐。
2. **性能预警**：后缀名提取必须使用 `record.suffix` 以绕过 `QFileInfo` 的磁盘 IO 瓶颈。
3. **逻辑闭环**：空图标返回是触发 Delegate 绘制占位背景的唯一信号，必须严格匹配。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|-------------|----------------------|----------------|
| UI 刷新抑制 | 避免不必要的闪烁/抖动 | ✅ 符合。 |
| 卡片圆角规范 | 圆角 6px | ✅ 符合。 |

## 8. 待确认事项（可选）
- 无。
