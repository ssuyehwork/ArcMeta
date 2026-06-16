# Analysis_Modification_Plan-52.md — 批量重命名下拉框 UI 升级方案

## 1. 任务描述
当前版本中，“批量重命名”窗口及其规则行（`RuleRow`）使用的 `QComboBox` 下拉框样式过于传统，带有明显的原生 Windows 痕迹。用户期望参考现代设计（图二），将其升级为具有**圆角设计**和**实心向下三角形箭头**的现代化组件。

## 2. 需求强制对照表

| 编号 | 用户原话 / 我的理解 | 我的方案对应点 | 是否一致 |
| :--- | :--- | :--- | :--- |
| 1 | 下拉框显示的这种样式非常的传统 | 通过 QSS 样式表彻底重写 `::drop-down` 子控件样式，禁用原生外观 | 是 |
| 2 | 期望的是圆角设计 | 在 `QComboBox` 基础样式中统一设置 `border-radius: 4px` | 是 |
| 3 | 像图二那样圆角设计+一个向下三角形即可 | 引入 `menu_triangle` SVG 图标替换现有的线条箭头，并应用圆角 | 是 |

## 3. 技术实施方案

### 3.1 图标准备
- **图标修正**：在 `src/ui/SvgIcons.h` 中新增 `dropdown_triangle`。
  - *原因*：原 `menu_triangle` 具有 24x24 视图框且留白过多，导致缩小后不可见。新图标使用 10x10 紧凑视图框，路径为饱满的中心三角形。
- 使用 `UiHelper::getSvgTempFilePath("dropdown_triangle", QColor("#AAAAAA"))` 获取物理路径。
- **性能优化**：在代码中使用 `static const QString` 缓存生成的路径，避免在高频调用的初始化函数中重复执行磁盘 IO。

### 3.2 样式重构逻辑
针对以下四个下拉框进行 QSS 注入：
1. `BatchRenameDialog` -> `m_presetCombo`
2. `RuleRow` -> `m_typeCombo`
3. `RuleRow` -> `m_paddingCombo`
4. `RuleRow` -> `m_dateFormatCombo`

**核心 QSS 模板：**
```css
QComboBox {
    background: #2D2D2D;
    border: 1px solid #444444;
    border-radius: 4px;
    padding-left: 6px;
    color: #EEEEEE;
}
QComboBox::drop-down {
    border: none;
    width: 18px;
}
QComboBox::down-arrow {
    image: url(%1); /* 动态注入生成的 PNG 路径 */
    width: 12px;    /* 二次迭代：提升至 12px 确保在大气度上对齐现代设计 */
    height: 12px;
}
QComboBox::drop-down {
    border: none;
    width: 24px;    /* 二次迭代：提升至 24px 增强视觉平衡感 */
}
QComboBox QAbstractItemView {
    background-color: #252526;
    border: 1px solid #444444;
    selection-background-color: #3E3E42;
    color: #EEEEEE;
    outline: none;
}
```

### 3.3 修改文件列表
- `src/ui/RuleRow.cpp`
- `src/ui/BatchRenameDialog.cpp`
- `src/ui/SvgIcons.h`

### 3.4 预设区细节优化
- **间距调整**：预设下拉框、删除按钮、导入/导出按钮之间的布局间距统一设定为 `5px`。
- **按钮颜色**：快速删除按钮由“悬停高亮”改为“**持续高亮**”。默认背景色设为 `#3E3E42`（灰色），文字设为白色。

## 4. 潜在风险
- QSS 路径转义：必须使用 `QDir::fromNativeSeparators` 确保路径在 Windows 下使用 `/` 而非 `\`。
- 控件高度：目前 `RuleRow` 锁定高度为 25px，需确保箭头在小尺寸下居中且清晰。
