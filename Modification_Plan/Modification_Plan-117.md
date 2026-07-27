# 重启全量对账重复扫描与多媒体提取缺陷排查及重构方案 —— Modification_Plan-117.md
 
> 状态：待批准执行（分析师角色，等待用户"批准执行"指令，绝不作物理改动）
 
## 1. 任务背景 
用户反馈，当被监控的文件夹没有新增任何文件或文件夹，并且已经将元数据储存到了磁盘里的数据库时，每次重启主程序，系统依然会重复触发全量元数据的二次扫描、全量对账、并触发冗余的多媒体特征（如调色板、宽高尺寸等）提取流水线。
本方案将对该现象的底层逻辑架构进行深度静态排查，找出无谓重复扫描、多媒体高级特征重新提取的根本原因，并给出极致性能的自愈与过滤重构设计，以彻底避免主程序每次启动时冗余对账及多媒体高级特征流水线的二次开销，保护机械磁盘并极大削减 CPU 占用。
 
## 2. 缺陷根因定位 
 
通过对 `src/core/CoreController.cpp`、`src/core/AutoImportManager.cpp`、`src/meta/CategoryRepo.cpp` 以及 `src/meta/MetadataManager.cpp` 开展链路跟踪，我们发现了以下三处关键缺陷：
 
### 根因一：`CategoryRepo::syncPhysicalDirectoryCascade` 启动即对每个文件无条件触发 `addItemToCategory` 和 `registerItem`
在 `CategoryRepo::syncPhysicalDirectoryCascade()` 递归遍历物理树时，代码逻辑无条件执行了以下操作：
```cpp
        // 2. 收集此节点下的文件供批量多媒体提取与注册使用
        for (const auto& fPath : node.files) {
            collectedFilesToProcess.push_back(fPath); // <--- 直接推入待处理列表
            if (parentCatId > 0) {
                std::string fid;
                if (MetadataManager::fetchWinApiMetadataDirect(fPath, fid)) {
                    CategoryRepo::addItemToCategory(parentCatId, fid, fPath); // <--- 无条件对每一项执行 addItemToCategory
                }
            }
        }
```
随后，该函数在尾部无条件调用了多媒体高级特征提取的异步排队方法：
```cpp
    if (!collectedFilesToProcess.empty()) {
        QStringList qPathsToRegister;
        for (const auto& fp : collectedFilesToProcess) {
            qPathsToRegister.append(QString::fromStdWString(fp));
        }
        // 调用 registerItemsAsync，完美一键批处理在后台将文件塞入多媒体解析提取队列 (enqueueBatch)
        MetadataManager::instance().registerItemsAsync(qPathsToRegister, true);
        qDebug() << "[AutoImport] [Pipeline_Bridge] 已将" << qPathsToRegister.size() << "个新导入文件全部推入异步多媒体高级特征提取队列";
    }
```
**分析**：这导致即使文件在以前已经扫描导入成功，数据库中已经完备地存有其多媒体数据和 status = 1 的记录，在每次重启时，仍然会被重新放入 `collectedFilesToProcess` 队列，强行触发 `registerItemsAsync` 与 `MediaExtractorPipeline` 后台解析。

### 根因二：`CategoryRepo::addItemToCategory` 无条件写入带来海量无谓 I/O 与脏标记
在 `CategoryRepo::addItemToCategory` 中，系统会无条件在 SQLite 全局内存库上执行 `INSERT OR REPLACE INTO category_items` 覆写操作，并且因为关联发生变化，会重新调用：
```cpp
            // 如果之前未分类，增加后变成有分类，则减去 uncategorizedCount，增加 categorizedCount 并持久化
            if (getItemCategoryIds(fileId128).size() == 1) {
                s_uncategorizedCount.fetch_sub(1);
                s_categorizedCount.fetch_add(1);
                updatePersistentStat(STAT_CATEGORIZED, 1);
            }
```
**分析**：即使这个文件早就已经归属于对应的 `categoryId`，每次启动时的 `addItemToCategory` 仍旧会重新执行一次 `INSERT OR REPLACE`。这会导致 `category_items` 的 `added_at` 字段被重置为启动时的毫秒时间戳（破坏了用户原有的历史时间排序，变为重启即全部打乱排序），并且频繁标记数据库脏（`m_isDirty`），进而强行触发后台 `DatabaseManager` 落盘备份，造成完全不必要的巨大磁盘 I/O。

