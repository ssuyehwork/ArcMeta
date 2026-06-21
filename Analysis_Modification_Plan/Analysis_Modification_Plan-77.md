# 侧边栏布局菜单触发范围限制优化 —— Analysis_Modification_Plan-77.md

## 1. 任务背景
用户反馈在元数据面板（MetaPanel）的色块（ColorPill）上右键单击时，会意外弹出侧边栏布局控制菜单（显示/隐藏面板）。该菜单本应仅在对准面板标题栏（ContainerHeader）右键时触发。目前该行为导致用户在尝试进行“右键快速搜索颜色”时受到干扰，严重影响交互体验。

## 2. 问题定位

### 2.1 根因分析
1.  **全局绑定范围过大**：在 `src/ui/MainWindow.cpp` 的 `setupSplitters()` 函数中，`connectPanelMenu` 逻辑将 `customContextMenuRequested` 信号绑定到了面板实例本身（如 `m_metaPanel`）。由于面板是其内部所有子控件的父容器，未被拦截的右键事件会通过事件冒泡机制传导至面板。
2.  **菜单策略冲突**：各面板（如 `MetaPanel`）在构造函数中设置了 `setContextMenuPolicy(Qt::CustomContextMenu)`。当右键事件传导至面板时，面板会发射信号，触发 `MainWindow` 弹出的布局菜单。
3.  **子控件屏蔽不彻底**：`ColorPill` 在 `mousePressEvent` 中虽然通过 `Qt::RightButton` 触发了搜索信号，但未显式调用 `event->accept()`，且未重写 `contextMenuEvent`。这使得 Qt 在某些交互时序下仍会产生上下文菜单事件并传递给父容器。

### 2.2 涉及文件
- `src/ui/MainWindow.cpp`：菜单连接逻辑。
- `src/ui/MetaPanel.h/cpp`：标题栏导出与子控件事件处理。
- `src/ui/CategoryPanel.h/cpp`、`src/ui/NavPanel.h/cpp`、`src/ui/ContentPanel.h/cpp`、`src/ui/FilterPanel.h/cpp`：标题栏导出。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 布局菜单必须对准容器标题单击右键才可以弹出 | 将 `customContextMenuRequested` 连接对象由“面板”改为“标题栏” | ✅ |
| 2    | 对准色块右键不应弹出该菜单 | 限制触发范围并增强 `ColorPill` 的右键屏蔽 | ✅ |
| 3    | 保持原有的右键搜索功能 | 保留 `ColorPill` 的信号发射逻辑 | ✅ |

## 4. 详细解决方案

### 4.1 面板架构调整（导出标题栏句柄）
为了让 `MainWindow` 能够精准绑定信号，各面板类需显式暴露其标题栏（ContainerHeader）控件。

1.  **修改各面板类定义（.h）**：
    - 在 `CategoryPanel`、`NavPanel`、`ContentPanel`、`MetaPanel`、`FilterPanel` 类中增加私有成员 `QWidget* m_headerWidget = nullptr;`。
    - 增加公共访问接口：`QWidget* headerWidget() const { return m_headerWidget; }`。

2.  **修改各面板实现（.cpp）**：
    - 在 `initUi` 或 `buildGroup` 等创建 `setObjectName("ContainerHeader")` 的位置，将该控件指针赋值给 `m_headerWidget`。
    - 确保 `m_headerWidget` 调用 `setContextMenuPolicy(Qt::CustomContextMenu)`。

### 4.2 修正 MainWindow 绑定逻辑
在 `src/ui/MainWindow.cpp` 中修改 `connectPanelMenu` 的实现。

```cpp
// src/ui/MainWindow.cpp

// 修改前的逻辑
auto connectPanelMenu = [&](QWidget* panel) {
    connect(panel, &QWidget::customContextMenuRequested, this, [this, panel](const QPoint& pos) {
        showPanelContextMenu(panel->mapToGlobal(pos));
    });
};

// 修改后的逻辑（需先将各面板类转换或要求面板提供接口）
auto connectPanelMenu = [&](auto* panel) {
    if (panel && panel->headerWidget()) {
        QWidget* header = panel->headerWidget();
        // 关键：将信号监听目标从 panel 转移到 header
        connect(header, &QWidget::customContextMenuRequested, this, [this, header](const QPoint& pos) {
            showPanelContextMenu(header->mapToGlobal(pos));
        });
    }
};
```

### 4.3 物理隔离 ColorPill 的右键传导
在 `src/ui/MetaPanel.cpp` 中增强 `ColorPill` 的防护：

1.  **mousePressEvent 强化**：
    ```cpp
    void ColorPill::mousePressEvent(QMouseEvent* event) {
        if (event->button() == Qt::RightButton) {
            event->accept(); // 显式标记已处理，阻止父级处理
            emit colorSelected(m_color);
            return;
        }
        // ... 其他逻辑
    }
    ```
2.  **增加 contextMenuEvent 拦截**：
    ```cpp
    void ColorPill::contextMenuEvent(QContextMenuEvent* event) {
        event->accept(); // 彻底切断上下文菜单事件的冒泡链路
    }
    ```

## 5. 修改边界声明【红线】

**本次方案涉及范围：**
- [ ] `src/ui/MainWindow.cpp` 的 `setupSplitters` 内部。
- [ ] `CategoryPanel`, `NavPanel`, `ContentPanel`, `MetaPanel`, `FilterPanel` 的头文件及初始化函数。
- [ ] `ColorPill` 组件的事件处理。

**明确禁止越界修改的范围：**
- [ ] 禁止修改面板菜单的内容（即 `populatePanelMenu`）。
- [ ] 禁止移除面板现有的 `setObjectName("ContainerHeader")` 逻辑。

## 6. 实现准则与预警【核心】

1.  **类型转换预警**：在 `MainWindow.cpp` 中，由于 `m_categoryPanel` 等成员是具体类指针，修改 `connectPanelMenu` 为模板或手动逐个绑定时，需确保已包含对应的头文件。
2.  **对象名检查**：确保所有面板的标题栏都正确设置了 `setObjectName("ContainerHeader")`，否则 `headerWidget()` 可能返回空导致绑定失败。
3.  **内存安全**：标题栏控件的生命周期随面板销毁，无需特殊管理，但需确保在 `setupSplitters` 调用 `connectPanelMenu` 时，面板的 `initUi` 已执行且 `m_headerWidget` 已赋值。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|-------------|----------------------|----------------|
| UI 布局 | TitleBar/ContainerHeader 需作为物理切割线 | ✅ 方案强化了 Header 的交互地位 |
| 右键菜单 | 布局控制需持久化且统一 | ✅ 方案仅限制触发范围，不改变存储逻辑 |
| 事件流 | 禁止无序事件冒泡 | ✅ 方案通过精准绑定和显式 accept() 解决了冒泡冲突 |

## 8. 待确认事项
- `ContentPanel` 的标题栏 `m_titleBar` 内部包含多个按钮（列表/网格切换等），右键点击这些按钮时是否也应弹出布局菜单？（方案默认包含整个 `ContainerHeader` 区域）。
