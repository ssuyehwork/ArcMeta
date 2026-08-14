# 彻底根除全量物理对账与启动递归盘点逻辑 —— Modification_Plan-24.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在目前的受控托管库中，系统频繁执行重型、高昂的全量物理目录深度扫描（`syncPhysicalDirectoryCascade`）和核对（`fullRecount`）机制。该机制的初衷是为了防止极个别用户在外部手动篡改物理文件。然而，这一设计由于破坏了“受控库”黑盒原则，不仅使启动变慢、消耗了大量的 CPU 和磁盘 I/O，更由于在多线程异步启动和多数据库挂载临界时刻元数据未就绪，误算出了空零计数并将其持久化脏写回 SQLite 的 stats 数据表中，造成了极为毁灭性的“全部数据 10 变 5 变 0”的踩踏与闪烁。

根据用户的最高原则指令（对应用户原话：“先把对账逻辑彻底根除，不可保留”），本方案彻底废除、注销、根除系统内所有全量物理目录树扫描同步、对账自愈、FRN 盘点清退以及系统启动、重载时触发的全量计数重新统计与脏写覆盖逻辑，使全应用回归优雅、清澈、职责单一的高性能运行。

## 2. 问题定位
经过走查与审计，导致该错误逻辑的代码位置及彻底裁撤路径如下：

1. **`CategoryRepo::fullRecount()` (物理对账与脏写汇总核心)**：
   - **定位**：位于 `src/meta/CategoryRepo.cpp` 中。该函数在后台调用 `getLightweightCacheSnapshot()` 核对快照，将内存中的 `s_totalCount` 等直接覆盖，并启动事务向 `system_stats` 写入脏计数。
   - **裁撤方案**：将 `fullRecount()` 的实现体**彻底清空并注销**，改为直接退避返回，切断一切自动重新统计、脏写覆写和物理校验的后台线程拉起。
2. **`DatabaseSynchronizer::syncPhysicalDirectoryCascade()` (全量 DFS 磁盘对账)**：
   - **定位**：位于 `src/core/DatabaseSynchronizer.cpp` 中。它包含 `scanPhysicalDirectory` 和 `syncPhysicalDirectoryCascade`，在后台做全量扫描对齐，并将失效项丢入 `pathsToRemove` 做清退。
   - **裁撤方案**：将此对账同步函数的内部具体实现体**彻底清空并注销**。仅保留极简的方法壳（不做任何操作直接返回）以确保代码层面的完美兼容性。
3. **`AutoImportManager::syncAllManagedLibraries()` 与 `handleRecursiveIngestion()` (启动同步对账)**：
   - **定位**：位于 `src/core/AutoImportManager.cpp` 中。它们在启动和监听时在后台通过线程池拉起上述全量物理目录对账及快照生成。
   - **裁撤方案**：干掉其调用的对账逻辑，使其在挂载和启动时不再拉起耗时的递归同步。
4. **`CategoryPanel.cpp` 中的 `fullRecount()` 信号链与定时器触发**：
   - **定位**：位于 `src/ui/CategoryPanel.cpp` 中。原先刷新定时器和重新载入信号触发时，会异步并发执行 `CategoryRepo::fullRecount()`。
   - **裁撤方案**：不再拉起异步的 `fullRecount` 重算，改为直接读取内存或持久化的最新有效值并刷新 UI。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 先把对账逻辑彻底根除，不可保留 (对应用户原话："先把对账逻辑彻底根除，不可保留") | 4.1、4.2 节将 `fullRecount` 和 `syncPhysicalDirectoryCascade` 的物理实现体彻底删空并注销 | ✅ 一致 |
| 2    | 不可保留 (对应用户原话) | 4.3 节将启动和改变路径时拉起的深度递归对账从 AutoImportManager 中完全裁撤 | ✅ 一致 |

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 彻底裁撤 `CategoryRepo::fullRecount()` 物理重算与脏写事务
在 `src/meta/CategoryRepo.cpp` 中，彻底删空并注销 `CategoryRepo::fullRecount()`：

