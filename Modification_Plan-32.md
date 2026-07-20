# DatabaseManager 任务凭证生命周期修复与同步状态点击行为排查 —— Modification_Plan-32.md

## 1. 任务背景
在系统落盘监控体系中，待落盘任务计数器（Pending Tasks Count）是反映元数据落地进度的核心指标。当用户修改数据（例如：打标签、写备注、评分、标星等）时，这些修改会生成相应的落盘任务并推入 `m_syncQueue`。然而，目前系统在运行大批量操作后，即使后台任务已经全部异步处理完毕并写回磁盘，界面上显示的“待落盘任务数”仍然高居不下，产生计数“只增不减”的假死现象。
经深度溯源排查，这是由于 `DatabaseManager::SyncTaskToken` 仅声明了拷贝构造函数而缺少移动构造函数。导致在多线程执行队列 `workerLoop` 中弹出任务执行 `std::move(m_syncQueue.front())` 时，由于没有可用的移动构造函数，编译器被动将其降级为传统的拷贝构造。这使得在整个传递、入队与出队链条中，同一凭证被无端复制了多次，额外调用了多次 `incrementPendingTasks()`，而在析构时却只被正常销毁了一次，从而引发了极度严重的计数泄露。

## 2. 问题定位
- **定位模块 1（DatabaseManager.h / DatabaseManager.cpp）**：
  `SyncTaskToken` 在拷贝时虚增了任务数，但由于 `std::move` 在缺少移动语义时降级，导致对象所有权未能正确转移，而是复制出了幽灵拷贝。
- **定位模块 2（MainWindow.cpp 同步图标点击绑定）**：
  在同步状态栏中，手动点击同步按钮 `m_btnSync` 并没有起效，需要排查具体的按钮点击绑定。

## 3. 强制对照表

| 编号 | 用户期望 / 我们的理解 | 方案对应点 | 是否一致 |
|------|-----------------------|------------|----------|
| 1    | 修复 `SyncTaskToken` 移动与拷贝语义 | 禁用拷贝构造、拷贝赋值、移动赋值，新增移动构造函数及 `m_moved` 控制标记。 | ✅ 一致 |
| 2    | 排查同步按钮点击逻辑 | 搜索 `m_btnSync` 的点击事件（QPushButton::clicked）以获知精确的行为，不带主观臆测。 | ✅ 一致 |

## 4. 详细解决方案

### 4.1 重构 SyncTaskToken 生命凭证
1. **彻底禁用拷贝（防范幽灵凭证）**：
   在 `src/meta/DatabaseManager.h` 中，声明删除拷贝构造函数、拷贝赋值运算符和移动赋值运算符：
   ```cpp
   struct SyncTaskToken {
       SyncTaskToken();
       SyncTaskToken(const SyncTaskToken&) = delete;
       SyncTaskToken& operator=(const SyncTaskToken&) = delete;
       SyncTaskToken(SyncTaskToken&& other) noexcept;
       SyncTaskToken& operator=(SyncTaskToken&&) = delete;
       ~SyncTaskToken();
   private:
       bool m_moved = false;
   };
   ```
2. **实现唯一的转移语义与单次扣减（m_moved）**：
   在 `src/meta/DatabaseManager.cpp` 中实现移动构造函数，并改造析构函数：
   - 移动构造函数：
     ```cpp
     DatabaseManager::SyncTaskToken::SyncTaskToken(SyncTaskToken&& other) noexcept {
         other.m_moved = true; // 标志源对象已被移走，其析构不应再次触发递减
     }
     ```
   - 析构函数（单次递减保障）：
     ```cpp
     DatabaseManager::SyncTaskToken::~SyncTaskToken() {
         if (!m_moved) {
             DatabaseManager::instance().decrementPendingTasks();
         }
     }
     ```

### 4.2 排查同步按钮点击事件绑定逻辑
经精确排查，同步按钮 `m_btnSync` 绑定在 `src/ui/MainWindow.cpp` 第 1254 行：
```cpp
    // 2026-06-15 按照用户要求：手动点击同步 (仅作交互反馈)
    connect(m_btnSync, &QPushButton::clicked, this, [this]() {
        if (SyncStatusService::instance().isSyncing()) {
            ToolTipOverlay::instance()->showText(m_btnSync->mapToGlobal(QPoint(0,0)), "同步正在进行中...", 1500);
        } else {
            ToolTipOverlay::instance()->showText(m_btnSync->mapToGlobal(QPoint(0,0)), "元数据已全部落地", 1500);
        }
    });
```
**排查结论**：目前手动点击同步状态按钮 `m_btnSync` **不执行任何实际的底层强制落盘或物理同步任务**，其功能纯粹只是“交互反馈”（根据落盘状态弹出 ToolTip 说明文字“同步正在进行中...”或“元数据已全部落地”）。

## 5. 修改边界声明【范围】
- **本方案涉及范围**：
  - `src/meta/DatabaseManager.h` (对 `SyncTaskToken` 的结构体声明重塑)
  - `src/meta/DatabaseManager.cpp` (实现移动构造和修正析构)
- **明确禁止越界修改的范围**：
  - 严禁修改 `SyncStatusService.h` / `SyncStatusService.cpp`。
  - 严禁在本次修改中改动 `m_btnSync` 的点击槽逻辑，保持其现有的“交互反馈”行为。

## 6. 实现准则与预警【核心】
1. **防止重复递减**：移动构造中必须牢固将 `other.m_moved = true;` 以免源对象析构时导致 `decrementPendingTasks()` 被多调用，从而引发计数跌破 0。
2. **编译防御检测**：禁用拷贝后，全项目一旦有任何地方按值拷贝传递 `SyncTaskToken`，编译器会直接在编译期报错（C2280 尝试引用已删除的函数）。若出现报错，必须分析原始意图改用 `std::move` 或引用传递，严禁将其简单恢复为允许拷贝。

## 7. Memories.md 合规检查
本次修改完全符合 MVC 分层原则与“执行者原则”。

## 8. 待确认事项
1. **同步状态按钮行为确认**：已确认该按钮点击后仅起反馈作用。
