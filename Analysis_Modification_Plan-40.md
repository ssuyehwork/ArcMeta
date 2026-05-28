## 分析计划 #40 ｜ 2026-06-xx

### § 0 需求原文
> MftReader 与 UsnWatcher 架构深度审计，解决上帝类耦合、状态管理混乱、IO 效率低下问题。支持真 V3 128位 FRN，引入双缓冲 compact，升级重叠 IO。

### § 1 现状诊断
- MftReader 职责过度堆叠。
- 并发锁机制混乱。
- 内存池 compact 导致 UI 掉帧。
- UsnWatcher 强耦合且使用低效轮询。

### § 2 方案设计
- 剥离 MftDataStore、NtfsEngine、SyncManager。
- 引入 Frn128 结构。
- UsnWatcher 升级 Overlapped IO。
- 分区锁与双缓冲技术。

### § 3 逐文件改动计划
- 新增 MftDataStore.h/cpp, NtfsEngine.h/cpp, SyncManager.h/cpp。
- 修改 MftReader.h/cpp, UsnWatcher.h/cpp。
- 修改 CMakeLists.txt 包含新文件。

### § 8 审批状态
⏳ 等待用户审批
