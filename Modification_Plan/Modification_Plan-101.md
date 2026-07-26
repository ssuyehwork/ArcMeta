# 侧边栏分类树状展开状态重启持久化失效根治重构二期 —— Modification_Plan-101.md

> 状态：已批准，执行中

## 1. 任务背景
在上一轮排查中，用户针对分类树状折叠/展开状态重启后无法持久化的问题，精准定位了 3 个隐藏极深的逻辑死穴（类型强转失败、内存/磁盘类型混用不一致、save 里的“我的分类”字符串过激匹配拦截）。本方案旨在遵循用户指示，对这 3 个逻辑死穴进行全方位的物理矫正与完美重构，彻底解决这一持久化问题。

## 2. 问题定位
1. **`QVariant` 无法隐式转换 `QVariantList` 到 `QList<int>`**：
   在 `loadExpandedStateFromSettings` 里向 tree property 暂存时类型为 `QList<QVariant>`；但在 `modelReset` 槽函数中却尝试使用 `value<QList<int>>()` 强转。因转换失败而静默返回空，导致重置后无法恢复展开状态。
2. **读写类型不一致**：
   `modelAboutToBeReset` 里存入 Property 的是 `QList<int>`，而 `loadExpandedStateFromSettings` 里写入的又是 `QVariantList`。两套类型混用加剧了解析失败的频次。
3. **“我的分类”过激字符串匹配拦截**：
   在 `saveExpandedStateToSettings` 中对 `NameRole` 进行等于 `"我的分类"` 的硬编码比对时，未考虑模型中附带有统计计数后缀（例如 `"我的分类 (5)"`）或代理过滤，导致匹配直接失败并 return 阻断了用户正常的物理落盘动作。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 侧边栏某个分类展开之后能够持续化，即便重启主程序之后仍然处于展开状态 (对应用户原话) | 彻底纠正类型转换和匹配过激 Bug，实现 100% 成功持久化与平滑恢复 (对应用户原话)。 | ✅ 一致 |

## 4. 详细解决方案

### 4.1 修正 `loadExpandedStateFromSettings`
统一向 Tree 的 Property 中暂存的数据类型为标准 `QList<int>`。
```cpp
void CategoryPanel::loadExpandedStateFromSettings() {
    bool hasRecord = !AppConfig::instance().getValue("Category/ExpandedIds").isNull() ||
                     !AppConfig::instance().getValue("Category/ExpandedNames").isNull();

    QVariantList idVarList = AppConfig::instance().getValue("Category/ExpandedIds").toList();
    QStringList names = AppConfig::instance().getValue("Category/ExpandedNames").toStringList();

    QSet<int> ids;
    QList<int> idIntList;
    for (const auto& v : idVarList) {
        int id = v.toInt();
        ids.insert(id);
        idIntList << id;
    }

    m_categoryTree->setProperty("expandedIds", QVariant::fromValue(idIntList));
    m_categoryTree->setProperty("expandedNames", names);
    m_categoryTree->setProperty("hasHistoryRecord", hasRecord);

    m_isRestoringState = true;
    {
        DataFlowGuard guard(m_isInternalUpdating);
        restoreExpandedState(QModelIndex(), ids, names);
    }
    m_isRestoringState = false;
}
```

### 4.2 修正 `saveExpandedStateToSettings`
移除过激的空状态拦截（删除包含 `"我的分类"` 的硬编码匹配，直接根据 `ids` 和 `names` 保存），并在物理落盘后，实时将最新的 `idIntList` 物理同步更新到 `m_categoryTree` 属性中，解决内存/磁盘不一致问题。
```cpp
void CategoryPanel::saveExpandedStateToSettings() {
    if (m_isRestoringState || m_isInternalUpdating) {
        return;
    }

    if (!m_categoryModel || m_categoryModel->rowCount() <= 0) return;

    if (m_categoryModel->rowCount() == 1) {
        QModelIndex first = m_categoryModel->index(0, 0);
        QString type = first.data(TypeRole).toString();
        if (type == "placeholder" || first.data(Qt::DisplayRole).toString().contains("正在统计")) {
            return;
        }
    }

    QSet<int> ids;
    QStringList names;
    saveExpandedState(QModelIndex(), ids, names);

    QVariantList idVarList;
    QList<int> idIntList = ids.values();
    for (int id : idIntList) idVarList << id;

    AppConfig::instance().setValue("Category/ExpandedIds", idVarList);
    AppConfig::instance().setValue("Category/ExpandedNames", names);
    AppConfig::instance().sync();

    m_categoryTree->setProperty("expandedIds", QVariant::fromValue(idIntList));
    m_categoryTree->setProperty("expandedNames", names);
}
```

### 4.3 修正 `initUi()` 中 `modelReset` 信号的类型兼容读取
对 `expandedIds` 进行兼容性转换。支持同时从 `QList<int>` 和 `QVariantList` 中安全解析并插入 Set 中。
```cpp
    connect(m_categoryModel, &QAbstractItemModel::modelReset, this, [this]() {
        QVariant varIds = m_categoryTree->property("expandedIds");
        QStringList expandedNames = m_categoryTree->property("expandedNames").toStringList();

        QSet<int> expandedIds;
        if (varIds.canConvert<QList<int>>()) {
            QList<int> list = varIds.value<QList<int>>();
            for (int id : list) expandedIds.insert(id);
        } else if (varIds.canConvert<QVariantList>()) {
            QVariantList list = varIds.toList();
            for (const auto& v : list) expandedIds.insert(v.toInt());
        }

        m_isRestoringState = true;
        {
            DataFlowGuard guard(m_isInternalUpdating);
            restoreExpandedState(QModelIndex(), expandedIds, expandedNames);
        }
        m_isRestoringState = false;
        m_isInternalUpdating = false;
    });
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/ui/CategoryPanel.cpp`
  - 涉及类/函数：`CategoryPanel::initUi`, `CategoryPanel::saveExpandedStateToSettings`, `CategoryPanel::loadExpandedStateFromSettings`

**明确禁止越界修改的范围：**
- [ ] 数据库层面的所有操作和 `CategoryModel` 的非连接逻辑 —— 不修改

## 6. 实现准则与预警【核心】
1. **类型强转兼容性**：在 `modelReset` 内，利用 `canConvert<QList<int>>()` 与 `canConvert<QVariantList>()` 进行防守型分流转换，杜绝因 QVariant 类型转换静默失败造成的数据清空。
2. **同步属性**：在 `saveExpandedStateToSettings` 完成 QSettings 落盘后，必须同步调用 `setProperty("expandedIds")`，以使内存状态和磁盘配置实时完美同频。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 状态持久化机制 | 此项无现有规范，建议用户补充 | ✅ 符合。本方案重构了状态读取及类型转换通道，消除了 QVariantList 无法强转为 QList<int> 的静默失败漏洞，大幅提升了状态持久化的稳定性。 |

## 8. 待确认事项（可选）
（无）