```
<<<<<<< SEARCH
void CategoryRepo::fullRecount() {
    // 物理加固：若元数据管理器尚未加载完成，且快照为空，拒绝重算以防止内存计数器归零并覆盖数据库
    if (!MetadataManager::instance().isLoaded()) {
        qDebug() << "[Recount] MetadataManager has not finished loading. Abort recount to prevent zeroing stats.";
        return;
    }

    // ----------------------------------------------------
    // 【增量判断拦截机制】：检查自上次重算/退出以来，监控目录是否改变
    // 物理加固：指纹比对拦截只针对启动后的首次对账重算生效，避免阻断应用内打标签或调整分类导致的实时计数刷新。
    // ----------------------------------------------------
    static bool s_firstRecountDone = false;

    // 1. 搜集当前所有的监控根目录绝对路径并计算 mtime 指纹
    QStringList monitoredPaths;
    const auto drives = QDir::drives();
    for (const QFileInfo& d : drives) {
        std::wstring wPath = d.absolutePath().toStdWString();
        std::wstring volSerial = MetadataManager::getVolumeSerialNumber(wPath);
        QString letter = d.absolutePath().left(1).toUpper();
        if (volSerial != L"UNKNOWN") {
            std::wstring managedAbsW = MetadataManager::getManagedLibraryPath(volSerial, letter);
            if (!managedAbsW.empty()) {
                monitoredPaths.append(QString::fromStdWString(managedAbsW));
            }
        }
    }
    monitoredPaths.removeDuplicates();

    QJsonObject currentFingerprints;
    for (const QString& path : monitoredPaths) {
        QFileInfo fi(path);
        if (fi.exists()) {
            currentFingerprints.insert(path, QString::number(fi.lastModified().toMSecsSinceEpoch()));
        }
    }

    if (!s_firstRecountDone) {
        // 2. 载入上一次保存的指纹进行比对
        QJsonObject lastFingerprints;
        QString lastFingerprintsStr = AppConfig::instance().getValue("Recount/LastMonitoredFingerprints", "").toString();
        if (!lastFingerprintsStr.isEmpty()) {
            QJsonDocument doc = QJsonDocument::fromJson(lastFingerprintsStr.toUtf8());
            if (doc.isObject()) {
                lastFingerprints = doc.object();
            }
        }

        // 3. 核心比对：如果所有监控路径和修改时间戳完全吻合，则直接拦截并返回，不进行全量重算
        bool isFingerprintMatch = !currentFingerprints.isEmpty() && (currentFingerprints == lastFingerprints);
        if (isFingerprintMatch) {
            qDebug() << "[Recount] [Incremental] All monitored root directories remain unchanged. Skip first full recount and physical check.";
            s_firstRecountDone = true; // 首次对账拦截完成
            return;
        }
    }

    auto dbs = DatabaseManager::instance().getActiveMemoryDbs();
    sqlite3* db = DatabaseManager::instance().getGlobalDb();

    // 1. 获取所有在各个分库中，被绑定了自定义分类 (category_id > 0) 的 folder_id 
    std::unordered_set<std::string> customizedFids; 
    for (sqlite3* loopDb : dbs) { 
        sqlite3_stmt* stmt = nullptr; 
        const char* sql = "SELECT DISTINCT folder_id FROM category_items "
                          "WHERE category_id > 0 AND category_id NOT IN "
                          "(SELECT id FROM categories WHERE parent_id = 0 AND name LIKE 'ArcMeta.Library_%')";
        if (sqlite3_prepare_v2(loopDb, sql, -1, &stmt, nullptr) == SQLITE_OK) { 
            while (sqlite3_step(stmt) == SQLITE_ROW) { 
                const char* fid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)); 
                if (fid) customizedFids.insert(fid); 
            } 
            sqlite3_finalize(stmt); 
        } 
    } 

    // 2. 物理核对对账
    int total = 0;
    int tags = 0;
    int recentlyVisited = 0;
    int untagged = 0;
    int uncategorized = 0;
    int trash = 0;

    QSet<QString> uniqueTags;
    double now = static_cast<double>(QDateTime::currentMSecsSinceEpoch());

    auto snapshot = MetadataManager::instance().getLightweightCacheSnapshot();
    for (const auto& meta : snapshot) {
        if (meta.folderId.empty()) continue;

        // 🚨 核心物理防火墙：如果是普通的磁盘导航模式下激活的库外普通项目，绝对禁止其污染侧边栏计数！
        // 各自执行各自的逻辑，两者相互不产生任何关联。
        if (!MetadataManager::instance().isInsideManagedLibrary(meta.path)) {
            continue;
        }

        // 仅对不是以 .arc 结尾的普通子文件夹进行剔除，确保合法的受控 .arc 资产包文件夹能够正常计入
        if (meta.isFolder && !QString::fromStdWString(meta.path).endsWith(".arc", Qt::CaseInsensitive)) {
            continue;
        }

        if (meta.isTrash) {
            trash++;
            continue;
        }

        total++;
        if (meta.tagsEmpty) {
            untagged++;
        } else {
            for (const QString& t : meta.tags) uniqueTags.insert(t);
        }

        if (meta.atime >= now - 86400000.0) {
            recentlyVisited++;
        }

        // 🚨 完美逻辑归位：如果资产没有绑定任何一个自定义分类 (id > 0)，100% 逻辑归于未分类！ 
        if (customizedFids.find(meta.folderId) == customizedFids.end()) {
            uncategorized++;
        }
    }

    tags = uniqueTags.size();

    // 3. 偏差增量回填：计算实际物理盘点与当前内存原子的差值 delta 进行 fetch_add
    s_totalCount.store(total);
    {
        std::lock_guard<std::mutex> tagsLock(s_tagsMutex);
        s_globalTagsSet = uniqueTags;
        s_tagsCount.store(tags);
    }
    s_recentlyVisitedCount.store(recentlyVisited);
    s_untaggedCount.store(untagged);
    s_uncategorizedCount.store(uncategorized);
    s_trashCount.store(trash);

    // 建立快照中的 folderId 快速索引集合，杜绝循环获取读锁造成主线程卡死
    std::unordered_set<std::string> activeFolderIds;
    for (const auto& meta : snapshot) {
        if (!meta.folderId.empty()) {
            activeFolderIds.insert(meta.folderId);
        }
    }

    // 查找并清理幽灵关联（在 category_items 中存在，但在 metadata 缓存中已不存在的记录）
    std::map<sqlite3*, std::vector<std::string>> dbToOrphanedFids;
    for (const auto& fid : customizedFids) {
        if (activeFolderIds.find(fid) == activeFolderIds.end()) {
            for (sqlite3* localDb : dbs) {
                dbToOrphanedFids[localDb].push_back(fid);
            }
        }
    }

    for (sqlite3* localDb : dbs) {
        const auto& oFids = dbToOrphanedFids[localDb];
        if (!oFids.empty()) {
            SqlTransaction trans(localDb);
            sqlite3_stmt* delStmt = nullptr;
            if (sqlite3_prepare_v2(localDb, "DELETE FROM category_items WHERE folder_id = ?", -1, &delStmt, nullptr) == SQLITE_OK) {
                for (const auto& fid : oFids) {
                    sqlite3_bind_text(delStmt, 1, fid.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_step(delStmt);
                    sqlite3_reset(delStmt);
                }
                sqlite3_finalize(delStmt);
            }
            trans.commit();
        }
    }

    // 4. 将这些准确数据持久化回所有激活的数据库中
    for (sqlite3* localDb : dbs) {
        SqlTransaction trans(localDb);
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "INSERT OR REPLACE INTO system_stats (key, value) VALUES (?, ?)";
        if (sqlite3_prepare_v2(localDb, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            auto saveStat = [&](const char* key, int val) {
                sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(stmt, 2, val);
                sqlite3_step(stmt);
                sqlite3_reset(stmt);
            };
            saveStat("sys_total_count", total);
            saveStat("sys_tags_count", tags);
            saveStat("sys_recently_visited_count", recentlyVisited);
            saveStat("sys_untagged_count", untagged);
            saveStat("sys_uncategorized_count", uncategorized);
            saveStat("sys_trash_count", trash);
            sqlite3_finalize(stmt);
        }
        trans.commit();
    }

    // 4.1 既然重算已经成功持久化，将当前最新的指纹集合更新至 AppConfig 内存并落盘，并重置首次状态
    QJsonDocument nextDoc(currentFingerprints);
    AppConfig::instance().setValue("Recount/LastMonitoredFingerprints", QString::fromUtf8(nextDoc.toJson(QJsonDocument::Compact)));
    AppConfig::instance().sync();
    s_firstRecountDone = true;

    qDebug() << "[Recount] Backstage Recount calibration completed. Total =" << total << "Uncategorized =" << uncategorized << "Trash =" << trash;

    // 2026-06-xx 核心逻辑升级：物理有效性对账 (盘点 FRN)
    // 这一步在后台异步执行，验证文件是否被第三方删除。若失效，直接物理清退。
    // 使用 [db] 显式捕获数据库指针，并增加错误检查
    (void)QtConcurrent::run([db, snapshot]() {
        if (!db) return;

        std::vector<std::pair<std::wstring, std::string>> itemsToCheck;
        for (const auto& meta : snapshot) {
            // 只对非回收站的文件进行物理校验
            if (!meta.isFolder && !meta.isTrash) {
                itemsToCheck.push_back({meta.path, meta.folderId});
            }
        }

        std::vector<std::wstring> pathsToRemove;
        for (const auto& item : itemsToCheck) {
            std::string currentFid;
            // 通过 WinAPI 直接检查物理文件是否存在且 ID 匹配
            bool exists = MetadataManager::fetchWinApiMetadataDirect(item.first, currentFid);
            if (!exists || currentFid != item.second) {
                // 物理校验失败：文件已被删除或移出，加入删除列表
                pathsToRemove.push_back(item.first);
            }
        }

        if (!pathsToRemove.empty()) {
            qDebug() << "[Recount] 物理校验发现" << pathsToRemove.size() << "个失效项，准备在安全线程彻底物理清退";
            QMetaObject::invokeMethod(&MetadataManager::instance(), [pathsToRemove]() {
                QStringList qPaths;
                for (const auto& p : pathsToRemove) {
                    qPaths.append(QString::fromStdWString(p));
                }
                MetadataManager::instance().removeMetadataBatchSync(qPaths);
                
                DatabaseManager::instance().flushAll();
                MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::FullRebuild);
            }, Qt::BlockingQueuedConnection);
        }
    });

    // 2026-08-xx 补全失效物理文件夹分类的异步盘点校验清退逻辑
    (void)QtConcurrent::run([db]() {
        if (!db) return;

        auto allCats = CategoryRepo::getAll();
        std::vector<int> catsToRemove;

        for (const auto& cat : allCats) {
            if (cat.physicalPath.empty()) continue; // 虚拟分类不参与

            // 库根目录保护：判定标准与 CategoryModel.cpp 的 setData() 重命名保护完全一致
            if (cat.parentId == 0 && QString::fromStdWString(cat.name).startsWith("ArcMeta.Library_", Qt::CaseInsensitive)) {
                continue;
            }

            std::string currentFid;
            std::wstring currentFrnStr;
            bool exists = MetadataManager::fetchWinApiMetadataDirect(cat.physicalPath, currentFid, &currentFrnStr);

            bool frnMismatch = false;
            if (exists && cat.physicalFrn != 0) {
                try {
                    uint64_t currentFrn = std::stoull(currentFrnStr, nullptr, 16);
                    frnMismatch = (currentFrn != cat.physicalFrn);
                } catch (...) { frnMismatch = true; }
            }

            if (!exists || frnMismatch) {
                catsToRemove.push_back(cat.id);
            }
        }

        if (!catsToRemove.empty()) {
            qDebug() << "[Recount] 物理校验发现" << catsToRemove.size() << "个失效文件夹，准备清退";
            QMetaObject::invokeMethod(&MetadataManager::instance(), [catsToRemove]() {
                for (int id : catsToRemove) {
                    CategoryRepo::remove(id); // 注意：remove() 是把文件夹下的文件移入回收站，不是物理删除记录
                }
                DatabaseManager::instance().flushAll();
                MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::FullRebuild);
            }, Qt::BlockingQueuedConnection);
        }
    });
}
=======
void CategoryRepo::fullRecount() {
    // 🚨 彻底根除全量物理对账逻辑：该函数已被完全注销，拒绝一切对账、覆盖和物理脏写，保持系统清澈
    qDebug() << "[Recount][CLEANUP] CategoryRepo::fullRecount has been completely removed. Skip recount.";
}
>>>>>>> REPLACE
```

