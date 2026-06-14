# Analysis_Modification_Plan-38.md

## 元数据面板标签绑定失效与侧边栏计数异常排查分析

针对用户提出的“多选项目绑定标签未持久化”以及“侧边栏标签管理计数为 0”的问题，经过深度逻辑排查，确认存在以下架构实现缺陷：

### 1. MetaPanel 批量标签绑定逻辑缺失
**缺陷表现**：在内容容器选中多个项目后，在元数据面板输入新标签，仅有面板当前显示的一个项目被绑定了标签，其余选中项无反应。
*   **根因分析**：
    *   `MetaPanel::onTagAdded` 函数硬编码为仅获取 `m_pathEdit`（即当前显示路径）并对其执行 `MetadataManager::instance().setTags`。
    *   `MetaPanel` 缺乏像 `metadataChanged(rating, color)` 那样的语义化标签变更信号，导致 `MainWindow` 无法感知并扩散修改到其他选中项。
*   **解决方案建议**：
    1.  在 `MetaPanel.h` 中新增信号：`void tagsChanged(const QStringList& tags);`。
    2.  在 `MetaPanel::onTagAdded` 和 `onTagDeleted` 中，不仅更新本地 UI，还需发射 `tagsChanged` 信号。
    3.  在 `MainWindow.cpp` 中建立联动：连接 `m_metaPanel->tagsChanged`，在槽函数中遍历 `m_contentPanel->getSelectedIndexes()`，对所有选中路径执行 `MetadataManager::instance().setTags`。

### 2. 侧边栏“标签管理”计数逻辑空缺
**缺陷表现**：侧边栏系统项“标签管理”右侧的徽标计数始终显示为 (0)，即使大量文件已打标。
*   **根因分析**：
    *   `CategoryRepo::getSystemCounts` 方法中，虽然定义了 `seenUntagged`（未标签计数），但完全没有定义针对“已标签项目”的计数集合（如 `seenTags`）。
    *   在返回的 `QMap<QString, int>` 中，缺少了键值为 `"tags"` 的条目。由于 `CategoryModel` 根据 `TypeRole`（值为 "tags"）查找计数，找不到键值则默认回退为 0。
*   **解决方案建议**：
    1.  在 `CategoryRepo::getSystemCounts` 内部新增 `std::unordered_set<std::string> seenTags;`。
    2.  在 `forEachCachedItem` 遍历逻辑中，增加判定：`if (!meta.tags.isEmpty()) seenTags.insert(meta.fileId128);`。
    3.  在结果映射中补全：`res["tags"] = static_cast<int>(seenTags.size());`。

### 3. 逻辑闭环验证
*   **持久化保障**：`MetadataManager::setTags` 内部已包含 `debouncePersist`，只要 `MainWindow` 成功分发了批量修改，数据库入库将由后台计时器自动完成。
*   **UI 刷新保障**：`setTags` 成功后会发射 `metaChanged` 信号，`CategoryPanel` 监听此信号并触发 `requestRefresh`，从而驱动 `updateStatistics` 调用更新后的 `getSystemCounts`，使计数实时刷新。

---

### 总结
本次缺陷属于**“单点操作未向群组扩散”**以及**“统计接口语义不完整”**导致的逻辑断层。通过补全 MetaPanel 的变更信号并完善 CategoryRepo 的统计维度，可彻底解决该问题。
