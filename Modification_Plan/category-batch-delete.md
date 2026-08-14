# 侧边栏分类批量级联删除修复 —— category-batch-delete.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在目前的侧边栏分类树（`CategoryPanel`）多选机制下，当用户选中多个分类后执行“删除”，实际结果却常常只删除了其中一个，无法实现预期的多选批量删除和级联深层子分类删除。

经深度排查，导致此 Bug 的根源有两处：
1. **右键点击清空多选 Bug**：在右键菜单弹出逻辑（`customContextMenuRequested`）中，系统无条件调用了 `m_categoryTree->setCurrentIndex(proxyIndex)`。在多选（ExtendedSelection）状态下，该调用会直接重置当前的多选，清空所有其他选中项，导致右键时实际上只剩一个选中项。
2. **Proxy-to-Source 索引查询 Bug**：在 `onDeleteCategory` 的递归收集函数 `collectIds` 中，系统直接将 Proxy Model 索引当作参数去查询 Source Model `m_categoryModel->rowCount(index)`，由于模型类型不认识，导致 rowCount 恒返回 0，彻底切断了对子孙分类的深层递归收集。

## 2. 问题定位
- **模块一：** `src/ui/CategoryPanel.cpp` 中的右键拦截机制。
  - 需要在 `customContextMenuRequested` 回调中，检查右键指针所在 index 是否已在选中列表中。若已在，则禁止重置，保全多选状态。
- **模块二：** `src/ui/CategoryPanel.cpp` 中的 `onDeleteCategory`。
  - 需要在收集 `selectedRows` 时，在第一时序将 Proxy Index 映射转换为 Source Index，使整个递归对账函数 `collectIds` 运行在 Source Index 的无损索引上。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 0    | Step 1 中确认 of "核心问题"：多选批量删除 | 本方案核心事件名：侧边栏分类批量级联删除修复 | ✅ |
| 1    | 当我在侧边栏选中多个之后，再来执行“删除”，结果只能删除其中一个，没有支持批量删除，显然存在傻逼逻辑架构。（对应用户原话：“当我在侧边栏选中多个之后，再来执行‘删除’，结果只能删除其中一个，没有支持批量删除，显然存在傻逼逻辑架构”） | 重写右键选中拦截，并且在批量删除时先转换 Proxy-to-Source，使 `collectIds` 完美工作 | ✅ |

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 修改 `src/ui/CategoryPanel.cpp` 弹出右键菜单处
增加防多选清空的右键安全保护拦截机制：

```
<<<<<<< SEARCH
    // 2026-05-27 物理加固：补全 this 上下文
    connect(m_categoryTree, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QModelIndex proxyIndex = m_categoryTree->indexAt(pos);
        QModelIndex index = m_proxyModel->mapToSource(proxyIndex);
        
        // 2026-03-xx 按照用户要求：实现右键点击即选中，解决“分类与其子分类”交互一致性问题
        if (proxyIndex.isValid()) {
            m_categoryTree->setCurrentIndex(proxyIndex);
        }
=======
    // 2026-05-27 物理加固：补全 this 上下文
    connect(m_categoryTree, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QModelIndex proxyIndex = m_categoryTree->indexAt(pos);
        QModelIndex index = m_proxyModel->mapToSource(proxyIndex);
        
        // 2026-03-xx 按照用户要求：实现右键点击即选中，解决“分类与其子分类”交互一致性问题
        // 多选防护：若右键所在的节点已在当前多选集合中，绝对禁止重置，以保全批量删除操作（对应用户原话：“当我在侧边栏选中多个之后，再来执行‘删除’，结果只能删除其中一个”）
        if (proxyIndex.isValid()) {
            if (!m_categoryTree->selectionModel()->isSelected(proxyIndex)) {
                m_categoryTree->setCurrentIndex(proxyIndex);
            }
        }
>>>>>>> REPLACE
```

### 4.2 修改 `src/ui/CategoryPanel.cpp` 中的 `onDeleteCategory()`
彻底重构 `collectIds` 为 Source 级对账删除：

```
<<<<<<< SEARCH
    QSet<int> idsToDelete;
    
    // 递归收集分类及其所有子分类 ID 的辅助函数
    std::function<void(const QModelIndex&)> collectIds;
    collectIds = [&](const QModelIndex& index) {
        QString type = index.data(TypeRole).toString();
        int id = index.data(IdRole).toInt();
        
        if (type == "category" && id > 0) {
            idsToDelete.insert(id);
            // 递归收集子分类
            for (int i = 0; i < m_categoryModel->rowCount(index); ++i) {
                collectIds(m_categoryModel->index(i, 0, index));
            }
        }
    };

    for (const QModelIndex& index : selectedRows) {
        collectIds(index);
    }
=======
    QSet<int> idsToDelete;
    
    // 递归收集分类及其所有子分类 ID 的辅助函数。在第一时序完全运行于 Source Index 的无损索引之上，彻底修复 rowCount() 失效导致的级联删除中断
    std::function<void(const QModelIndex&)> collectIds;
    collectIds = [&](const QModelIndex& srcIndex) {
        if (!srcIndex.isValid()) return;
        QString type = srcIndex.data(TypeRole).toString();
        int id = srcIndex.data(IdRole).toInt();
        
        if (type == "category" && id > 0) {
            idsToDelete.insert(id);
            // 递归收集子分类
            for (int i = 0; i < m_categoryModel->rowCount(srcIndex); ++i) {
                collectIds(m_categoryModel->index(i, 0, srcIndex));
            }
        }
    };

    for (const QModelIndex& proxyIdx : selectedRows) {
        QModelIndex srcIndex = m_proxyModel->mapToSource(proxyIdx);
        collectIds(srcIndex);
    }
>>>>>>> REPLACE
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [x] `src/ui/CategoryPanel.cpp` —— 重构 `customContextMenuRequested` 中的选中保护拦截。
- [x] `src/ui/CategoryPanel.cpp` —— 重构 `onDeleteCategory` 中的 `collectIds` 无损索引映射。

**明确禁止越界修改的范围：**
- [x] 分类的数据库后台批量删除逻辑（`CategoryRepo::remove`）—— 不修改，不产生破坏性重构。

## 6. 实现准则与预警【核心】
- **完美避免 Proxy 穿透**：强制在递归开始前将 Proxy Index 映射回 `m_proxyModel->mapToSource(proxyIdx)`。这避开了 Qt 所有由于代理模型导致树深度计数缩水的崩溃隐患，达到工业级的优雅整洁。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 输入框清除功能 | 一律使用 Qt 原生 setClearButtonEnabled(true)，不涉及本方案 | ✅ |
| 窗口置顶 | 一律使用 Win32 原生 SetWindowPos，不涉及本方案 | ✅ |
| 标题栏按钮样式 | 标题栏及按钮颜色规范，不涉及本方案 | ✅ |

## 8. 待确认事项（可选）
无。
