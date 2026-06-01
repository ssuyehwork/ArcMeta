# Analysis and Modification Plan - 68: 修复元数据面板编辑框字符级换行故障

## 1. 问题分析
根据用户提供的截图，元数据面板在实现高度弹性伸缩后，出现了严重的“字符级换行”现象（即每个汉字或字母都独立占据一行）。
经过代码审查（`src/ui/MetaPanel.cpp`），定位到以下根因：
- **换行模式配置冲突**：代码中使用了 `setLineWrapMode(QTextEdit::FixedColumnWidth)`，但未显式调用 `setLineWrapColumnOrWidth()`。在 `QTextEdit` 中，此模式默认可能导致渲染宽度被限制在极小值。
- **文档宽度强制截断**：`document()->setTextWidth(textW)` 虽然试图控制换行，但如果 `textW` 在初始化或 Resize 早期获取到了错误的小值（如 0 或接近 0），则会导致文档渲染区域极窄。
- **布局异步时序问题**：`resizeEvent` 中使用了 `QTimer::singleShot(0)`。虽然这能解决某些布局计算滞后问题，但如果此时 `viewport()` 尚未完全就绪，获取到的宽度可能不准确，从而触发了错误的 `setFixedWidth` 和 `adjustHeight`。

## 2. 修改目标
- **恢复正常换行**：文字应根据控件宽度自然换行，而非按字符强行断行。
- **保持高度自适应**：在正常换行的基础上，控件高度依然能随内容增长而撑开。
- **视觉稳定性**：消除初始化时的布局跳变。

## 3. 详细修改方案

### 3.1 优化 `ElasticEdit` 配置 (src/ui/MetaPanel.cpp)
- **切换换行模式**：将 `QTextEdit::FixedColumnWidth` 改为 `QTextEdit::WidgetWidth`（或 `NoWrap` 配合文档宽度控制）。建议优先使用 `WidgetWidth` 以利用 Qt 原生的布局适应能力。
- **修正文档边距**：确保 `documentMargin` 设置为 0，避免干扰像素高度计算。

### 3.2 优化高度计算逻辑 `adjustHeight`
- **宽度检测加固**：在调用 `setTextWidth` 前，增加更严格的宽度检查。
- **渲染高度获取**：继续使用 `document()->size().height()`，这是基类切换为 `QTextEdit` 后的核心优势。

### 3.3 重构 `MetaPanel::resizeEvent`
- **移除不必要的异步延迟**：尝试直接处理布局更新，或优化 `singleShot` 中的保护逻辑。
- **精确计算可用宽度**：确保扣除滚动条可能的宽度（虽然当前隐藏，但视口计算更稳健）。

## 4. 涉及文件清单
| 文件名 | 完整路径 | 当前职责 | 改动内容摘要 |
|--------|----------|----------|--------------|
| MetaPanel.cpp | `src/ui/MetaPanel.cpp` | 元数据面板实现 | 修正换行模式，优化高度计算与布局同步 |

## 5. 风险评估
- **风险**：如果完全移除 `singleShot`，在某些极端嵌套布局下可能引发递归 Resize。
- **应对**：保留适当的宽度变化检测（`if (newW != oldW)`），仅在真正变化时执行重算。

## 6. 执行前置条件
- [ ] 用户已审阅并批准本分析计划

## 7. 审批状态
**⏳ 等待用户审批 — 未获批准前禁止执行任何代码变更**
