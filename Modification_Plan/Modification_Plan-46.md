# 列表视图最左侧微型卡片背景透明化重构 —— Modification_Plan-46.md

> 状态：已批准，执行中 / 已执行完成

## 1. 任务背景
在“列表”视图模式下，最左侧的微型缩略图卡片容器背景当前采用硬编码的深灰色实色 `#2D2D2D`，限制了在选中、未选中以及悬停高亮等不同背景状态下的通透性，视觉上存在不连贯的卡片边界。为了提升界面视觉一致性，用户期望将其背景色彻底重构为完全透明。

## 2. 问题定位
该微型卡片容器背景的绘制逻辑处于单元格展示代理 `src/ui/TreeItemDelegate.h` 的 `paint` 绘制流程中。
具体代码处于 `src/ui/TreeItemDelegate.h` 第 81~85 行：

```cpp
81:             // 1. 绘制 4px 圆角微型卡片容器背景
82:             painter->setPen(Qt::NoPen);
83:             painter->setBrush(QColor("#2D2D2D"));
84:             QPainterPath cardPath;
85:             cardPath.addRoundedRect(squareRect, 4, 4);
86:             painter->drawPath(cardPath);
```

此处使用的实色刷子 `QColor("#2D2D2D")` 会遮挡下方的整行高亮高光色或斑马纹。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 无论是任何时候（选中 / 未选中 / 悬停高亮）卡片的背景色都必须是透明的 | 更改最左侧微型卡片绘制刷子为 `Qt::transparent` | ✅ |

## 4. 详细解决方案
将 `src/ui/TreeItemDelegate.h` 第 83 行的背景色刷子 `painter->setBrush(QColor("#2D2D2D"))` 变更为完全透明的 `painter->setBrush(Qt::transparent)`。
这样无论行处于何种状态（选中、未选中、悬停高亮），背景色均会彻底透明穿透，显露出下方的斑马纹背景或直角高亮高光色。

### 详细代码变更说明：

```cpp
<<<<<<< SEARCH
            // 1. 绘制 4px 圆角微型卡片容器背景
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor("#2D2D2D"));
            QPainterPath cardPath;
            cardPath.addRoundedRect(squareRect, 4, 4);
            painter->drawPath(cardPath);
=======
            // 1. 绘制 4px 圆角微型卡片容器背景（透明背景穿透，对应用户原话：“卡片的背景色都必须是透明的”）
            painter->setPen(Qt::NoPen);
            painter->setBrush(Qt::transparent);
            QPainterPath cardPath;
            cardPath.addRoundedRect(squareRect, 4, 4);
            painter->drawPath(cardPath);
>>>>>>> REPLACE
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/ui/TreeItemDelegate.h`（第 83 行）

**明确禁止越界修改的范围：**
- [ ] 列表视图中除微型卡片背景色以外的其他渲染与逻辑 —— 不修改

## 6. 实现准则与预警【核心】
1. **头文件依赖**：本项目修改仅限于 `src/ui/TreeItemDelegate.h` 头文件，不涉及新增外部头文件或外部标识符，能保持编译绝对安全。
2. **防闪烁与防抖动**：卡片背景变透明后，缩略图的绘制会直接叠加到整行直角高亮块或斑马纹之上。由于背景为纯透明，缩略图非正方形时，两端会露出下方的选中高亮色，符合透明背景设计初衷。
3. **结合上下文**：确保 `Qt::transparent` 标识符处于正确的 namespace 或直接可用状态。由于 `TreeItemDelegate.h` 已引入了标准的 Qt 绘图核心，`Qt::transparent` 是开箱即用且高度安全的。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 列表微卡片透明背景 | 无论是任何时候卡片的背景色都必须是透明的，通过设为 `Qt::transparent` 来实现纯净穿透。 | ✅ 符合 |

## 8. 待确认事项（可选）
*无。*
