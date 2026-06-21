# 标题栏布局管理按钮及分栏重置功能实现 —— Analysis_Modification_Plan-78.md

## 1. 任务背景
为了提升交互的确定性并降低误操作风险，用户决定废弃通过面板标题或容器右键触发“布局菜单”的交互模式。改为在 `MainWindow` 标题栏新增一个专用的“布局”按钮（`m_btnLayout`），通过该按钮触发菜单。同时，为了应对用户手动调整分栏导致的布局混乱，新增“重置分栏”功能，一键恢复默认的面板显隐状态及宽度比例。

## 2. 问题定位

### 2.1 现状分析
1.  **右键触发冲突**：目前的布局菜单通过监听各面板容器的 `customContextMenuRequested` 信号触发（见 `MainWindow::setupSplitters` 中的 `connectPanelMenu`）。这与面板内部子控件（如 `ColorPill`）的右键交互存在冒泡冲突。
2.  **入口隐蔽**：隐藏所有面板后，用户难以找回显示面板的入口。
3.  **缺乏一键恢复**：分栏宽度在 `AppConfig` 中持久化，一旦调乱，手动恢复至精确的比例（230|230|600|230|230）较为繁琐。

### 2.2 涉及文件
- `src/ui/MainWindow.h`：新增按钮成员变量及重置函数声明。
- `src/ui/MainWindow.cpp`：清理旧逻辑，实现新按钮、新菜单项及重置逻辑。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 彻底取消所有面板容器及标题栏对右键菜单的触发逻辑 | 移除 `MainWindow::setupSplitters` 中的 `connectPanelMenu` 调用 | ✅ |
| 2    | 新增图标为 `layout.svg` 的布局按钮 | 在 `setupCustomTitleBarButtons` 中实现 `m_btnLayout` | ✅ |
| 3    | 菜单新增“重置分栏”选项 | 在 `populatePanelMenu` 中添加该选项及分割线 | ✅ |
| 4    | 重置逻辑：全显 + 恢复 230\|230\|600\|230\|230 | 在 `resetSplitterLayout` 中原子化执行上述操作 | ✅ |
| 5    | 清除 AppConfig 中的旧布局状态 | 在重置函数中调用 `AppConfig::remove` 对应键值 | ✅ |

## 4. 详细解决方案

### 4.1 移除旧有右键逻辑
1.  在 `src/ui/MainWindow.cpp` 的 `setupSplitters()` 函数中，删除 `connectPanelMenu` lambda 定义及其对各面板的调用。
2.  （可选但推荐）移除各面板类（`CategoryPanel` 等）构造函数中不必要的 `setContextMenuPolicy(Qt::CustomContextMenu)` 设置，除非该面板内部仍需处理私有右键菜单。

### 4.2 实现标题栏布局按钮
在 `src/ui/MainWindow.h` 增加：
```cpp
QPushButton* m_btnLayout = nullptr;
void resetSplitterLayout();
```

在 `src/ui/MainWindow.cpp` 的 `setupCustomTitleBarButtons()` 中：
```cpp
m_btnLayout = createTitleBtn("layout");
m_btnLayout->setProperty("tooltipText", "布局管理与重置");
m_btnLayout->installEventFilter(m_hoverFilter);

// 放置在 m_btnSync 和 m_btnCreate 之间或合适位置
layout->insertWidget(0, m_btnLayout); // 举例

connect(m_btnLayout, &QPushButton::clicked, this, [this]() {
    QMenu menu(this);
    UiHelper::applyMenuStyle(&menu);
    populatePanelMenu(&menu);
    menu.exec(m_btnLayout->mapToGlobal(QPoint(0, m_btnLayout->height())));
});
```

