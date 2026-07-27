# 悬浮进度条体验升级：扫描数据中语境、预计耗时 (ETA) 与从左向右推进彻底落地 —— Modification_Plan-106.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在上一代方案（Plan-105）中，设计的进度条和文案存在一些体验上的小瑕疵：
1. 语境文案为“正在同步元数据”，不够贴合数据感知的“扫描”语境。
2. 耗时展示仅为已消耗的累计时长，无法直观反映任务还需要多久结束，不符合工业级体验。
3. 同步开始时直接采用 `100 - count` 导致百分比呈现倒退，产生了进度条“从右向左”缩短的负向视觉错觉。

本方案承接自 `Modification_Plan-105.md`（旧方案已作为铁证永久只读保留，不再修改，符合 3.1.1 规范），在其基础上对文案、剩余预计时间（ETA）推算公式以及进度推进的几何方向进行彻底优化重构。

## 2. 问题定位
- **语境定位**：状态栏常态展示与数据扫描高内聚。启动时应呈现 **“扫描数据中...”**，完成时展示 **“数据扫描完成”**。
- **预计耗时（ETA）算法推算**：
  已知当前已消耗时长为 $T_{elapsed}$（秒），当前已完成进度为 $P$（百分比，即当前 `value()`）。
  则整个任务的预计总时长为 $T_{total} = T_{elapsed} / (P / 100.0)$，预计剩余时长（即还需要多久完成）为：
  $$T_{remaining} = T_{total} - T_{elapsed} = T_{elapsed} \times \frac{100 - P}{P}$$
  为了在任务启动初期（数据量极小或波动时）避免产生剧烈抖动，在进度 $P < 5\%$ 时，耗时显示统一兜底为 `预计耗时: 计算中...`。
- **自左向右绝对递增推进**：
  在 `SyncStatusService::statusUpdated` 首次捕获 `syncing == true` 瞬间，将待处理项 `pendingCount` 锁定为当前批次的总任务量 **`m_totalBatchCount`**。
  后续随着扫描推进，`pendingCount` 逐步减小。
  当前已完成的任务项数为 `completedCount = m_totalBatchCount - pendingCount`。
  当前进度的绝对物理百分比计算为：
  $$P = \text{qBound}\left(1, \frac{completedCount}{m_totalBatchCount} \times 100, 99\right)$$
  该百分比具有从 1 逐渐向 99 单调递增的物理特性（完成时瞬间拉满到 100%），从而配合 `setInvertedAppearance(false)`，彻底锁定进度条**“由左向右单向平滑扩展”**的运动轨迹。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 文案更正为“扫描数据中...”（用户原话） | 状态栏文案完美更正，且结束时更正为“数据扫描完成”。 | ✅ |
| 2    | 预计耗时 = 已消耗 * (100 - P) / P 算法，以及 < 5% 提示计算中（用户原话） | 引入该公式推导预计剩余时间，并在 P < 5% 时显示“计算中...”。 | ✅ |
| 3    | 锁定在任务开始时拦截并记录初始待处理项总量 `m_totalBatchCount`（用户原话） | 新增 `m_totalBatchCount` 并在首次激发时锁定记录。 | ✅ |
| 4    | 严格计算【由左向右】递增的百分比：已完成 / 总项数（用户原话） | 采用已完成除以总项数再转换为百分比，保证进度条绝对不倒退。 | ✅ |
| 5    | 5px 悬浮进度条、不加布局、绝对定位（承接 Plan-105 原话） | 承接并完整保留 5px 悬浮条、`centralC` 绝对定位、`raise()` 提层等底层不抖动方案。 | ✅ |

## 4. 详细解决方案

### 4.1 头文件成员变量扩充
在 `src/ui/MainWindow.h` 中，相比 Plan-105 新增或扩充以下私有成员变量：
```cpp
private:
    QProgressBar* m_topProgressBar = nullptr; // 5px 悬浮覆盖层进度条
    QTimer* m_elapsedTimer = nullptr;         // 实时耗时与 ETA 刷新定时器
    qint64 m_syncStartTime = 0;               // 扫描开始时间戳 (毫秒)
    int m_totalBatchCount = 0;                // 当前批次扫描的任务总项数
```

### 4.2 进度条方向物理属性锁定
在 `src/ui/MainWindow.cpp` 的 `setupSplitters()` 进度条初始化位置，显式锁定递增外观：
```cpp
    m_topProgressBar->setInvertedAppearance(false); // 🚨 强制方向：绝对由左向右平滑推进！
```

