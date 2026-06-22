# 文件夹显隐状态与筛选勾选逻辑联动 —— Analysis_Modification_Plan-94.md

## 1. 任务背景
在 ArcMeta 现有逻辑中，内容面板顶部的“文件夹显隐”按钮具有最高拦截优先级。用户要求实现一个逻辑特例：即使该按钮处于关闭（隐藏）状态，只要用户在筛选面板中显式勾选了“文件夹”或“空文件夹”，中间的内容区域也必须强制显示出对应的文件夹项，且不改变顶部按钮的置灰状态。

## 2. 问题定位
- **模块**：`src/ui/ContentPanel.cpp`
- **类/函数**：`FilterProxyModel::filterAcceptsRow()`
- **根因分析**：
  在 `filterAcceptsRow` 的判定流程中，代码首先检查 `currentFilter.showFolders`（由顶部按钮驱动）。若为 `false`，则对所有目录/分类项直接返回 `false`，导致筛选器的类型勾选逻辑无法生效。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 顶栏按钮灰色（隐藏）时，筛选勾选也要显示 | 修改判定逻辑，为筛选勾选设置“优先通过权” | ✅ |
| 2    | 不改变灰色文件夹的持续性（按钮保持灰色） | 逻辑修改局限于模型层，不回调 UI 按钮状态 | ✅ |
| 3    | 大白话：总闸关了，但右边勾了也要出特例 | 实现 `showFolders || selected` 逻辑逻辑叠加 | ✅ |

## 4. 详细解决方案

### 4.1 修改 `FilterProxyModel::filterAcceptsRow` (约 2380 行)
将原有的单一开关拦截逻辑修改为“开关 OR 筛选”联动判定。

```cpp
// 查找位置：ContentPanel.cpp -> FilterProxyModel::filterAcceptsRow
// --- 按照 Plan-73：显示/隐藏文件夹/文件 ---

// 原逻辑：
/*
if (record.isCategory || record.isDir) {
    if (!currentFilter.showFolders) return false;
}
*/

// 修改后的逻辑：
if (record.isCategory || record.isDir) {
    // 1. 判断用户是否在筛选器的“类型勾选”中显式选中了文件夹相关的项
    // 逻辑依据：currentFilter.types 包含 "folder" (普通文件夹) 或 "空文件夹" (空文件夹筛选)
    bool isFolderExplicitlySelected = currentFilter.types.contains("folder") || 
                                     (record.isEmpty && currentFilter.types.contains("空文件夹"));

    // 2. 只有当“顶栏全局开关为隐藏”且“筛选器未显式勾选文件夹”时，才执行拦截
    if (!currentFilter.showFolders && !isFolderExplicitlySelected) {
        return false;
    }
}
```

## 5. 修改边界声明【红线】

**本次方案涉及范围：**
- [x] 模块/文件：`src/ui/ContentPanel.cpp` 中的 `FilterProxyModel` 类。

**明确禁止越界修改的范围：**
- [ ] 禁止修改 `ContentPanel::m_btnToggleFolders` 的选中状态或 QSS。
- [ ] 禁止修改 `FilterState` 数据结构。
- [ ] 禁止修改文件（`record.isDir == false`）的显隐判定逻辑。

## 6. 实现准则与预警【核心】
1. **优先级排布**：该判定必须位于 `filterAcceptsRow` 的首部拦截区，因为它决定了项目是否能进入后续的颜色、评级等二次过滤。
2. **逻辑一致性**：当用户在筛选器勾选“空文件夹”时，只有满足 `record.isEmpty` 的文件夹会被显示；而勾选“文件夹”则显示所有文件夹。这符合用户对筛选结果的预期。
3. **UI 性能**：该判定在主线程运行，使用 `contains` 进行字符串查找，考虑到 `currentFilter.types` 通常元素极少（<10个），其性能损耗可忽略不计。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|-------------|----------------------|----------------|
| 角色红线 | Jules 禁止修改代码，仅产出文档 | ✅ 符合 |
| UI 交互红线 | 不得擅自修改用户未授权的 UI 状态 | ✅ 符合，不改变顶栏按钮状态 |

## 8. 待确认事项
无。
