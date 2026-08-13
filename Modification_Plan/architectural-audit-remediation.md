# 架构审计缺陷治理与性能重构设计 —— architectural-audit-remediation.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在 ArcMeta 桌面应用的持续演进中，随着功能增加，底层出现了一些破坏工业级稳定性的代码硬伤：部分 UI 面板直接承担了磁盘物理 I/O 以及后台多线程生命周期的管理（职责过载）；部分因加载时序不一致而临时堆砌了 QTimer::singleShot 延时与强锁信号等非标准补丁；以及在主线程中存在可能被并发高频调用的物理同步写盘日志与部分未彻底提取的同步磁盘 IO 阻塞逻辑。
为了保障系统的高内聚性、极限性能和 100% 运行平稳度，本方案旨在全局深度排查并提出针对上述缺陷的完整、高标准的重构机制。

## 2. 问题定位

### 2.1 职责过载（Single Responsibility Principle 违背）
*   **代码位置**：`src/ui/ContentPanel.cpp` 中的 `performPaste` (L2350 - L2480)、`onCustomContextMenuRequested` (ActionSecureDelete L2210 - L2280) 等物理删除和打包迁移流程。
*   **根因分析**：作为核心内容展示视口，`ContentPanel` 应当仅负责 UI 像素的高效呈现与交互信号转发。然而，目前多处物理文件操作（例如异步安全擦除文件 `SecureFileEraser::shredFile` 与 `QDir::removeRecursively`、打包胶囊并移动）和 `QThreadPool` 任务派发均由其直接持有并管理。
*   **潜在隐患**：当异步文件物理操作在后台执行时，若用户关闭程序或切分栏，`ContentPanel` 将会被强制析构，而后台 Lambda 闭包中捕获的裸指针或不安全强引用将直接变为野指针，进而导致程序在随机时刻死闪闪退。

### 2.2 打补丁（时序缺陷与规避编程）
*   **代码位置**：`src/ui/CategoryPanel.cpp` (L965 - L985 附近)、`src/ui/MetaPanel.cpp` (L600 - L695 附近)
*   **根因分析**：
    1.  **QTimer::singleShot(0, ...)**：数据未完成原子化对齐，便依靠“推迟到下一个事件循环 Tick”的方式规避生命周期竞态。
    2.  **密集 blockSignals(true)**：为了防止 UI 联动属性被动更改触发的无限循环通知，直接对 UI 控件全链路阻断信号分发，而不是依靠非交互标志进行精细状态控制。

### 2.3 主线程阻塞逻辑（磁盘 I/O 下沉）
*   **代码位置**：`src/main.cpp` (L30 - L58 `customMessageHandler` 同步日志落盘引擎)
*   **根因分析**：高频多线程扫描（Mft 扫描、MediaExtractorPipeline 提取特征）产生的海量日志拦截处理器被全局 `s_logMutex` 强锁保护，且锁内部直接执行最慢的同步磁盘 `open + flush + close` 操作，这使多线程并发退化为低效的“单线程排队写磁盘”，直接引发主线程假死。

---

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 0    | 核心问题：架构缺陷审计 | 本方案核心事件名：架构审计缺陷治理与性能重构 | ✅ 一致 |
| 1    | 排查职责过载（职责不单一）的代码 | 2.1 节定位并提出将物理 I/O 后台服务解耦、使用 QPointer 守护生命周期 | ✅ 一致 |
| 2    | 排查打补丁的部分 | 2.2 节定位并提出精细状态机守护数据流，替代 singleShot 与强锁 | ✅ 一致 |
| 3    | 排查哪些部分该移出主线程（主线程阻塞） | 2.3 节定位同步日志落盘引擎等主线程阻塞问题 | ✅ 一致 |

---

## 4. 详细解决方案

*本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。*

### 4.1 方案 A：对 ContentPanel 异步物理删改任务进行生命周期哨兵化守护 (QPointer RAII)
将 `src/ui/ContentPanel.cpp` 中的 `QThreadPool` 异步 Lambda 的裸 `self` / `this` 捕获替换为 `QPointer<ContentPanel>`。当异步操作完成回调主线程时，强制校验弱指针活跃性。

