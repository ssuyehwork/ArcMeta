# 侧边栏分类树状展开状态重启不持久化与模型重置竞态修复方案 —— Modification_Plan-99.md

> 状态：已批准，执行中

## 1. 任务背景
在上一个方案 Plan-98 实施后，用户反馈侧边栏分类树状展开状态在主程序重启后依然无法持久化，原本展开的顶级托管库如 `ArcMeta.Library_Z` 及其子分类，在程序重新启动时又回到了折叠状态。本方案旨在针对最新的重构代码，对模型重设和视图节点建立之间的异步竞态成因进行深度定位，提供彻底、无缝且高强健性的异步恢复与状态加锁方案。

## 2. 问题定位
通过排查 `src/ui/CategoryPanel.cpp` 中 `initUi` 内对模型信号连接的代码，发现了核心的竞态恢复缺陷：
1. **模型重置（Reset）与视图渲染的时间差**：
   在最近的修改中，原本采用 `QTimer::singleShot(0)` 异步延迟恢复的逻辑被移除，改为了在 `modelReset` 信号触发时同步直接执行展开：
   ```cpp
   connect(m_categoryModel, &QAbstractItemModel::modelReset, this, [this]() {
       ...
       m_isRestoringState = true;
       {
           DataFlowGuard guard(m_isInternalUpdating);
           restoreExpandedState(QModelIndex(), expandedIds, expandedNames);
       }
       m_isRestoringState = false;
   });
   ```
   然而，在 Qt 架构中，当 `QAbstractItemModel` 调用 `endResetModel()` 发送 `modelReset` 信号时，订阅该模型的视图组件 `QTreeView` 还没有开始或尚未完成对其事件循环中重置事件的消费和处理，更未生成并映射出其内部树状物理节点（Visual Index）。
   如果在 `modelReset` 信号处理器中**直接、同步**调用 `restoreExpandedState` 递归寻找节点并设置 `m_categoryTree->setExpanded(idx, true)`，由于此时物理节点尚未实例化，所有的展开方法均无法生效，造成状态恢复在启动加载时静默失效。

2. **异步锁定（m_isInternalUpdating）时序闭合完整性**：
   通过重新将恢复操作移回 `QTimer::singleShot(0, this, [this]() { ... })` 异步延迟处理，需要确保整个状态恢复结束前，所有的 `expanded` / `collapsed` 信号一律被 `m_isInternalUpdating` 或 `m_isRestoringState` 锁牢拦截，以防止节点在异步重绘过程中意外回写空或不完整的状态配置。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 侧边栏分类树状展开之后没有被持久化，重启主程序之后又被自动折叠了 (对应用户原话) | 修复展开持久化逻辑（对应用户原话），确保在模型重置、异步刷新或析构时不被错误覆写且重启后能精准恢复。 | ✅ 一致 |

## 4. 详细解决方案

解决此问题的黄金法则是：**通过 `singleShot(0)` 错开视图事件队列，确保在 `QTreeView` 物理节点生成之后再同步执行状态还原，并在还原完全落定后才解开拦截锁**。

### 4.1 异步包裹与锁时序闭合
在 `CategoryPanel::initUi` 中，改写 `m_categoryModel` 的 `modelAboutToBeReset` 与 `modelReset` 信号连接，将逻辑重构为：
- `modelAboutToBeReset` 触发时**立即开启** `m_isInternalUpdating = true` 数据流拦截锁：
  ```cpp
  connect(m_categoryModel, &QAbstractItemModel::modelAboutToBeReset, this, [this]() {
      m_isInternalUpdating = true; // 立即上锁，拦截 beginResetModel 时物理节点销毁产生的大量 collapsed 信号
      ...
  ```
- `modelReset` 触发时**使用 `QTimer::singleShot(0)` 包裹状态恢复逻辑**，在异步回调完全处理完毕后，才重置并开放状态拦截：
  ```cpp
  connect(m_categoryModel, &QAbstractItemModel::modelReset, this, [this]() {
      // 2026-07-26 物理修复：在异步单次定时器中延迟执行，以确保 QTreeView 已经处理完了 modelReset，节点在视图中已经物理生成并映射
      QTimer::singleShot(0, this, [this]() {
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

          // 状态还原完全落定后，再安全解开拦截锁
          m_isInternalUpdating = false;
      });
  });
  ```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/ui/CategoryPanel.cpp`
  - 涉及类/函数：`CategoryPanel::initUi` 中的 `modelAboutToBeReset` 和 `modelReset` 的信号处理器。

**明确禁止越界修改的范围：**
- [ ] `CategoryModel::refresh` 中构建分类树和系统项的具体加载逻辑——不修改
- [ ] 数据库层面的 `CategoryRepo` 的数据查询和更改行为——不修改

## 6. 实现准则与预警【核心】
1. **防止野指针生命期隐患**：在调用 `QTimer::singleShot(0, this, [this]() { ... })` 时，**必须显式提供 `this` 作为 Context 对象**（第二个参数）。这可保证如果 `CategoryPanel` 在下一次事件循环前被突然析构销毁，该异步任务会自动从 Qt 事件队列中注销并取消，彻底杜绝野指针崩溃。
2. **锁的闭合覆盖**：必须确保拦截锁（`m_isInternalUpdating`）从 `modelAboutToBeReset` 触发瞬间起，直到异步 lambda 完成所有 `restoreExpandedState` 物理展开动作后，才彻底关闭。这形成了完美的时有时无屏蔽，将批量重绘和展开引发的高频信号风暴彻底隔离在 settings 持久化之外。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 状态持久化机制 | 界面控件在被重置、销毁时不应该用临时或空的状态覆盖用户的历史持久化配置数据。 | ✅ 符合。本方案完美通过引入内部更新状态防护标志、异步单次延迟和覆盖整个重绘时段的拦截锁解决了由于重构时产生的节点物理渲染竞态导致持久化空覆写的问题。 |

## 8. 待确认事项（可选）
（无）
