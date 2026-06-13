# Analysis_Modification_Plan-34: 筛选器圆角一致性修复与竖向布局重构

## 1. UI 风格偏差诊断

### 1.1 圆角缺失根因 (QComboBox)
标记为 ② 的 `QComboBox` 呈现直角，主要由于以下技术细节被忽略：
*   **子组件遮挡**：在自定义 `QComboBox` 样式时，若未显式对 `::drop-down`（下拉箭头区域）设置 `border-top-right-radius` 和 `border-bottom-right-radius`，其默认的背景色或边框会覆盖父级的圆角效果。
*   **物理红线缺失**：在全局 QSS 或内联样式中，只针对 `QLineEdit` 统一设置了 `6px` 圆角，而 `QComboBox` 的基础样式未被强制对齐。

### 1.2 布局层级矛盾
当前的水平排列方案强行将三个控件塞入单行，导致：
*   **交互局促**：输入框宽度被过度压缩，无法显示完整的占位符。
*   **违背规范**：ArcMeta 侧边栏整体采用纵向滚动流，水平布局显得突兀且难以扩展。

---

## 2. 逻辑架构修正方案

### 2.1 全量圆角规范同步 (`src/ui/FilterPanel.cpp`)
对 `QComboBox` 进行视觉加固，确保与输入框 ① 达成 1:1 的视觉克隆：
```css
QComboBox {
    background: #2D2D2D;
    border: 1px solid #444444;
    border-radius: 6px; /* 强制对齐输入框 */
    padding: 4px 8px;
}
QComboBox::drop-down {
    border: none;
    background: transparent;
    width: 20px;
}
/* 必须处理下拉箭头容器的圆角，防止直角溢出 */
QComboBox::drop-down:hover {
    border-top-right-radius: 6px;
    border-bottom-right-radius: 6px;
}
```

### 2.2 极致竖向布局重构
按照“主打竖排”的要求，对“其他属性”分组进行结构化降维：

1.  **逻辑项纵向化**：
    *   利用 `addFilterRow` 接口，将“有链接”、“无链接”、“有备注”、“无备注”分别作为独立的复选框行纵向排列。
    *   移除所有水平的 `QLabel` + `RadioButton` 组合。

2.  **数值范围组件堆叠**：
    *   **行 1**：显示“文件大小”说明标签（小字降权）。
    *   **行 2**：全宽显示的“最小”输入框，内部使用 `placeholderText` 或 `LeadingAction` 标明单位。
    *   **行 3**：全宽显示的“最大”输入框。
    *   **行 4**：全宽显示的单位选择器（MB/KB），利用圆角对齐增强整体感。

### 2.3 信号链路微调
由于布局由横变竖，`m_filter` 的更新逻辑保持不变，但 UI 组件的生成顺序由 `QHBoxLayout` 切换为 `QVBoxLayout`（即直接添加到 `gl` 布局中），确保滚动条能正确感应内容高度。

---

## 3. 验证要点
1.  **视觉对账**：将输入框与下拉框并排观察，确认其边框色、圆角弧度、背景深度在不同 DPI 缩放下的绝对一致性。
2.  **纵向适应**：在侧边栏极其狭窄的情况下，验证竖排布局是否依然能完整显示文字，且不出现横向滚动条。
3.  **状态流转**：勾选“有链接”复选框后，手动触发“无链接”勾选，验证纵向排列下的互斥动画是否平滑自然。
