# 实施方案：托管库增量扫描准入与特征提取流水线并发优化 (ManagedLibraryScanPipeline)

## 所属大纲章节
**1.1 全局数据与内存管理**（1.1.10 托管库增量扫描准入与特征提取流水线并发优化规范）

---

## 涉及代码文件
* `src/meta/MetadataManager.h` （修改：声明 ExtractedFeatureResult 结构体与 updateExtractedMediaFeaturesBatch 接口）
* `src/meta/MetadataManager.cpp` （修改：重构 `markAsRegistered` 引入毫秒级物理指纹增量准入逻辑；实现 `updateExtractedMediaFeaturesBatch` 批量事务落盘接口）
* `src/meta/MediaExtractorPipeline.h` （修改：增加并发 Worker 线程计数与调度函数声明，移除 `m_timer` 成员）
* `src/meta/MediaExtractorPipeline.cpp` （修改：彻底废除 1500ms 定时器依赖，通过 QThreadPool 按 CPU 核心数全速并发消费，完成特征计算后组装调色盘与分辨率，大事务批量落盘）

---

## 功能描述
在使用“重新扫描托管库”功能时，现有逻辑在 `MainWindow::rescanManagedLibrary` 触发后，进入 `MetadataManager::markAsRegistered` 递归扫描磁盘文件，并直接调用 `updateIngestionStatus(p, 0)` 将所有资产的特征提取状态强行置为 0（未完成），随后把万级资产全量塞入 `MediaExtractorPipeline` 的队列中。`MediaExtractorPipeline` 内部硬编码了 1500ms 定时器以及 `CHUNK_SIZE = 16`，单线程串行计算尺寸与颜色并逐条执行单条 SQL `UPDATE metadata`，导致 16,000+ 文件的扫描解析耗时长达 30~36 分钟，极大地占用了系统 CPU 与磁盘 I/O 资源。

本实施方案按照 ArcMeta 架构标准重构增量扫描与提取流水线：
1. **增量指纹准入机制**：在 `MetadataManager::markAsRegistered` 中比对资产物理修改时间（`mtime`，毫秒级时间戳）与文件大小（`fileSize`，字节数）。若资产数据库状态为完成 (`ingestionStatus == 1`) 且文件未变动，绝对禁止重置为 0，跳过重复投递；
2. **多线程池全速并发提取**：彻底废除基于 `QTimer` 1500ms 慢轮询单线程模式，按系统 CPU 核心数 (`QThread::idealThreadCount()`) 启动并发 Worker 线程池全速消费任务队列，精准维护 `SyncStatusService` 待处理总数（`m_queue.size() + m_activeCount`）；
3. **大事务批量落盘**：提取完成的多媒体特征不再逐条发起异步 DB 更新，而是汇总成批后使用 `SqlTransaction` 包裹大事务一次性提交到 SQLite，并同步更新 `mtime` 和 `file_size` 物理指纹。

---

## 技术决策
1. **增量准入原则（Incremental Admission Policy）**：
   - 扫描托管库时，通过 `QFileInfo` 获取文件的物理 `mtime`（`fi.lastModified().toMSecsSinceEpoch()`，毫秒级）和 `fileSize`（`fi.size()`，字节数），对比 `MetadataManager` 内存/数据库中的 `RuntimeMeta` 记录；
   - 仅当记录不存在，或 `ingestionStatus != 1`，或物理 `mtime` / `fileSize` 与记录不一致时，才将 `ingestionStatus` 标记为 `0` 并投递进 `MediaExtractorPipeline` 队列。
2. **真正的多线程池并发控制与按需调度（ThreadPool Worker Dispatcher）**：
   - 弃用 1500ms 单定时器轮询，引入 `std::atomic<int> m_activeWorkers{0}` 动态跟踪活跃 Worker 线程数；
   - 投递任务时，根据当前队列积压量按需计算需要的 Worker 数 `neededWorkers = std::min(maxWorkers, (queueSize + batchSize - 1) / batchSize)`，避免任务极少时无谓超发线程；
   - 每次获取批次或完成批次时，严格计算 `remaining = queueSize + activeCount` 并上报 `SyncStatusService::instance().updateMediaPending(remaining)`，确保 UI 进度条准确平滑推进。
3. **跨平台 safe UTF-16 绑定与大事务落盘（Batch SQL Transaction）**：
   - 新增 `MetadataManager::updateExtractedMediaFeaturesBatch` 接口；
   - 跨平台绑字串时，统一直言使用 `QString::utf16()` 替代 `std::wstring::c_str()`，消除 Linux/macOS 上 32-bit `wchar_t` 与 SQLite 16-bit UTF-16 的内存对齐崩溃隐患；
   - 在单个 `SqlTransaction` 事务块中通过一条 `BEGIN TRANSACTION ... COMMIT` 提交数百条 `UPDATE`，同步写入 `mtime` 与 `file_size` 指纹，将磁盘 IOPS 开销降低 99% 以上。

