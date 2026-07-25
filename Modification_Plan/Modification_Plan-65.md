# 彻底物理根除分类右键菜单归类到此分类选项方案 —— Modification_Plan-65.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在目前的侧边栏分类面板 `CategoryPanel` 的右键菜单中，当点击某个分类节点时，顶部会展现一个带有蓝色文件夹图标的“归类到此分类”选项，其在后台调用 `CategoryPanel::onClassifyToCategory` 写入配置项进行状态标记。用户明确指示，需将“归类到此分类”选项彻底从底根除，不可保留。

## 2. 问题定位
- “归类到此分类”选项声明、定义和绑定位置：
  - **菜单项构建**：`src/ui/CategoryPanel.cpp` 中的 `CategoryPanel::showContextMenu()`
    ```cpp
    menu.addAction(UiHelper::getIcon("folder_filled", PrimaryBlue, 18), "归类到此分类", this, &CategoryPanel::onClassifyToCategory);
    ```
  - **槽函数声明**：`src/ui/CategoryPanel.h` 中的 `void onClassifyToCategory();`
  - **槽函数实现**：`src/ui/CategoryPanel.cpp` 中的整个 `void CategoryPanel::onClassifyToCategory()` 逻辑。
- 目标：将上述三处代码完全删除并清理，实现物理级根除，不留死角。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 我需要将分类右键菜单中的”归类到此分类“选项彻底根除，不可保留 (对应用户原话) | 彻底删除并清理 `CategoryPanel.cpp/h` 中所有“归类到此分类”的菜单 Action 构建逻辑，以及整个 `onClassifyToCategory` 的头文件声明与源文件函数实现，拒绝代码残留。 | ✅ |

## 4. 详细解决方案

### 4.1 清理 `CategoryPanel.h` 头文件
删除第 77 行附近关于 `onClassifyToCategory()` 成员函数的生命声明。

### 4.2 清理 `CategoryPanel.cpp` 构造与右键菜单绑定
删除 `CategoryPanel::showContextMenu` 函数中关于 `onClassifyToCategory` 的右键 Action 添加段落：

```cpp
<<<<<<< SEARCH
                // [Plan-6] 如果已经设定了分类
                if (id > 0) {
                    menu.addAction(UiHelper::getIcon("folder_filled", PrimaryBlue, 18), "归类到此分类", this, &CategoryPanel::onClassifyToCategory);
                    menu.addSeparator();
                }
=======
                // [Plan-6] 如果已经设定了分类 （已物理删除“归类到此分类”选项）
                if (id > 0) {
                    menu.addSeparator();
                }
>>>>>>> REPLACE
```

### 4.3 物理删除源文件函数实现
彻底移除 `CategoryPanel.cpp` 中的整个 `void CategoryPanel::onClassifyToCategory()` 函数实现段（第 525-543 行附近）。

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/ui/CategoryPanel.h` (清理 `onClassifyToCategory` 函数声明)
- [ ] 模块/文件：`src/ui/CategoryPanel.cpp` (清除“归类到此分类”Action创建及对应的函数实现)

**明确禁止越界修改的范围：**
- [ ] 模块/文件：`src/ui/CategoryModel.cpp` —— 不修改

## 6. 实现准则与预警【核心】
1. **彻底物理擦除**：除右键 Action 移除外，必须确包对应的 `onClassifyToCategory` 槽函数实现和头文件声明被物理删除，杜绝代码中留存无用死代码或未引用槽函数造成的项目架构污染。
2. **编译验证防范**：删除本项后，不影响任何其他正常菜单（如新建、重命名、删除及全新设计的色块快捷条）的构建与显示。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 代码死区清理 | 无用死代码、遗留冗余函数应彻底根除物理清理，杜绝残留。 | ✅ 符合。本方案将彻底物理根除遗留方法及绑定，完美符合要求。 |

## 8. 待确认事项（可选）
（无）
