# 行内编辑及全选状态下方向键选择漂移与光标一键定位至基名末尾加固 —— Modification_Plan-96.md

> 状态：已批准，执行中

## 1. 任务背景
在内容面板（Grid/Thumbnail视图）和侧边栏（Tree/Category视图）等行内编辑重命名（F2）状态下，用户按下向上/向下键会使选中项漂移，意外退出编辑；而在全选高亮状态下按向右方向键，光标没有精准定位到文件名基名（Base Name）的末尾，而是尴尬地停留在后缀名点号 `.` 的后面。本方案旨在对所有代理编辑器的按键事件拦截逻辑进行加固重构，从而彻底解决按键冲突与不符合预期的光标定位行为。

## 2. 问题定位
行内编辑时，Qt 会在 Delegate（`GridItemDelegate`, `ThumbnailDelegate`, `TreeItemDelegate`, `CategoryDelegate`）创建编辑器（通常是 `QLineEdit`）并为其安装 `eventFilter`（部分 Delegate 之前甚至没有安装或没有重写 `eventFilter` 成员）。
当用户按下方向键时，
- `Qt::Key_Up`/`Qt::Key_Down` 没有被拦截或没有完全吞噬（有些仅拦截但没有返回 `true`，或者未安装），导致事件泄露给了底层的视图（`QTreeView`/`QListView`/`JustifiedView`），引发了项目行选择变化（Up/Down 游动），使得编辑器失去焦点并被迫关闭销毁。
- `Qt::Key_Left`/`Qt::Key_Right` 在处于全选状态时，默认会让光标移动 to 特定的选择边界（如文件的扩展名分隔点 `.` 之前后），而非一键直达整个输入文本的最前方（位置 0）或基名（不含扩展名的文件名部分）的最末端。

因此，我们需要改写和加固 4 个 Delegate 的 `eventFilter` 和 `createEditor`。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 已经进入了编辑状态，用户按下向上/向下方向键时则不该向上游动选中上方/下方的项目 (对应用户原话) | 在所有行内编辑器的 `eventFilter` 中彻底拦截并吞噬 `Qt::Key_Up`/`Qt::Key_Down`（对应用户原话：“按下向上/向下方向键时则不该向上游动选中上方/下方的项目”），绝对不让其传导给视图。 | ✅ 一致 |
| 2    | 如果用户按下向左/向右方向键，应该将光标定位到名称最前面或最后面，而不是“.”的后面，除非处于非全选状态 (对应用户原话) | 处于 `hasSelectedText()` 状态下，按下向左键光标一键定位到位置 0（对应用户原话：“定位到名称最前面”），按下向右键光标一键定位到文件名基名（不含扩展名部分）的末端、即最后一个点号的前面（对应用户原话：“我指的是文件名，不是后缀名...我指的是基名”），无点号时到文本最末尾。 | ✅ 一致 |

## 4. 详细解决方案

我们需要对以下 4 个 Delegate 进行统一重构加固：

1. **`GridItemDelegate`** (位于 `src/ui/ContentPanel.cpp`):
   - 加固 `GridItemDelegate::eventFilter` 逻辑，识别 `QLineEdit` 的按键事件。
2. **`ThumbnailDelegate`** (位于 `src/ui/ThumbnailDelegate.cpp`):
   - 加固 `ThumbnailDelegate::eventFilter` 逻辑，识别 `QLineEdit` 的按键事件。
3. **`TreeItemDelegate`** (位于 `src/ui/TreeItemDelegate.h`):
   - 之前在 `createEditor` 中未安装事件过滤器，需要修改为 `editor->installEventFilter(const_cast<TreeItemDelegate*>(this))`（对应用户原话：“进入了编辑状态”）。
   - 重写 `bool eventFilter(QObject* obj, QEvent* event) override;` 函数实现键盘过滤逻辑。
4. **`CategoryDelegate`** (位于 `src/ui/CategoryDelegate.h`):
   - 之前在 `createEditor` 中未安装事件过滤器，需要修改为 `editor->installEventFilter(const_cast<CategoryDelegate*>(this))`（对应用户原话：“进入了编辑状态”）。
   - 新增 `bool eventFilter(QObject* obj, QEvent* event) override;` 函数实现键盘过滤逻辑。

### 4.1 统一按键过滤核心算法（开箱即用代码）
针对各 Delegate 过滤器中处理 `QEvent::KeyPress` 类型的按键：
```cpp
bool filterLineEditKeyEvent(QLineEdit* editor, QKeyEvent* keyEvent) {
    if (!editor || !keyEvent) return false;

    int key = keyEvent->key();
    // 彻底拦截向上/向下方向键，不让其传导给底层的视图引发选择游动
    if (key == Qt::Key_Up || key == Qt::Key_Down) {
        keyEvent->accept();
        return true; // 彻底吞噬
    }

    if (key == Qt::Key_Left || key == Qt::Key_Right) {
        if (editor->hasSelectedText()) {
            // 只有在高亮全选（或有选中文字）状态下才进行特殊一键定位
            if (key == Qt::Key_Left) {
                editor->setCursorPosition(0); // 定位到最前面（对应用户原话：“定位到名称最前面”）
            } else {
                QString val = editor->text();
                int lastDot = val.lastIndexOf('.');
                if (lastDot > 0) {
                    editor->setCursorPosition(lastDot); // 精准定位到文件名基名末尾、即扩展名点号的前面（对应用户原话：“我指的是文件名，不是后缀名...基名”）
                } else {
                    editor->setCursorPosition(val.length()); // 如果没有扩展名，定位到最末尾（对应用户原话：“我指的是基名”）
                }
            }
            editor->deselect(); // 必须清除高亮选中，否则后续输入会覆盖整段文本
            keyEvent->accept();
            return true; // 吞噬该事件，避开系统默认光标定位行为
        }
        // 非高亮全选状态下，放行事件，走普通的逐字移动
        return false;
    }
    return false;
}
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/ui/ContentPanel.cpp`
  - 涉及类/函数：`GridItemDelegate::eventFilter`
- [ ] 模块/文件：`src/ui/ThumbnailDelegate.cpp`
  - 涉及类/函数：`ThumbnailDelegate::eventFilter`
- [ ] 模块/文件：`src/ui/TreeItemDelegate.h`
  - 涉及类/函数：`TreeItemDelegate::createEditor`（安装过滤器）, `TreeItemDelegate::eventFilter`（新增重写拦截过滤器）
- [ ] 模块/文件：`src/ui/CategoryDelegate.h`
  - 涉及类/函数：`CategoryDelegate::createEditor`（安装过滤器）, `CategoryDelegate::eventFilter`（新增重写拦截过滤器）

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