### 4.2 彻底删空并注销 `DatabaseSynchronizer::syncPhysicalDirectoryCascade`
在 `src/core/DatabaseSynchronizer.cpp` 中，彻底清空 `syncPhysicalDirectoryCascade` 的内部重型扫描对账逻辑：

```
<<<<<<< SEARCH
void DatabaseSynchronizer::syncPhysicalDirectoryCascade(const std::wstring& rootPath) {
    // ----------------------------------------------------
    // 【第一阶段】：纯 I/O 目录树收集，绝对不持任何 DB 写锁，杜绝假死
    // ----------------------------------------------------
    ScanNode rootNode;
    rootNode.path = rootPath;
    QFileInfo rootInfo(QString::fromStdWString(rootPath));
    rootNode.name = rootInfo.fileName().toStdWString();
    
    std::string rootFid;
    std::wstring rootFrnStr;
    if (!MetadataManager::fetchWinApiMetadataDirect(rootPath, rootFid, &rootFrnStr)) {
        return; 
    }
    try {
        rootNode.frn = std::stoull(rootFrnStr, nullptr, 16);
    } catch (...) { return; }
    rootNode.isDir = true;

    // 同步纯磁盘递归扫描，此时数据库不被上任何锁
    scanPhysicalDirectory(QString::fromStdWString(rootPath), rootNode);

    // ----------------------------------------------------
    // 【第二阶段】：超高速、高安全性纯内存与 CPU 对账，并开启极速写事务
    // ----------------------------------------------------
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    SqlTransaction trans(db);

    int rootCatId = CategoryRepo::findByFrn(rootNode.frn);
    if (rootCatId == 0) {
        std::wstring parentPath = rootInfo.absolutePath().toStdWString();
        std::string parentFid;
        std::wstring parentFrnStr;
        int parentCatId = 0;
        if (MetadataManager::fetchWinApiMetadataDirect(parentPath, parentFid, &parentFrnStr)) {
            try {
                uint64_t pFrn = std::stoull(parentFrnStr, nullptr, 16);
                parentCatId = CategoryRepo::findByFrn(pFrn);
            } catch (...) {}
        }

        Category cat;
        if (rootInfo.fileName().startsWith("ArcMeta.Library_", Qt::CaseInsensitive)) {
            cat.parentId = 0;
        } else {
            cat.parentId = parentCatId;
        }
        cat.name = rootNode.name;
        cat.physicalFrn = rootNode.frn;
        cat.physicalPath = rootNode.path;
        if (CategoryRepo::add(cat)) {
            rootCatId = cat.id;
            MetadataManager::instance().registerItem(rootPath, true);
        }
    }

    if (rootCatId <= 0) {
        trans.commit();
        return;
    }

    std::vector<std::wstring> collectedFilesToProcess;

    // 递归对账 lambda 函数
    std::function<void(const ScanNode&, int)> processNode;
    processNode = [&](const ScanNode& node, int parentCatId) {
        // 1. 处理子文件夹分类对账
        for (const auto& childNode : node.children) {
            // 【核心加固】：优先使用物理 FRN 指纹绝对唯一性检索，防止同名不同物理路径的映射重叠冲突
            int existingId = CategoryRepo::findByFrn(childNode.frn);
            if (existingId == 0) {
                // 如果指纹未命中，再尝试根据父分类ID and 名字在数据库查找（处理可能的历史空 FRN 数据）
                existingId = CategoryRepo::findCategoryId(parentCatId, childNode.name);
                if (existingId > 0) {
                    // 若名字匹配了，检查其原有的 FRN。如果是空白或不符，安全修复并升级为 FRN 指纹标识
                    Category existingCat = CategoryRepo::getById(existingId);
                    if (existingCat.physicalFrn == 0 || existingCat.physicalFrn != childNode.frn) {
                        CategoryRepo::updatePhysicalMapping(existingId, childNode.frn, childNode.path);
                    }
                } else {
                    // 彻底未命中任何已知记录，新建分类
                    Category cat;
                    cat.parentId = parentCatId;
                    cat.name = childNode.name;
                    cat.physicalFrn = childNode.frn;
                    cat.physicalPath = childNode.path;
                    if (CategoryRepo::add(cat)) {
                        existingId = cat.id;
                        MetadataManager::instance().registerItem(childNode.path, true);
                    }
                }
            } else {
                // 指纹命中了，但可能物理路径由于用户外部移动发生过位移，执行安全升级修复路径关联
                Category existingCat = CategoryRepo::getById(existingId);
                if (existingCat.physicalPath != childNode.path || existingCat.parentId != parentCatId) {
                    existingCat.physicalPath = childNode.path;
                    existingCat.parentId = parentCatId;
                    CategoryRepo::update(existingCat);
                }
            }

            if (existingId > 0) {
                processNode(childNode, existingId);
            }
        }

        // 2. 收集此节点下的文件供批量多媒体提取与注册使用
        for (const auto& fPath : node.files) {
            collectedFilesToProcess.push_back(fPath);
            if (parentCatId > 0) {
                std::string fid;
                if (MetadataManager::fetchWinApiMetadataDirect(fPath, fid)) {
                    CategoryRepo::addItemToCategory(parentCatId, fid, fPath);
                }
            }
        }
    };

    processNode(rootNode, rootCatId);

    trans.commit();

    // ----------------------------------------------------
    // 【第三阶段】：异步投递多媒体高级特征提取流水线，解决断流 Bug
    // ----------------------------------------------------
    if (!collectedFilesToProcess.empty()) {
        QStringList qPathsToRegister;
        for (const auto& fp : collectedFilesToProcess) {
            qPathsToRegister.append(QString::fromStdWString(fp));
        }
        // 调用 registerItemsAsync，完美一键批处理在后台将文件塞入多媒体解析提取队列 (enqueueBatch)
        MetadataManager::instance().registerItemsAsync(qPathsToRegister, true);
        qDebug() << "[AutoImport] [Pipeline_Bridge] 已将" << qPathsToRegister.size() << "个新导入文件全部推入异步多媒体高级特征提取队列";
    }
}
=======
void DatabaseSynchronizer::syncPhysicalDirectoryCascade(const std::wstring& rootPath) {
    // 🚨 彻底根除全量物理对账逻辑：对账和盘点扫描程序已被完全裁撤，直接安全退避，100% 杜绝启动高负载
    Q_UNUSED(rootPath);
    qDebug() << "[Sync][CLEANUP] DatabaseSynchronizer::syncPhysicalDirectoryCascade has been completely removed.";
}
>>>>>>> REPLACE
```

