# 选中分类按下F2进入行内编辑重命名方案 —— Modification_Plan-63.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在目前的侧边栏分类面板 `CategoryPanel` 中，用户若需对某个选中的分类、物理文件夹或文件项进行重命名，只能通过右键点击并选择“重命名分类”这一单一交互入口。这极不符合桌面级文件管理器的典型交互直觉。用户期望：在选中某个分类并按下键盘的 `F2` 键时，也同样能够流畅地进入行内编辑重命名状态。

## 2. 问题定位
- 侧边栏分类树组件 `m_categoryTree` 在 `CategoryPanel::initUi` 中安装了 `CategoryPanel` 的事件过滤器：
  ```cpp
  m_categoryTree->installEventFilter(this);
  ```
- 键盘事件都在 `CategoryPanel::eventFilter` 方法中进行集中物理拦截。例如目前已实现了对 `Delete` 键和 `Ctrl+A` 的过滤与拦截处理。
- 当接收到 `F2` 键盘信号时，我们只需将拦截分发至现成的重命名处理方法：
  ```cpp
  CategoryPanel::onRenameCategory();
  ```
- 对应的物理路径为：
  - `src/ui/CategoryPanel.cpp` 中的 `CategoryPanel::eventFilter` 函数。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 我期望在选中某个分类并按下F2之后同样进入行内编辑状态 (对应用户原话) | 在 `CategoryPanel::eventFilter` 中的 `KeyPress` 处理流程里拦截 `Qt::Key_F2`。当触发 F2 且当前焦点/对象在分类树上时，调用 `onRenameCategory()` 进入行内重命名。 | ✅ |

## 4. 详细解决方案

在 `src/ui/CategoryPanel.cpp` 中的 `CategoryPanel::eventFilter` 键盘按键事件判断区域，追加对 `Qt::Key_F2` 按键的捕获。

具体拦截分发逻辑设计如下：

```cpp
bool CategoryPanel::eventFilter(QObject* obj, QEvent* event) {
    // ... 前置事件过滤保持不变 ...

    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);

        // 1. 禁用 Ctrl+A 全选 (保持既有)
        if (obj == m_categoryTree && keyEvent->modifiers() == Qt::ControlModifier && keyEvent->key() == Qt::Key_A) {
            return true;
        }

        // 2. 支持 Delete 键物理删除选中分类 (保持既有)
        if (obj == m_categoryTree && keyEvent->key() == Qt::Key_Delete) {
            onDeleteCategory();
            return true;
        }

        // 3. 新增支持：按下 F2 键，同步进入行内编辑状态 (对应用户原话："我期望在选中某个分类并按下F2之后同样进入行内编辑状态")
        if (obj == m_categoryTree && keyEvent->key() == Qt::Key_F2) {
            onRenameCategory();
            return true;
        }

        // ... 后置保持不变 ...
    }
    return QFrame::eventFilter(obj, event);
}
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/ui/CategoryPanel.cpp` (仅在 `eventFilter` 内追加 `Qt::Key_F2` 拦截，对齐交互规范)

**明确禁止越界修改的范围：**
- [ ] 模块/文件：`src/ui/CategoryPanel.h` —— 不修改成员结构
- [ ] 模块/文件：`src/ui/CategoryModel.cpp` —— 不修改底层命名更新逻辑

## 6. 实现准则与预警【核心】
1. **防止事件冒泡**：在拦截 `Qt::Key_F2` 并在成功执行 `onRenameCategory()` 后，必须返回 `true`。阻止此事件继续传递给基类，防止引发其他默认按键行为。
2. **状态验证**：行内编辑动作会自动触发 delegate 创建编辑控件。由于仅限在分类树上有焦点时生效，对 `obj == m_categoryTree` 的断言保护是必要的。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 新增快捷键行为 | 修改代码必须结合上下文来修改代码，防止发生编译错误，做到开箱即用。 | ✅ 符合。直接复用已有的重命名函数 `onRenameCategory()`，逻辑清晰，无编译隐患。 |

## 8. 待确认事项（可选）
（无）
