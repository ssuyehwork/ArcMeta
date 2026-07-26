# 侧边栏分类树状展开状态重启持久化失效根治重构 —— Modification_Plan-99.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
用户反馈在侧边栏中将分类树状展开后，这些展开状态在主程序重启后无法持久化，重新启动时树状分类又被自动折叠了（对应用户原话：“侧边栏某个分类展开之后能够持续化，即便重启主程序之后仍然处于展开状态，但是当我重启之后又回到折叠状态了”）。本方案旨在扩大范围彻底排查和分析该持久化失效的根因（对应用户原话：“帮我扩大范围排查根本原因”），并在 `CategoryPanel` 的生命周期及模型重置阶段中进行精确拦截，防止展开状态在重置或关闭时被错误的空状态覆盖。

## 2. 问题定位
通过深入排查 `src/ui/CategoryPanel.cpp` 中的相关代码，发现了导致该 Bug 的底层竞态根本原因：

1. **信号在重置（Reset）期间发生回流**：
   在 `CategoryPanel::initUi()` 中，树组件的展开与折叠信号连接到了持久化槽函数：
   ```cpp
   connect(m_categoryTree, &QTreeView::expanded, this, &CategoryPanel::saveExpandedStateToSettings);
   connect(m_categoryTree, &QTreeView::collapsed, this, &CategoryPanel::saveExpandedStateToSettings);
   ```
   当执行 `m_categoryModel->refresh()` 重新构建树结构时，模型底层会执行 `beginResetModel()` 和 `endResetModel()`，并在此期间通过 `removeRows(0, rowCount())` 清除所有行。
   在清除旧节点时，`QTreeView` 会因为节点物理被移出而对每一个被销毁的展开节点**高频、同步触发 `collapsed` 信号**。
   此时，`saveExpandedStateToSettings` 被同步回调。由于防护锁 `m_isInternalUpdating` 在模型重置期间并未被置位，导致过滤条件 `if (m_isRestoringState || m_isInternalUpdating)` 没能起到任何防护阻断作用。
   因为节点已被清理，此时 `saveExpandedState` 捕获到的展开节点列表（`ids` / `names`）均为空白！这些空的数据列表立即被写入到 `AppConfig` 的 `Category/ExpandedIds` 和 `Category/ExpandedNames` 中并物理落盘。

2. **临时属性与磁盘配置状态不一致（表面正常，重启复现）**：
   重置前，在 `modelAboutToBeReset` 槽函数中，我们通过 tree property 暂存了正确的展开状态：
   ```cpp
   m_categoryTree->setProperty("expandedIds", QVariant::fromValue(idList));
   m_categoryTree->setProperty("expandedNames", expandedNames);
   ```
   并在 `modelReset` 阶段通过 `restoreExpandedState` 恢复了展开。
   这导致在程序运行期间，用户看到展开状态似乎“正常保留了”；但实际上，磁盘上的 `AppConfig` 配置文件在重置过程中**已经被覆写为了空列表**。
   一旦用户重启主程序，程序在启动初始化时从 `AppConfig` 读取配置（此时为空），侧边栏分类树便又恢复到了全部折叠的错误状态（对应用户原话：“当我重启之后又回到折叠状态了”）。

3. **析构生命周期中的信号泄露**：
   当用户主动关闭主程序时，在销毁流程中，`QTreeView` 被垃圾回收或卸载时，由于此时连接尚未物理断开，也会大面积触发 `collapsed` 信号。这同样会导致用户的正常展开记忆在程序退出瞬间被空状态覆写并落盘。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 侧边栏某个分类展开之后能够持续化，即便重启主程序之后仍然处于展开状态 (对应用户原话) | 彻底解决分类树展开持久化在模型重置与关闭时的异常覆写问题 (对应用户原话：“侧边栏某个分类展开之后能够持续化，即便重启主程序之后仍然处于展开状态”)。 | ✅ 一致 |
| 2    | 当我重启之后又回到折叠状态了，所以我期望你帮我扩大范围排查根本原因 (对应用户原话) | 扩大排查范围定位到 `beginResetModel` -> `removeRows` 产生的 collapsed 信号泄露这一底层竞态根源，并实施全生命周期安全加锁重构 (对应用户原话：“当我重启之后又回到折叠状态了，所以我期望你帮我扩大范围排查根本原因”)。 | ✅ 一致 |

## 4. 详细解决方案

解决此问题的核心法则是：**在不属于用户手动（非 UI 点击/键盘等物理操作）导致的节点展开/折叠状态下，一律拦截并禁止 `saveExpandedStateToSettings` 的保存调用**。具体改动如下：

