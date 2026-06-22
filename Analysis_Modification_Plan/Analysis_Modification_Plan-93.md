# 恢复“空文件夹”筛选至“文件类型”分组 —— Analysis_Modification_Plan-93.md

## 1. 任务背景
在之前的 UI 重构中，“空文件夹”筛选复选框被从“文件类型”分组中移除，并单独归类到了一个名为“属性”的新分组中。用户要求撤销此改动，将“空文件夹”选项恢复至“文件类型”分组内，并移除临时的“属性”分组。

## 2. 问题定位
- **模块**：`src/ui/FilterPanel.cpp`
- **函数**：`FilterPanel::rebuildGroups()`
- **根因分析**：
  1. 在“文件类型”分组（Section 4）中，代码显式 `continue` 跳过了键值为 `"空文件夹"` 的统计项。
  2. 在“属性”分组（Section 10）中，代码独立创建了分组容器并添加了“空文件夹”项。
  3. 这种逻辑割裂导致用户习惯的交互位置发生了偏移。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 将“空文件夹”复选框选项恢复到“文件类型”中 | 在 `rebuildGroups` 的 Section 4 中插入“空文件夹”逻辑 | ✅ |
| 2    | 移除现在的“属性”分组（由 Jules 之前违规添加） | 删除 `rebuildGroups` 中关于 Section 10 的全部代码 | ✅ |

## 4. 详细解决方案

### 4.1 修改 `FilterPanel::rebuildGroups()` 中的“文件类型”部分
在“文件夹”和“文件”基础分类之后，循环遍历扩展名之前，插入对 `m_emptyFolderCount` 的判断和 UI 构建。

```cpp
// ── 4. 文件类型 ──────────────────────────────────────────
if (!m_typeCounts.isEmpty() || !m_filter.typeFilterText.isEmpty() || m_emptyFolderCount > 0) {
    // ... (现有 QLineEdit 构建逻辑)

    // 1. 基础分类：文件夹
    if (m_typeCounts.contains("folder")) { /*...*/ }
    // 2. 基础分类：文件
    if (m_typeCounts.contains("file")) { /*...*/ }

    // 3. 恢复点：空文件夹 (如果计数大于 0)
    if (m_emptyFolderCount > 0) {
        QCheckBox* cb = addFilterRow(gl, "空文件夹", m_emptyFolderCount);
        cb->blockSignals(true);
        cb->setChecked(m_filter.types.contains("空文件夹"));
        cb->blockSignals(false);
        connect(cb, &QCheckBox::toggled, this, [this](bool on) {
            if (on) { if (!m_filter.types.contains("空文件夹")) m_filter.types.append("空文件夹"); }
            else    m_filter.types.removeAll("空文件夹");
            emit filterChanged(m_filter);
        });
    }

    // 4. 其他扩展名循环
    QStringList exts = m_typeCounts.keys(); exts.sort();
    for (const QString& ext : exts) {
        // 确保继续屏蔽 "空文件夹"，因为它已在上面显式处理
        if (ext == "folder" || ext == "file" || ext == "空文件夹") continue;
        // ...
    }
}
```

### 4.2 移除 Section 10（属性分组）
彻底删除 `FilterPanel.cpp` 中约第 930 行至第 945 行的代码块。

## 5. 修改边界声明【红线】

**本次方案涉及范围：**
- [x] 模块/文件：`src/ui/FilterPanel.cpp` 中的 `rebuildGroups` 函数。

**明确禁止越界修改的范围：**
- [ ] 禁止修改 `FilterState` 结构体定义。
- [ ] 禁止修改 `populate` 函数的数据传递逻辑。
- [ ] 禁止修改筛选器的样式表（QSS）。

## 6. 实现准则与预警【核心】
1. **位置对齐**：确保“空文件夹”位于“文件夹/文件”之后，但在具体文件扩展名（如 .png, .txt）之前，以符合逻辑层级。
2. **信号阻塞**：在设置 `setChecked` 时必须调用 `blockSignals(true)`，防止初始化时触发无效的 `filterChanged` 信号。
3. **副作用核查**：确认 `m_typeCounts` 中即使包含了 `"空文件夹"` 键值（通常在 `populate` 中处理），Section 4 的循环也会通过 `if (ext == "空文件夹") continue;` 正确屏蔽，避免重复显示。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|-------------|----------------------|----------------|
| UI 布局控制 | 筛选面板需动态根据 populate 数据重建 | ✅ 符合，方案基于 rebuildGroups 动态构建 |
| 筛选器逻辑 | 搜索关键词与筛选器需通过 filterChanged 同步 | ✅ 符合，通过 connect 触发 filterChanged |
| 角色红线 | Jules 禁止修改代码，仅产出文档 | ✅ 符合，本次仅提交方案文档 |

## 8. 待确认事项
- 目前 `m_emptyFolderCount` 是作为 `populate` 的独立参数传入的。如果未来后端将此计数直接并入 `m_typeCounts`（使用 "空文件夹" 作为键），当前的显式处理逻辑依然兼容且鲁棒。
