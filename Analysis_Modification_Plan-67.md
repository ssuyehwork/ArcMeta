# Analysis and Modification Plan - 67: 元数据面板弹性编辑框高度优化

## 1. 问题分析
用户反馈元数据面板（MetaPanel）中的编辑框（如名称、备注、链接等）高度疑似固定（28px），无法随内容增加而垂直伸缩。
经过代码分析（`src/ui/MetaPanel.cpp`），发现虽然 `ElasticEdit` 类实现了 `adjustHeight()` 逻辑，但可能存在以下问题：
- **高度计算不精确**：当前的 `newHeight = qMax(28, (int)docHeight + 10)` 使用了固定偏移量 10px，可能未完全覆盖 QSS 中定义的 padding (4px * 2) 和 border (1px * 2)，或者在某些 DPI 下存在舍入误差。
- **布局刷新延迟**：虽然调用了 `updateGeometry()` 和 `activate()`，但在某些情况下，父容器 `m_container` 的高度未同步更新，导致内容被截断或出现滚动条。
- **文档宽度设置时机**：`setTextWidth` 的调用依赖于控件的 `width()`，如果在布局完成前获取宽度，可能导致高度计算错误。

## 2. 修改目标
- 实现真正的弹性高度：最小高度 28px，最大高度随内容自适应。
- 确保文本换行与控件宽度严格同步。
- 确保高度变化后，元数据面板的滚动区域（QScrollArea）能立即感知并更新滚动范围。

## 3. 详细修改方案

### 3.1 优化 `ElasticEdit::adjustHeight` (src/ui/MetaPanel.cpp)
- **修正高度公式**：使用更加严谨的计算方式。
- **添加调试日志**：输出宽度和高度的计算过程，便于追踪。

```cpp
void ElasticEdit::adjustHeight() {
    // 获取当前控件宽度并扣除 padding
    int w = width();
    // 假设 padding 是 4px 10px，border 是 1px
    int horizontalPadding = 20; // 10px * 2
    int verticalPadding = 8;    // 4px * 2
    int border = 2;             // 1px * 2

    int textW = qMax(0, w - horizontalPadding - border);

    if (textW > 0) {
        document()->setTextWidth(textW);
    }

    qreal docHeight = document()->documentLayout()->documentSize().height();
    int newHeight = qMax(28, (int)qCeil(docHeight + verticalPadding + border));

    if (this->height() != newHeight) {
        setFixedHeight(newHeight);
        updateGeometry();

        // 向上通知父容器重绘布局
        QWidget* p = parentWidget();
        while (p) {
            if (p->layout()) p->layout()->activate();
            if (qobject_cast<QScrollArea*>(p)) break;
            p = p->parentWidget();
        }
    }
}
```

### 3.2 强化 `MetaPanel::resizeEvent` (src/ui/MetaPanel.cpp)
- 在窗口缩放时，不仅同步宽度，还要确保 `m_container` 显式调用 `adjustSize()`。
- 确保所有 `ElasticEdit` 实例都被正确触发高度调整。

### 3.3 验证计划
- **静态检查**：核对 QSS 中的 padding 值与代码中的常量是否一致。
- **运行时验证**：
    - 输入多行文本，观察编辑框是否向下撑开。
    - 缩小窗口宽度，观察文本换行后编辑框高度是否自动增加。
    - 检查 `arcmeta_debug.log` 中的高宽日志。

## 4. 后续操作
- 申请执行代码修改。
- 进行功能自测。
