# Analysis_Modification_Plan-133: 初始化链条优化与逻辑一致性加固

本方案针对 Plan-132 的深层缺陷进行补完，旨在解决系统初始化过程中的时序冲突及边缘场景下的逻辑失效。

## 1. 初始化时序重构 (Boot Sequence)

### 1.1 现状分析
`CoreController::startSystem` 当前顺序：
1. `AutoImportManager::startListening()` (建立信号连接)
2. `MftReader::loadFromCache()` (构建索引并启动 Watcher)
3. `AutoImportManager::syncAllManagedLibraries()` (物理扫描)

### 1.2 风险
在第 2 步执行期间，若 Watcher 触发信号，`AutoImportManager` 的判定逻辑可能因索引未就绪而失效。

### 1.3 修正方案 (src/core/CoreController.cpp)
```cpp
// 调整后的顺序：
// 1. 构建内存索引 (Silent Mode)
MftReader::instance().loadFromCacheOnly(); // 需拆分接口，仅加载数据不启动监控

// 2. 建立信号连接
AutoImportManager::instance().startListening();

// 3. 激活 USN 监控 (Ignition)
MftReader::instance().activateWatchers(); // 此时索引已就绪，信号可安全处理
```

## 2. 增强型 FRN 判定安全性 (AutoImportManager)

### 2.1 循环依赖预防 (src/core/AutoImportManager.cpp)
```cpp
bool AutoImportManager::isUnderManagedLibrary(uint64_t key) {
    uint64_t currentFrn = key & 0x0000FFFFFFFFFFFFull;
    int depth = 0;
    const int MAX_DEPTH = 64; // 物理路径深度红线

    while (currentFrn != 5 && currentFrn != 0 && depth < MAX_DEPTH) {
        if (m_managedLibraryFrns.count(currentFrn)) return true;
        // ... (获取父级逻辑)
        depth++;
    }
    return false;
}
```

## 3. 驱动器在线校验 (MftReader)

### 3.1 物理存活检查 (src/mft/MftReader.cpp)
在 `loadFromCache` 循环启动 `UsnWatcher` 前增加：
```cpp
std::wstring root = driveName + L"\\";
if (GetDriveTypeW(root.c_str()) == DRIVE_NO_ROOT_DIR) {
    qDebug() << "[Mft] 跳过离线磁盘监控:" << QString::fromStdWString(driveName);
    continue;
}
```

## 4. 失效数据指纹强制策略 (MetadataManager)

### 4.1 逻辑覆盖补全 (src/meta/MetadataManager.cpp)
```cpp
if (current.isManaged && !current.isInvalid && current.ingestionStatus == 1) {
    // 只有在非失效状态下，指纹校验才生效
    if (mtime == current.mtime && size == current.fileSize) return;
}
```

## 5. 状态同步解耦 (MainWindow)

### 5.1 驱动器栏持久化逻辑
将 `MainWindow` 中的按钮创建逻辑改为由 `CoreController` 的信号驱动。
```cpp
// src/core/CoreController.h
signals:
    void driveStatusChanged(const QString& letter, bool hasLibrary);

// src/ui/MainWindow.cpp
connect(&CoreController::instance(), &CoreController::driveStatusChanged, this, &MainWindow::updateDriveBar);
```

## 6. 实施约束
*   **严禁**：在 `MftReader::loadFromCache` 内部直接发射 UI 信号，必须通过 `CoreController` 中转。
*   **要求**：所有物理磁盘属性获取操作必须增加 `INVALID_HANDLE_VALUE` 的异常分支处理。
