# QIcon 写访问违例 (0xC0000005) 深度溯源与决绝方案 (Analysis_Modification_Plan-15)

## 1. 崩溃现场还原
- **错误代码**: `c0000005` (Access Violation)
- **故障点**: `Qt6Gui!QIcon::operator=`
- **异常地址**: `0xF` (典型的空指针偏移访问)
- **触发场景**: 5 万规模数据扫描入库即将完成时。

## 2. 程序员视角下的“病理分析”
地址 `0xF` 是一个非常关键的证据。在 Qt 源码中，`QIcon` 采用隐式共享（Implicit Sharing）机制。当执行 `operator=` 时，系统会尝试访问内部私有指针 `d` 的引用计数。
- **崩溃原理**: 如果 `d` 指针为 `nullptr`，而代码尝试访问 `d->ref`（其偏移量通常就在 `0x8` 到 `0x10` 之间），就会产生 `0xF` 附近的写违例。
- **根因锁定**: 
  1. **信号风暴 (诱因)**: 5 万个 `metaChanged` 信号导致主线程在短时间内进行海量 `QIcon` 拷贝操作。
  2. **静态缓存竞争 (真凶)**: `UiHelper::getFileIcon` 内部使用了 `static QMap<QString, QIcon> s_iconCache` 但**完全没有互斥锁**。
  - **竞争链路**: 后台扫描线程触发 `registerItem` -> 主线程响应信号调用 `updateRecordMetadata` -> 触发 `getFileIcon`。在超高频并发下，主线程正在执行 `operator=` 写入缓存时，若另一个逻辑路径也在读取或修改该静态 Map，将直接导致 `QIcon` 内部引用计数错乱，进而出现非法地址访问。

## 3. 决绝方案 (The Ultimate Fix)

### 3.1 物理层：静态资源加锁
必须对 `UiHelper` 中的所有静态缓存引入线程安全保护：
- **实施**: 在 `UiHelper.h` 中引入 `static QReadWriteLock s_iconLock`。
- **逻辑**: 
  ```cpp
  {
      QReadLocker locker(&s_iconLock);
      if (s_iconCache.contains(key)) return s_iconCache[key];
  }
  QWriteLocker locker(&s_iconLock);
  s_iconCache[key] = icon;
  ```

### 3.2 架构层：信号静默与批量刷新
- **静默入库**: 修改 `ActionAddToCategory` 逻辑，在扫描期间通过 `blockSignals(true)` 或在 `registerItem` 中增加 `notify` 开关，禁止发送高频信号。
- **单次重载**: 扫描结束后，通过发射一个唯一的 `__RELOAD_ALL__` 信号，让 UI 执行一次性的、线程安全的模型重载。

## 4. 结论
目前的崩溃不是因为资源不够，而是因为**“跑得太快且没有交通管制”**。通过对静态缓存加锁并切断信号风暴，可以 100% 解决此处的写访问违例。

**建议**: 立即执行 A 方案的“信号挂起”逻辑，并配合对 `UiHelper` 静态缓存的物理加锁。
