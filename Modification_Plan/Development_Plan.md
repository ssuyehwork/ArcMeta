## [2026-07-28] 磁盘模式主线程 I/O 阻塞缺陷异步化重构

- 用户描述的现象/问题：在磁盘模式（DiskNav）下进行物理粘贴（performPaste）、物理拖放（onPathsDropped）和物理删除（moveToTrash）等操作时，由于直接在 UI 线程同步调用了阻塞系统函数（如 SHFileOperationW 和 QFile::rename），导致界面出现白屏、冻结、假死且没有进度交互。
- 用户期望的结果：设计将这些重型物理 I/O 阻塞操作移至后台多线程（QtConcurrent::run）异步执行的解决方案，彻底避免 UI 线程被阻塞，并在完成后回到主线程刷新视图。
- 本次任务边界：编写 Modification_Plan-12.md 技术方案，仅设计，不修改实际代码文件。
- 不在本次范围内的：对代码文件的实际物理修改。
- 对应方案文档：Modification_Plan-12.md
