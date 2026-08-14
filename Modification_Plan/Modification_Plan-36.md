# 侧边栏一键清理空托管包 —— Modification_Plan-36.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
用户期望在侧边栏“分类”面板（Panel 1）的标题栏右侧增加一个一键扫描按钮，用以清理空的 `.arc` 托管包容器（对应用户原话：“放弃之前要求的将‘ArcMeta.Library_[盘符]’文件夹纳入NativeFolderWatcher监控范围内。在分类面板的标题栏（箭头指向的位置）添加一个扫描按钮，该按钮的用途是 验证DIR 00ms73182x000.arc是否为空，如果为空则将该文件夹和该ID 00ms73182x000 彻底从数据库中移除掉”）。

本方案旨在为分类面板标题栏右侧实现极简、无缝嵌入、与系统整体视觉高度一致的一键“扫描并清理空白托管包”功能。

## 2. 问题定位
- 分类面板的标题栏（`CategoryPanel::initUi` 中的 `header`）当前包含：一个文件夹图标、一个 `titleLabel` 分类标题文本和一个 `addStretch()` 弹簧。我们需要在弹簧之后放置一个新的一键扫描按钮 `m_btnScan`。
- 扫描与清理逻辑：
  1. 通过遍历挂载盘符 `QDir::drives()` 寻找所有格式形如 `ArcMeta.Library_[盘符]`（可通过 `MetadataManager::getManagedLibraryPath` 定位）的资源库根目录。
  2. 遍历该资源库目录下的所有以 `.arc` 结尾的胶囊文件夹（长度为 13 的 Base36 名，加上 `.arc` 扩展名）。
  3. 通过检查 `.arc` 目录是否除了 `_thumbnail.png` 和 `.ArcMeta.json` 隐藏配置文件之外无任何真实子项来验证是否为空。
  4. 对判定为空的包：
     - 执行物理磁盘级彻底删除：调用 `QDir::removeRecursively()`。
     - 执行数据库级彻底清退：获取其 13 位 ID（例如 `00ms73182x000`），作为 `folderId` 传入 `MetadataManager::instance().removeMetadataBatchSync` 级联擦除所有数据库表的残留元数据记录。
  5. 完全清理后，触发全量 UI 刷新与计数对账，并利用气泡消息弹出清理结果反馈。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 放弃之前要求的将“ArcMeta.Library_[盘符]”文件夹纳入NativeFolderWatcher监控范围内 | 本方案不涉及、不改动 NativeFolderWatcher 的任何逻辑。 | ✅ 一致 |
| 2    | 在分类面板的标题栏（箭头指向的位置）添加一个扫描按钮 | 在 `CategoryPanel` 的 `headerLayout` 最右侧新增一个 `m_btnScan`，位置、微动及配色样式对齐系统风格。 | ✅ 一致 |
| 3    | 该按钮的用途是 验证DIR 00ms73182x000.arc是否为空，如果为空则将该文件夹和该ID 00ms73182x000 彻底从数据库中移除掉 | 扫描全量资源库，对空 `.arc`（无除隐藏图和配置外的实质文件）进行 `QDir::removeRecursively()`，并级联抹除数据库元数据后刷新界面。 | ✅ 一致 |

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 在 `src/ui/CategoryPanel.h` 中追加按钮声明与槽函数
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

    // 2026-07-xx：手动扫描清理空白托管包
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

### 4.2 在 `src/ui/CategoryPanel.cpp` 初始化界面标题栏处创建并添加该按钮
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

    // 2026-07-xx 按照用户要求 (Modification_Plan-36)：在分类标题栏右侧加入一键扫描空托管包按钮
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
    m_btnScan->setProperty("tooltipText", "扫描并清理空白托管包");
    m_btnScan->installEventFilter(m_hoverFilter); // 复用既有悬停滤镜以触发 tooltip
    connect(m_btnScan, &QPushButton::clicked, this, &CategoryPanel::onScanAndCleanEmptyArcs);
    headerLayout->addWidget(m_btnScan);

    m_mainLayout->addWidget(header);