---

## 强制性七项断层排查清单

1. **头文件核对**：
   * `MetadataManager.cpp` 包含 `<QDateTime>` 与 `<QFileInfo>` 用于物理文件属性获取与比对。
   * `MediaExtractorPipeline.cpp` 包含 `<QThreadPool>`、`ImageDecoderFacade.h`、`ColorAlgorithmEngine.h`、`MediaColorExtractor.h`、`DatabaseManager.h` 与 `SqlTransaction.h`。
2. **成员核对**：
   * `MetadataManager.h` 声明新增结构体 `ExtractedFeatureResult` 与函数 `void updateExtractedMediaFeaturesBatch(const std::vector<ExtractedFeatureResult>& results);`。
   * `MediaExtractorPipeline.h` 声明 `void dispatchWorkerLoop();` 及 `std::atomic<int> m_activeWorkers{0};` 标识。
3. **残留核对**：
   * 全局搜索 `updateExtractedMediaFeatures` 的所有调用点，在 `MediaExtractorPipeline.cpp` 中统一切换为批量接口或兼容重载，确保无残留冗余的单条 SQL 瓶颈。
4. **断层核对（上下文连续性）**：
   * 核对 `MetadataManager.cpp` 第 780-790 行上下文，替换原盲目 `updateIngestionStatus(p, 0)` 的循环。
5. **C++ 语法与特殊成员函数合规排查**：
   * 确保改动不引入带有形参的构造函数 `= default` 声明。
6. **废除成员全量引用点清扫排查**：
   * 废除 `m_timer` 单定时器轮询，清扫 `MediaExtractorPipeline` 中所有 `m_timer` 的引用。
7. **未引用局部变量（-Wunused-variable）防断层排查**：
   * 擦除定时器及单条更新逻辑后，连同未引用的局部变量一并擦除，确保 MSVC/GCC 无 C4189 / -Wunused-variable 警告。

---

## 核心代码实现与改动对照

### 修改文件：`src/meta/MetadataManager.h`

#### 改动 1：新增批量特征更新结构体与接口声明
```cpp
<<<<<<< SEARCH
    void updateExtractedMediaFeatures(
        const std::wstring& path, 
        int width, 
        int height, 
        const std::wstring& autoColor, 
        const QVector<QPair<QColor, float>>& palettes, 
        int ingestionStatus);
=======
    struct ExtractedFeatureResult {
        std::wstring path;
        int width{0};
        int height{0};
        int64_t mtime{0};
        int64_t fileSize{0};
        std::wstring autoColor;
        QVector<QPair<QColor, float>> palettes;
        int ingestionStatus{1};
    };

    void updateExtractedMediaFeatures(
        const std::wstring& path, 
        int width, 
        int height, 
        const std::wstring& autoColor, 
        const QVector<QPair<QColor, float>>& palettes, 
        int ingestionStatus);

    void updateExtractedMediaFeaturesBatch(const std::vector<ExtractedFeatureResult>& results);
>>>>>>> REPLACE
```

---

### 修改文件：`src/meta/MetadataManager.cpp`

#### 改动 1：`markAsRegistered` 增加毫秒级物理指纹增量准入比对
```cpp
<<<<<<< SEARCH
        QStringList qPathsToRegister; 
        SqlTransaction trans(db); 
        for (const auto& p : pathsToRegister) { 
            ensureActivated(p); 
            updateIngestionStatus(p, 0); 
            qPathsToRegister << QString::fromStdWString(p); 
        } 
         
        if (trans.commit()) { 
            registerItemsAsync(qPathsToRegister, true); 
        } 
=======
        QStringList qPathsToRegister; 
        SqlTransaction trans(db); 
        for (const auto& p : pathsToRegister) { 
            ensureActivated(p); 
            RuntimeMeta meta = getMeta(p);
            QFileInfo fi(QString::fromStdWString(p));
            int64_t diskMtime = fi.lastModified().toMSecsSinceEpoch();
            int64_t diskSize = fi.size();

            // 🚨 物理增量准入准则：已解析完成且物理文件修改时间（毫秒）与大小未发生变化的资产，跳过状态重置与重复投递
            if (meta.ingestionStatus == 1 && meta.mtime == diskMtime && meta.fileSize == diskSize && diskSize > 0) {
                continue;
            }
            updateIngestionStatus(p, 0); 
            qPathsToRegister << QString::fromStdWString(p); 
        } 
         
        if (trans.commit() && !qPathsToRegister.isEmpty()) { 
            registerItemsAsync(qPathsToRegister, true); 
        } 
>>>>>>> REPLACE
```

