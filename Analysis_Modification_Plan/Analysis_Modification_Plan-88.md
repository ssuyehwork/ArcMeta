# ArcMeta 性能瓶颈重构修复方案 —— Analysis_Modification_Plan-88.md

## 1. 任务背景
基于 `Analysis_Modification_Plan-87.md` 的审计结果，ArcMeta 目前存在严重的主线程 I/O 阻塞、大粒度锁竞争以及高频重复计算问题。本方案提供一套详细的异步化与锁优化重构路径，旨在彻底解决 UI 假死，提升大批量数据操作下的系统流畅性。

## 2. 问题定位
- **阻塞源 A**：`MetadataManager::registerItem` 同步链条（Win32 I/O + 图像像素级分析）。
- **阻塞源 B**：`TagManagerView` 直接在 UI 线程执行数据库写操作。
- **竞争源**：`m_mutex` 在执行磁盘 I/O（`CreateFileW` 等）期间被写锁锁定。
- **死锁隐患**：`renameTag` 等方法在持有 `unique_lock` 时调用 `debouncePersist`，引发锁递归。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 维度一：解决主线程同步 I/O | 方案 4.1 (registerItemsBatch), 4.4 (DB Async) | ✅ |
| 2    | 维度二：优化锁持有时间 | 方案 4.2 (Lock-Free I/O) | ✅ |
| 3    | 维度三：后台任务线程安全 | 方案 4.3 (Atomic Status), 6.1 (COM) | ✅ |
| 4    | 维度四：信号频率与死锁修复 | 方案 4.5 (Recursive Lock Fix) | ✅ |

## 4. 详细解决方案

### 4.1 MetadataManager：registerItem 全异步化与批量入队
**修改位置**：`MetadataManager.h / .cpp`
**具体逻辑**：
1. **接口变更**：废弃 UI 直接调用 `registerItem` 的模式。新增 `registerItemsAsync(const QStringList& paths)`。
2. **异步执行链**：使用 `QtConcurrent::run` 包装注册逻辑。
```cpp
// MetadataManager.cpp 伪代码实现
void MetadataManager::registerItemsAsync(const QStringList& paths) {
    (void)QtConcurrent::run([this, paths]() {
        #ifdef Q_OS_WIN
        CoInitializeEx(NULL, COINIT_APARTMENTTHREADED); // 赋予 Shell 能力
        #endif
        for (const auto& qp : paths) {
            std::wstring nPath = normalizePath(qp.toStdWString());
            ensureActivated(nPath); // 已优化为锁外 I/O
            tryExtractDimensions(nPath);
            syncPhysicalMetadata(nPath, false); // 内部 persistAsync 应进入后台
            tryExtractColor(nPath); 
            notifyUI(RefreshLevel::PathUpdate, qp);
        }
        CoUninitialize();
    });
}
```

### 4.2 ensureActivated 锁粒度优化（杜绝锁内 I/O）
**目标**：将 Win32 I/O 移出 `unique_lock` 保护区。
```cpp
void MetadataManager::ensureActivated(const std::wstring& nPath) {
    // 1. 读锁检查
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        if (m_cache.find(nPath) != m_cache.end()) return;
    }

    // 2. 锁外同步获取物理属性 (耗时操作)
    RuntimeMeta rm;
    std::wstring frn, type;
    if (fetchWinApiMetadataDirect(nPath, rm.fileId128, &frn, &rm.fileSize, &type, &rm.ctime, &rm.mtime, &rm.atime)) {
        rm.isFolder = (type == L"folder");
        
        // 3. 写锁写入缓存
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        if (m_cache.count(nPath)) return; // 二次检查防止竞态

        // 共享元数据逻辑 (FID 关联)
        if (!rm.fileId128.empty() && m_fidToPath.count(rm.fileId128)) {
            const RuntimeMeta& existing = m_cache[m_fidToPath[rm.fileId128]];
            rm.rating = existing.rating;
            rm.color = existing.color;
            // ... (补全所有 RuntimeMeta 字段)
        }
        m_cache[nPath] = rm;
        if (!rm.fileId128.empty()) m_fidToPath[rm.fileId128] = nPath;
        // 索引同步逻辑 (m_fileNameToFids 等) 也移入此处
    }
}
```

