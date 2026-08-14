# 侧边栏一键顺逆向清理空白与幽灵托管包 —— Modification_Plan-38.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在侧边栏“分类”面板的标题栏右侧增加一个一键扫描按钮，用以彻底、洁净、深度清理空白的 `.arc` 托管包容器以及因磁盘上手动删除而在数据库中残留的“幽灵记录”脏数据（对应用户原话：“放弃之前要求的将‘ArcMeta.Library_[盘符]’文件夹纳入NativeFolderWatcher监控范围内。在分类面板的标题栏（箭头指向的位置）添加一个扫描按钮，该按钮的用途是 验证DIR 00ms73182x000.arc是否为空，如果为空则将该文件夹和该ID 00ms73182x000 彻底从数据库中移除掉”，以及用户针对残留脏数据的痛点反馈：“‘文件早就不存在’的垃圾删不掉、扫描范围太窄、数据库死角未载入内存等导致侧边栏计数不发生变化的深层次原因”）。

本方案旨在从根本上融合“顺向空包检索”与“逆向数据库幽灵记录对账”的双轨深度清理逻辑，彻底解决侧边栏计数无法刷新、脏数据残留的核心问题。

## 2. Problem Positioning (问题定位)
- **UI 展现位置**：在分类面板（`CategoryPanel::initUi`）标题栏的 headerLayout 右侧添加 `m_btnScan`，位置、外观及悬停微动与系统其他标题栏按钮对齐。
- **级联删除不彻底漏洞**：`MetadataManager::removeMetadataBatchSync` 仅从 `metadata` 与 `system_stats` 两个表中删除对应的 `folderId` 记录，但完全遗漏了 `category_items` 分类关联表，导致侧边栏计数因残留记录而完全无法更新。必须补充 `DELETE FROM category_items WHERE folder_id = ?`。
- **顺向磁盘物理空包检索**：
  1. 后台线程全量遍历各个挂载盘符 `QDir::drives()` 下的 `ArcMeta.Library_[盘符]` 托管库。
  2. 识别出所有以 `.arc` 结尾的胶囊文件夹，判断除 `_thumbnail.png` 和 `.ArcMeta.json` 外是否为空。
  3. 如果判定为空，物理删除之，并将其 13 位 `folderId` 收集。
- **逆向数据库幽灵记录检索（深度对账）**：
  1. 从内存缓存及当前所有已加载的数据库中检索所有 `.arc`（或者是 `is_folder = 1` 且路径以 `.arc` 结尾）的托管包记录。
  2. 反向检查这些记录对应的物理路径是否在磁盘上仍然存在。
  3. 若在磁盘上早已不存在（被手动物理删除），则该记录被判定为“幽灵记录”，收集其 `folderId` 与路径，将其加入批量级联清除列表，彻底从缓存和数据库各关联表中抹除。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 在分类面板的标题栏（箭头指向的位置）添加一个扫描按钮 | 在 `CategoryPanel` 标题栏右侧加入 `m_btnScan` 按钮，图标为 `sync`。 | ✅ 一致 |
| 2    | 验证DIR 00ms73182x000.arc是否为空，如果为空则彻底从数据库和物理中移除 | 顺向磁盘物理检索，完全匹配 `.arc` 文件夹并清算。 | ✅ 一致 |
| 3    | 逆向“文件早就不存在”的垃圾删不掉，数据库死角与范围太窄问题 | 引入逆向全库幽灵记录对账机制，检查数据库里所有 `.arc` 记录的物理存在性，级联强力抹除。 | ✅ 一致 |

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 在 `src/meta/MetadataManager.h` 中追加接口声明
```merge_diff
<<<<<<< SEARCH
    // 2026-xx-xx 按照 Plan-131：极速批量对账元数据记录
    void removeMetadataBatchSync(const QStringList& paths);
=======
    // 2026-xx-xx 按照 Plan-131：极速批量对账元数据记录
    void removeMetadataBatchSync(const QStringList& paths);

    // 2026-07-xx 按照 Modification_Plan-38：获取全量数据库中的 .arc 托管包记录以作逆向对账
    QList<QPair<QString, QString>> getDbManagedArcRecords();
>>>>>>> REPLACE
```

