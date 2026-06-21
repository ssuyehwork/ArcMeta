# 内容面板右键菜单逻辑优化与批量扫描支持 —— Analysis_Modification_Plan-80.md

## 1. 任务背景
用户反馈内容面板（ContentPanel）的右键菜单存在多项逻辑不合理之处：多选时显示单选专用的“重命名”、未区分已入库项目的“重新扫描”、以及扫描入库不支持批量操作。这些问题严重影响了重度管理场景下的交互效率。

## 2. 问题定位

### 2.1 根因分析
1.  **菜单构建缺乏计数判定**：`ActionRename` 动作在 `onItem` 为真时直接添加，未检查当前选中的项目总数。
2.  **入库状态未感知**：构建“扫描入库”菜单项时，仅检查了文件类型（文件夹或非图像），未读取数据模型中的 `ManagedRole` 状态。
3.  **处理函数路径采集单一**：在 `switch (action)` 的 `ActionAddToCategory` 分支中，程序仅使用了右键点击位置对应的 `path` 变量，忽略了其他已选中的项目。

### 2.2 涉及文件
- `src/ui/ContentPanel.cpp`：右键菜单构建逻辑及响应逻辑。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 多选时不该出现“重命名” | 构建菜单时增加 `selectedCount <= 1` 判定 | ✅ |
| 2    | 如果选中的项目已入库，那么应该显示“重新扫描入库” | 读取 `currentIndex.data(ManagedRole)` 并动态设置菜单文本 | ✅ |
| 3    | 多选项目时，扫描入库必须支持批量 | 在处理分支中遍历 `selectedIndexes` 并调用批量导入接口 | ✅ |

## 4. 详细解决方案

### 4.1 菜单构建逻辑优化
修改 `src/ui/ContentPanel.cpp` 中的 `onCustomContextMenuRequested` 函数：

1.  **入库状态判定**：
    ```cpp
    bool isManaged = currentIndex.data(ManagedRole).toBool();
    QString scanText = isManaged ? "重新扫描入库" : "扫描入库";
    ```
2.  **动态菜单项添加**：
    ```cpp
    // [扫描入库区]
    if (isFolder || !isGraphic) {
        menu.addAction(UiHelper::getIcon("add", QColor("#FF8C00"), 18), scanText)->setData(ActionAddToCategory);
    }

    // ... 其他逻辑 ...

    // [通用编辑区]
    if (selectedCount <= 1) { // 仅单选时显示
        menu.addAction("重命名")->setData(ActionRename);
    }
    ```

### 4.2 处理函数批量化改造
修改 `switch (action)` 中的 `ActionAddToCategory` 分支：

```cpp
case ActionAddToCategory: {
    QStringList paths;
    auto indexes = view->selectionModel()->selectedIndexes();
    for (const auto& idx : indexes) {
        if (idx.column() == 0) {
            QString p = idx.data(PathRole).toString();
            if (!p.isEmpty()) paths << p;
        }
    }

    // 降级保护：如果由于某种原因 paths 为空，则回退到当前点击项
    if (paths.isEmpty() && !path.isEmpty()) paths << path;

    if (!paths.isEmpty()) {
        // 调用统一导入中枢（已天然支持 QStringList）
        ImportHelper::importPaths(paths, 0, this);
    }
    break;
}
```

## 5. 修改边界声明【红线】

**本次方案涉及范围：**
- [ ] `src/ui/ContentPanel.cpp` 的右键菜单逻辑。

**明确禁止越界修改的范围：**
- [ ] 禁止修改 `ImportHelper::importPaths` 的内部实现。
- [ ] 禁止修改批量重命名（`ActionBatchRename`）的触发逻辑。

## 6. 实现准则与预警【核心】

1.  **Role 准确性**：确保使用的 `ManagedRole` 能正确反映项目在数据库中的持久化状态（通常由 `MetadataManager` 注入）。
2.  **选择模型同步**：在采集路径时，必须遍历 `view->selectionModel()->selectedIndexes()` 且限定 `column() == 0`，防止因多列选择导致的路径重复采集。
3.  **UI 反馈**：批量扫描由于是异步过程，需确保 `ImportHelper` 能提供足够的进度反馈（当前架构已具备）。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|-------------|----------------------|----------------|
| UI 交互规则 | 功能按钮在非 nav 模式下需禁用 | ✅ 方案仅涉及右键菜单文本与路径采集逻辑 |
| 批量操作规范 | 批量操作需支持 Undo/进度展示 | ✅ `ImportHelper` 已封装上述能力 |

## 8. 待确认事项
- 无。
