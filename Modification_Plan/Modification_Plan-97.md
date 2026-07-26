# 列表模式下行内编辑框高度限制及视觉样式美化重构 —— Modification_Plan-97.md

> 状态：已批准，执行中

## 1. 任务背景
当前在列表模式（TreeView，基于 `TreeItemDelegate`）下激活重命名（F2）行内编辑时，由于缺少对编辑器的几何形变约束及自定义样式定义，编辑器高度直接顶满整格，显得极为臃肿，且直接盖在左侧的微型缩略图卡片上；样式也使用了系统默认的白色外观，与应用全暗黑、精致、卡片圆角的风格格格格不入。本方案旨在对列表模式的行内编辑框进行高内聚美化重构，实现物理避让、高度不大于 28 像素的精致约束以及样式统合。

## 2. 问题定位
- `TreeItemDelegate` 目前未实现 `createEditor` 成员的样式定制（仅返回父类默认创建的 `QLineEdit`），因此默认样式不协调且缺少圆角。
- 目前未重写 `updateEditorGeometry` 虚函数，导致 Qt 默认将编辑器铺满整格高度（行高通常为 36-40 像素），未提供上下、左右的 padding 裁切，从而导致编辑框高度过大，视觉上非常臃肿粗糙。
- 列表第 0 列在显示文件时渲染了最左侧的微型卡片预览（`m_drawMiniCards` 为真），未重写的 `updateEditorGeometry` 无法感知该微卡片的存在，导致编辑框直接铺在微卡片上方，发生了难看的重叠覆盖。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 列表模式情况下，行内编辑的编辑框高度极其恶心，这纯粹属于重复造轮子 (对应用户原话) | 为 `TreeItemDelegate` 重写 `createEditor` 函数，配置与网格、自适应模式完全统一的精致暗色、带圆角样式（背景 `#2D2D2D`，外框 `#3498db`，圆角 `4px`），消除粗糙视觉。 | ✅ 一致 |
| 2    | 行内编辑的编辑框高度不可大于28像素 (对应用户原话) | 在重写的 `updateEditorGeometry` 中精准计算，采用 `textRect.adjusted(...)` 将编辑器上下内缩，并在几何上强制其高度（`height`）不可大于 28 像素（对应用户原话：“编辑框高度不可大于28像素”）。 | ✅ 一致 |
| 3    | 渲染左侧微型卡片时绝不覆盖、叠占卡片 (对应用户原话) | 在 `updateEditorGeometry` 内部，若 `m_drawMiniCards` 且 col == 0 时，根据微型卡片的物理宽度（`side`）偏移编辑器的 `left` 边界，实现完全的物理避让。 | ✅ 一致 |

## 4. 详细解决方案

我们需要对 `TreeItemDelegate`（位于 `src/ui/TreeItemDelegate.h`）进行重构，实现以下两个核心成员函数：

### 4.1 定制 `createEditor`
重写并返回一个精心修饰样式的 `QLineEdit`：
```cpp
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QLineEdit* editor = new QLineEdit(parent);
        // 配置与 ThumbnailDelegate、GridItemDelegate 统合一致的精致样式
        editor->setStyleSheet(
            "QLineEdit {"
            "  background-color: #2D2D2D;"
            "  color: white;"
            "  selection-background-color: #3498db;"
            "  border: 1px solid #3498db;"
            "  border-radius: 4px;"
            "  padding: 0px 4px;"
            "  margin: 0px;"
            "  font-size: 8pt;"
            "}"
        );
        editor->installEventFilter(const_cast<TreeItemDelegate*>(this)); // 安装事件过滤器
        return editor;
    }
```

### 4.2 定制 `updateEditorGeometry`
限制编辑框高度最大不超过 28 像素（对应用户原话：“编辑框高度不可大于28像素”），且避让左侧微型卡片：
```cpp
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QRect targetRect = option.rect;

        // 如果是第 0 列且渲染了最左侧微卡片
        if (index.column() == 0 && m_drawMiniCards) {
            int padding = 3;
            int side = option.rect.height() - (padding * 2);
            if (side <= 0) side = 16;
            QRect squareRect(option.rect.left() + 6, option.rect.top() + padding, side, side);
            
            // 物理避让：向右平移编辑器起始位置，绝不叠占左侧微卡片
            targetRect.setLeft(squareRect.right() + 10);
        } else {
            // 普通列，提供一定的左右边距
            targetRect.adjust(6, 0, -6, 0);
        }

        // 强制约束最大高度不超过 28 像素
        const int maxH = 28; // 物理约束限制（对应用户原话：“高度不可大于28像素”）
        if (targetRect.height() > maxH) {
            int diff = targetRect.height() - maxH;
            int topAdj = diff / 2;
            int botAdj = diff - topAdj;
            targetRect.adjust(0, topAdj, 0, -botAdj); // 居中压缩高度
        } else {
            // 如果原本高度也偏大，稍微做上下内缩保护
            targetRect.adjust(0, 2, 0, -2);
        }

        editor->setGeometry(targetRect);
    }
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/ui/TreeItemDelegate.h`
  - 涉及类/函数：`TreeItemDelegate::createEditor`, `TreeItemDelegate::updateEditorGeometry`

**明确禁止越界修改的范围：**
- [ ] `TreeItemDelegate::paint` 绘制列或星级的核心代码——不修改
- [ ] `ContentPanel` 内容列表多级重装填刷新逻辑——不修改

## 6. 实现准则与预警【核心】
1. **高度硬核限幅**：必须保证几何高度 `targetRect.height() <= 28` 始终成立（对应用户原话：“不可大于28像素”）。我们使用 `diff` 计算，安全地自适应任何可能被 DPI 放大后的高行宽，确保完美居中内合，不顶格，不贴线。
2. **微卡片边界对齐**：避让逻辑中的 `squareRect` 计算 and 文字左侧平移值 `squareRect.right() + 10` 必须与 `TreeItemDelegate::paint` 绘制文本时的左端对齐公式保持 100% 同步，保证点击编辑瞬间光标和文字不跳跃不闪烁。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 编辑框样式与高度规范 | 编辑框样式须对齐暗色背景与蓝框圆角风格，大小不应贴顶顶，确保留白 | ✅ 符合。强制限制了最大高度不超过 28 像素，并使用 `adjusted` 合理内缩留白。 |

## 8. 待确认事项（可选）
（无）