### 4.2 在 `src/meta/MetadataManager.cpp` 中级联删除 `category_items` 数据
```merge_diff
<<<<<<< SEARCH
    // 2. 数据库执行
    const char* sql = "DELETE FROM metadata WHERE folder_id = ?";
    for (auto& entry : groupedFids) {
        sqlite3* db = entry.first;
        const auto& fids = entry.second;

        // [Plan-131 方案 A] 直连模式，废除冗余异步分发
        SqlTransaction trans(db);
        sqlite3_stmt* memStmt;
        if (sqlite3_prepare_v2(db, sql, -1, &memStmt, nullptr) == SQLITE_OK) {
            for (const auto& fid : fids) {
                sqlite3_bind_text(memStmt, 1, fid.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(memStmt);
                sqlite3_reset(memStmt);
            }
            sqlite3_finalize(memStmt);
        }

        // 🚨 2026-07-27 按照 Plan-107：极速级联清除 system_stats 中的 PROGRESS 进度记录
=======
    // 2. 数据库执行
    const char* sql = "DELETE FROM metadata WHERE folder_id = ?";
    const char* sqlCategory = "DELETE FROM category_items WHERE folder_id = ?";
    for (auto& entry : groupedFids) {
        sqlite3* db = entry.first;
        const auto& fids = entry.second;

        // [Plan-131 方案 A] 直连模式，废除冗余异步分发
        SqlTransaction trans(db);
        sqlite3_stmt* memStmt;
        if (sqlite3_prepare_v2(db, sql, -1, &memStmt, nullptr) == SQLITE_OK) {
            for (const auto& fid : fids) {
                sqlite3_bind_text(memStmt, 1, fid.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(memStmt);
                sqlite3_reset(memStmt);
            }
            sqlite3_finalize(memStmt);
        }

        // 2026-07-xx 按照 Modification_Plan-38：级联清理 category_items 表中的脏关联记录，彻底刷新侧边栏计数
        sqlite3_stmt* catStmt;
        if (sqlite3_prepare_v2(db, sqlCategory, -1, &catStmt, nullptr) == SQLITE_OK) {
            for (const auto& fid : fids) {
                sqlite3_bind_text(catStmt, 1, fid.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(catStmt);
                sqlite3_reset(catStmt);
            }
            sqlite3_finalize(catStmt);
        }

        // 🚨 2026-07-27 按照 Plan-107：极速级联清除 system_stats 中的 PROGRESS 进度记录
>>>>>>> REPLACE
```

### 4.3 在 `src/meta/MetadataManager.cpp` 中实现全量托管包记录检索接口
```merge_diff
<<<<<<< SEARCH
std::wstring MetadataManager::getVolumeSerialNumber(const std::wstring& path) {
=======
QList<QPair<QString, QString>> MetadataManager::getDbManagedArcRecords() {
    QList<QPair<QString, QString>> records;
    
    // 收集所有数据库连接（全局库与各挂载盘数据库）
    std::vector<sqlite3*> dbs;
    dbs.push_back(DatabaseManager::instance().getGlobalDb());
    
    // 全量遍历当前已挂载挂载点数据库
    const auto drives = QDir::drives();
    for (const QFileInfo& drive : drives) {
        QString letter = drive.absolutePath().left(1).toUpper();
        std::wstring volSerial = getVolumeSerialNumber(drive.absolutePath().toStdWString());
        if (volSerial != L"UNKNOWN") {
            sqlite3* db = DatabaseManager::instance().getDriveDb(volSerial, letter);
            if (db && std::find(dbs.begin(), dbs.end(), db) == dbs.end()) {
                dbs.push_back(db);
            }
        }
    }

    const char* querySql = "SELECT folder_id, path FROM metadata WHERE is_folder = 1 AND path LIKE '%.arc%'";
    for (sqlite3* db : dbs) {
        if (!db) continue;
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, querySql, -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const unsigned char* fidText = sqlite3_column_text(stmt, 0);
                const unsigned char* pathText = sqlite3_column_text(stmt, 1);
                if (fidText && pathText) {
                    records.append(qMakePair(
                        QString::fromUtf8(reinterpret_cast<const char*>(fidText)),
                        QString::fromUtf8(reinterpret_cast<const char*>(pathText))
                    ));
                }
            }
            sqlite3_finalize(stmt);
        }
    }
    return records;
}

std::wstring MetadataManager::getVolumeSerialNumber(const std::wstring& path) {
>>>>>>> REPLACE
```

