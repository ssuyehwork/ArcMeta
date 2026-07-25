# 修复托管库搬移文件夹解析颜色及存储失效问题 —— Modification_Plan-59.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
当用户将含有图形图像文件的文件夹迁移到 `ArcMeta.Library_[盘符]` 托管库后，系统常出现无法成功提取和持久化文件夹及图片代表色，或者提取颜色后重启失效等问题。但在选中单项手动重新解析时一切正常。本次方案旨在解决底层多线程数据库死锁、共享连接句柄竞态冲突、内存数据库备份失败时脏标记提前被无条件清除导致的数据丢失，以及文件夹在 categories 与 metadata 之间颜色不一致的上下文冲突，并额外添加详细的调试日志用于追踪定位问题。

## 2. 问题定位
1. **多线程句柄“裸奔”竞态**：`MetadataManager::updateIngestionStatus` 触发父目录进度统计任务 `calculateAndPersistProgress`（运行在全局线程池 `QThreadPool`）时，会有上百个线程同时共享使用同一个内存数据库 `sqlite3*` 连接句柄，导致严重的 `SQLITE_BUSY`/`SQLITE_LOCKED` 并发锁冲突，使得主流水线写入失败。
2. **备份脏标记机制缺陷**：`DatabaseManager::flushAll` 调用 `saveDb` 备份内存数据库到硬盘时，若高并发写入未完成导致备份失败，脏标记 `m_isDirty` 依然会被无条件重置为 `false`，从而导致已提取出的颜色数据永久丢失。
3. **文件夹代表色逻辑 Gap**：文件夹本身提取出的颜色写入在 `metadata` 表中，而侧边栏分类树组件渲染只读取并展示 `categories` 分类表中的 `color`，两者数据脱节。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 如果要修复，那么该怎么修复呢？请将修复方案描述出来 | 制定包含互斥锁、安全落盘机制、文件夹代表色双向同步的核心修改方案 | ✅ 一致 |
| 2    | 添加调试日志以便追踪并定位问题的所在 | 在每个关键落盘、加锁、多线程计算、跨表更新及备份失败步骤添加极尽详实的 `qDebug()` / `qWarning()` 调试日志 | ✅ 一致 |

## 4. 详细解决方案

### 核心修改 1：复用盘符互斥锁，保护多线程共享连接句柄
在 `MetadataManager::persistAsync` 和 `MetadataManager::calculateAndPersistProgress` 中加锁，防止多线程同时执行 SQL 导致的 sqlite 锁冲突或崩溃。

**对 `MetadataManager::persistAsync` 的修改：**
```cpp
void MetadataManager::persistAsync(const std::wstring& path, bool notify, bool authorized) {
    WriteGuard guard;
    std::wstring nPath = MetadataManager::normalizePath(path);
    
    RuntimeMeta rMeta = getMeta(nPath);
    sqlite3* memDb = nullptr;
    std::wstring volSerial;
    
    if (nPath.length() == 3 && nPath[1] == L':' && (nPath[2] == L'\\' || nPath[2] == L'/')) {
        memDb = DatabaseManager::instance().getGlobalDb();
    } else {
        volSerial = getVolumeSerialNumber(nPath);
        QString letter = (nPath.length() >= 2 && nPath[1] == L':') ? QString::fromWCharArray(&nPath[0], 1) : "";
        memDb = DatabaseManager::instance().getMemoryDb(volSerial, letter);
    }
    if (!memDb) {
        qWarning() << "[DB_TRACE] persistAsync 失败：未能获取 memDb，路径:" << QString::fromStdWString(nPath);
        return;
    }

    // 【新增调试日志与互斥锁保护】
    std::shared_ptr<std::mutex> dbLock;
    if (!volSerial.empty()) {
        dbLock = DatabaseManager::instance().getDriveMutex(volSerial);
    }
    std::unique_lock<std::mutex> lockConn;
    if (dbLock) {
        lockConn = std::unique_lock<std::mutex>(*dbLock);
        qDebug() << "[DB_TRACE] persistAsync 成功锁定驱动盘互斥锁，开始执行写入，路径:" << QString::fromStdWString(nPath);
    }

    // 1. 内存库操作 (Memory Commit)
    bool isNew = true;
    {
        sqlite3_stmt* checkStmt;
        if (sqlite3_prepare_v2(memDb, "SELECT 1 FROM metadata WHERE file_id = ?", -1, &checkStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(checkStmt, 1, rMeta.fileId128.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(checkStmt) == SQLITE_ROW) isNew = false;
            sqlite3_finalize(checkStmt);
        }
    }
    // ... 后续绑定和准备逻辑 ...
    if (sqlite3_step(memStmt) == SQLITE_DONE) {
        qDebug() << "[DB_TRACE] persistAsync 写入内存库成功，是否新项:" << isNew << "路径:" << QString::fromStdWString(nPath);
    } else {
        qWarning() << "[DB_TRACE] persistAsync 写入内存库失败！Error:" << sqlite3_errmsg(memDb) << "路径:" << QString::fromStdWString(nPath);
    }
    // ...
}
```

