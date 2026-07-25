# 重复多媒体及色彩解析清理与兜底重扫描路径加固 —— Modification_Plan-60.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在项目进行重构优化的过程中，我们发现在内容面板中存在“重新解析颜色”和“重新扫描”两个操作。其中，“重新解析颜色”（ActionExtractColor）通过 UI 线程单独另建线程（QtConcurrent::run）进行解析，直接操纵数据库调色板及模型重塑，不仅绕过了统一的 `MediaExtractorPipeline` 后台数据提取管线，其实现残缺且污染了元数据字段；
而手动触发的“重新扫描”（ActionRescan）则是用户在怀疑数据状态受损或未正常完全同步时的核心手动兜底补救出口，其“先置为待处理状态，再启动后台统一提取”的流程可以给用户提供良好的进度视觉反馈。

为了消除多套数据抽取对轮子的重复建构并统一管线，本方案旨在彻底物理根除 UI 中的“重新解析颜色”逻辑并清理冗余代码，同时加固并保护“重新扫描”的现行闭环路径不被破坏。

## 2. 问题定位
- 冗余组件及代码位于 `src/ui/ContentPanel.cpp`。
- `ActionExtractColor` 及其菜单的动作创建、点击分流处理逻辑和后台 COM 环境下的提取实现严重违反了“单一职责原则 (SRP)”，与 `MediaExtractorPipeline` 的通用调色板解析逻辑形成恶劣的重复。
- `ActionRescan` 调用 `MetadataManager::instance().registerItemsAsync(targetPaths, true)`，内部包含：
  - `normalizePath(qp.toStdWString())` 对路径归一化。
  - `ensureActivated(nPath)` 确保缓存项存在。
  - `updateIngestionStatus(nPath, 0)` 将入库状态回重设为 `0` (即进度环恢复到“待处理”的等待动画)，确保了在用户手动强制修复时提供正在重算的信息。
  - 最后放入 `MediaExtractorPipeline::instance().enqueueBatch(stdPaths)` 排队。
  此路径非常清晰健全，属于完全应当予以保留的优秀实现。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 物理删除“重新解析颜色”这一独立分支，不再提供“重新解析颜色”（解析颜色）选项 | 彻底物理删除 `src/ui/ContentPanel.cpp` 中 `ActionExtractColor` 的菜单动作定义、分发分支及相关的批处理解析线程 | ✅ 一致 |
| 2    | “重新扫描”目前调用 `registerItemsAsync` 这条路径本身没有问题，不需要简化 | 明确对 `ActionRescan` 进行保留并保护，不做任何代码和调用的简化 | ✅ 一致 |
| 3    | “进度环短暂显示待处理状态”这个视觉反馈，对“兜底”这个定位来说其实是有意义的，不该省 | 维持在 `registerItemsAsync` 中的 `updateIngestionStatus(nPath, 0)` 逻辑不予移除 | ✅ 一致 |

## 4. 详细解决方案
本方案属于典型的**减法式重构**：

### 4.1 物理删除右键上下文菜单中的“重新解析颜色”选项
在 `src/ui/ContentPanel.cpp` 中，物理擦除以下菜单构建代码：
```cpp
// 需删除的代码片段
bool isManaged = currentIndex.data(ManagedRole).toBool();
if (isManaged) {
    menu.addAction(UiHelper::getIcon("palette", QColor("#EEEEEE"), 18), "重新解析颜色")->setData(ActionExtractColor);
} else {
    menu.addAction(UiHelper::getIcon("palette", QColor("#EEEEEE"), 18), "解析颜色")->setData(ActionExtractColor);
}
```

### 4.2 物理删除 `ActionExtractColor` 的事件响应及提取线程逻辑
在 `src/ui/ContentPanel.cpp` 中，物理擦除 `switch(action)` 中的 `case ActionExtractColor:` 分支，整个大分支完全删除。包括：
- `QModelIndexList selectedRows = view->selectionModel()->selectedRows();`
- 过滤行逻辑、COM 线程池运行初始化、以及在后台调用 `UiHelper::extractPalette` 解析颜色并回写 `MetadataManager::instance().setPalettes` 和 `model->setData` 的行为。

### 4.3 保护及加固 `ActionRescan`（重新扫描）
保留 `case ActionRescan:` 块，不做任何修改。
保留 `MetadataManager::registerItemsAsync` 内的 `updateIngestionStatus(nPath, 0)`，确保其对入库进度环“待处理”动画的即时响应（对应用户原话：“‘进度环短暂显示待处理状态’这个视觉反馈，对‘兜底’这个定位来说其实是有意义的... 建议保留，不要为了简化而去掉”）。

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/ui/ContentPanel.cpp`（行号 2070~2080 与 2305~2390 左右，物理删除 `ActionExtractColor` 入口及后台解析任务分支）

**明确禁止越界修改的范围：**
- [ ] 模块/文件：`src/meta/MetadataManager.cpp` —— `registerItemsAsync` 及 `updateIngestionStatus` 的原有控制及异步处理链条，禁止做任何简化与破坏。
- [ ] 模块/文件：`src/meta/MediaExtractorPipeline.cpp` —— `MediaExtractorPipeline` 的多媒体调色板和通用入库提取链路核心逻辑，禁止修改。

## 6. 实现准则与预警【核心】
1. **防范 COM 环境失效**：虽然我们删除了 `ActionExtractColor` 内部在子线程中通过 `CoInitializeEx(NULL, COINIT_APARTMENTTHREADED)` 初始化 COM 的局部实现，但主提取管线 `MediaExtractorPipeline` 的后台环境依然具备完善的 COM 初始化及销毁支持，此举完全不会影响系统原有的调色板生成。
2. **清理彻底性预警**：在物理移除整个 `ActionExtractColor` 分支时，需仔细核查相关的局部变量（如 `progress` 等）的生命周期和引用。保证代码移除后编译能彻底通过且没有悬空指针。
3. **防止越权行为**：在执行代码修改时，应自检一遍所修改的部分是否越权、越界，防止发生脑补行为而导致浪费时间。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 输入框清除功能 | 一律使用 Qt 原生 setClearButtonEnabled(true)，严禁自定义按钮模拟 | ✅ 本方案不涉及新增或修改输入框清除功能 |
| 窗口置顶 | 一律使用 Win32 原生 SetWindowPos 并配合 SWP_NOSENDCHANGING 标志，严禁使用 Qt 重建 | ✅ 本方案不涉及任何窗口置顶功能 |
| 标题栏按钮样式 | 悬停：#3E3E42（Style::HoverBackground），按下：#4E4E52，严禁 rgba 蒙版 | ✅ 本方案不涉及新标题栏按钮样式 |
| 物理/逻辑源 Focus 对齐 | 所有具备作用域功能执行范围必须与 UI 顶部蓝色 Focus Line 实时对齐 | ✅ 本方案不涉及过滤或搜索作用域改动 |

## 8. 待确认事项（可选）
暂无待确认事项。