### 4.4 在 `src/ui/CategoryPanel.h` 中追加按钮声明与槽函数
```merge_diff
<<<<<<< SEARCH
    // 2026-06-xx 按照用户要求：补全回收站专属操作
    void onEmptyTrash();
    void onRestoreAllFromTrash();

    // 2026-xx-xx 按照 Plan-98：搜索过滤
    void onSearchTextChanged(const QString& text);

private:
    void initUi();
=======
    // 2026-06-xx 按照用户要求：补全回收站专属操作
    void onEmptyTrash();
    void onRestoreAllFromTrash();

    // 2026-07-xx 按照 Modification_Plan-38：手动顺逆双轨一键清理空白/幽灵托管包
    void onScanAndCleanEmptyArcs();

    // 2026-xx-xx 按照 Plan-98：搜索过滤
    void onSearchTextChanged(const QString& text);

private:
    void initUi();
>>>>>>> REPLACE
```

并在 `src/ui/CategoryPanel.h` 底部的私有成员中追加扫描按钮定义：
```merge_diff
<<<<<<< SEARCH
    DropTreeView* m_categoryTree = nullptr;
    CategoryModel* m_categoryModel = nullptr;
    CategoryFilterProxyModel* m_proxyModel = nullptr;
    QLineEdit* m_searchEdit = nullptr;
=======
    DropTreeView* m_categoryTree = nullptr;
    CategoryModel* m_categoryModel = nullptr;
    CategoryFilterProxyModel* m_proxyModel = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QPushButton* m_btnScan = nullptr;
>>>>>>> REPLACE
```

### 4.5 在 `src/ui/CategoryPanel.cpp` 初始化界面标题栏处创建并添加该按钮
```merge_diff
<<<<<<< SEARCH
    QLabel* titleLabel = new QLabel("分类", header);
    titleLabel->setStyleSheet(QString("font-size: 13px; font-weight: bold; color: %1; background: transparent; border: none;").arg(qssColor(PrimaryBlue)));
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();

    m_mainLayout->addWidget(header);
=======
    QLabel* titleLabel = new QLabel("分类", header);
    titleLabel->setStyleSheet(QString("font-size: 13px; font-weight: bold; color: %1; background: transparent; border: none;").arg(qssColor(PrimaryBlue)));
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();

    // 2026-07-xx 按照用户要求 (Modification_Plan-38)：在分类标题栏右侧加入一键扫描空托管包按钮
    m_btnScan = new QPushButton(header);
    m_btnScan->setFixedSize(24, 24);
    m_btnScan->setIcon(UiHelper::getIcon("sync", QColor("#B0B0B0"), 16));
    m_btnScan->setStyleSheet(
        "QPushButton { "
        "  background: transparent; "
        "  border: none; "
        "  border-radius: 3px; "
        "} "
        "QPushButton:hover { "
        "  background-color: #3E3E42; "
        "} "
        "QPushButton:pressed { "
        "  background-color: #4E4E52; "
        "}"
    );
    m_btnScan->setProperty("tooltipText", "扫描并清理空白与幽灵托管包");
    m_btnScan->installEventFilter(m_hoverFilter); // 复用既有悬停滤镜以触发 tooltip
    connect(m_btnScan, &QPushButton::clicked, this, &CategoryPanel::onScanAndCleanEmptyArcs);
    headerLayout->addWidget(m_btnScan);

    m_mainLayout->addWidget(header);
>>>>>>> REPLACE
```

