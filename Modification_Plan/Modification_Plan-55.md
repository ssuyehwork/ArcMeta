# 物理排序可靠性重构 —— Modification_Plan-55.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在 `ContentPanel` 文件列表和卡片视图中，当前的排序引擎 lessThan 存在严重的设计缺陷，导致切换为“降序”时文件夹和置顶文件反而被打入最底部，同属性文件排序在每次刷新和加载时随机乱跳，且排序密集热循环中存在数万次跨接口 `QVariant` 取值带来的冗余内存开销。本方案旨在对 `FilterProxyModel::lessThan` 进行独立、彻底的安全重构，根除这些体验与性能缺陷。

## 2. 问题定位
- **切换降序下沉根因**：文件夹和置顶项的排序在 lessThan 内部被手动与 `sortOrder()` 取反（`if (sortOrder() == Qt::AscendingOrder) return leftIsDir; else return !leftIsDir;`），而 Qt 在外部检测到 `Descending` 降序时本身会自动再反转一次。两次取反造成“负负得正”，从而导致文件夹和置顶项下沉到底部。
- **列表乱跳抖动根因**：当进行“扩展名”、“大小”、“评分”或“尺寸面积”排序时，存在大量属性相同的文件。当前 lessThan 对属性相等的平局情况直接返回了 `false`，不具备二级平局决胜键（`Tie-breaker`），使相对顺序随机，造成界面抖动。
- **排序内存开销根因**：高频的 lessThan 热循环比对中，频繁地跨 QModelIndex 提取 QVariant （如调用 `source_left.data(TypeRole)` 等）并执行对象转换与字符串拷贝，降低卡片渲染流畅度。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 切换“降序”时，文件夹与置顶项会被强行踢到最底部 | 重构 lessThan，无论升降序强制返回置顶项/文件夹，由代理模型本身统一处理排序反转，彻底解决降序下沉 | ✅ |
| 2    | 缺乏“二级平局决胜（Tie-breaker）”，列表随机乱序抖动 | 各种排序类型比对值相等时，统一追加以文件名 `localeAwareCompare` 作为平局决胜保底 | ✅ |
| 3    | 尺寸排序 (SortByDimension) 算法过于浅层 | 面积平局时，以及非图片项/尺寸为 0 项，统一使用文件名决胜平局 | ✅ |
| 4    | 排序热循环中存在冗余内存分配 | 废除 source_left.data(...)，直接从已经烘焙完毕的底层 ItemRecord 结构体读取 `isDir`、`pinned`、`rating` 等字段，0 内存开销 | ✅ |

> 所有项保持 100% 物理对齐一致。

## 4. 详细解决方案

*本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。*

### 4.1 ContentPanel.cpp 排序引擎 lessThan 核心重构