>>>>>>> REPLACE
```

### 4.3 在 `src/ui/CategoryPanel.cpp` 中实现点击扫描与彻底清洗逻辑
```merge_diff
<<<<<<< SEARCH
void CategoryPanel::onRestoreAllFromTrash() {
=======
void CategoryPanel::onScanAndCleanEmptyArcs() {
    // 🚨 核心阻断：防止重复高频点击触发扫描风暴
    m_btnScan->setEnabled(false);
    m_btnScan->setIcon(UiHelper::getIcon("sync", QColor("#888888"), 16));

    // 使用 QtConcurrent 在线程池中执行物理磁盘扫描，避免阻塞主线程 UI
    QtConcurrent::run([this]() {
        const auto drives = QDir::drives();
        int cleanCount = 0;
        QStringList allEmptyArcDirs;
        QStringList allEmptyFolderIds;

        for (const QFileInfo& drive : drives) {
            QString letter = drive.absolutePath().left(1).toUpper();
            std::wstring volSerial = MetadataManager::getVolumeSerialNumber(drive.absolutePath().toStdWString());
            if (volSerial == L"UNKNOWN") continue;

            // 获取资源库根目录绝对路径
            std::wstring managedRootW = MetadataManager::getManagedLibraryPath(volSerial, letter);
            if (managedRootW.empty()) continue;

            QString managedRoot = QString::fromStdWString(managedRootW);
            QDir libDir(managedRoot);
            if (!libDir.exists()) continue;

            // 寻找全部 .arc 格式容器文件夹
            QStringList arcEntries = libDir.entryList({"*.arc"}, QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
            for (const QString& arcName : arcEntries) {
                // 托管包文件夹名格式必须为 13 位 Base36 (例如 00ms73182x000.arc)
                QFileInfo arcInfo(libDir.absoluteFilePath(arcName));
                QString baseName = arcInfo.completeBaseName();
                if (baseName.length() != 13) continue;

                QDir arcDir(arcInfo.absoluteFilePath());
                // 获取包内所有物理项：排除隐藏的 _thumbnail.png 以及 .ArcMeta.json 配置文件以外
                QStringList entries = arcDir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
                bool hasRealMaterials = false;
                for (const QString& fName : entries) {
                    if (fName.endsWith("_thumbnail.png", Qt::CaseInsensitive)) continue;
                    if (fName.compare(".ArcMeta.json", Qt::CaseInsensitive) == 0) continue;
                    hasRealMaterials = true;
                    break;
                }

                // 如果确实是空的包，记录路径和 13 位 ID 进行级联抹除
                if (!hasRealMaterials) {
                    allEmptyArcDirs << arcInfo.absoluteFilePath();
                    allEmptyFolderIds << baseName;
                }
            }
        }

        // 批量对数据库与磁盘进行擦除
        if (!allEmptyArcDirs.isEmpty()) {
            // 1. 批量清理数据库元数据记录（涉及 metadata 和 category_items 的级联删除）
            MetadataManager::instance().removeMetadataBatchSync(allEmptyArcDirs);

            // 2. 物理彻底擦除磁盘空目录
            for (const QString& path : allEmptyArcDirs) {
                QDir(path).removeRecursively();
            }
            cleanCount = allEmptyArcDirs.size();
        }

        // 3. 在主线程同步 UI 数据、播放反馈通知并恢复按钮状态
        QMetaObject::invokeMethod(this, [this, cleanCount]() {
            m_btnScan->setEnabled(true);
            m_btnScan->setIcon(UiHelper::getIcon("sync", QColor("#B0B0B0"), 16));

            if (cleanCount > 0) {
                // 重新计数对账以更新侧边栏和主界面
                CategoryRepo::fullRecount();
                requestRefresh(true);

                // 尝试寻找主面板进行内容区全局自愈重构刷新
                QWidget* mw = window();
                if (mw) {
                    QMetaObject::invokeMethod(mw, "refreshAll", Qt::QueuedConnection);
                }

                ToolTipOverlay::instance()->showText(QCursor::pos(), 
                    QString("<b style='color:#00A650;'>已成功清理 %1 个空白托管包</b>").arg(cleanCount), 
                    2500, QColor("#00A650"));
            } else {
                ToolTipOverlay::instance()->showText(QCursor::pos(), 
                    "<b style='color:#CCCCCC;'>未检测到多余的空白托管包</b>", 
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
- [ ] 模块/文件：`src/ui/CategoryPanel.h` —— 声明 `onScanAndCleanEmptyArcs` 槽函数、以及 `m_btnScan` 按钮实例指针。
- [ ] 模块/文件：`src/ui/CategoryPanel.cpp` —— 在 `initUi` 标题栏布局中创建并添加该按钮；实现 `onScanAndCleanEmptyArcs` 物理及级联元数据库擦除函数。

**明确禁止越界修改的范围：**
- [ ] 监控与后台守护：`NativeFolderWatcher` 文件夹监控及对账引擎 —— 绝不修改
- [ ] 核心多媒体提取算法和 `DatabaseManager` 底层驱动 —— 绝不修改

## 6. 实现准则与预警【核心】
1. **异步闭锁保护机制**：由于扫描全量挂载磁盘的 `.arc` 文件夹可能有轻微的 I/O 延时，物理检索与文件夹大小计算一律放在 `QtConcurrent::run` 后台线程池中。只有主界面数据刷盘和气泡弹出在 `QMetaObject::invokeMethod` 主线程中同步运行。
2. **防点击抖动**：点击按钮后，首行必须对 `m_btnScan` 实例调用 `setEnabled(false)` 进行禁置高亮处理，等全部后台清理和落盘完成后，再于主线程中安全解锁。
3. **级联元数据清理**：对于定位为空的包，必须同时对数据库（调用统一的大事务 `removeMetadataBatchSync` 级联删除）和物理介质进行销毁。
4. **排除隐藏资产**：空包检测算法中，必须跳过隐藏预览图 `_thumbnail.png` 和配置文件 `.ArcMeta.json`。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 双轨隔离 | 托管库下进行逻辑处理（仅改写 SQLite 映射字段）；磁盘模式下进行物理处理并同步缓存，两者独立运行。 | ✅ 符合（本清理按钮是在分类面板特化对托管包资源库进行离散对账，不影响磁盘导航模式） |
| UI 异步加载与防闪烁 | 在内容面板（`ContentPanel`）进行异步数据扫描前，禁止先行调用 `m_model->clear()`。 | ✅ 符合（本清理在清理成功后，仅发送刷新信号让内容面板完全自愈更新，并不硬清除模型，杜绝界面闪烁现象） |

## 8. 待确认事项（可选）
无。