### 4.6 在 `src/ui/CategoryPanel.cpp` 中实现顺逆双轨一键清理机制
```merge_diff
<<<<<<< SEARCH
void CategoryPanel::onRestoreAllFromTrash() {
=======
void CategoryPanel::onScanAndCleanEmptyArcs() {
    // 🚨 核心防抖：防止高频重复点击引发扫描风暴
    m_btnScan->setEnabled(false);
    m_btnScan->setIcon(UiHelper::getIcon("sync", QColor("#888888"), 16));

    // 使用 QtConcurrent 在后台线程池中执行磁盘物理扫描与数据库反向核查，保障主线程 UI 绝无卡顿
    QtConcurrent::run([this]() {
        const auto drives = QDir::drives();
        int cleanCount = 0;
        QStringList allDeletePaths;

        // --- 1. 顺向物理扫描空托管包 ---
        for (const QFileInfo& drive : drives) {
            QString letter = drive.absolutePath().left(1).toUpper();
            std::wstring volSerial = MetadataManager::getVolumeSerialNumber(drive.absolutePath().toStdWString());
            if (volSerial == L"UNKNOWN") continue;

            // 获取当前资源库根目录绝对路径
            std::wstring managedRootW = MetadataManager::getManagedLibraryPath(volSerial, letter);
            if (managedRootW.empty()) continue;

            QString managedRoot = QString::fromStdWString(managedRootW);
            QDir libDir(managedRoot);
            if (!libDir.exists()) continue;

            // 获取资源库下的全部 .arc 资产文件夹
            QStringList arcEntries = libDir.entryList({"*.arc"}, QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
            for (const QString& arcName : arcEntries) {
                QFileInfo arcInfo(libDir.absoluteFilePath(arcName));
                QString baseName = arcInfo.completeBaseName();
                if (baseName.length() != 13) continue;

                QDir arcDir(arcInfo.absoluteFilePath());
                // 排除隐藏缩略图 _thumbnail.png 以及 .ArcMeta.json 配置文件
                QStringList entries = arcDir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
                bool hasRealMaterials = false;
                for (const QString& fName : entries) {
                    if (fName.endsWith("_thumbnail.png", Qt::CaseInsensitive)) continue;
                    if (fName.compare(".ArcMeta.json", Qt::CaseInsensitive) == 0) continue;
                    hasRealMaterials = true;
                    break;
                }

                // 如果包物理上为空，则加入清除队列，后续一并物理及数据库同步清退
                if (!hasRealMaterials) {
                    allDeletePaths << arcInfo.absoluteFilePath();
                }
            }
        }

        // --- 2. 逆向数据库核查，深挖“物理文件早已被删”的幽灵残留记录 ---
        QList<QPair<QString, QString>> dbRecords = MetadataManager::instance().getDbManagedArcRecords();
        for (const auto& record : dbRecords) {
            QString recordPath = record.second;
            // 物理磁盘上文件夹不存在 并且 该路径不属于本轮已经确定要删的物理空目录
            if (!QFileInfo::exists(recordPath) && !allDeletePaths.contains(recordPath)) {
                allDeletePaths << recordPath;
            }
        }

        // --- 3. 批量级联清除磁盘及所有数据库表的脏元数据关系 ---
        if (!allDeletePaths.isEmpty()) {
            allDeletePaths.removeDuplicates();

            // 批量删除内存及各级 DB 记录（会自动级联清除 metadata、category_items 等数据）
            MetadataManager::instance().removeMetadataBatchSync(allDeletePaths);

            // 彻底清除存在的物理目录
            for (const QString& path : allDeletePaths) {
                if (QFileInfo::exists(path)) {
                    QDir(path).removeRecursively();
                }
            }
            cleanCount = allDeletePaths.size();
        }

        // --- 4. 返回主线程完成 UI 同步对账、播放视听反馈 ---
        QMetaObject::invokeMethod(this, [this, cleanCount]() {
            m_btnScan->setEnabled(true);
            m_btnScan->setIcon(UiHelper::getIcon("sync", QColor("#B0B0B0"), 16));

            if (cleanCount > 0) {
                // 重启计数装载
                CategoryRepo::fullRecount();
                requestRefresh(true);

                // 刷新 MainWindow 的全部内容面板及侧边栏计数树
                QWidget* mw = window();
                if (mw) {
                    QMetaObject::invokeMethod(mw, "refreshAll", Qt::QueuedConnection);
                }

                ToolTipOverlay::instance()->showText(QCursor::pos(), 
                    QString("<b style='color:#00A650;'>已彻底深度清理 %1 个物理及幽灵托管包</b>").arg(cleanCount), 
                    2500, QColor("#00A650"));
            } else {
                ToolTipOverlay::instance()->showText(QCursor::pos(), 
                    "<b style='color:#CCCCCC;'>未检测到任何多余的空包或幽灵数据</b>", 
                    2000, QColor("#2D2D2D"));
            }
        });
    });
}

void CategoryPanel::onRestoreAllFromTrash() {
>>>>>>> REPLACE
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/meta/MetadataManager.h` —— 声明 `getDbManagedArcRecords` 接口。
- [ ] 模块/文件：`src/meta/MetadataManager.cpp` —— 
  - 在 `removeMetadataBatchSync` 中级联删除 `category_items` 数据记录。
  - 实现 `getDbManagedArcRecords` 全量挂载盘及全局 DB 中的 `.arc` 文件夹记录加载。