### 4.3 ContentPanel：批量归类 (ActionCategorize) 异步化
**修改位置**：`ContentPanel.cpp` 中的 `switch (action)` 块。
**具体逻辑**：
- 在 `ActionCategorize` 分支中，不再直接对每个循环项调用 `registerItem`。
- 将所有需要注册的 `wPath` 收集到 `QStringList`，一次性丢入 `MetadataManager::registerItemsAsync`。

### 4.4 TagManagerView：数据库异步写入队列
**修改位置**：`TagManagerView.cpp`
**具体逻辑**：
- 将 `addTagToGroup` / `removeTagFromGroup` / `renameGroup` / `deleteGroup` 逻辑改为异步执行。
- **模板示例**：
```cpp
void TagManagerView::addTagToGroup(const QString& tagName, int groupId) {
    QPointer<TagManagerView> weakThis(this);
    (void)QtConcurrent::run([weakThis, tagName, groupId]() {
        sqlite3* db = DatabaseManager::instance().getMemoryDb(L"C");
        if (!db) return;
        // ... 执行 SQL ...
        QMetaObject::invokeMethod(weakThis.data(), "refresh", Qt::QueuedConnection);
    });
}
```

### 4.5 递归死锁修复 (Internal Dirty Push)
**问题**：`renameTag` 持有 `m_mutex` 期间调用 `debouncePersist`，后者又尝试获取 `m_mutex`。
**修复建议**：
1. 在 `MetadataManager` 私有域新增 `void pushToDirty_NoLock(const std::wstring& path)`。
2. `renameTag` 和 `removeTag` 内部改用此无锁推送方法，并在函数退出前统一触发计时器。

## 5. 修改边界声明【红线】

**本次方案涉及范围：**
- [ ] `src/meta/MetadataManager.cpp`: 锁结构调整、新增异步注册方法、死锁路径修复。
- [ ] `src/ui/ContentPanel.cpp`: 归类 Action 流程异步化调整。
- [ ] `src/ui/TagManagerView.cpp`: 所有数据库写操作改为异步 Task。

**明确禁止越界修改的范围：**
- [ ] 禁止修改 `MftReader.cpp` 的 USN 枚举核心算法。
- [ ] 禁止修改任何 `.db` 物理文件的预设表结构。
- [ ] 禁止修改 `FerrexVirtualDbModel` 的基础渲染逻辑。

## 6. 实现准则与预警【核心】
1. **COM 预警**：所有后台线程调用的 Win32 API 涉及路径处理或 Shell 扩展时，必须在线程起始位置调用 `CoInitializeEx(NULL, COINIT_APARTMENTTHREADED)`，并在退出前调用 `CoUninitialize()`。
2. **锁顺序预警**：在 `ensureActivated` 的优化中，必须确保“先释放读锁，再获取写锁”。严禁在未释放读锁的情况下尝试获取写锁，这在 `std::shared_mutex` 中可能导致死锁（取决于具体实现对升级锁的支持）。
3. **UI 回调预警**：异步任务完成后回传 UI 线程必须使用 `QMetaObject::invokeMethod` 且配合 `QPointer` 防护，杜绝已销毁窗口的非法访问。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|-------------|----------------------|----------------|
| 锁管理规范 | DatabaseManager flushAll in destructor | ✅ 本方案通过异步写库确保不阻塞，退出逻辑保持 flushAll 完整性 |
| 信号风暴解决 | 采用单例计时器与脏路径集解决计时器风暴 | ✅ 本方案通过 registerItemsBatch 进一步强化了批处理能力 |
| 意图判断 | 区分事实查询与分析委托 | ✅ 已严格执行共识流程 |

## 8. 待确认事项（可选）
- **UI 进度提示**：大批量归类（如选中 1000 个文件）在后台异步注册时，是否需要在 `ContentPanel` 顶部显示一个微型的“正在同步物理元数据...”进度条？目前方案默认为静默后台处理。