### 4.3 裁撤 `AutoImportManager` 在挂载与启动时拉起的递归物理对账盘点
在 `src/core/AutoImportManager.cpp` 中，修改 `handleRecursiveIngestion()`，使其不再调用任何物理对账或 MFT 级别重刷，实现完美减负运行：

```
<<<<<<< SEARCH
void AutoImportManager::handleRecursiveIngestion(const std::wstring& rootPath, bool allowLightweight) {
    QDir dir(QString::fromStdWString(rootPath));
    if (!dir.exists()) return;

    if (allowLightweight && !hasTopLevelChanged(rootPath)) {
        qDebug() << "[AutoImport] [Incremental] 顶层快照无变化，跳过资源库深度递归对账与盘点:" << QString::fromStdWString(rootPath);
        return;
    }

    std::wstring vol = MetadataManager::getVolumeSerialNumber(rootPath);
    auto driveLock = DatabaseManager::instance().getDriveMutex(vol);
    std::lock_guard<std::recursive_mutex> dLock(*driveLock);

    // 物理同步：对账期间限制界面并发，设置状态标志
    MetadataManager::instance().setInternalOperating(true);

    DatabaseSynchronizer::syncPhysicalDirectoryCascade(rootPath);

    MetadataManager::instance().setInternalOperating(false);
    MetadataManager::instance().notifyFullUIRebuild();

    saveTopLevelSnapshot(rootPath);
}
=======
void AutoImportManager::handleRecursiveIngestion(const std::wstring& rootPath, bool allowLightweight) {
    // 🚨 彻底根除全量物理对账逻辑：该函数已被清空，直接忽略后台盘点扫描，实现库挂载秒级无缝预热
    Q_UNUSED(rootPath);
    Q_UNUSED(allowLightweight);
    qDebug() << "[AutoImport][CLEANUP] handleRecursiveIngestion ignored to skip full physical scanning.";
}
>>>>>>> REPLACE
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/meta/CategoryRepo.cpp` (删空 fullRecount 内部)
- [ ] 模块/文件：`src/core/DatabaseSynchronizer.cpp` (删空 syncPhysicalDirectoryCascade 内部)
- [ ] 模块/文件：`src/core/AutoImportManager.cpp` (删空 handleRecursiveIngestion 内部)

