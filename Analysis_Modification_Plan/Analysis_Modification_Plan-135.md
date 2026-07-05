# Analysis_Modification_Plan-135: 信号闭环与状态同步深度补全方案

本方案旨在修补 Plan-131~134 中遗留的高级逻辑冲突，确保系统在极端压力（洪流信号）与边缘场景（离线设备）下的绝对健壮。

## 1. 终结 USN “洪流”盲区 (Signal Tier)

### 1.1 问题描述
`MftReader` 在批处理 > 50 项时仅发射 `dataChanged(-1)`，导致 `AutoImportManager` 监听失效。

### 1.2 修复方案 (src/core/AutoImportManager.cpp)
*   **动作**：订阅 `dataChanged(int)` 信号。
*   **逻辑**：
```cpp
connect(&MftReader::instance(), &MftReader::dataChanged, this, [this](int index) {
    if (index == -1) {
        // 收到洪流信号，触发增量对账
        qDebug() << "[AutoImport] 检测到变动洪流，启动补偿性增量扫描";
        this->syncAllManagedLibraries(); 
    }
});
```

## 2. 统一同步状态视图 (UI Service Tier)

### 2.1 现状分析
`SyncStatusService` 漏掉了非数据库队列的异步任务。

### 2.2 修复方案 (src/core/SyncStatusService.cpp)
*   **动作**：引入多源计数器。
*   **代码逻辑**：
```cpp
// 监听三路信号
connect(&DatabaseManager::instance(), &DatabaseManager::pendingTasksCountChanged, ...);
connect(&MetadataManager::instance(), &MetadataManager::ingestionQueueChanged, ...); // 需新增此信号
connect(&MftReader::instance(), &MftReader::metadataTasksChanged, ...); // 需新增此信号

// updateState 逻辑改为求和
void SyncStatusService::updateState() {
    int total = dbCount + ingestionCount + mftCount;
    m_pendingCount.store(total);
    // ... 触发节流发射
}
```

## 3. 防御型 FRN 回溯 (Algorithm Tier)

### 3.1 风险规避
预防 NTFS 循环挂载导致的死循环。

### 3.2 伪代码实现 (src/core/AutoImportManager.cpp)
```cpp
bool AutoImportManager::isUnderManagedLibrary(uint64_t key) {
    uint64_t currentFrn = key & 0x0000FFFFFFFFFFFFull;
    int safetyCounter = 0;
    while (currentFrn != 5 && currentFrn != 0) {
        if (safetyCounter++ > 100) break; // 强制熔断，防止 Junctions 环路
        if (m_managedLibraryFrns.count(currentFrn)) return true;
        // ... (回溯逻辑)
    }
    return false;
}
```

## 4. 离线驱动器静默化 (MFT Tier)

### 4.1 逻辑加固 (src/mft/MftReader.cpp)
*   **动作**：在 `UsnWatcher` 构造函数或启动前增加 `DRIVE_FIXED` 校验，非固定盘或离线盘严禁启动监控线程。

## 5. 实施约束
*   **警告**：`syncAllManagedLibraries` 在洪流触发时必须经过 `debounce` 处理，防止因 USN 频繁洪流导致扫描任务重叠。
*   **要求**：`MetadataManager` 的 `m_isRecounting` 标志位必须作为全局原子变量暴露，供 `AutoImportManager` 判定是否需要跳过某些冗余信号。
