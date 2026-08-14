# 全树自定义文件夹总数统计 —— category-tree-count-refactor.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在侧边栏分类树界面中，自定义分类列表顶部的主标题“文件夹”旁的数字，在初始重绘加载时仅统计了第一级顶级分类数量，而在动态刷新数据时也仅读取了第一级子节点的 rowCount()。当存在深层嵌套的自定义文件夹时，该统计数字无法 100% 准确反映全树自定义文件夹的总和。

本方案旨在重构统计计数方式，确保无论文件夹嵌套有多深，“文件夹”旁的数字始终精准显示系统中全树所有自定义文件夹的总数。

## 2. 问题定位
- **模块：** `src/ui/CategoryModel.cpp`
- **函数 1：** `CategoryModel::refresh()`
  - **位置：** 计算顶级分类计数变量 `userTopCatCount` 部分。
  - **原因：** 代码中仅遍历了满足 `cat.parentId == 0 && cat.kind != CategoryKind::SystemLibrary` 的分类项目，未将嵌套更深的自定义分类（`parentId > 0`）一并计入。
- **函数 2：** `CategoryModel::updateStatistics(const QMap<QString, int>& sysCounts, const QMap<int, int>& catCounts)`
  - **位置：** 匹配到主标题节点 `id == CAT_GROUP_SYS_ID` 时。
  - **原因：** 代码中仅通过 `item->rowCount()` 读取了该标题下的第一级直接子节点数目，未递归累加其深层子孙节点数量，导致动态更新时数字显示不一致。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 0    | Step 1 中确认的"核心问题"：全树自定义文件夹总数统计 | 本方案核心事件名：全树自定义文件夹总数统计 | ✅ |
| 1    | 无论文件夹嵌套有多深，“文件夹”大标题旁的数字就会 100% 准确显示所有自定义文件夹的总和（对应用户原话：“无论文件夹嵌套有多深，‘文件夹’大标题旁的数字就会 100% 准确显示所有自定义文件夹的总和。”） | `refresh` 处循环全量统计非 `SystemLibrary` 分类个数；`updateStatistics` 递归或深度统计全树分类节点总数。 | ✅ |
| 2    | 按照产品标准：全量统计全树所有深度的自定义文件夹总数（对应用户原话：“按照产品标准：全量统计全树所有深度的自定义文件夹总数”） | 使用 `categories` 中 `kind != CategoryKind::SystemLibrary` 的分类进行统计。 | ✅ |
| 3    | 动态更新时，同样显示包含所有深度的子文件夹总数（对应用户原话：“动态更新时，同样显示包含所有深度的子文件夹总数”） | 在 `updateStatistics` 的 `id == CAT_GROUP_SYS_ID` 分支中递归统计子树中类型为 `"category"` 的节点。 | ✅ |

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 修改 `CategoryModel::refresh()`
在初始重绘分类树时，不区分 `parentId`，累加所有非 `SystemLibrary` 项目。

```
<<<<<<< SEARCH
        // 6. 挂载用户自定义分类至“分类”主标题下
        int userTopCatCount = 0;
        for (const auto& cat : categories) {
            int id = cat.id;
            QStandardItem* item = itemMap[id];
            int parentId = cat.parentId;

            if (parentId == 0) {
                if (cat.kind != CategoryKind::SystemLibrary) {
                    userTopCatCount++;
                    if (catGroup) {
                        catGroup->appendRow(item);
                    } else {
                        root->appendRow(item);
                    }
                }
            }
        }

        if (catGroup) {
            catGroup->setText(QString("文件夹 (%1)").arg(userTopCatCount));
            root->appendRow(catGroup);
        }
=======
        // 6. 挂载用户自定义分类至“分类”主标题下
        // 按照产品标准：全量统计全树所有深度的自定义文件夹总数（对应用户原话：“按照产品标准：全量统计全树所有深度的自定义文件夹总数”）
        int totalUserFolderCount = 0;
        for (const auto& cat : categories) {
            if (cat.kind != CategoryKind::SystemLibrary) {
                totalUserFolderCount++;
            }
        }

        for (const auto& cat : categories) {
            int id = cat.id;
            QStandardItem* item = itemMap[id];
            int parentId = cat.parentId;

            if (parentId == 0) {
                if (cat.kind != CategoryKind::SystemLibrary) {
                    if (catGroup) {
                        catGroup->appendRow(item);
                    } else {
                        root->appendRow(item);
                    }
                }
            }
        }

        if (catGroup) {
            catGroup->setText(QString("文件夹 (%1)").arg(totalUserFolderCount));
            root->appendRow(catGroup);
        }
>>>>>>> REPLACE
```

### 4.2 修改 `CategoryModel::updateStatistics()`
在接收到数据动态刷新时，不采用 `item->rowCount()`，而是编写一个递归辅助 Lambda，计算该文件夹大标题节点下所有子孙节点中类型为 `"category"` 节点的总数（或全量统计所有嵌套的有效自定义文件夹节点），实现动态刷新的对等计数。

```
<<<<<<< SEARCH
            if (id == CAT_GROUP_SYS_ID) {
                item->setText(QString("文件夹 (%1)").arg(item->rowCount()));
            } else if (id < 0) { 
=======
            if (id == CAT_GROUP_SYS_ID) {
                // 动态更新时，同样显示包含所有深度的子文件夹总数（对应用户原话：“动态更新时，同样显示包含所有深度的子文件夹总数”）
                std::function<int(QStandardItem*)> countCategories;
                countCategories = [&](QStandardItem* node) -> int {
                    int c = 0;
                    for (int j = 0; j < node->rowCount(); ++j) {
                        QStandardItem* child = node->child(j);
                        if (child->data(TypeRole).toString() == "category") {
                            c++;
                        }
                        if (child->hasChildren()) {
                            c += countCategories(child);
                        }
                    }
                    return c;
                };
                int totalFolders = countCategories(item);
                item->setText(QString("文件夹 (%1)").arg(totalFolders));
            } else if (id < 0) { 
>>>>>>> REPLACE
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [x] 模块/文件：`src/ui/CategoryModel.cpp`
  - `CategoryModel::refresh()` 计算及设置 `catGroup` 标题文字处。
  - `CategoryModel::updateStatistics()` 匹配并更新 `CAT_GROUP_SYS_ID` 分支文字处。

**明确禁止越界修改的范围：**
- [x] `categories` 各项数据的解析逻辑、分类树的级联挂载顺序——不修改。
- [x] 系统逻辑桶节点（“全部数据”、“回收站”等）及其计数加载规则——不修改。

## 6. 实现准则与预警【核心】
- **递归计算类型感知**：在 `updateStatistics()` 的递归 Lambda 中，必须通过检查 `child->data(TypeRole).toString() == "category"` 过滤非分类项（比如防止将来大标题下有可能出现的其他占位或辅助节点被计入文件夹数量）。
- **完全对等数据流**：`refresh()` 静态统计到的非系统库分类数量 `totalUserFolderCount`，应该在不修改数据库和整体树型结构的前提下，通过 `updateStatistics` 中递归得到的 `totalFolders` 保持完美一致。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 输入框清除功能 | 一律使用 Qt 原生 setClearButtonEnabled(true)，不涉及本方案 | ✅ |
| 窗口置顶 | 使用 Win32 原生 SetWindowPos，不涉及本方案 | ✅ |
| 标题栏按钮样式 | 标题栏及按钮颜色规范，不涉及本方案 | ✅ |

## 8. 待确认事项（可选）
暂无。