### 4.3 预计剩余耗时（ETA）刷新器实现
在 `src/ui/MainWindow.cpp` 的 `initUi()` 底部，连接 `m_elapsedTimer` 的 `timeout` 信号：
```cpp
    connect(m_elapsedTimer, &QTimer::timeout, this, [this]() {
        if (m_syncStartTime > 0) {
            double elapsedSec = (QDateTime::currentMSecsSinceEpoch() - m_syncStartTime) / 1000.0;
            int currentPct = m_topProgressBar->value();
            
            // 动态推算预计剩余耗时 (ETA)
            QString etaStr = "计算中...";
            if (currentPct >= 5) {
                double estRemainingSec = elapsedSec * (100.0 - currentPct) / (double)currentPct;
                etaStr = QString("%1s").arg(QString::number(estRemainingSec, 'f', 1));
            }

            // 联动更新文本
            m_statusLeft->setText(QString("扫描数据中... %1%  |  预计耗时: %2")
                                  .arg(currentPct)
                                  .arg(etaStr));
        }
    });
```

### 4.4 同步服务精确联动（百分比单调递增）
在 `src/ui/MainWindow.cpp` 的 `initUi()` 底部，连接后台同步服务：
```cpp
    connect(&SyncStatusService::instance(), &SyncStatusService::statusUpdated,
            this, [this](bool syncing, int pendingCount) {
        if (syncing && pendingCount > 0) {
            // --- 扫描任务启动 ---
            if (m_syncStartTime == 0) {
                m_syncStartTime = QDateTime::currentMSecsSinceEpoch();
                m_totalBatchCount = pendingCount; // 锁定初始任务总量
                m_elapsedTimer->start();
                updateProgressBarGeometry();
                
                m_topProgressBar->setValue(1); // 初始展现 1%
                m_topProgressBar->show();
            }
            
            // 动态加固：防止扫描过程中突然有新追加的大批次项导致百分比计算溢出
            if (pendingCount > m_totalBatchCount) {
                m_totalBatchCount = pendingCount;
            }

            // 严格计算自左向右递增的百分比
            int completedCount = m_totalBatchCount - pendingCount;
            int pct = qBound(1, (int)((double)completedCount / m_totalBatchCount * 100), 99);
            
            m_topProgressBar->setValue(pct); // 百分比单调递增，推动进度条平滑从 Left -> Right
        } else {
            // --- 扫描任务完成 ---
            if (m_syncStartTime > 0) {
                m_topProgressBar->setValue(100); // 极光条充满拉满至最右侧
                m_elapsedTimer->stop();
                
                double totalSec = (QDateTime::currentMSecsSinceEpoch() - m_syncStartTime) / 1000.0;
                m_statusLeft->setText(QString("数据扫描完成  |  实际耗时: %1s").arg(QString::number(totalSec, 'f', 1)));
                
                // 400ms 后隐藏，3秒后恢复常态
                QTimer::singleShot(400, this, [this]() {
                    m_topProgressBar->hide();
                    m_syncStartTime = 0;
                    m_totalBatchCount = 0;
                    QTimer::singleShot(3000, this, [this]() {
                        updateStatusBar(); // 复位状态栏
                    });
                });
            }
        }
    });
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] `src/ui/MainWindow.h`：添加 `m_totalBatchCount` 变量声明及 `resizeEvent` 等相关结构。
- [ ] `src/ui/MainWindow.cpp`：升级 `setupSplitters` 初始化、`resizeEvent` 及 `updateProgressBarGeometry`，并彻底重构 `initUi` 中的定时器计时与进度信号连接。

**明确禁止越界修改的范围：**
- [ ] `SyncStatusService` 后台异步服务的状态转换逻辑——不修改
- [ ] 系统常态底栏重置函数 `updateStatusBar` 内部统计——不修改

## 6. 实现准则与预警【核心】

1. **防除以零崩溃（Zero Division Guard）**：
   预计耗时公式中，分母为 `currentPct`。在代码中必须加设 `currentPct >= 5` 安全屏障，绝对杜绝当 `currentPct` 为 0 时执行除法导致系统崩溃的死穴。
2. **百分比安全限幅（Boundary Clamp）**：
   在计算百分比时，必须采用 `qBound(1, pct, 99)` 进行严密包裹。确保在尚未真正完成前，百分比绝不会由于临时四舍五入提前跳到 `100%`，也绝不会出现越界负值。
3. **动态总量补偿（Dynamic Total Compensation）**：
   扫描过程中有可能在后续管道中突然追加新的任务数（导致 `pendingCount > m_totalBatchCount`）。方案中加入了 `if (pendingCount > m_totalBatchCount) { m_totalBatchCount = pendingCount; }` 动态防爆自适应，确保百分比分母永不低于分子，保证进度条绝对单调递增不紊乱。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 动画/计时器 | 超时操作使用毫秒级 `QTimer` 在主线程内处理，定时器具有父对象绑定或成员级释放，绝无内存/资源泄漏 | ✅ 符合 |
| 边界限制 | 精确计算各子面板几何关系，Y 轴公式对齐 `statusBar->geometry().top() - 5` | ✅ 符合 |

## 8. 待确认事项（可选）
- 暂无待确认事项。