#### 改动 2：实现 `updateExtractedMediaFeaturesBatch` 跨平台安全 UTF-16 与大事务提交
```cpp
<<<<<<< SEARCH
void MetadataManager::updateExtractedMediaFeatures( 
=======
void MetadataManager::updateExtractedMediaFeaturesBatch(const std::vector<ExtractedFeatureResult>& results) {
    if (results.empty()) return;

    // 按物理归属驱动库分组，避免跨库死锁
    std::unordered_map<sqlite3*, std::vector<ExtractedFeatureResult>> dbGroupMap;
    for (const auto& res : results) {
        std::wstring nPath = normalizePath(res.path);
        {
            size_t idx = getShardIndex(nPath);
            std::unique_lock<std::shared_mutex> shardLock(m_shards[idx].mutex);
            if (m_shards[idx].items.count(nPath)) {
                RuntimeMeta& meta = m_shards[idx].items[nPath];
                meta.width = res.width;
                meta.height = res.height;
                if (res.mtime > 0) meta.mtime = res.mtime;
                if (res.fileSize > 0) meta.fileSize = res.fileSize;
                meta.autoColor = res.autoColor;
                meta.ingestionStatus = res.ingestionStatus;
                meta.palettes.clear();
                for (const auto& p : res.palettes) {
                    meta.palettes.emplace_back(p.first, p.second);
                }
            }
        }
        sqlite3* db = DatabaseManager::instance().getDbForPath(nPath);
        if (db) {
            dbGroupMap[db].push_back(res);
        }
    }

    // 在 AsyncSync 线程池中执行大事务提交
    for (auto& pair : dbGroupMap) {
        sqlite3* db = pair.first;
        auto items = pair.second;
        DatabaseManager::instance().enqueueSyncTask([db, items]() {
            SqlTransaction trans(db);
            const char* sql = "UPDATE metadata SET width = ?, height = ?, auto_color = ?, palettes = ?, ingestion_status = ?, mtime = ?, file_size = ? WHERE path = ?";
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                for (const auto& item : items) {
                    sqlite3_bind_int(stmt, 1, item.width);
                    sqlite3_bind_int(stmt, 2, item.height);
                    
                    QString qColor = QString::fromStdWString(item.autoColor);
                    sqlite3_bind_text16(stmt, 3, qColor.utf16(), -1, SQLITE_TRANSIENT);
                    
                    QJsonArray arr;
                    for (const auto& pe : item.palettes) {
                        QJsonObject obj; obj["color"] = pe.first.name(); obj["ratio"] = (double)pe.second; arr.append(obj);
                    }
                    QByteArray ba = QJsonDocument(arr).toJson(QJsonDocument::Compact);
                    sqlite3_bind_blob(stmt, 4, ba.constData(), ba.size(), SQLITE_TRANSIENT);
                    sqlite3_bind_int(stmt, 5, item.ingestionStatus);
                    sqlite3_bind_int64(stmt, 6, item.mtime);
                    sqlite3_bind_int64(stmt, 7, item.fileSize);

                    QString qPath = QString::fromStdWString(item.path);
                    sqlite3_bind_text16(stmt, 8, qPath.utf16(), -1, SQLITE_TRANSIENT);

                    sqlite3_step(stmt);
                    sqlite3_reset(stmt);
                }
                sqlite3_finalize(stmt);
            }
            trans.commit();
        });
    }
}

void MetadataManager::updateExtractedMediaFeatures( 
>>>>>>> REPLACE
```

---

### 修改文件：`src/meta/MediaExtractorPipeline.cpp`

#### 改动 1：废除 QTimer 1500ms 慢轮询构造
```cpp
<<<<<<< SEARCH
MediaExtractorPipeline::MediaExtractorPipeline(QObject* parent) : QObject(parent) {
    m_timer = new QTimer(this);
    m_timer->setInterval(1500);
    connect(m_timer, &QTimer::timeout, this, &MediaExtractorPipeline::processNextBatch);

    if (QCoreApplication::instance()) {
        this->moveToThread(QCoreApplication::instance()->thread());
    }
}
=======
MediaExtractorPipeline::MediaExtractorPipeline(QObject* parent) : QObject(parent) {
    if (QCoreApplication::instance()) {
        this->moveToThread(QCoreApplication::instance()->thread());
    }
}
>>>>>>> REPLACE
```