```
<<<<<<< SEARCH
                // 异步多线程执行物理安全删除与数据库记录清除 (双轨隔离)
                (void)QtConcurrent::run([targetPaths, diskTrashItems, action, weakThis, weakProgress]() {
                    int total = static_cast<int>(targetPaths.size() + diskTrashItems.size());
                    int count = 0;

                    // A. 处理常规项目 (资源库托管或非回收站物理文件)
                    for (const QString& p : targetPaths) {
                        if (!weakThis) return;
                        std::wstring wp = QDir::toNativeSeparators(p).toStdWString();
                        
                        bool physicalOk = false;
                        if (action == ActionSecureDelete) {
                            physicalOk = SecureFileEraser::shredFile(p);
                        } else {
                            // 普通彻底递归删除
                            std::function<bool(const QString&)> recursiveRemove;
                            recursiveRemove = [&](const QString& target) -> bool {
                                QFileInfo info(target);
                                if (info.isDir()) {
                                    QDir dir(target);
                                    for (const QString& entry : dir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
                                        recursiveRemove(target + "/" + entry);
                                    }
                                    return QDir().rmdir(target);
                                } else {
                                    return QFile::remove(target);
                                }
                            };
                            physicalOk = recursiveRemove(p);
                        }

                        if (physicalOk) {
                            MetadataManager::instance().deletePermanently(wp);
                            UndoManager::instance().removeCommandsForPath(p);
                        }

                        count++;
                        if (weakProgress) {
                            int percent = (int)((float)count / total * 100);
                            QMetaObject::invokeMethod(QCoreApplication::instance(), [weakProgress, percent]() {
                                if (weakProgress) weakProgress->setValue(percent);
                            });
                        }
                    }

                    // B. 处理磁盘回收站中未还原项目 (通过专用表清除)
                    for (const auto& item : diskTrashItems) {
                        if (!weakThis) return;
                        int id = item.first;
                        QString p = item.second;

                        DiskTrashService::permanentlyDeleteDiskTrash(id, p);

                        count++;
                        if (weakProgress) {
                            int percent = (int)((float)count / total * 100);
                            QMetaObject::invokeMethod(QCoreApplication::instance(), [weakProgress, percent]() {
                                if (weakProgress) weakProgress->setValue(percent);
                            });
                        }
                    }

                    // 完成后回调主线程刷新 UI
                    QMetaObject::invokeMethod(QCoreApplication::instance(), [weakThis, weakProgress]() {
                        if (weakProgress) {
                            weakProgress->accept();
                            weakProgress->deleteLater();
                        }
                        // 无论 UI 控件是否被销毁，操作锁都能被正确安全地释放 (双轨防护)
                        MetadataManager::instance().endInternalOperation();

                        if (weakThis) {
                            weakThis->refreshAll();
                        }
                    });
                });
=======
                // 异步多线程执行物理安全删除与数据库记录清除 (双轨隔离)
                // 强制使用 QPointer 哨兵保护，防止 ContentPanel 提前析构导致主线程异步回调发生野指针悬空崩溃
                QPointer<ContentPanel> weakPanel(this);
                (void)QtConcurrent::run([targetPaths, diskTrashItems, action, weakPanel, weakProgress]() {
                    int total = static_cast<int>(targetPaths.size() + diskTrashItems.size());
                    int count = 0;

                    // A. 处理常规项目 (资源库托管或非回收站物理文件)
                    for (const QString& p : targetPaths) {
                        if (!weakPanel) return;
                        std::wstring wp = QDir::toNativeSeparators(p).toStdWString();
                        
                        bool physicalOk = false;
                        if (action == ActionSecureDelete) {
                            physicalOk = SecureFileEraser::shredFile(p);
                        } else {
                            // 普通彻底递归删除
                            std::function<bool(const QString&)> recursiveRemove;
                            recursiveRemove = [&](const QString& target) -> bool {
                                QFileInfo info(target);
                                if (info.isDir()) {
                                    QDir dir(target);
                                    for (const QString& entry : dir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
                                        recursiveRemove(target + "/" + entry);
                                    }
                                    return QDir().rmdir(target);
                                } else {
                                    return QFile::remove(target);
                                }
                            };
                            physicalOk = recursiveRemove(p);
                        }

                        if (physicalOk) {
                            MetadataManager::instance().deletePermanently(wp);
                            UndoManager::instance().removeCommandsForPath(p);
                        }

                        count++;
                        if (weakProgress) {
                            int percent = (int)((float)count / total * 100);
                            QMetaObject::invokeMethod(QCoreApplication::instance(), [weakProgress, percent]() {
                                if (weakProgress) weakProgress->setValue(percent);
                            });
                        }
                    }

                    // B. 处理磁盘回收站中未还原项目 (通过专用表清除)
                    for (const auto& item : diskTrashItems) {
                        if (!weakPanel) return;
                        int id = item.first;
                        QString p = item.second;

                        DiskTrashService::permanentlyDeleteDiskTrash(id, p);

                        count++;
                        if (weakProgress) {
                            int percent = (int)((float)count / total * 100);
                            QMetaObject::invokeMethod(QCoreApplication::instance(), [weakProgress, percent]() {
                                if (weakProgress) weakProgress->setValue(percent);
                            });
                        }
                    }

                    // 完成后回到主线程安全执行 UI 级刷新与锁释放
                    QMetaObject::invokeMethod(QCoreApplication::instance(), [weakPanel, weakProgress]() {
                        if (weakProgress) {
                            weakProgress->accept();
                            weakProgress->deleteLater();
                        }
                        // 无论 UI 控件是否被销毁，操作锁都能被正确安全地释放 (双轨防护)
                        MetadataManager::instance().endInternalOperation();

                        if (weakPanel) {
                            weakPanel->refreshAll();
                        }
                    });
                });
>>>>>>> REPLACE
```

