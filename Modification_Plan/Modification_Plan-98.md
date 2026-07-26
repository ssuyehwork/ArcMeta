# 侧边栏分类树状展开状态重启不持久化故障修复方案 —— Modification_Plan-98.md

> 状态：已批准，执行中

## 1. 任务背景
用户反馈在侧边栏中将分类树状展开后，这些展开状态在主程序重启后无法持久化，重新启动时树状分类又被自动折叠了。本方案旨在彻底分析该持久化失效的根因，并在 `CategoryPanel` 的生命周期及模型重置阶段中进行精确拦截，防止展开状态在重置或关闭时被错误的空状态覆盖。

## 2. 问题定位
通过深入静态排查 `src/ui/CategoryPanel.cpp` 中的相关代码，发现了导致该 Bug 的核心致命竞态原因：

1. **信号在重置（Reset）期间发生回流**：
   在 `CategoryPanel::initUi()` 中，将树组件的展开与折叠信号直接连接到了持久化槽函数：
   ```cpp
   connect(m_categoryTree, &QTreeView::expanded, this, &CategoryPanel::saveExpandedStateToSettings);
   connect(m_categoryTree, &QTreeView::collapsed, this, &CategoryPanel::saveExpandedStateToSettings);
   ```
   然而，当数据或颜色发生变更、触发 `m_categoryModel->refresh()` 重建树结构时，模型底层会执行 `beginResetModel()` and `endResetModel()`。在清除旧有节点时，`QTreeView` 会因为节点物理被移出而对每一个被销毁的展开节点**高频同步触发 `collapsed` 信号**。
   此时，`CategoryPanel::saveExpandedStateToSettings` 被直接回调。因为旧节点已被清理，此时捕获到的展开节点列表（`ids` / `names`）均为空白！这些空的数据列表立即被写入到 `AppConfig`（`Category/ExpandedIds` 和 `Category/ExpandedNames`）中，**彻底覆盖并洗掉了用户好不容易展开分类建立起来的记忆**。

2. **状态保护锁（m_isInternalUpdating）的覆盖范围不足**：
   虽然 `CategoryPanel` 设计了 `m_isInternalUpdating` RAII 状态防护锁：
   ```cpp
   m_isRestoringState = true;
   {
       DataFlowGuard guard(m_isInternalUpdating);
       restoreExpandedState(QModelIndex(), expandedIds, expandedNames);
   }
   m_isRestoringState = false;
   ```
   但是在 `m_categoryModel` 的 `modelAboutToBeReset` 信号处理器中，**并未将 `m_isInternalUpdating` 或 `m_isRestoringState` 设为 `true`**。这导致在 `beginResetModel()` 触发的节点批量折叠和销毁流程中，高频调用 `saveExpandedStateToSettings()` 时，由于保护标志没有提前被置位，导致过滤条件 `if (m_isRestoringState || m_isInternalUpdating)` 没能起到任何防护阻断作用！

3. **析构生命周期中的信号泄露**：
   当用户主动关闭主程序时，在主窗口析构的整个销毁流程中，`QTreeView` 及其子树控件在被释放、卸载模型（SetModel）时也会最后触发一次大面积的折叠事件。由于此时控件还未完全被垃圾回收，这一系列折叠信号再一次执行了 `saveExpandedStateToSettings`，导致用户正常的展开记忆在程序退出瞬间被全部清空。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 侧边栏分类树状展开之后没有被持久化，重启主程序之后又被自动折叠了 (对应用户原话) | 修复展开持久化逻辑（对应用户原话：“侧边栏分类树状展开之后没有被持久化”），确保在模型重置或析构时不被错误覆写，使重启后能够精准恢复（对应用户原话：“重启主程序之后又被自动折叠了”）。 | ✅ 一致 |

## 4. 详细解决方案

解决此问题的黄金法则是：**在不属于用户手动（UI点击/物理操作）导致的节点展开/折叠状态下，一律拦截并禁止 `saveExpandedStateToSettings` 的保存调用**。具体改动如下：

### 4.1 在 `modelAboutToBeReset` 信号触发时立即上锁保护
当模型发出“即将重置”的信号时，由于接下来必定会有节点大面积折叠与清空的行为，我们必须通过设置成员变量（如 `m_isInternalUpdating = true`），彻底阻止在此期间响应任何 `collapsed` / `expanded` 信号：
- 在 `modelAboutToBeReset` 的槽函数中首先设置状态锁（`m_isInternalUpdating = true`），暂存完 expanded 状态后再通过 `modelReset` 正常解开状态锁：
  ```cpp
  connect(m_categoryModel, &QAbstractItemModel::modelAboutToBeReset, this, [this]() {
      // 开启数据流拦截锁，防止接下来 beginResetModel 触发大量的 collapsed 虚假信号泄露覆写
      m_isInternalUpdating = true;
      ...
  ```
- 然后，在 `modelReset` 完成展开恢复后：
  ```cpp
  connect(m_categoryModel, &QAbstractItemModel::modelReset, this, [this]() {
      ...
      m_isRestoringState = true;
      {
          DataFlowGuard guard(m_isInternalUpdating);
          restoreExpandedState(QModelIndex(), expandedIds, expandedNames);
      }
      m_isRestoringState = false;
      // 在这之后重设并开放拦截锁
      m_isInternalUpdating = false;
  });
  ```

