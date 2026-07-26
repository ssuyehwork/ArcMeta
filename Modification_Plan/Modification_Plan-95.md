# 行内编辑及全选状态下方向键选择漂移与光标一键定位修复 —— Modification_Plan-95.md

> 状态：已批准，执行中

## 1. 任务背景
在内容面板和侧边栏分类树等行内编辑重命名（F2）状态下，用户按下向上/向下键会使选中项漂移，意外退出编辑；而在全选状态下按向左/向右方向键，光标会尴尬地停留在点号 `.` 的后面。本方案旨在对所有代理编辑器的按键事件拦截逻辑进行加固重构，从而彻底解决按键冲突与非预期的光标定位行为。

## 2. 问题定位
行内编辑时，Qt 会在 Delegate（`GridItemDelegate`, `ThumbnailDelegate`, `TreeItemDelegate`, `CategoryDelegate`）创建编辑器（通常是 `QLineEdit`）并为其安装 `eventFilter`（或默认使用）。
当用户按下方向键时，
- `Qt::Key_Up`/`Qt::Key_Down` 没有被拦截或没有完全吞噬（有些仅拦截但没有返回 `true`，或者未安装），导致事件泄露给了底层的视图（`QTreeView`/`QListView`/`JustifiedView`），引发了项目行选择变化（Up/Down 游动），使得编辑器失去焦点并被迫关闭销毁。
- `Qt::Key_Left`/`Qt::Key_Right` 在处于全选状态时，默认会让光标移动到特定的选择边界（如文件的扩展名分隔点 `.` 之前后），而非一键直达整个输入文本的最前方（位置 0）或最后方（位置末尾）。

因此，我们需要改写和加固 4 个 Delegate 的 `eventFilter` 和 `createEditor`。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 已经进入了编辑状态，用户按下向上/向下方向键时则不该向上游动选中上方/下方的项目 (对应用户原话) | 在所有行内编辑器的 `eventFilter` 中彻底拦截并吞噬 `Qt::Key_Up`/`Qt::Key_Down`，绝对不让其传导给视图。 | ✅ 一致 |
| 2    | 如果用户按下向左/向右方向键，应该将光标定位到名称最前面或最后面，而不是“.”的后面，除非处于非全选状态 (对应用户原话) | 处于 `hasSelectedText()` 状态（全选）下，按向左键精准定位到光标位置 `0` 并清除选中；按向右键精准定位到整个文本最末尾（`text().length()`）并清除选中，吞噬按键事件；非全选状态下放行默认逐字移动。 | ✅ 一致 |

## 4. 详细解决方案

我们需要对以下 4 个 Delegate 的 `eventFilter` 进行统一重构加固：

1. **`GridItemDelegate::eventFilter`** (位于 `src/ui/ContentPanel.cpp`)
2. **`ThumbnailDelegate::eventFilter`** (位于 `src/ui/ThumbnailDelegate.cpp`)
3. **`TreeItemDelegate`** 的 `eventFilter` (位于 `src/ui/TreeItemDelegate.h`)：此前未实现 `eventFilter` 成员，也未给编辑器安装事件过滤器。需要重写 `createEditor` 和 `eventFilter`。
4. **`CategoryDelegate`** 的 `eventFilter` (位于 `src/ui/CategoryDelegate.h`)：此前未实现 `eventFilter` 成员，也未给编辑器安装事件过滤器。需要重写 `createEditor` 和 `eventFilter`。

### 4.1 统一按键过滤核心算法（开箱即用代码）
```cpp
bool filterLineEditKeyEvent(QLineEdit* editor, QKeyEvent* keyEvent) {
    if (!editor || !keyEvent) return false;

    int key = keyEvent->key();
    if (key == Qt::Key_Up || key == Qt::Key_Down) {
        keyEvent->accept();
        return true; // 彻底吞噬，不让 View 漂移
    }

    if (key == Qt::Key_Left || key == Qt::Key_Right) {
        if (editor->hasSelectedText()) {
            // 只有在全选或有选中文字的高亮状态下才特殊处理
            if (key == Qt::Key_Left) {
                editor->setCursorPosition(0);
            } else {
                editor->setCursorPosition(editor->text().length());
            }
            editor->deselect(); // 清除全选高亮状态
            keyEvent->accept();
            return true; // 吞噬该事件，不执行 QLineEdit 默认的“点号后面”定位
        }
        // 非全选高亮状态下：放行，走系统普通的逐字移动
        return false;
    }
    return false;
}
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/ui/ContentPanel.cpp`
  - 涉及类/函数：`GridItemDelegate::createEditor`（安装过滤器）, `GridItemDelegate::eventFilter`（加固拦截）
- [ ] 模块/文件：`src/ui/ThumbnailDelegate.cpp`
  - 涉及类/函数：`ThumbnailDelegate::eventFilter`（加固拦截）
- [ ] 模块/文件：`src/ui/TreeItemDelegate.h`
  - 涉及类/函数：`TreeItemDelegate::createEditor`（安装过滤器）, `TreeItemDelegate::eventFilter`（新增拦截过滤器）
- [ ] 模块/文件：`src/ui/CategoryDelegate.h`
  - 涉及类/函数：`CategoryDelegate::createEditor`（安装过滤器）, `CategoryDelegate::eventFilter`（新增拦截过滤器）

**明确禁止越界修改的范围：**
- [ ] 内容面板中的滚轮缩放、双向同步或元数据编辑逻辑——不修改
- [ ] 侧边栏分类树及密码解锁校验逻辑——不修改

## 6. 实现准则与预警【核心】
1. **统一为 QLineEdit 安装 eventFilter**：`TreeItemDelegate` 和 `CategoryDelegate` 在创建编辑器时，必须确保调用 `editor->installEventFilter(const_cast<TreeItemDelegate*>(this))`（或其对应的子类指针），否则它们将无法捕捉到键盘事件。
2. **事件所有权与返回值**：若 `eventFilter` 返回 `true`，代表我们已经将此事件拦截并完全消费，Qt 将不会再将其分发给 `QLineEdit` 本身或者任何父级视图（`QAbstractItemView`），从而能够一键阻止选择漂移。
3. **精准判定 hasSelectedText()**：该状态只有在行内编辑器初次启动高亮全选（或用户执行 Ctrl+A）时才生效，如果是用户已经在输入并处于常规编辑态时（无选中字），向左/向右方向键能顺畅地正常移动光标。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 键盘导航事件分流 | 拦截并优化键盘快捷操作行为时，不能影响正常的输入流及普通的移动 | ✅ 符合。非高亮全选状态下对 Left/Right 进行了放行，保证逐字输入和移动体验完全一致。 |

## 8. 待确认事项（可选）
（无）
