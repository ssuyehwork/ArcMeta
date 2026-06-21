# 递归显示按钮样式与交互逻辑优化 —— Analysis_Modification_Plan-73.md

## 1. 任务背景
用户反馈内容面板（ContentPanel）标题栏中的“显示子文件夹中的项目”按钮（`m_btnLayers`）存在以下问题：
- **视觉不统一**：开启递归模式时，按钮呈现蓝色背景与边框，与主程序标题栏的灰色扁平化风格不符。
- **ToolTip 行为不一致**：提示文字会自动消失，用户要求其在鼠标离开前持续显示。
- **逻辑缺陷**：在侧边栏分类视图下（非物理目录导航），该按钮未被正确禁用，且提示信息不准确。

## 2. 问题定位
- **样式定义**：`src/ui/ContentPanel.cpp` 中的 `m_btnLayers->setStyleSheet` 显式设置了蓝色背景。
- **ToolTip 逻辑**：`src/ui/ContentPanel.cpp` 的 `eventFilter` 函数在调用 `ToolTipOverlay::showText` 时使用了默认超时参数（700ms）。
- **状态更新逻辑**：`src/ui/ContentPanel.cpp` 的 `updateLayersButtonState()` 仅排查了 `computer://` 路径，未感知 `m_currentCategoryType` 状态。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 按钮按下后应持续显示灰色，不该是蓝色 | 修改 QSS，将 `:checked` 状态背景设为 `#3E3E42` 并移除边框 | ✅ |
| 2    | ToolTipOverlay 不该受时间限制，直到鼠标离开后关闭 | 在 `eventFilter` 中将此按钮的 ToolTip 超时设为 0 | ✅ |
| 3    | 分类数据来源情况下同样不可执行递归 | 在 `updateLayersButtonState` 中增加对 `m_currentCategoryType` 的判断 | ✅ |

## 4. 详细解决方案

### 4.1 样式表（QSS）调整
修改 `src/ui/ContentPanel.cpp` 中 `m_btnLayers` 的样式表，对齐标题栏标准：
- `hover` 与 `checked` 状态均使用 `#3E3E42`。
- `pressed` 状态使用 `#4E4E52`。
- 移除 `border: 1px solid #3498db;`。

### 4.2 ToolTip 永驻逻辑
在 `src/ui/ContentPanel.cpp` 的 `eventFilter` 中，针对 `m_btnLayers`（以及 `m_btnLayersBlue`）触发的 `HoverEnter/Enter` 事件，调用 `ToolTipOverlay::instance()->showText` 时显式传递参数 `0` 作为 `timeout`。

### 4.3 禁用逻辑增强
重写 `ContentPanel::updateLayersButtonState`：
- 增加逻辑：若 `!m_currentCategoryType.isEmpty()`，视为分类/列表模式，禁用递归功能。
- 动态更新 `tooltipText`：区分“此电脑不支持”与“当前视图不支持”。

## 5. 修改边界声明【红线】

**本次方案涉及范围：**
- [x] 模块/文件：`src/ui/ContentPanel.cpp`

**明确禁止越界修改的范围：**
- [ ] 禁止修改 `src/ui/ToolTipOverlay.cpp` 核心实现。
- [ ] 禁止修改 `src/ui/MainWindow.cpp` 逻辑。

## 6. 实现准则与预警【核心】

1. **头文件依赖**：无需新增头文件。
2. **上下文匹配**：修改 `updateLayersButtonState` 时需确保不会误伤正常的目录导航（`m_currentCategoryType` 在 `loadDirectory` 中已被正确清空）。
3. **QSS 覆盖**：注意 `m_btnLayers` 和 `m_btnLayersBlue` 的样式需同步调整以保持一致。
4. **编译预警**：确保在 `eventFilter` 中对 `obj` 进行正确的指针比对（`obj == m_btnLayers`）。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|-------------|----------------------|----------------|
| 标题栏样式 | 悬停背景 `#3E3E42`，按下背景 `#4E4E52` | ✅ 符合 |
| ToolTipOverlay | 强制对接 ToolTipOverlay，禁止原生 Tip | ✅ 符合 |
| 核心驱动隔离 | 分类模式与路径模式严格区分 | ✅ 符合 |

## 8. 待确认事项
无。
