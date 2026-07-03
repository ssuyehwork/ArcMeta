# SyncStatusService 试点解耦与高性能节流设计 —— Analysis_Modification_Plan-121.md

## 1. 任务背景
在 `Analysis_Modification_Plan-119.md` 中，系统引入了实时增量同步机制。由于元数据变更（如批量修改标签）会产生极高频的同步信号，若 `MainWindow` 直接订阅这些信号，将导致严重的 UI 消息风暴。作为首个解耦试点，`SyncStatusService` 旨在通过“状态节流”验证解耦后的 UI 丝滑度。

## 2. 问题定位
- **现状耦合**：`MainWindow` 直接连接 `MetadataManager::pendingSyncChanged`。
- **性能瓶颈**：高频触发 UI 刷新（图标切换、ToolTip 更新）会导致主线程 CPU 占用飙升，甚至造成假死。
- **职责错位**：UI 容器承担了同步进度的统计与过滤逻辑。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 将 SyncStatusService 作为首个试点解耦模块 | 创建独立的 `SyncStatusService` 类，接管 `m_btnSync` 逻辑 | ✅ |
| 2    | 验证节流机制的性能表现 | 引入 `QTimer` 驱动的 200ms 时间窗口状态节流 | ✅ |
| 3    | 坚决不可发生假死、卡顿 | 采用非阻塞通信模型，UI 仅订阅平滑后的最终状态 | ✅ |
| 4    | 避免抑制锁、线程锁定、线程竞争 | 内部使用 `std::atomic` 维护队列计数，严禁 UI 申请 I/O 锁 | ✅ |

## 4. 详细解决方案

### 4.1 SyncStatusService 接口设计 (对应用户原话：“解耦试点”)
```cpp
class SyncStatusService : public QObject {
    Q_OBJECT
public:
    static SyncStatusService& instance();
    
    // UI 查询接口：O(1) 无锁操作
    bool isSyncing() const;
    int pendingCount() const;

signals:
    // 节流后的信号：保证 200ms 内最多触发一次，确保 UI 丝滑
    void statusUpdated(bool isSyncing, int pendingCount);

private:
    SyncStatusService();
    void onRawSignalReceived(); // 接收底层的原始高频信号
    
    std::atomic<int> m_rawCount{0};
    QTimer* m_throttleTimer; // 节流计时器
};
```

### 4.2 高性能节流算法 (对应用户原话：“验证节流机制”、“操作丝滑”)
- **逻辑实现**：
    1.  **信号接入**：`SyncStatusService` 连接 `MetadataManager` 或异步队列的“任务进出”信号。
    2.  **计数更新**：使用 `std::atomic` 进行原子增减，主线程与后台线程无需互斥锁即可交换计数值。
    3.  **时间窗口节流**：
        - 接收到原始信号时，若 `m_throttleTimer` 未在运行，则启动一个 200ms 的单次计时器。
        - 计时器运行期间，屏蔽所有原始信号触发的 UI 通知。
        - 计时器 `timeout` 时，统一计算一次当前的 `isSyncing` 状态，并对外发射一次 `statusUpdated`。
- **效果**：无论后台持久化队列如何波动，`MainWindow` 的图标刷新频率被强制限制在 5Hz (200ms/次) 以下，人眼感知极度丝滑且无计算负担。

### 4.3 MainWindow 消费端重构 (对应用户原话：“操作流程丝滑”)
- **移除旧连接**：从 `MainWindow.cpp` 中删除 `connect(&MetadataManager::instance(), ...)`。
- **接入新 Service**：
    ```cpp
    // 在 MainWindow 初始化中
    connect(&SyncStatusService::instance(), &SyncStatusService::statusUpdated, 
            this, [this](bool syncing, int count) {
        updateSyncButtonUI(syncing); // 仅做简单的图标切换
    });
    ```
- **点击逻辑迁移**：原本 `m_btnSync` 的点击动作改为向 `SyncStatusService` 发送指令，由 Service 协调是否需要“强制清空异步队列”。

## 5. 修改边界声明【红线】

**本次方案涉及范围：**
- [ ] `src/core/SyncStatusService.cpp/h` (新建)
- [ ] `src/ui/MainWindow.cpp`：UI 连接重构。

**明确禁止越界修改的范围：**
- [ ] 禁止在 `SyncStatusService` 中调用任何会阻塞线程的 `sqlite3` API。
- [ ] 禁止在 UI 线程使用 `while` 循环等待同步完成。

## 6. 实现准则与预警【核心】
- **锁分离**：确保 Service 内部的节流计时器在主线程运行，而计数的修改来自后台线程。利用原子变量 (`std::atomic`) 替代互斥锁，彻底杜绝“线程竞争”导致的 UI 微卡顿。
- **编译防错**：需在 `CMakeLists.txt` 或相应工程文件中正确注册新创建的 Service 文件。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|-------------|----------------------|----------------|
| 同步按钮样式 | 存在待同步元数据时显示 ErrorRed | ✅ 符合 (通过 Service 节流信号驱动) |
| 信号/事件问题追踪 | 必须完整追踪链路 | ✅ 符合 |

## 8. 待确认事项（可选）
- 节流周期 200ms 为初始设定值，可根据实测反馈微调至 100ms。