**对 `MetadataManager::calculateAndPersistProgress` 的修改：**
```cpp
void MetadataManager::calculateAndPersistProgress(const std::wstring& folderPath) {
    std::wstring nFolder = normalizePath(folderPath);
    
    // 1. 获取库归属数据库
    std::wstring volSerial = getVolumeSerialNumber(nFolder);
    QString letter = (nFolder.length() >= 2 && nFolder[1] == L':') ? QString::fromWCharArray(&nFolder[0], 1) : "";
    sqlite3* db = DatabaseManager::instance().getMemoryDb(volSerial, letter);
    if (!db) {
        qWarning() << "[DB_TRACE] calculateAndPersistProgress 失败：无法取得分库，文件夹:" << QString::fromStdWString(nFolder);
        return;
    }

    // 【新增调试日志与互斥锁保护】
    auto dbLock = DatabaseManager::instance().getDriveMutex(volSerial);
    std::lock_guard<std::mutex> lockConn(*dbLock);
    qDebug() << "[DB_TRACE] calculateAndPersistProgress 开始计算导入进度，获取连接互斥锁，文件夹:" << QString::fromStdWString(nFolder);

    // 2. 执行统计逻辑
    int count0 = 0;
    int count1 = 0;
    // ...
    // 执行 INSERT OR REPLACE INTO system_stats ...
    if (sqlite3_step(stmt) == SQLITE_DONE) {
        qDebug() << "[DB_TRACE] calculateAndPersistProgress 写入进度数据成功，进度:" << progress << "文件夹:" << QString::fromStdWString(nFolder);
    } else {
        qWarning() << "[DB_TRACE] calculateAndPersistProgress 写入进度失败！Error:" << sqlite3_errmsg(db);
    }
}
```

### 核心修改 2：安全落盘备份与脏标记校验机制
重构 `DatabaseManager::saveDb` 返回 `bool`，在 `flushAll` 中进行判断：

```cpp
bool DatabaseManager::saveDb(DbConnection& conn, bool forceFull) {
    if (!conn.diskDb || !conn.memDb) {
        qWarning() << "[DB_TRACE] saveDb 失败：连接句柄为空，路径:" << QString::fromStdWString(conn.diskPath);
        return false;
    }

    (void)forceFull;
    sqlite3_backup* backup = sqlite3_backup_init(conn.diskDb, "main", conn.memDb, "main");
    if (backup) {
        int rc = sqlite3_backup_step(backup, -1);
        sqlite3_backup_finish(backup);
        if (rc == SQLITE_DONE) {
            qDebug() << "[DB_TRACE] saveDb 成功备份内存数据库至硬盘！路径:" << QString::fromStdWString(conn.diskPath);
            return true;
        } else {
            qWarning() << "[DB_TRACE] saveDb 备份到硬盘中途失败！错误代码:" << rc << "路径:" << QString::fromStdWString(conn.diskPath);
            return false;
        }
    } else {
        qWarning() << "[DB_TRACE] saveDb 初始化备份失败！错误:" << sqlite3_errmsg(conn.diskDb) << "路径:" << QString::fromStdWString(conn.diskPath);
        return false;
    }
}
```

重构 `DatabaseManager::flushAll`：
```cpp
void DatabaseManager::flushAll(bool forceFull) {
    MetadataManager::instance().slideRecentWindow();

    if (!m_isDirty.load()) {
        qDebug() << "[DB_TRACE] flushAll 跳过：当前没有脏数据需要备份。";
        return;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    qDebug() << "[DB_TRACE] flushAll 开始将所有脏数据库备份到硬盘...";

    bool allSucceeded = true;
    if (!saveDb(m_globalDb, forceFull)) {
        allSucceeded = false;
        qWarning() << "[DB_TRACE] flushAll: 全局库备份失败！";
    }
    for (auto& pair : m_driveDbs) {
        if (!saveDb(pair.second, forceFull)) {
            allSucceeded = false;
            qWarning() << "[DB_TRACE] flushAll: 磁盘分库备份失败，序列号:" << QString::fromStdWString(pair.first);
        }
    }
    
    if (allSucceeded) {
        qDebug() << "[DB_TRACE] flushAll: 所有分库已全部成功持久化落盘，清空脏标记。";
        m_isDirty.store(false);
    } else {
        qWarning() << "[DB_TRACE] flushAll: 存在分库备份失败！保留脏标记以防数据丢失，等候重试。";
    }
}
```