- [ ] 模块/文件：`src/ui/CategoryPanel.h` —— 声明 `m_btnScan` 按钮实例指针与后台异步点击响应槽函数。
- [ ] 模块/文件：`src/ui/CategoryPanel.cpp` —— 
  - 初始化标题栏右侧的 `m_btnScan` 并设置视觉样式。
  - 实现 `onScanAndCleanEmptyArcs` 线程池安全的顺逆双向清理与对账主逻辑。

**明确禁止越界修改的范围：**
- [ ] 自动同步驱动：`NativeFolderWatcher` 文件夹监控及对账引擎 —— 绝不修改。
- [ ] 视图层及基本模型底层多媒体提取核心结构 —— 绝不修改。

## 6. 实现准则与预警【核心】
1. **多级数据库级联清退**：在清除 `metadata` 元数据记录时，必须将对应的 `category_items` 分类映射条目同时擦除，这是侧边栏计数不更新的核心死角。
2. **异步后台闭锁**：物理 QDir 的 `exists()`、`removeRecursively()` 以及多挂载盘 SQL 核对具有明显的磁盘 IO 开销，必须在 `QtConcurrent::run` 后台线程池中独立运行，严禁在主 UI 线程内直接轮询引发界面冻结。
3. **UI 回归同步**：后台扫描结束后，通过 `QMetaObject::invokeMethod` 回调，调用主窗口的自愈式 `refreshAll` 接口，使界面完全同步对账刷新。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 双轨隔离 | 托管库下进行逻辑处理（仅改写 SQLite 映射字段）；磁盘模式下进行物理处理并同步缓存，两者独立运行。 | ✅ 符合（本清理按钮是在分类面板特化对托管包资源库进行离散对账，不影响磁盘导航模式） |
| UI 异步加载与防闪烁 | 在内容面板（`ContentPanel`）进行异步数据扫描前，禁止先行调用 `m_model->clear()`。 | ✅ 符合（本清理在清理成功后，仅发送刷新信号让内容面板及计数树完全自愈更新，并不硬清除模型，杜绝界面闪烁现象） |

## 8. 待确认事项（可选）
无。