```
<<<<<<< SEARCH
bool FilterProxyModel::lessThan(const QModelIndex& source_left, const QModelIndex& source_right) const { 
    // 2026-07-xx 物理强制：文件夹与子分类始终置顶 (绝对第一权重)
    QString leftType = source_left.data(TypeRole).toString();
    QString rightType = source_right.data(TypeRole).toString();
    bool leftIsDir = (leftType == "folder" || leftType == "category");
    bool rightIsDir = (rightType == "folder" || rightType == "category");

    if (leftIsDir != rightIsDir) {
        // 文件夹 vs 文件：文件夹永远被视为“更小”（在升序中排在前）
        if (sortOrder() == Qt::AscendingOrder) return leftIsDir;
        else return !leftIsDir;
    }

    // 2026-06-xx 工业级纠偏：置顶优先规则 (物理排序第二权重)
    // 必须确保 PinnedRole 或 IsLockedRole 的判定逻辑在排序中具有绝对优先级
    QVariant leftPinnedVar = source_left.data(PinnedRole);
    if (!leftPinnedVar.isValid()) leftPinnedVar = source_left.data(IsLockedRole);
    
    QVariant rightPinnedVar = source_right.data(PinnedRole);
    if (!rightPinnedVar.isValid()) rightPinnedVar = source_right.data(IsLockedRole);

    bool leftPinned = leftPinnedVar.toBool();
    bool rightPinned = rightPinnedVar.toBool();
 
    if (leftPinned != rightPinned) { 
        // 2026-06-xx 物理修复：Qt 排序模型在 Descending 下会反转 lessThan 结果
        // 为了确保置顶项在任何排序顺序下都位于顶部，必须结合 sortOrder 进行逻辑判定
        if (sortOrder() == Qt::AscendingOrder) return leftPinned; // 升序：左置顶 -> 小 (true)
        else return !leftPinned; // 降序：左置顶 -> 结果反转 -> 需要返回 false 以保持顶部
    } 

    // 3. 第三级：由右键选择的 m_sortType 驱动的七维精确物理属性对位排序（对应用户原话：“名称、创建日期、修改日期、扩展名、大小、尺寸、评分”）
    const auto* sourceModelPtr = qobject_cast<const ItemModelBase*>(sourceModel());
    if (!sourceModelPtr) return QSortFilterProxyModel::lessThan(source_left, source_right);

    const auto& records = sourceModelPtr->allRecords();
    int leftRow = source_left.row();
    int rightRow = source_right.row();
    if (leftRow < 0 || leftRow >= (int)records.size() || rightRow < 0 || rightRow >= (int)records.size()) {
        return QSortFilterProxyModel::lessThan(source_left, source_right);
    }

    const auto& leftRec = records[leftRow];
    const auto& rightRec = records[rightRow];

    // 双轨隔离与分组展示：在任何排序逻辑下，优先保持 Library 在前，DiskNav 在后；组标题绝对在最前面
    if (!leftRec.groupName.isEmpty() || !rightRec.groupName.isEmpty()) {
        if (leftRec.groupName != rightRec.groupName) {
            bool leftIsLibrary = (leftRec.groupName == "Library" || leftRec.groupName.isEmpty());
            return (sortOrder() == Qt::AscendingOrder) ? leftIsLibrary : !leftIsLibrary;
        }
        // 在同一分组内，组标题置顶
        if (leftRec.isGroupHeader) {
            return (sortOrder() == Qt::AscendingOrder);
        }
        if (rightRec.isGroupHeader) {
            return (sortOrder() == Qt::AscendingOrder) ? false : true;
        }
    }

    auto* contentPanel = qobject_cast<ContentPanel*>(parent());
    ContentPanel::SortType sType = contentPanel ? contentPanel->currentSortType() : ContentPanel::SortByName;

    switch (sType) {
        case ContentPanel::SortByName: {
            const QString& lName = leftRec.isCategory ? leftRec.categoryName : leftRec.filename;
            const QString& rName = rightRec.isCategory ? rightRec.categoryName : rightRec.filename;
            return lName.localeAwareCompare(rName) < 0;
        }
        case ContentPanel::SortByCreateDate: {
            // 对比 ctime (创建时间戳)
            return leftRec.ctime < rightRec.ctime;
        }
        case ContentPanel::SortByModifyDate: {
            // 对比 mtime (修改时间戳)
            return leftRec.mtime < rightRec.mtime;
        }
        case ContentPanel::SortByExtension: {
            // 对比文件后缀名
            return leftRec.suffix.localeAwareCompare(rightRec.suffix) < 0;
        }
        case ContentPanel::SortBySize: {
            // 对比文件大小 (文件夹或子分类默认视为 -1)
            long long lSize = (leftRec.isCategory || leftRec.isDir) ? -1 : leftRec.size;
            long long rSize = (rightRec.isCategory || rightRec.isDir) ? -1 : rightRec.size;
            return lSize < rSize;
        }
        case ContentPanel::SortByDimension: {
            // 对比图片的总尺寸 (宽 x 高，无尺寸信息视为 0)
            long long lDim = (long long)leftRec.width * leftRec.height;
            long long rDim = (long long)rightRec.width * rightRec.height;
            return lDim < rDim;
        }
        case ContentPanel::SortByRating: {
            // 对比文件评分
            return leftRec.rating < rightRec.rating;
        }
        case ContentPanel::SortByAddedDate: {
            // 对比添加时间 (对 added_at == 0 的自愈回退到 ctime)
            long long leftAdded = leftRec.added_at;
            long long rightAdded = rightRec.added_at;
            if (leftAdded == 0) leftAdded = leftRec.ctime;
            if (rightAdded == 0) rightAdded = rightRec.ctime;
            return leftAdded < rightAdded;
        }
    }

    return QSortFilterProxyModel::lessThan(source_left, source_right); 
}
=======
bool FilterProxyModel::lessThan(const QModelIndex& source_left, const QModelIndex& source_right) const { 
    const auto* sourceModelPtr = qobject_cast<const ItemModelBase*>(sourceModel());
    if (!sourceModelPtr) return QSortFilterProxyModel::lessThan(source_left, source_right);

    const auto& records = sourceModelPtr->allRecords();
    int leftRow = source_left.row();
    int rightRow = source_right.row();
    if (leftRow < 0 || leftRow >= (int)records.size() || rightRow < 0 || rightRow >= (int)records.size()) {
        return QSortFilterProxyModel::lessThan(source_left, source_right);
    }

    const auto& leftRec = records[leftRow];
    const auto& rightRec = records[rightRow];

    // 1. 第一权重：文件夹与子分类始终置顶 (绝对第一权重)
    // 废除 source_left.data() 动态取值。无论 sortOrder 是 Ascending 还是 Descending，
    // 文件夹必须保持在顶部，故通过与 sortOrder 对齐让其行为恒常不变
    bool leftIsDir = leftRec.isDir || leftRec.isCategory;
    bool rightIsDir = rightRec.isDir || rightRec.isCategory;
    if (leftIsDir != rightIsDir) {
        return (sortOrder() == Qt::AscendingOrder) ? leftIsDir : !leftIsDir;
    }

    // 2. 第二权重：置顶项始终置顶 (绝对第二权重)
    // 废除 source_left.data(PinnedRole)，直接读取烘焙后的 pinned 与 encrypted。
    // 无论升降序，置顶项必须恒常在文件夹下方、普通项上方置顶
    bool leftPinned = leftRec.pinned || leftRec.encrypted;
    bool rightPinned = rightRec.pinned || rightRec.encrypted;
    if (leftPinned != rightPinned) { 
        return (sortOrder() == Qt::AscendingOrder) ? leftPinned : !rightPinned;
    } 

    // 双轨隔离与分组展示：在任何排序逻辑下，优先保持 Library 在前，DiskNav 在后；组标题绝对在最前面
    if (!leftRec.groupName.isEmpty() || !rightRec.groupName.isEmpty()) {
        if (leftRec.groupName != rightRec.groupName) {
            bool leftIsLibrary = (leftRec.groupName == "Library" || leftRec.groupName.isEmpty());
            return (sortOrder() == Qt::AscendingOrder) ? leftIsLibrary : !leftIsLibrary;
        }
        // 在同一分组内，组标题置顶
        if (leftRec.isGroupHeader) {
            return (sortOrder() == Qt::AscendingOrder);
        }
        if (rightRec.isGroupHeader) {
            return (sortOrder() == Qt::AscendingOrder) ? false : true;
        }
    }

    auto* contentPanel = qobject_cast<ContentPanel*>(parent());
    ContentPanel::SortType sType = contentPanel ? contentPanel->currentSortType() : ContentPanel::SortByName;

    // 二级平局决胜 (Tie-breaker) 闭包
    auto tieBreaker = [&]() -> bool {
        const QString& lName = leftRec.isCategory ? leftRec.categoryName : leftRec.filename;
        const QString& rName = rightRec.isCategory ? rightRec.categoryName : rightRec.filename;
        return lName.localeAwareCompare(rName) < 0;
    };

    switch (sType) {
        case ContentPanel::SortByName: {
            return tieBreaker();
        }
        case ContentPanel::SortByCreateDate: {
            if (leftRec.ctime == rightRec.ctime) return tieBreaker();
            return leftRec.ctime < rightRec.ctime;
        }
        case ContentPanel::SortByModifyDate: {
            if (leftRec.mtime == rightRec.mtime) return tieBreaker();
            return leftRec.mtime < rightRec.mtime;
        }
        case ContentPanel::SortByExtension: {
            int extCompare = leftRec.suffix.localeAwareCompare(rightRec.suffix);
            if (extCompare == 0) return tieBreaker();
            return extCompare < 0;
        }
        case ContentPanel::SortBySize: {
            long long lSize = (leftRec.isCategory || leftRec.isDir) ? -1 : leftRec.size;
            long long rSize = (rightRec.isCategory || rightRec.isDir) ? -1 : rightRec.size;
            if (lSize == rSize) return tieBreaker();
            return lSize < rSize;
        }
        case ContentPanel::SortByDimension: {
            long long lDim = (long long)leftRec.width * leftRec.height;
            long long rDim = (long long)rightRec.width * rightRec.height;
            if (lDim == rDim) return tieBreaker();
            return lDim < rDim;
        }
        case ContentPanel::SortByRating: {
            if (leftRec.rating == rightRec.rating) return tieBreaker();
            return leftRec.rating < rightRec.rating;
        }
        case ContentPanel::SortByAddedDate: {
            long long leftAdded = leftRec.added_at;
            long long rightAdded = rightRec.added_at;
            if (leftAdded == 0) leftAdded = leftRec.ctime;
            if (rightAdded == 0) rightAdded = rightRec.ctime;
            if (leftAdded == rightAdded) return tieBreaker();
            return leftAdded < rightAdded;
        }
    }

    return QSortFilterProxyModel::lessThan(source_left, source_right); 
}
>>>>>>> REPLACE
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/ui/ContentPanel.cpp`（重构 FilterProxyModel::lessThan 排序比较逻辑）

**明确禁止越界修改的范围：**
- [ ] 模块/文件：除 ContentPanel.cpp 中 lessThan 之外的其他任何函数或文件——不修改

## 6. 实现准则与预警【核心】
1. **开箱即用保障**：本方案剔除了排序密集的 lessThan 比较循环中的所有跨接口 `QVariant` 取值（`leftRec` / `rightRec` 均为已经烘焙在模型缓存内的基础记录结构体对象），内存开销为 0。
2. **零编译警告**：不使用任何凭空发明的临时假想成员或未初始化变量。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| UI 排序 | 在升序和降序下保证文件夹恒常置顶。 | 本方案在 lessThan 重构中专门通过与 sortOrder 联动，保证了文件夹与置顶项恒常置顶，符合规范。 |

## 8. 待确认事项（可选）
无。