### 核心修改 3：文件夹解析色同步写至 `categories` 关联表（消除界面逻辑 Gap）
在 `CategoryRepo.cpp` 中定义根据路径更新分类颜色的方法：

```cpp
bool CategoryRepo::updateCategoryColorByPath(const std::wstring& path, const std::wstring& color) {
    WriteGuard guard;
    sqlite3* memDb = DatabaseManager::instance().getGlobalDb();
    if (!memDb) return false;

    const char* sql = "UPDATE categories SET color = ? WHERE physical_path = ?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(memDb, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text16(stmt, 1, color.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(stmt, 2, path.c_str(), -1, SQLITE_TRANSIENT);
        bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        if (ok) {
            qDebug() << "[DB_TRACE] updateCategoryColorByPath 成功同步更新 categories 表分类颜色，路径:" << QString::fromStdWString(path) << "颜色:" << QString::fromStdWString(color);
            DatabaseManager::instance().flushAll();
            return true;
        }
    }
    qWarning() << "[DB_TRACE] updateCategoryColorByPath 执行失败！路径:" << QString::fromStdWString(path);
    return false;
}
```

在 `MetadataManager::setItemVisualMetadata` 尾端执行调用：
```cpp
void MetadataManager::setItemVisualMetadata(const std::wstring& path, const std::wstring& color, const QVector<QPair<QColor, float>>& palettes, bool notify) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    ensureActivated(nPath);
    std::vector<PaletteEntry> entries;
    for (int i = 0; i < palettes.size(); ++i) { entries.push_back(PaletteEntry(palettes[i].first, palettes[i].second)); }
    
    bool isFolder = false;
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        RuntimeMeta& meta = m_cache[nPath];
        meta.color = color;
        meta.palettes = entries;
        isFolder = meta.isFolder;
    }
    
    // 【新增同步】文件夹主色提取时，同步更新 categories 关联表
    if (isFolder) {
        qDebug() << "[DB_TRACE] setItemVisualMetadata 判定为文件夹，触发 categories 颜色同步更新。路径:" << QString::fromStdWString(nPath);
        CategoryRepo::updateCategoryColorByPath(nPath, color);
    }
    
    if (notify) notifyUI(RefreshLevel::PathUpdate, QString::fromStdWString(nPath));
    persistAsync(nPath);
}
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/meta/MetadataManager.cpp`（加锁及文件夹主色同步触发逻辑）
- [ ] 模块/文件：`src/meta/DatabaseManager.cpp`（`saveDb` 状态重构与 `flushAll` 安全清脏机制）
- [ ] 模块/文件：`src/meta/CategoryRepo.h` / `src/meta/CategoryRepo.cpp`（新增根据物理路径更新分类色方法）

**明确禁止越界修改的范围：**
- [ ] 媒体特征抽取核心函数：`MediaExtractorPipeline::extractColor` 与 `extractDimensions` —— 不修改任何颜色提取本身的多媒体算法。

## 6. 实现准则与预警【核心】
1. **线程死锁预警**：加锁保护时必须先调用 `getDriveMutex()` 并在写事务开始前完成，加锁期间切忌再次调用会间接在同物理驱动器获取锁的耗时同步方法。
2. **连接验证保障**：在进度统计的全局子线程中一定要执行锁连接保护，防止多核密集计算时导致 `SQLITE_MISUSE`。
3. **高精准调试追踪**：所有通过修改插入的调试日志统一加上 `[DB_TRACE]` 前缀，以方便用户在终端控制台或日志输出里秒级过滤并验证每一个文件的“加锁 -> 解析 -> 写入内存 -> 触发落盘备份 -> 同步分类树颜色”全生命周期流程。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 置顶激活色 / 品牌橙色 | 置顶激活色值 `#ff551c`，品牌橙色 `#cb7208` 独立且解耦 | ✅ 符合，不改动任何置顶与品牌色彩变量。 |
| 输入框清除功能 | 必须配置 Qt 原生的 `setClearButtonEnabled(true)` | ✅ 符合，不改动任何输入框组件。 |
| 窗口置顶 | 必须使用 Win32 原生 `SetWindowPos`（`HWND_TOPMOST` / `HWND_NOTOPMOST`）并带有 `SWP_NOSENDCHANGING` | ✅ 符合，不涉及窗口置顶操作。 |
| 标题栏按钮 | 圆角固定 `4px`，背景 Hover 为 `#3E3E42`，Pressed 为 `#4E4E52` | ✅ 符合，不涉及标题栏按钮修改。 |

## 8. 待确认事项（可选）
无。
