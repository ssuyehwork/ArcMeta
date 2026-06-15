# Analysis and Modification Plan - 输入框清除按钮规范化与规则增补

## 1. 现状剖析与问题定位
目前的 `MainWindow` 搜索框中存在一个严重的“脑补式”错误实现：
- **错误实现**：在 `src/ui/MainWindow.cpp` 第 896-906 行，使用 `addAction` 配合自定义 `close` 图标模拟了一个清除按钮，并手动编写了控制显隐的 `textChanged` 逻辑。
- **后果**：这种做法破坏了项目早期建立的“原生优先”视觉规范，导致其与筛选面板（FilterPanel）中标准的 `setClearButtonEnabled(true)` 效果（带圈的 X）产生了明显的视觉割裂。

## 2. 拨乱反正方案 (Restoration Solution)

### 2.1 修改 `MainWindow.cpp`
彻底废除自定义 Action 逻辑，回归 Qt 工业标准：
- **删除**：移除 `clearAction` 的创建、连接逻辑及其在 `textChanged` 信号中的显隐控制。
- **替换**：直接调用 `m_searchEdit->setClearButtonEnabled(true);`。
- **结果**：搜索框的清除按钮将自动获得与筛选面板完全一致的形态与交互表现。

### 2.2 视觉样式对齐
由于回归了原生按钮，需要确保 QSS 中对原生清除按钮（Indicator）的微调（如果存在）能正确覆盖，维持暗色主题的低对比度美感。

## 3. 行为准则增补 (AGENTS.md Rules)
为了杜绝此类“另起炉灶”的脑补行为，必须在 `AGENTS.md` 中新增以下约束：

> **# 4. UI 实现规范**
> ## 4.1 统一输入框清除功能
> - **唯一标准**：所有可编辑输入框（QLineEdit）如需清除功能，必须且只能使用 Qt 原生的 `setClearButtonEnabled(true)`。
> - **严禁脑补**：严禁通过 `addAction`、手动绘图或创建自定义按钮等方式模拟清除逻辑。
> - **杜绝另创**：任何此类“创新”均被视为违反项目一致性红线，必须立即回归原生方案。

## 4. 执行规范提醒
- **严格执行参数**：修改时仅限于移除违规代码并启用原生接口。
- **申报备案**：如果在回归原生方案后，发现原生图标在特定 DPI 下存在模糊等问题，必须记录至 `Declaration_Log.md`，不得再次开启脑补修复模式。
