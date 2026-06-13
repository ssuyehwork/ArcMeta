# Analysis & Modification Plan - “解析颜色”功能多选失效分析

## 1. 现状分析 (Current State)
用户反馈在右键菜单中点击“解析颜色”时，即使同时选中了多个文件，系统也仅对其中一个文件执行了解析操作。

### 1.1 代码根因 (Code Root Cause)
在 `src/ui/ContentPanel.cpp` 的 `onCustomContextMenuRequested` 函数中：
*   **输入绑定错误：** `ActionExtractColor` 分支直接引用了在 `switch` 外部定义的局部变量 `path`。
*   **定义域局限：** 该 `path` 变量是通过 `view->indexAt(pos).data(PathRole)` 获取的，它仅代表鼠标**右键点击瞬间所在位置**的单个项目，完全忽略了当前的选中集合（Selection Set）。
*   **缺乏迭代：** 相比于“归类”或“设定星级”逻辑，该分支内部没有使用 `selectionModel()->selectedIndexes()` 进行循环处理。

## 2. 解决方案建议 (Proposed Solutions)

### 2.1 重构任务获取逻辑
必须将“获取目标路径”从单值改为列表：
```cpp
// 建议修改逻辑
case ActionExtractColor: {
    auto indexes = view->selectionModel()->selectedIndexes();
    QStringList pathsToProcess;
    for (const auto& idx : indexes) {
        if (idx.column() == 0) pathsToProcess << idx.data(PathRole).toString();
    }
    // 若点击处未在选区内，回退到点击项
    if (pathsToProcess.isEmpty()) pathsToProcess << path;
    
    // 发起批量解析...
}
```

### 2.2 引入并发批处理引擎
考虑到颜色解析涉及 Shell 缩略图提取和复杂的聚类计算，批量执行时必须考虑性能：
1.  **分发模式：** 使用 `QtConcurrent::run` 配合信号槽，或者 `QRunnable` 任务池。
2.  **UI 反馈：** 若处理文件数超过 5 个，应自动弹出 `BatchProgressDialog` 进度条，防止用户因界面无响应而重复点击。
3.  **环境对齐：** 再次强调，批量处理的后台线程必须初始化 COM 环境（详见 Plan-23），否则批量操作仍会因环境缺失而大面积失败。

### 2.3 局部原子化更新
解析完成后，不应刷新整个视图，而应通过 `MetadataManager::notifyUI(RefreshLevel::PathUpdate, p)` 逐个通知模型更新对应的 `ItemRecord`。

## 3. 结论 (Conclusion)
该问题的本质是**交互逻辑编写时的疏忽**。只需将单路径处理改为选区路径迭代，并配合异步批处理框架，即可实现流畅的多文件颜色解析功能。