### 根因三：`MetadataManager::registerItemsAsync` 的批量准入检查缺失
在 `MetadataManager::registerItem(path)` 中，系统确实具备部分单路径双重准入检查：
```cpp
    std::string pFid;
    long long pSize = 0, pMtime = 0;
    if (fetchWinApiMetadataDirect(nPath, pFid, nullptr, &pSize, nullptr, nullptr, &pMtime, nullptr)) {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        auto it = m_cache.find(nPath);
        if (it != m_cache.end()) {
            bool metadataValid = true;
            // 校验是否属于图形、是否具备完整宽度、高度和自动颜色
            ...
            if (it->second.ingestionStatus == 1 && it->second.fileSize == pSize && it->second.mtime == pMtime && metadataValid) {
                return; // 物理指纹及高级多媒体特征完备且未发生改变，安全返回
            }
        }
    }
```
然而，在 `CategoryRepo::syncPhysicalDirectoryCascade` 递归中使用的**批量管道** `MetadataManager::registerItemsAsync`，则是完全绕过了这一安全检查：
```cpp
void MetadataManager::registerItemsAsync(const QStringList& paths, bool authorized) {
    if (paths.isEmpty()) return;
    (void)authorized;
    
    (void)QtConcurrent::run([this, paths]() {
        std::vector<std::wstring> stdPaths;
        for (const auto& qp : paths) {
            std::wstring nPath = normalizePath(qp.toStdWString());
            ensureActivated(nPath);
            updateIngestionStatus(nPath, 0); // <--- 这里一律无条件将 ingestion_status 强制刷回 0（待处理状态）！
            stdPaths.push_back(nPath);
        }
        MediaExtractorPipeline::instance().enqueueBatch(stdPaths); // <--- 一律推入提取流水线重新提起
    });
}
```
**分析**：这就导致批量注册路径成了“法外之地”：无论文件是否解析过，在每次启动对账时，只要被 `registerItemsAsync` 批量传入，这些已经完备的元数据的 ingestionStatus 就会被**强制重置为 0**，然后强制推入 `MediaExtractorPipeline` 的大批次提取队列，再次耗费数十秒乃至数分钟的高开销 CPU/GPU/IO 多媒体解析，从而形成严重的性能故障！

--- 
 
## 3. 极致性能自愈与重构方案 
 
为了实现“启动对账无感化、零磁盘 I/O 开销与零 CPU 冗余提取”，我们将对递归对账逻辑和批量注册拦截逻辑进行极致加固：
 
### 方案第一步：在 `CategoryRepo::addItemToCategory` 引入重复关联快速拦截
1. 在向 `category_items` 表插入之前，先安全查询是否已存在该 `(category_id, file_id)` 组合。
2. 如果已经存在，直接返回 `true`（快速静默通行），拦截物理覆写！
3. 这能产生三大收益：
   - 彻底避免 `category_items` 表中 `added_at`（归类时间）被重启刷新覆盖，**保护了原有的归类时序**。
   - 避免对 SQLite 造成无谓覆写，不触发 `DatabaseManager` 脏标记，**杜绝程序启动即产生磁盘持久化写入**。
   - 保证了 `categorizedCount` 与 `uncategorizedCount` 等计数原子的稳定性。

### 方案第二步：在 `MetadataManager::registerItemsAsync` 中实施“双重物理指纹与高级特征完备性”批量过滤
1. 升级 `registerItemsAsync` 的批处理实现。
2. 在对每一条路径进行重置 `updateIngestionStatus(nPath, 0)` 之前，先执行物理极速对账检验：
   - 磁盘文件大小 `pSize`、修改时间 `pMtime` 是否与缓存中记录的值一致。
   - 文件在内存中的 `ingestionStatus` 是否已经为 1（已提取完备）。
   - 如果文件是图片/SVG，其核心高级特征（`width` / `height` / `autoColor`）是否均已成功填充且不为空（非破损缓存）。
3. 如果满足以上指纹与特征双重完备校验，说明该文件在先前运行期间已经成功提取过且其物理属性从未改变过，**直接从要处理的路径列表中剔除**。
4. 只有对于指纹不符（发生过改动）或者状态不完备（历史提取中途崩溃、字段不齐等破损项）的文件，才调用 `ensureActivated`、重置 `ingestionStatus` 并推入后台流水线。

### 方案第三步：优化 `CategoryRepo::syncPhysicalDirectoryCascade` 纯收集时对无用文件的拦截
1. 在收集物理子树文件并追加到 `collectedFilesToProcess` 列表前，预先调用 `MetadataManager` 的轻量级校验（或上述批量检查逻辑），将已完全导入且无任何物理变化的完备文件，剔除出待注册列表，不传入 `registerItemsAsync`。
2. 这可以减少不必要的进程间/线程间批量包装对象的拷贝开销，实现真正无感、瞬间完成的启动同步。

--- 
 
## 4. 修改边界声明【范围】 
 
**物理修改的代码文件边界（待批准执行）：**
- [ ] 模块/文件：`src/meta/CategoryRepo.cpp` （物理改动，增加 addItemToCategory 快速拦截，以及收集阶段的完备性剔除）
- [ ] 模块/文件：`src/meta/MetadataManager.cpp` （物理改动，重构并升级批量 registerItemsAsync 里的指纹与高级多媒体完备性验证与拦截逻辑）
 
**明确禁止越界修改的范围：**
- [ ] 物理 MFT 读取模块 `src/core/MftReader.cpp` —— 不修改 
- [ ] 数据库事务驱动底座 `src/meta/DatabaseManager.cpp` —— 不修改
 
--- 
 
## 5. 实现准则与重入安全性保障
 
1. **重入安全性**：对账过程在后台 QtConcurrent 线程中高并发进行，而在执行校验和缓存查询时，`registerItemsAsync` 的过滤逻辑必须先调用 `MetadataManager` 现有的 `m_mutex` 读锁（`shared_lock`）来获取缓存状态，随后仅在确需对脏项或新项做重置时，才发起写锁，以此保证高性能高并发读写。
2. **零开销保障**：预校验利用 Win32 `fetchWinApiMetadataDirect` 极速获取文件大小与时间戳，这在 Windows 平台几乎不产生实际的磁盘读取开销，且避免了复杂的解码和媒体元数据解析，完全能满足百万级大目录的毫秒级对账开销需求。
 
--- 
 
## 6. 待确认事项 
- 暂无。