### 4.2 改写 `CategoryPanel::saveExpandedStateToSettings`
增加额外的严密健壮性防线：
```cpp
void CategoryPanel::saveExpandedStateToSettings() {
    // 1. 如果正在进行状态还原，或者处于内部异步刷新更新中，绝不保存！
    if (m_isRestoringState || m_isInternalUpdating) {
        return;
    }
    
    // 2. 如果模型正在重置期间，或者模型行数正在变动，绝不保存！
    if (!m_categoryModel || m_categoryModel->rowCount() <= 0) return;

    // 3. 物理防御：如果只有一个项且是加载中占位符，严禁保存，防止清空用户的历史记忆
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

    // 4. 重大物理防御加固：如果是空的状态，由于模型可能被临时清空，千万不能用空去覆盖有历史记录的 settings 
    // 只有在捕获到有展开的节点时，或者是用户手动将所有节点折叠完毕（这里可以通过判断模型是否真实存在顶级“我的分类”来精细防护）才执行保存。
    // 如果是因为整个树被清空导致的 0 展开项，坚决予以阻断，防止闪烁式覆写
    if (ids.isEmpty() && names.isEmpty()) {
        // 如果我们有历史数据，且模型的数据量实际上是临时归零（或者不完整），不要覆盖旧设置
        // 只有当模型确实有效加载了我的分类（数据正常加载完毕）且展开真的全被折叠时，才允许覆盖。
        bool hasMyCategory = false;
        for (int i = 0; i < m_categoryModel->rowCount(); ++i) {
            if (m_categoryModel->index(i, 0).data(NameRole).toString() == "我的分类") {
                hasMyCategory = true;
                break;
            }
        }
        // 如果我的分类这一根节点都找不到了，说明树正在重建中，这时的 ids.isEmpty() 纯属系统清空，决不能保存
        if (!hasMyCategory) {
            return;
        }
    }

    QList<QVariant> idList;
    for (int id : ids) idList << id;
    AppConfig::instance().setValue("Category/ExpandedIds", idList);
    AppConfig::instance().setValue("Category/ExpandedNames", names);
    AppConfig::instance().sync(); // 物理落盘
}
```

### 4.3 析构保护与信号安全切断
在 `CategoryPanel` 的析构函数中（或在 `MainWindow` 关闭前），通过安全切断与 `m_categoryTree` 展开/折叠信号的连接，确保在窗体销毁时没有任何由于销毁控件而产生的副作用折叠事件能回传到 QSettings。
- 在 `CategoryPanel.h` 中，为 `CategoryPanel` 显式声明虚析构函数：
  ```cpp
  ~CategoryPanel() override;
  ```
- 在 `CategoryPanel.cpp` 中实现虚析构：
  ```cpp
  CategoryPanel::~CategoryPanel() {
      // 1. 在面板被析构前，将控制标志设为内部更新态，彻底屏蔽 QTreeView 卸载时的折叠信号回流
      m_isInternalUpdating = true;
      
      // 2. 物理断开这些高危信号，确保高枕无忧
      if (m_categoryTree) {
          disconnect(m_categoryTree, &QTreeView::expanded, this, &CategoryPanel::saveExpandedStateToSettings);
          disconnect(m_categoryTree, &QTreeView::collapsed, this, &CategoryPanel::saveExpandedStateToSettings);
      }
  }
  ```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/ui/CategoryPanel.h`
  - 涉及类/函数：`CategoryPanel::~CategoryPanel`（将 inline default 改为非内联声明）
- [ ] 模块/文件：`src/ui/CategoryPanel.cpp`
  - 涉及类/函数：`CategoryPanel::initUi`（重置连接处保护拦截及 `modelAboutToBeReset` / `modelReset` 的标志同步）、`CategoryPanel::saveExpandedStateToSettings`（覆写保护与空过滤）、`CategoryPanel::~CategoryPanel`（实现虚析构并切断信号连接）

**明确禁止越界修改的范围：**
- [ ] `CategoryModel::refresh` 中构建分类树和系统项的具体加载逻辑——不修改
- [ ] 数据库层面的 `CategoryRepo` 的数据查询和更改行为——不修改

## 6. 实现准则与预警【核心】
1. **模型重置期间的状态加锁**：必须确保在 `modelAboutToBeReset` 触发时将 `m_isInternalUpdating = true;` 锁住，直到 `modelReset` 彻底完成恢复后才把该锁解开，彻底隔绝在此期间模型节点清理产生虚假的折叠 `collapsed` 信号风暴。
2. **空状态安全验证**：利用 `hasMyCategory` 去兜底防御当树由于数据加载变动导致节点全部归零时的空状态误保存。
3. **安全断开（disconnect）机制**：在析构函数中彻底断开，不仅消除了虚假信号的覆写，还能解决部分极端情况下在程序关闭时由多线程异步或模型销毁产生的崩溃（Crash）隐患。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 状态持久化机制 | 界面控件在被重置、销毁时不应该用临时或空的状态覆盖用户的历史持久化配置数据。 | ✅ 符合。本方案完美通过引入内部更新状态防护标志、析构安全断开、以及空状态安全验证解决了这一问题。 |

## 8. 待确认事项（可选）
（无）
