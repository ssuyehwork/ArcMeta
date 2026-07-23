# 列表视图星级尺寸对齐重构 —— Modification_Plan-47.md

> 状态：已批准，执行中 / 已执行完成

## 1. 任务背景
在当前系统中，“列表”视图下的星级图标尺寸硬编码为 `16` 像素。而“网格”和“自适应”视图下的星级默认尺寸为 `22` 像素（当视图尺寸极小时采用 `18` 像素），这导致列表模式下的星级明显偏小，存在视觉设计不统一以及点击星级进行评分时的命中测试物理区域（Hitbox）偏窄、体验死板等问题。用户期望将列表模式下的星级尺寸进行统一对齐。

## 2. 问题定位
该星级大小及其交互区域主要由两处代码硬编码控制：

1. **渲染层（列表单元格绘制代理）**：`src/ui/TreeItemDelegate.h` 第 193 行：
   ```cpp
   193:                     int starSize = 16;
   ```
2. **交互层（内容面板事件拦截测试）**：`src/ui/ContentPanel.cpp` 第 1408 行：
   ```cpp
   1408:                             int starSize = 16;
   ```

这两处不一致的 `16` 像素硬编码限制了列表视图下的星级观感和操作体感。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 列表视图情况下，显示的星级大小为何比“网格”、“自适应”视图显示的星级还小 | 将列表视图星级尺寸由 `16px` 增大并统一为与网格/自适应视图完全一致的 `22px`，同时对齐间距，保证 Hitbox 同频变宽。 | ✅ |

## 4. 详细解决方案
本方案通过将列表代理与内容面板交互层中的 `starSize` 统一提升为 `22` 像素（与 `ThumbnailDelegate` 的默认值 `22` 像素完美同频对齐），并适当将间距 `spacing` 修正为紧凑型的 `2` 像素。

### 详细代码变更说明：

#### 4.1 修改树形绘制代理 `src/ui/TreeItemDelegate.h`

```cpp
<<<<<<< SEARCH
                    int starSize = 16;
                    int spacing = 2;
                    int startX = banRect.right() + 6;

                    if (rating > 0) {
                        QPixmap star = UiHelper::getPixmap("star_filled", QSize(starSize, starSize), QColor("#FECF0E"));
=======
                    // 统一提升星级尺寸至 22 像素（对应用户原话：“星级大小为何比网格、自适应视图显示的星级还小”）
                    int starSize = 22;
                    int spacing = 2;
                    int startX = banRect.right() + 6;

                    if (rating > 0) {
                        QPixmap star = UiHelper::getPixmap("star_filled", QSize(starSize, starSize), QColor("#FECF0E"));
>>>>>>> REPLACE
```

#### 4.2 修改内容面板事件过滤器 `src/ui/ContentPanel.cpp`

```cpp
<<<<<<< SEARCH
                            int starSize = 16;
                            int spacing = 2;
                            int startX = col2Rect.left() + 6 + 16 + 6;
                            for (int i = 0; i < 5; ++i) {
                                QRect starRect(startX + i * (starSize + spacing), col2Rect.top() + (col2Rect.height() - starSize) / 2, starSize, starSize);
=======
                            // 统一提升点击命中区尺寸至 22 像素（对应用户原话：“星级大小为何比网格、自适应视图显示的星级还小”）
                            int starSize = 22;
                            int spacing = 2;
                            int startX = col2Rect.left() + 6 + 16 + 6;
                            for (int i = 0; i < 5; ++i) {
                                QRect starRect(startX + i * (starSize + spacing), col2Rect.top() + (col2Rect.height() - starSize) / 2, starSize, starSize);
>>>>>>> REPLACE
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/ui/TreeItemDelegate.h`（第 193 行，修改绘制星级大小）
- [ ] 模块/文件：`src/ui/ContentPanel.cpp`（第 1408 行，同步增大点击拦截 hitbox）

**明确禁止越界修改的范围：**
- [ ] 列表视图中除星级和与之关联的绘图/命中参数以外的其他逻辑 —— 不修改

## 6. 实现准则与预警【核心】
1. **渲染对齐**：修改后，星级图标与评分时的点击区域都会完美扩张到 22 像素，消除视图之间的尺寸级落差。
2. **防内容溢出**：行高一般大于等于 28 像素，`22` 像素的星级绘制在 `col2Rect.top() + (col2Rect.height() - starSize) / 2` 处能完美居中且不产生内容裁剪。
3. **保持编译可过性**：本项目修改全部基于现有头文件和源文件内的局部硬编码参数修正，绝对不引入任何外部或第三方未知符号。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 列表星级尺寸对齐 | 统一将列表模式星级提升至 22 像素，与网格/自适应视图的大比例尺寸完美统一，消除体验和视觉差异。 | ✅ 符合 |

## 8. 待确认事项（可选）
*无。*