### 4.3 增强布局菜单与重置逻辑
修改 `MainWindow::populatePanelMenu(QMenu* menu)`：
```cpp
void MainWindow::populatePanelMenu(QMenu* menu) {
    // 1. 原有的显隐控制（保持现状）
    auto addToggleAction = [&](const QString& text, QWidget* panel, bool canHide = true) {
        QAction* action = menu->addAction(text);
        action->setCheckable(true);
        action->setChecked(panel->isVisible());
        action->setEnabled(canHide);
        connect(action, &QAction::toggled, panel, [panel](bool visible) {
            panel->setVisible(visible);
        });
    };
    addToggleAction("显示分类栏", m_categoryPanel);
    addToggleAction("显示目录导航", m_navPanel);
    addToggleAction("显示内容区", m_contentPanel, false);
    addToggleAction("显示元数据栏", m_metaPanel);
    addToggleAction("显示筛选栏", m_filterPanel);

    // 2. 新增重置选项
    menu->addSeparator();
    QAction* resetAct = menu->addAction("重置分栏");
    connect(resetAct, &QAction::triggered, this, &MainWindow::resetSplitterLayout);
}
```

实现 `MainWindow::resetSplitterLayout()`：
```cpp
void MainWindow::resetSplitterLayout() {
    // 1. 物理恢复可见性并退出特殊模式
    m_isTagManagerMode = false;
    m_tagManagerView->hide();

    m_categoryPanel->show();
    m_navPanel->show();
    m_contentPanel->show();
    m_metaPanel->show();
    m_filterPanel->show();

    // 2. 物理恢复尺寸比例 (索引 0-4)
    QList<int> sizes;
    sizes << 230 << 230 << 600 << 230 << 230;
    // 如果 TagManagerView 在索引 5，需确保其 size 为 0 或处于 hide 状态
    if (m_mainSplitter->count() > 5) sizes << 0;

    m_mainSplitter->setSizes(sizes);

    // 3. 清除持久化状态，防止重启后回滚旧布局
    AppConfig::instance().remove("MainWindow/SplitterState");
    AppConfig::instance().remove("MainWindow/PanelVisibility");
    AppConfig::instance().sync();

    ToolTipOverlay::instance()->showText(QCursor::pos(), "布局已重置为默认值", 1500);
}
```

## 5. 修改边界声明【红线】

**本次方案涉及范围：**
- [ ] `src/ui/MainWindow` 的 UI 初始化与布局控制逻辑。
- [ ] `AppConfig` 的布局相关键值处理。

**明确禁止越界修改的范围：**
- [ ] 禁止修改面板内部的业务逻辑。
- [ ] 禁止修改 `QSplitter` 的样式常量（如 `handleWidth`）。

## 6. 实现准则与预警【核心】

1.  **头文件依赖**：`resetSplitterLayout` 使用了 `QCursor::pos()`，需确保 `MainWindow.cpp` 已包含 `<QCursor>`。虽然目前已有包含，但在移动或重构逻辑时需警惕。
2.  **图标确认**：确保 `src/ui/SvgIcons.h` 中包含 `layout` 键名，否则会导致按钮图标缺失。
3.  **原子操作**：`resetSplitterLayout` 必须同步执行显隐和尺寸调整，否则 `QSplitter` 可能因某些面板处于 `hide` 状态而导致尺寸分配不符合预期（强烈建议遵循先 `show()` 后 `setSizes()` 的顺序）。
4.  **AppConfig 键名匹配**：必须与 `closeEvent` 及 `initUi` 中使用的键名（`MainWindow/SplitterState` 和 `MainWindow/PanelVisibility`）保持严格一致，避免产生孤立的配置垃圾。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|-------------|----------------------|----------------|
| UI 实现标准 | 标题栏按钮 24x24px，圆角 4px | ✅ 方案复用了 `createTitleBtn` 符合此规范 |
| 布局索引规范 | m_mainSplitter 组件顺序固定 | ✅ 重置逻辑基于 (0,1,2,3,4) 索引分配尺寸，符合规范 |
| 主界面持久化 | 面板显隐状态需持久化 | ✅ 方案通过重置并清除缓存，确保了状态的一致性 |

## 8. 待确认事项
- 无。方案已包含对 `TagManagerView` 的隐藏处理及 `m_isTagManagerMode` 状态重置。
