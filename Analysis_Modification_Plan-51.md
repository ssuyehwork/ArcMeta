# 架构纠偏与交互精细化设计方案 (Analysis & Modification Plan) - #51

## 1. 核心设计偏差审计

经过对用户反馈与视觉稿的深度对齐，纠正之前的两项严重逻辑偏差：

### 1.1 颜色展示逻辑偏差
- **原错误实现**：为每个颜色独立创建一个胶囊按钮，导致 UI 零散。
- **正确预期**：**单体胶囊化调色盘**。所有色点应包裹在一个统一的圆角胶囊容器内，整体感强。
- **缺失交互**：悬停时缺乏 1px 像素环反馈，且 ToolTip 未显示详细颜色信息。

### 1.2 编辑框展示逻辑偏差
- **原错误实现**：使用了 `setFixedHeight` 锁定高度，导致长文件名或多行备注被截断。
- **正确预期**：**弹性流式高度**。编辑框应随内容的增多自动向下撑开（Auto-expanding），确保所见即所得。

---

## 2. 现状诊断（Current State Analysis）

**2.1 涉及文件清单**
| 文件名 | 完整路径 | 当前职责 |
|--------|----------|----------|
| MetaPanel.h | src/ui/MetaPanel.h | 声明面板组件 |
| MetaPanel.cpp | src/ui/MetaPanel.cpp | UI 构建与鼠标交互逻辑 |

**2.2 根因定位**
- `PaletteSwatch` 类的设计粒度过细，无法形成有机的颜色链条。
- `initUi` 中广泛存在的 `setFixedHeight(32/80/26)` 物理锁死了布局的弹性。

---

## 3. 详细修改方案

### 3.1 调色盘重构：单体 PaletteCapsule
创建全新的 `PaletteCapsule` 组件替代旧有的 `PaletteSwatch` 循环。

#### 核心逻辑预览：
```cpp
class PaletteCapsule : public QWidget {
    // 1. paintEvent：
    //    - 绘制统一的圆角矩形背景 (Capsule)
    //    - 遍历 palette 数据，在胶囊内依次绘制色点
    // 2. mouseMoveEvent (启用了 MouseTracking)：
    //    - 物理计算当前鼠标落在哪个色点坐标内
    //    - 触发 1px 白色描边环的重绘
    //    - 调用 ToolTipOverlay::showText("#HEX (Ratio%)")
};
```

### 3.2 弹性高度引擎：Auto-Growing Editors
通过移除硬编码高度并引入动态高度适配逻辑。

#### 修改步骤：
1. **移除禁锢**：删除 `m_nameEdit->setFixedHeight(32)`、`m_noteEdit->setFixedHeight(80)` 等。
2. **动态适配**：
   - 对于 `m_nameEdit` 和 `m_linkEdit`：使用 `setMinimumHeight(24)`，并结合 `textChanged` 信号动态计算文本行数。
   - 对于 `m_noteEdit` (QPlainTextEdit)：
     ```cpp
     connect(m_noteEdit, &QPlainTextEdit::textChanged, [this](){
         int docHeight = m_noteEdit->document()->size().height();
         m_noteEdit->setMinimumHeight(qMax(64, docHeight + 10)); // 动态呼吸高度
     });
     ```

### 3.3 布局刷新增强
在 `MainWindow.cpp` 中补全 `updateInfo` 后，调用 `m_container->adjustSize()`，确保父容器能即时响应子控件的高度变迁。

---

## 4. 逐文件改动计划

#### 1. `src/ui/MetaPanel.h`
- 移除 `PaletteSwatch` 定义。
- 声明 `PaletteCapsule` 类及其实员函数。

#### 2. `src/ui/MetaPanel.cpp`
- **实现 `PaletteCapsule` 绘制与交互**：
  - 计算间距：`int spacing = (width() - (count * dotSize)) / (count + 1);`
  - 悬停检测：映射坐标到颜色索引。
- **重构 `initUi`**：
  - 将所有编辑框替换为弹性版本。
  - 使用 `QPlainTextEdit` 包装名称编辑以支持超长名称自动换行（可选）。

---

## 5. 性能与交互风险
- **计算频率**：鼠标追踪计算量极小，不影响性能。
- **布局跳动**：高度动态调整可能导致下方控件瞬间下移。建议增加高度变化的动画平滑度（未来优化项）。

---
**分析人员：** 资深程序员 (Jules)
**日期：** 2024-05-22
