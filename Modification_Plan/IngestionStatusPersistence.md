# 实施方案：资产解析状态持久化落盘与增量免扫机制 (IngestionStatusPersistence)

## 所属大纲章节
**1.1 全局数据与内存管理**（1.1.10 资产解析状态持久化落盘与增量免扫机制）

---

## 涉及代码文件
* `src/meta/MetadataManager.h` （修改：声明批量特征与解析状态持久化落盘接口 `updateExtractedMediaFeaturesBatch`）
* `src/meta/MetadataManager.cpp` （修改：在 `markAsRegistered` 中引入物理指纹（`mtime` + `fileSize`）增量准入与 `ingestionStatus == 1` 免扫跳过逻辑；实现 `updateExtractedMediaFeaturesBatch` 大事务落盘）
* `src/meta/MediaExtractorPipeline.h` （修改：移除单定时器轮询，声明多线程 Worker Dispatcher 调度机制）
* `src/meta/MediaExtractorPipeline.cpp` （修改：在后台 Worker 解析完缩略图、宽高、颜色特征后，立刻将 `ingestionStatus = 1` 写入磁盘数据库，实现“一次解析，终身免扫”）

---

## 功能描述
在当前 ArcMeta 架构中，当用户触发“重新扫描托管库”或应用重新启动载入资产时，系统缺乏持久化的解析状态落盘验证，导致每次扫描都会将资产状态无脑重置为未解析 (`ingestion_status = 0`)，重新将上万条资产投递到 `MediaExtractorPipeline` 后台队列中重复提取特征。同时，视口卡片滚动时触发的插队解析（`prioritizeBatch`）会误触发全局扫描进度条，造成严重的 UI 假死、假刷新与 CPU/IO 资源浪费。

本实施方案提供完整的“完成标记即时落盘与增量免扫”架构：
1. **解析完成即时持久化**：后台流水线 `MediaExtractorPipeline` 在提取完资产的宽高、主色调、调色盘等特征后，立刻将 `ingestion_status = 1` 连同文件的毫秒级修改时间 (`mtime`) 与大小 (`file_size`) 写入磁盘 SQLite 数据库（`.arcmeta/Arcmeta_*.db`），确保物理磁盘与内存状态实时同步；
2. **增量指纹准入（免扫跳过）**：在 `MetadataManager::markAsRegistered` 增量扫描准入时，先读取该资产的 `RuntimeMeta` 记录。若 `ingestion_status == 1` 且磁盘文件的物理 `mtime` 与 `file_size` 未发生任何变动，判定为“已解析资产”，绝对禁止将其重置为 `0`，直接免扫跳过；
3. **视口插队解耦**：视口内卡片滚动触发的动态插队操作（`prioritizeBatch`）独立处理，不再广播全局进度事件，彻底消除滚动卡片时界面误弹出全局扫描进度条的现象。

---

## 技术决策
1. **物理指纹 + ingestion_status 双重校验准入（Incremental Admission Policy）**：
   - 扫描资产时，提取物理文件的 `mtime`（毫秒级时间戳）与 `fileSize`（字节数）；
   - 对比内存及数据库记录：当且仅当 `meta.ingestionStatus == 1` 且 `meta.mtime == diskMtime` 且 `meta.fileSize == diskSize` 时，视为无变化已解析资产，直接跳过；
   - 只要文件被外部修改（`mtime` 或 `fileSize` 变动）或尚未解析（`ingestionStatus == 0`），才重置为 `0` 并加入后台提取队列。
2. **SQLite 大事务即时落盘（Instant Transactional Persistence）**：
   - 特征提取完成后，利用 `SqlTransaction` 包裹多条 `UPDATE metadata SET ingestion_status = 1, width = ?, height = ?, auto_color = ?, palettes = ?, mtime = ?, file_size = ? WHERE path = ?` 提交；
   - 保证落盘原子性，即使中途断电或崩溃，已完成提取的资产在下次启动时依然为 `ingestion_status = 1`，不会丢失解析成果。
3. **全局进度解耦与插队隔离（Viewport Priority Isolation）**：
   - `MediaExtractorPipeline::prioritizeBatch` 插队任务仅调整内部队列顺序，不重新触发 `SyncStatusService` 的全局增量计数，彻底剥离视口卡片绘制与后台全库扫描进度的耦合。

---

## 强制性七项断层排查清单

1. **头文件核对**：
   * `MetadataManager.cpp` 包含 `<QDateTime>` 与 `<QFileInfo>` 用于物理文件属性获取与比对。
   * `MediaExtractorPipeline.cpp` 包含 `<QThreadPool>`、`ImageDecoderFacade.h`、`ColorAlgorithmEngine.h`、`MediaColorExtractor.h`、`DatabaseManager.h` 与 `SqlTransaction.h`。
2. **成员核对**：
   * `MetadataManager.h` 声明新增结构体 `ExtractedFeatureResult` 与函数 `void updateExtractedMediaFeaturesBatch(const std::vector<ExtractedFeatureResult>& results);`。
   * `MediaExtractorPipeline.h` 声明 `void dispatchWorkerLoop();` 及 `std::atomic<int> m_activeWorkers{0};` 标识。
3. **残留核对**：
   * 全局搜索 `updateExtractedMediaFeatures` 的所有调用点，在 `MediaExtractorPipeline.cpp` 中统一切换为批量接口，确保无单条 SQL 瓶颈。
4. **断层核对（上下文连续性）**：
   * 核对 `MetadataManager.cpp` 第 780-790 行上下文，替换原盲目 `updateIngestionStatus(p, 0)` 的循环。
5. **C++ 语法与特殊成员函数合规排查**：
   * 确保改动不引入带有形参的构造函数 `= default` 声明。
6. **废除成员全量引用点清扫排查**：
   * 废除 `m_timer` 单定时器轮询，清扫 `MediaExtractorPipeline` 中所有 `m_timer` 的引用。
7. **未引用局部变量（-Wunused-variable）防断层排查**：
   * 擦除定时器及单条更新逻辑后，连同未引用的局部变量一并擦除，确保 MSVC/GCC 无警告。

---

## 核心代码实现与改动对照

### 修改文件：`src/meta/MetadataManager.h`

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

            // 🚨 物理指纹与 ingestion_status 增量免扫准入：已解析完成且物理文件未变动的资产，直接免扫跳过
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

---

## 已知问题 / 待办
* 无。

---

## 涉及文件清单
1. `src/meta/MetadataManager.h`（修改：声明 ExtractedFeatureResult 结构体与 updateExtractedMediaFeaturesBatch 接口）
2. `src/meta/MetadataManager.cpp`（修改：markAsRegistered 中加入物理指纹与 ingestion_status == 1 免扫准入比对）
3. `src/meta/MediaExtractorPipeline.h`（修改：声明 dispatchWorkerLoop）
4. `src/meta/MediaExtractorPipeline.cpp`（修改：多线程特征提取完成后，立刻将 ingestionStatus = 1 批量落盘写入 SQLite 数据库）
