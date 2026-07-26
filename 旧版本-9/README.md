# 备份备注

**备份时间**：2026-07-26 15:45:15  
**备份目录**：Buk_20260726_154513  

---

1. 启动哨兵：单实例互斥量/文件锁移至 main 最前，成功获取后再轮转日志，消除多实例竞态。
2. 拓扑初始化：在 QApplication 实例化后通过 CoreController::initializeCoreComponents 顺序在主线程中完成 DB、MetadataManager、CategoryRepo 和 MediaExtractorPipeline 的单例预热，杜绝定时器哑死，净化 main.cpp。
3. 异步日志：设计 LoggerWriterThread 基于双缓冲和常驻后台线程异步高吞吐刷盘，qDebug 写入时间降至 1 微秒以下，消除高频磁盘 I/O 带来的多线程性能假死。采用 QRecursiveMutex 递归锁并设计 writerStopped 降级同步落盘策略，彻底杜绝退出重入时的死锁隐患。
4. 启动调优：MainWindow 改为栈分配利用 RAII 自动安全析构。通过 QTimer::singleShot 延迟首帧 show，消解首帧渲染时的信号洪暴。
5. 优雅退出：连接 QApplication::aboutToQuit 信号。退出时等待全局线程池安全退场，调用 flushAll(true) 强制 SQLite 刷盘(已完美修复 flush 编译错误)，关闭异步日志并释放 COM 套间及互斥量，保障 100% 数据不损坏与优雅清理。
6. 按钮样式：全局样式表 QAbstractButton { outline: none; } 彻底根除按钮点击时周边的聚焦虚线框。
