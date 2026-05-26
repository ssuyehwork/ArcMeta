# 图像与卡片尺寸缩放逻辑分析 (Analysis_Modification_Plan-14)

根据对 `ContentPanel` 和 `JustifiedView` 核心源码的分析，针对您提出的“是图片随卡片变，还是卡片随图片变”的问题，结论如下：

## 1. 核心结论
在您截图中显示的 **合理布局视图 (JustifiedView)** 模式下：
**卡片的宽度是随着图片的宽高比（Aspect Ratio）动态变化的，而图片的高度则受到卡片高度（缩放级别）的约束。**

简而言之：**高度决定宽度，比例决定形态。**

## 2. 逻辑深度解析

### 2.1 谁是“主谋”？（高度控制）
在 `ContentPanel.cpp` 的 `updateGridSize()` 函数中，用户的缩放操作（Ctrl+滚轮）直接修改的是 `m_zoomLevel`。
*   对于 `JustifiedView`，这个值被设置为 **目标行高 (Target Row Height)**。
*   代码片段：`jv->setTargetRowHeight(m_zoomLevel);`

### 2.2 谁是“从犯”？（宽度计算）
在 `JustifiedView.cpp` 的 `doLayout()` 函数中，程序会遍历当前行内的所有图片：
1.  **获取比例**：读取图片的原始宽高比 `AspectRatioRole`。
2.  **计算宽度**：`itemWidth = qRound(图片比例 * 实际行高) + 6`（+6 是为了补偿边距）。
3.  **动态对齐**：为了保证每一行都能占满容器宽度，程序会微调 `actualHeight`，从而导致卡片宽度也随之微调。

### 2.3 图片如何填充？
在 `ThumbnailDelegate.cpp` 的 `paint()` 函数中：
```cpp
// 缩放逻辑：保持比例缩放，并居中显示
QPixmap scaled = thumb.scaled(m.cardRect.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
```
图片会被缩放到卡片允许的最大范围内。注意当前代码使用的是 `KeepAspectRatioByExpanding`（Cover 模式），这会确保卡片被填满。

## 3. 总结
*   **卡片高度**：由您手动控制（缩放级别）。
*   **卡片宽度**：由图片自身的“长宽相貌”决定。
*   **图片尺寸**：最终缩放以适应上述计算出的卡片矩形。

---

## 4. 星级间距分析与修改建议

### 4.1 现状分析
*   **代码设定值**：目前 `src/ui/ThumbnailDelegate.cpp` 中的星级间距 `m.starSpacing` 为 **-2 像素**（已通过最近的同步更新）。
*   **视觉偏差原因**：由于使用的 SVG 图标本身在图形边缘留有透明 Padding，即使间距为 -2px，视觉上星星之间仍有微小空隙。
*   **与原图相符性**：目前已比最初的 +1px 紧凑很多。

### 4.2 修改建议
若要达到极致紧凑效果：
*   **位置**：`src/ui/ThumbnailDelegate.cpp` 第 43 行。
*   **修改建议**：将 `m.starSpacing = -2;` 修改为 **`-4`**。

---

## 5. 卡片留白（红箭头处）优化建议

### 5.1 留白产生原因
1.  **行末拉伸**：在 `JustifiedView.cpp` 中，为了让每一行图片都能完美对齐右边界，程序会将每一行剩余的像素分配给该行的最后一个卡片。
2.  **绘制模式**：如果缩略图因比例问题无法完全填满拉伸后的卡片，则会露出背景色。

### 5.2 优化方案
*   **方案 A**：修改 `JustifiedView::doLayout` 中的行末分配逻辑，将剩余像素分配给间距而非卡片宽度。
*   **方案 B**：在 `ThumbnailDelegate::paint` 中，确保卡片背景色与缩略图主色调一致。

---

## 7. 系统预定义色码清单

为了确保修改方案中的颜色表现一致，以下是当前系统中定义的标准颜色对照表：

| 颜色名称 | 存储色码 (用于数据筛选) | 预览色码 (用于界面显示) |
| :--- | :--- | :--- |
| **无颜色** | `""` (空字符串) | `#888780` |
| **红色** | `#E04040` | `#E24B4A` |
| **橙色** | `#E09020` | `#EF9F27` |
| **黄色** | `#F0C070` | `#FAC775` |
| **绿色** | `#609020` | `#639922` |
| **青色** | `#109070` | `#1D9E75` |
| **蓝色** | `#3080D0` | `#378ADD` |
| **紫色** | `#7070D0` | `#7F77DD` |
| **灰色** | `#505050` | `#5F5E5A` |

---

## 8. 颜色设定显示现状与问题分析

### 8.1 颜色目前“躲”在哪里？
经过代码审计，目前右键菜单设定的颜色数据（ColorRole）仅在以下场景被使用：
1.  **元数据交互**：选中项目后，右侧 `MetaPanel` 会读取并显示色码。
2.  **行内编辑器**：在重命名状态下，`GridItemDelegate::createEditor` 会将颜色设置为输入框的 `background-color`。
3.  **筛选器统计**：`ContentPanel::recalculateAndEmitStats` 会统计颜色占比。

### 8.2 核心痛点
在 `ThumbnailDelegate`（缩略图视图）和 `GridItemDelegate`（网格视图）的 `paint` 函数中，**完全没有绘制 ColorRole 数据的代码**。这意味着用户设定颜色后，在静态浏览模式下无法从卡片上获得任何视觉反馈。

---

## 6. 星级区域背景色彩同步方案

### 6.1 需求描述
希望将项目的“手动设定颜色”（如红色、绿色等标签颜色）显示在星级评分区域的背景下方，形成一个带颜色的胶囊状背景（如 PixPin 截图所示）。

### 6.2 现状分析
*   **数据层**：颜色数据已存储在 `ColorRole` 中。
*   **委派层**：`ThumbnailDelegate` 目前尚未接收 `ColorRole`。

### 6.3 详细修改建议

#### 第一步：引入颜色 Role
在 `src/ui/ThumbnailDelegate.h` 中增加成员：
```cpp
void setColorRole(int role);
int m_colorRole = -1;
```
在 `src/ui/ContentPanel.cpp` 中绑定：
```cpp
delegate->setColorRole(ColorRole);
```

#### 第二步：绘制带颜色的背景
在 `src/ui/ThumbnailDelegate.cpp` 的 `paint` 函数中，在循环绘制星星之前，插入以下逻辑：
```cpp
QString colorStr = index.data(m_colorRole).toString();
QColor bgColor = UiHelper::parseColorName(colorStr);
if (bgColor.isValid() && shouldShowRating) {
    painter->save();
    bgColor.setAlpha(150); // 设置半透明度
    painter->setBrush(bgColor);
    painter->setPen(Qt::NoPen);
    // 计算并绘制圆角矩形背景
    QRect totalRect = m.banRect.united(m.starRect(4));
    painter->drawRoundedRect(totalRect.adjusted(-4, -1, 4, 1), 4, 4);
    painter->restore();
}
```

### 6.4 预期效果
设置颜色标签后，星级下方将出现对应的半透明彩色背景，极大增强了视觉识别度。
