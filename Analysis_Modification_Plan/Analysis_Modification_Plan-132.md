# Analysis_Modification_Plan-132: 架构逻辑“去毒”深度技术实施方案

本方案旨在为 AI 及开发团队提供极其详尽的代码修改指引，杜绝逻辑脑补，确保每一行变更都有据可依。

## 1. RAII 状态令牌实现 (DatabaseManager)

### 1.1 类定义 (src/meta/DatabaseManager.h)
```cpp
// 在 DatabaseManager 类外部定义
class SyncTaskToken {
public:
    SyncTaskToken() {
        DatabaseManager::instance().m_pendingTasksCount++;
        emit DatabaseManager::instance().pendingTasksCountChanged(DatabaseManager::instance().m_pendingTasksCount.load());
    }
    ~SyncTaskToken() {
        int count = --DatabaseManager::instance().m_pendingTasksCount;
        emit DatabaseManager::instance().pendingTasksCountChanged(count);
    }
};
```
*注意：需将 `DatabaseManager` 的 `m_pendingTasksCount` 设为 `friend` 或通过私有方法暴露给 `SyncTaskToken`。*

### 1.2 异步循环重构 (src/meta/DatabaseManager.cpp)
```cpp
void DatabaseManager::workerLoop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_queueCv.wait(lock, [this] { return m_stopWorker || !m_syncQueue.empty(); });
            if (m_stopWorker && m_syncQueue.empty()) break;
            task = std::move(m_syncQueue.front());
            m_syncQueue.pop_front();
        }
        // 移除原有的手动计数逻辑，改用 RAII
        if (task) {
            task(); 
        }
    }
}
```

## 2. 移除冗余异步持久化 (MetadataManager)

### 2.1 `persistAsync` 逻辑精简 (src/meta/MetadataManager.cpp)
*   **废除**：完全移除 `DatabaseManager::instance().enqueueSyncTask(...)` 的调用。
*   **理由**：在直连模式下，`memDb` 写入即磁盘写入。
*   **代码删减**：
```cpp
// 移除这一整块逻辑
DatabaseManager::instance().enqueueSyncTask([nPath, rMeta, memDb, sql, bindMeta]() {
    // ... 冗余的磁盘写入逻辑 ...
});
```

## 3. FRN 判定链优化 (AutoImportManager)

### 3.1 增加托管 FRN 缓存 (src/core/AutoImportManager.h)
```cpp
private:
    std::unordered_set<uint64_t> m_managedLibraryFrns; // 存储所有库根目录的物理 FRN
```

### 3.2 高效判定算法 (src/core/AutoImportManager.cpp)
```cpp
bool AutoImportManager::isUnderManagedLibrary(uint64_t key) {
    uint64_t currentFrn = key & 0x0000FFFFFFFFFFFFull;
    size_t dIdx = static_cast<size_t>(key >> 48);

    // O(log N) 向上回溯
    while (currentFrn != 5 && currentFrn != 0) {
        if (m_managedLibraryFrns.count(currentFrn)) return true;
        
        // 关键：通过 MftReader 暴露的 SoA 数据直接获取父级 FRN，无 I/O
        int idx = MftReader::instance().getIndexByKey(MftReader::makeKey(dIdx, currentFrn));
        if (idx < 0) break;
    // 需要在 MftReader 中公开接口：uint64_t getParentFrnOnly(int index) const { return m_parent_frns[index] & 0x0000FFFFFFFFFFFFull; }
    currentFrn = MftReader::instance().getParentFrnOnly(idx); 
    }
    return false;
}
```

## 4. 物理指纹准入校验 (MetadataManager)

### 4.1 `registerItem` 防抖重构 (src/meta/MetadataManager.cpp)
```cpp
void MetadataManager::registerItem(const std::wstring& path, bool authorized) {
    std::wstring nPath = normalizePath(path);
    
    // 增加物理指纹预检
    RuntimeMeta current = getMeta(nPath);
    if (current.isManaged && current.ingestionStatus == 1) {
        long long mtime = 0, size = 0;
        // 仅执行极速的 GetFileAttributesExW 获取物理属性
        if (fetchBasicPhysicalInfo(nPath, mtime, size)) { 
            if (mtime == current.mtime && size == current.fileSize) {
                return; // 指纹未变，拒绝冗余写入
            }
        }
    }
    
    // 只有指纹变更才继续执行后续的 0->1 状态切换
    // ... (原有注册逻辑)
}
```

## 5. MainWindow 职责剥离路线图

### 5.1 消息分流 (src/ui/MainWindow.cpp)
*   **移除**：`nativeEvent` 中关于 `WM_DEVICECHANGE` 的 Case 处理。
*   **动作**：在 `CoreController` 中创建一个隐形窗口或使用 Qt 的 `QStorageInfo` 轮询信号。
*   **中转信号**：
    `DeviceService -> emit driveChanged() -> CoreController -> AutoImportManager::syncAllManagedLibraries()`

## 6. 实施约束
*   **严禁**：在 `MftReader` 的读锁持有期间调用 `MetadataManager` 的写操作，防止 AB-BA 死锁。
*   **要求**：所有数据库写入必须包裹在 `SqlTransaction` 中以确保 WAL 性能。
*   **检查**：修改后必须运行 `PendingTaskCount` 冒烟测试，确保计数器在崩溃/异常路径下依然归零。