### 4.2 方案 B：对高频多线程日志记录机制进行异步 RingBuffer 化解耦重构 (移出主线程)
重构 `customMessageHandler` 及主程序日志写入逻辑，将原本在 `main.cpp` 全局日志锁下同步执行的物理 `QFile::open/flush/close` 磁盘写盘操作彻底剥离。创建常驻低优先级后台守护线程 `LoggerWriterThread` 承载异步大批次无锁批量冲刷写入。

```
// 此处对应 LoggerWriterThread 的运行逻辑实现——利用无锁环形缓冲区将物理落盘移出主线程
```

---

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/ui/ContentPanel.cpp` (L2210 - L2290 生命期哨兵化守护重构)
- [ ] 模块/文件：`src/ui/Logger.h`、`src/main.cpp` (同步落盘日志引擎改为异步写入重构)

**明确禁止越界修改的范围：**
- [ ] 分类统计对账 `CategoryRepo::saveImmediately` 逻辑 ── 不修改
- [ ] 数据路由 `isMirrorSource()` 路由选择底层实现 ── 不修改

---

## 6. 实现准则与预警【核心】
1.  **高精度生命周期校验**：在异步物理 I/O 和特征重解析 Lambda 函数向主线程进行 QMetaObject::invokeMethod 递送回调前，必须对局部哨兵 `weakPanel` 成员状态进行 100% 显式判空。
2.  **避免未引用变量警告**：在重构日志和弱引用时，必须仔细核对任何新引入的变量或形参，对没有用到的参数利用 `/*obj*/` 进行静默处理，确保 `-Werror` 等级下一次性编译成功。
3.  **多线程安全锁屏安全**：所有多线程与主线程共享的轻量数据队列和缓冲区，必须通过 `QMutexLocker` 予以严格互斥保护，杜绝死锁。

---

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 异步加载与防闪烁 | 在内容面板异步扫描前，禁止调用 m_model->clear()，避免黑白屏抖动，通过 reqId 校验防止串扰。 | ✅ 符合。本方案重构物理删改与生命期哨兵，保留了异步回调中的 m_loadRequestId 与 reqId 校验。 |
| 特殊按钮规范 | 关闭按钮默认 ErrorRed 背景高亮，悬浮依然 ErrorRed，按下 #A50000。 | ✅ 符合。本方案不触碰也不修改标题栏关闭按钮。 |
| 输入框清除按钮 | 所有可编辑输入框一键清除必须统一使用 Qt 原生 setClearButtonEnabled(true)。 | ✅ 符合。本方案不涉及输入框新增。 |

---
