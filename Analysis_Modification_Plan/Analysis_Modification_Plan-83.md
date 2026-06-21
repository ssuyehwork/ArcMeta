# “回归未分类”操作的撤销支持实现 —— Analysis_Modification_Plan-83.md

## 1. 任务背景
在内容面板的右键菜单中，用户可以选择将项目“回归未分类”。该操作目前被实现为物理删除项目在数据库中的所有分类关联。由于缺乏撤销（Undo）支持，一旦用户误操作，原本辛苦维护的多维分类关系将永久丢失，属于高风险逻辑缺陷。

## 2. 问题定位

### 2.1 根因分析
1.  **TODO 遗留**：`ContentPanel.cpp` 在处理 `catId == -2` 时明确标注了撤销支持缺失。
2.  **Command 模型单一**：现有的 `CategorizeCommand` 仅支持单个分类的增删，无法描述“批量切断并批量恢复”的原子操作。
3.  **缺乏预备份**：执行删除前未先查询并记录该项目当前所属的分类集合。

### 2.2 涉及文件
- `src/core/BasicCommands.h`：需新增 `BulkUncategorizeCommand`。
- `src/ui/ContentPanel.cpp`：修改右键菜单处理逻辑，对接新指令。
- `src/meta/CategoryRepo.h/cpp`：需提供获取项目所属全部分类 ID 的接口。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 回归未分类操作不可撤销 | 实现 `BulkUncategorizeCommand` 并压入 Undo 栈 | ✅ |
| 2    | 该操作不可逆是高风险的 | 在删除前先进行数据备份，撤销时全量恢复关联 | ✅ |

## 4. 详细解决方案

### 4.1 新增批量撤销指令
在 `BasicCommands.h` 中定义 `BulkUncategorizeCommand`：

```cpp
class BulkUncategorizeCommand : public ActionCommand {
public:
    BulkUncategorizeCommand(const QString& path, const std::string& fid, const std::vector<int>& oldCatIds)
        : m_path(path), m_fid(fid), m_oldCatIds(oldCatIds) {}

    void undo() override {
        for (int catId : m_oldCatIds) {
            CategoryRepo::addItemToCategory(catId, m_fid, m_path.toStdWString());
        }
        MetadataManager::instance().notifyCategoryCountChanged();
    }

    void redo() override {
        CategoryRepo::removeAllCategories(m_fid);
        MetadataManager::instance().notifyCategoryCountChanged();
    }

    QString description() const override { return "回归未分类"; }
    // ... affectsPath ...
private:
    QString m_path;
    std::string m_fid;
    std::vector<int> m_oldCatIds;
};
```

### 4.2 提供关联查询接口
在 `CategoryRepo` 中实现：
```cpp
// 根据 File ID 获取该项目所属的所有分类 ID
static std::vector<int> getItemCategoryIds(const std::string& fid);
```

### 4.3 完善 ContentPanel 逻辑
在 `case ActionCategorize` 的 `catId == -2` 分支中：
1.  **先备份**：调用 `CategoryRepo::getItemCategoryIds(fid)`。
2.  **再执行**：调用 `CategoryRepo::removeAllCategories(fid)`。
3.  **压栈**：`UndoManager::instance().pushCommand(std::make_unique<BulkUncategorizeCommand>(...))`。

## 5. 修改边界声明【红线】

**本次方案涉及范围：**
- [ ] `ActionCommand` 体系的扩展。
- [ ] `ContentPanel` 的归类处理分支。

**明确禁止越界修改的范围：**
- [ ] 禁止修改 `CategoryRepo::removeAllCategories` 的底层原子性。
- [ ] 禁止修改数据库中关联表的唯一性约束。

## 6. 实现准则与预警【核心】

1.  **空集合处理**：如果项目本身就处于“未分类”状态（`oldCatIds` 为空），则无需产生 `Command` 以节省撤销栈空间。
2.  **时序一致性**：必须在物理执行 `removeAllCategories` **之前**获取 ID 列表。
3.  **UI 同步**：撤销操作执行后，必须调用 `notifyCategoryCountChanged` 确保侧边栏的数字统计实时更新。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|-------------|----------------------|----------------|
| 撤销系统 | 物理与逻辑操作需同步记录 | ✅ 方案通过预备份 ID 集合解决了逻辑丢失问题 |
| 分类管理 | 一对多关联需严谨维护 | ✅ 批量恢复逻辑确保了关联的完整性 |

## 8. 待确认事项
- 如果在撤销前，某个原始分类被用户删除了，撤销逻辑应如何处理？（本方案建议：忽略已失效的分类 ID）。