#### 改动 2：多线程池全速按需 Worker 调度与精准进度同步上报
```cpp
<<<<<<< SEARCH
void MediaExtractorPipeline::enqueueBatch(const std::vector<std::wstring>& paths) {
    m_isCanceled.store(false); // 投递新任务时自动重置取消状态
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_queue.insert(m_queue.end(), paths.begin(), paths.end());
    
    // 🚨 联动通知：特征待提取总项数（排队 + 正在解析数）
    SyncStatusService::instance().updateMediaPending(static_cast<int>(m_queue.size()) + m_activeCount.load());

    QMetaObject::invokeMethod(m_timer, "start", Qt::QueuedConnection);
}
=======
void MediaExtractorPipeline::enqueueBatch(const std::vector<std::wstring>& paths) {
    m_isCanceled.store(false);
    size_t qSize = 0;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_queue.insert(m_queue.end(), paths.begin(), paths.end());
        qSize = m_queue.size();
        SyncStatusService::instance().updateMediaPending(static_cast<int>(qSize) + m_activeCount.load());
    }

    // 根据队列积压量按需调度并发 Worker 线程，最高不超过核心数
    int maxWorkers = std::max(2, QThread::idealThreadCount());
    int targetWorkers = std::min(maxWorkers, static_cast<int>((qSize + 31) / 32));
    while (m_activeWorkers.load() < targetWorkers) {
        int current = m_activeWorkers.load();
        if (m_activeWorkers.compare_exchange_strong(current, current + 1)) {
            QThreadPool::globalInstance()->start([this]() {
                dispatchWorkerLoop();
            });
        }
    }
}

void MediaExtractorPipeline::dispatchWorkerLoop() {
#ifdef Q_OS_WIN
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
#endif

    while (!m_isCanceled.load()) {
        std::vector<std::wstring> batch;
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if (m_queue.empty()) break;
            size_t batchSize = std::min(m_queue.size(), static_cast<size_t>(32));
            batch.assign(m_queue.begin(), m_queue.begin() + batchSize);
            m_queue.erase(m_queue.begin(), m_queue.begin() + batchSize);
            
            m_activeCount.fetch_add(static_cast<int>(batch.size()));
            int remaining = static_cast<int>(m_queue.size()) + m_activeCount.load();
            SyncStatusService::instance().updateMediaPending(remaining);
        }

        std::vector<MetadataManager::ExtractedFeatureResult> results;
        results.reserve(batch.size());

        for (const auto& path : batch) {
            if (m_isCanceled.load()) break;
            
            MetadataManager::ExtractedFeatureResult res;
            res.path = path;
            extractDimensions(path, res.width, res.height);

            QString qPath = QString::fromStdWString(path);
            QFileInfo info(qPath);
            res.mtime = info.lastModified().toMSecsSinceEpoch();
            res.fileSize = info.size();

            if (info.isFile() && MediaColorExtractor::isGraphicsFile(info.suffix().toLower())) {
                QImage thumb = ImageDecoderFacade::loadScaledImage(qPath, 512);
                if (!thumb.isNull()) {
                    auto pal = ColorAlgorithmEngine::extractPaletteFromImage(thumb);
                    if (!pal.isEmpty()) {
                        QColor dominant = MediaColorExtractor::quantizeColor(pal.first().first);
                        res.autoColor = dominant.name().toUpper().toStdWString();
                        res.palettes = pal;
                    }
                }
            } else if (info.isDir()) {
                std::wstring colorStr;
                QVector<QPair<QColor, float>> palette;
                extractColor(path, colorStr, palette);
                res.autoColor = colorStr;
                res.palettes = palette;
            }
            res.ingestionStatus = 1;
            results.push_back(res);
        }

        if (!results.empty() && !m_isCanceled.load()) {
            MetadataManager::instance().updateExtractedMediaFeaturesBatch(results);
        }

        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            m_activeCount.fetch_sub(static_cast<int>(batch.size()));
            int remaining = static_cast<int>(m_queue.size()) + m_activeCount.load();
            if (remaining < 0) remaining = 0;
            SyncStatusService::instance().updateMediaPending(remaining);
        }
    }

    m_activeWorkers.fetch_sub(1);

#ifdef Q_OS_WIN
    CoUninitialize();
#endif
}
>>>>>>> REPLACE
```

---

## 已知问题 / 待办
* 对历史已经被误洗白为 `ingestion_status = 0` 的资产，可在启动或后端初始化时检测已有 `width > 0 && height > 0` 的记录进行自动一次性数据库修复，更正为 `ingestion_status = 1`。

---

## 涉及文件清单
1. `src/meta/MetadataManager.h`（修改：声明 ExtractedFeatureResult 结构体与 updateExtractedMediaFeaturesBatch 接口）
2. `src/meta/MetadataManager.cpp`（修改：markAsRegistered 中加入物理指纹毫秒级增量准入比对；实现 updateExtractedMediaFeaturesBatch 跨平台 safe UTF-16 大事务提交）
3. `src/meta/MediaExtractorPipeline.h`（修改：声明 dispatchWorkerLoop，添加 atomic<int> m_activeWorkers 成员，擦除 m_timer 成员）
4. `src/meta/MediaExtractorPipeline.cpp`（修改：废除定时器轮询，实现多线程并发 Worker 消费、完整的调色盘/分辨率计算以及精准的进度 SyncStatusService 同步）
