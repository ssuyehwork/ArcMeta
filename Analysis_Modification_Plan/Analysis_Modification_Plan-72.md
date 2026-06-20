# 面板控制菜单触发区域精确化重构 —— Analysis_Modification_Plan-72.md

## 1. 任务背景
当前版本中，面板布局控制菜单（显示/隐藏面板）被绑定到了各面板容器的整体区域。用户明确要求：**仅在对准五个容器的“标题栏”点击右键时才弹出菜单**。在面板的其他内容区域（如树列表、网格视图等）右键应触发各面板自身的业务菜单，而非全局布局控制菜单。

## 2. 问题定位
- **模块**：`src/ui/MainWindow.cpp`
- **代码行**：约 1071 行处的 `connectPanelMenu` Lambda 函数。
- **根因分析**：现有逻辑直接对 `panel`（容器本身）监听 `customContextMenuRequested` 信号，且容器策略设置为 `Qt::CustomContextMenu`。这导致了触发范围过大，掩盖了子控件的正常右键行为，严重违背了“范围感知”的交互原则。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 只有对准五个容器的“标题栏”单击右键才可以弹出这菜单选项 | 使用 `findChild<QWidget*>("ContainerHeader")` 精确锁定标题栏 Widget 并监听信号 | ✅ |
| 2    | 为什么不按照我的要求去实现 | 在 `MainWindow.cpp` 中重写绑定逻辑，移除容器级的信号监听 | ✅ |

## 4. 详细解决方案

### 4.1 核心重构逻辑
在 `MainWindow.cpp` 中，不再将信号绑定到面板容器本身，而是通过面板容器寻找其内部名为 `ContainerHeader` 的子控件。

#### 伪代码实现对比：
```cpp
// --- MainWindow.cpp ---

// [修改点 1] 移除各面板构造函数中的 setContextMenuPolicy(Qt::CustomContextMenu)
// 注意：此步需要在各面板的构造函数中执行，或由 MainWindow 强制覆盖

// [修改点 2] 重新实现 connectPanelMenu
auto connectPanelMenu = [&](QWidget* panel) {
    // 1. 从面板中递归查找名为 "ContainerHeader" 的 Widget
    QWidget* header = panel->findChild<QWidget*>("ContainerHeader");
    
    if (header) {
        // 2. 确保标题栏开启右键菜单策略
        header->setContextMenuPolicy(Qt::CustomContextMenu);
        
        // 3. 仅对标题栏监听信号
        connect(header, &QWidget::customContextMenuRequested, this, [this, header](const QPoint& pos) {
            showPanelContextMenu(header->mapToGlobal(pos));
        });
    } else {
        qWarning() << "[MainWindow] 警告：未能找到面板标题栏句柄，无法绑定右键菜单";
    }
};

// [修改点 3] 对各容器面板逐一应用
connectPanelMenu(m_categoryPanel);
connectPanelMenu(m_navPanel);
connectPanelMenu(m_contentPanel);
connectPanelMenu(m_metaPanel);
connectPanelMenu(m_filterPanel);
```

### 4.2 各面板构造函数调整
为了确保各面板内容区域的右键菜单不受干扰，需要撤销之前在容器层级误加的菜单策略。
- 在 `CategoryPanel.cpp`, `NavPanel.cpp`, `ContentPanel.cpp`, `MetaPanel.cpp`, `FilterPanel.cpp` 的构造函数中，应移除 `setContextMenuPolicy(Qt::CustomContextMenu)`。

## 5. 修改边界声明【红线】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/ui/MainWindow.cpp` (核心绑定逻辑重构)
- [ ] 模块/文件：`src/ui/CategoryPanel.cpp` 等五个面板文件 (移除容器级菜单策略)

**明确禁止越界修改的范围：**
- [ ] 禁止修改面板内部控件（如 `QTreeView`, `QListView`）自身的右键逻辑。
- [ ] 禁止修改 `showPanelContextMenu` 的菜单内容生成逻辑。

## 6. 实现准则与预警【核心】
1. **标识符一致性**：方案高度依赖于 `setObjectName("ContainerHeader")`。已核实所有面板在 `initUi` 中均执行了此命名操作。如果未来新增面板，必须强制遵守此命名规范。
2. **QObject 查找开销**：`findChild` 是递归查找，但在初始化阶段执行一次对性能影响极小。
3. **坐标转换预警**：信号传回的 `pos` 是相对于 `header` 的局部坐标，弹出菜单时必须使用 `header->mapToGlobal(pos)` 转换为屏幕坐标。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|-------------|----------------------|----------------|
| 范围感知 | 功能范围必须与 UI 表现实时对齐 | ✅ (本方案正是为了修复触发范围过大的问题) |
| UI 布局规范 | 标题栏高度固定为 32px | ✅ (所有面板已符合此高度) |

## 8. 待确认事项
- 标签管理视图 (`m_tagManagerView`) 是否也需要支持此右键菜单？其当前实现中也存在名为 `ContainerHeader` 的 Widget，若需要支持，需在 `connectPanelMenu` 调用列表中补全。