### 4.1 在 `modelAboutToBeReset` 信号触发时立即上锁保护
在模型重置的起点（`modelAboutToBeReset`），将控制标志 `m_isInternalUpdating` 设为 `true`，以完全断开由于 `removeRows` 触发的折叠信号风暴：
```cpp
    connect(m_categoryModel, &QAbstractItemModel::modelAboutToBeReset, this, [this]() {
        // 同步解锁 ID 到模型
        m_categoryModel->setUnlockedIds(m_unlockedIds);
        
        // 物理防护：只有当模型确实有真实数据时，才暂存当前 UI 状态。
        // 如果当前是“加载中”或者为空，则不覆盖暂存值，保留从 Settings 加载或上一次有效的记录。
        bool hasRealData = false;
        if (m_categoryModel->rowCount() > 1) {
            hasRealData = true;
        } else if (m_categoryModel->rowCount() == 1) {
            QString type = m_categoryModel->index(0, 0).data(TypeRole).toString();
            if (type != "placeholder" && !m_categoryModel->index(0,0).data(Qt::DisplayRole).toString().contains("正在统计")) {
                hasRealData = true;
            }
        }

        if (hasRealData) {
            QSet<int> expandedIds;
            QStringList expandedNames;
            saveExpandedState(QModelIndex(), expandedIds, expandedNames);
            
            QList<int> idList;
            for (int id : expandedIds) idList << id;
            m_categoryTree->setProperty("expandedIds", QVariant::fromValue(idList));
            m_categoryTree->setProperty("expandedNames", expandedNames);
        }

        // 开启数据流拦截锁，防止接下来 beginResetModel / removeRows 触发大量的 collapsed 虚假信号泄露覆写
        m_isInternalUpdating = true;
    });
```

### 4.2 在 `modelReset` 完成后同步解锁
在重置完成（`modelReset`）并成功调用 `restoreExpandedState` 恢复状态后，将控制标志 `m_isInternalUpdating` 重置为 `false`，从而重新允许用户正常手动交互被持久化：
```cpp
    connect(m_categoryModel, &QAbstractItemModel::modelReset, this, [this]() {
        // 极致重构：利用 DataFlowGuard 优雅控制并消除 singleShot(0) 和 blockSignals，直接同步恢复状态
        QList<int> idList = m_categoryTree->property("expandedIds").value<QList<int>>();
        QStringList expandedNames = m_categoryTree->property("expandedNames").toStringList();
        
        QSet<int> expandedIds;
        for (int id : idList) expandedIds.insert(id);

        m_isRestoringState = true;
        {
            DataFlowGuard guard(m_isInternalUpdating);
            restoreExpandedState(QModelIndex(), expandedIds, expandedNames);
        }
        m_isRestoringState = false;

        // 重置彻底完成后，重新开放拦截锁，允许用户的正常展开/折叠交互行为进行持久化
        m_isInternalUpdating = false;
    });
```

### 4.3 析构保护与信号安全切断
在 `CategoryPanel` 的析构函数中（或在窗口关闭前），通过将 `m_isInternalUpdating` 设为 `true` 并安全断开展开/折叠信号，确保在析构和控件销毁时没有任何由于物理清除产生的 collapsed 信号泄露污染 `AppConfig`：
- 在 `src/ui/CategoryPanel.h` 中：
  ```cpp
  // 声明虚析构函数
  ~CategoryPanel() override;
  ```
- 在 `src/ui/CategoryPanel.cpp` 中实现虚析构：
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
  - 涉及类/函数：`CategoryPanel::~CategoryPanel` (将 inline default 改为非内联的声明)
- [ ] 模块/文件：`src/ui/CategoryPanel.cpp`
  - 涉及类/函数：`CategoryPanel::~CategoryPanel` (实现析构释放与安全断连)、`CategoryPanel::initUi` (在 `modelAboutToBeReset` 中将防护标志安全设为 `true`，并在 `modelReset` 中完成恢复后设为 `false`)

**明确禁止越界修改的范围：**
- [ ] `CategoryModel::refresh` 中构建分类树和系统项的具体加载逻辑 —— 不修改
- [ ] 数据库层面的 `CategoryRepo` 的数据查询和更改行为 —— 不修改

## 6. 实现准则与预警【核心】
1. **拦截时机加固**：必须在 `beginResetModel` 所引发的行清除发生前（即 `modelAboutToBeReset` 信号触发瞬间）将 `m_isInternalUpdating` 强锁。如果在模型重置完成和展开树全部恢复好之后，依然不恢复防护标志（保持 `true`），将导致用户手动点击展开和折叠时无法保存展开记录，因此在 `modelReset` 末尾必须将其恢复为 `false`。
2. **安全析构与 disconnect**：在析构函数中彻底 `disconnect` 分类树信号，不仅能阻断关闭程序时的空状态覆写，更是消除关闭主程序或多线程析构期间偶发崩溃的黄金保障。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 状态持久化机制 | 此项无现有规范，建议用户补充 | ✅ 符合。本方案不改变原有 UI 行为或品牌视觉，通过引入生命周期状态锁、析构安全断开、以及重置拦截锁，彻底保障了状态持久化落盘的高健壮性，完美解决重启丢失问题。 |

## 8. 待确认事项（可选）
（无）