**明确禁止越界修改的范围：**
- [ ] `src/core/CoreController.cpp` 的生命周期流程控制 —— 不修改。
- [ ] 磁盘导航模式下（DiskNav）通过 I/O 获取物理文件系统的增量操作 —— 不修改。

## 6. 实现准则与预警【核心】
1. **防止编译符号丢失**：由于其他类（例如 `CategoryPanel`、`CoreController` 等）可能在某些信号槽或历史生命周期中调用了 `fullRecount()` 或 `syncPhysicalDirectoryCascade()`，我们**绝不删除其方法声明**，而是将其**内部实现彻底删空和注销**，直接返回，实现零耦合改动的安全过渡。
2. **极速零开销**：根除后，系统启动时将 100% 裁撤耗费数百毫秒乃至数秒的文件树扫描，启动直接秒开，且原子计数器不受任何空元数据污染。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|--------------------------------------------|----------------|
| 双轨制路由分流 | 各自独立，逻辑分类写入 SQLite，磁盘导航 100% 独立，写入 `ArcMeta.cache` 离散缓存。 | ✅ 符合。本方案不改变任何正常的落盘机制，仅根除了冗余对账。 |
| 统一数据来源判断复用 | 视图必须复用 `isMirrorSource()` 或数据源契约。 | ✅ 符合。 |

## 8. 待确认事项（可选）
- **无**。
