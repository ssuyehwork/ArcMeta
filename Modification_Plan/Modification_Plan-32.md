# 右键菜单顺序调整与新增复制名称功能 —— Modification_Plan-32.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
本方案针对用户提出的调整右键菜单选项顺序和新增“复制名称”的要求进行专项系统重构（对应用户原话：“我期望按照这个顺序去调整右键菜单选项的顺序 ... 复制名称 ------新增”）。我们将重排内容面板（ContentPanel）右键菜单中的全部项，将其归入三个高度明确的区块（Item 级动作、剪贴板/编辑动作、通用配置动作），在每个区块中严格遵守用户设定的顺序，并安全实现多选文件名称复制功能。

## 2. 问题定位
1. **菜单顺序杂乱**：原有右键菜单各功能项由于长期的迭代开发，排列有些重叠。我们需要在 `ContentPanel::onCustomContextMenuRequested` 中按照：
   - 第一组：打开、用系统默认程序打开、在“资源管理器”中显示、归类到..、置顶、添加至收藏夹
   - 第二组：复制、剪切、粘贴、复制名称（新增）、复制路径
   - 第三组：刷新、属性、重新扫描、加密保护、排序、删除
   的顺序进行严格重排。
2. **新增复制名称功能**：在 `ContentPanel.h` 的 `ContextAction` 中增加 `ActionCopyName`，并在右键菜单触发对应动作时读取选中的项目的 `PathRole`（或 `DisplayRole`），利用 `QFileInfo::fileName()` 解析出名字并写入 `QApplication::clipboard()`。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 按照这个顺序去调整右键菜单选项的顺序 ... 复制名称 ------新增（对应用户原话） | 在 `ContentPanel.cpp` 中完全按照用户提供的顺序和分组调整右键菜单生成逻辑，并在 `ContextAction` 枚举与 switch 中新增 `ActionCopyName` 复制名称处理逻辑（对应用户原话）。 | ✅ |

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 修改 `src/ui/ContentPanel.h`
在 `ContextAction` 枚举中新增 `ActionCopyName` 项。

```diff
<<<<<<< SEARCH
        ActionPermanentDelete,
        ActionSecureDelete,
        ActionRestore,
        ActionCopyPath,
        ActionProperties,
=======
        ActionPermanentDelete,
        ActionSecureDelete,
        ActionRestore,
        ActionCopyName,
        ActionCopyPath,
        ActionProperties,
>>>>>>> REPLACE
```

### 4.2 修改 `src/ui/ContentPanel.cpp`
彻底重写 `ContentPanel::onCustomContextMenuRequested` 函数中的菜单项构造顺序，并实现 `ActionCopyName`。

```diff
<<<<<<< SEARCH
void ContentPanel::onCustomContextMenuRequested(const QPoint& pos) { 
    QAbstractItemView* view = qobject_cast<QAbstractItemView*>(sender()); 
    if (!view) return; 
 
    QModelIndex currentIndex = view->indexAt(pos); 
    bool onItem = currentIndex.isValid(); 
    bool isFolder = onItem && (currentIndex.data(TypeRole).toString() == "folder"); 
    QString path = currentIndex.data(PathRole).toString(); 
 
    QMenu menu(this); 
    UiHelper::applyMenuStyle(&menu); 
 
    if (onItem) { 
        // 2026-06-xx 物理修复：在回收站分类中，顶部增加“还原”选项
        if (m_currentCategoryType == "trash") {
            menu.addAction(UiHelper::getIcon("sync", QColor("#2ecc71"), 18), "还原")->setData(ActionRestore);
            menu.addSeparator();
        }

        // [核心操作区] 
        QAction* actOpen = menu.addAction(isFolder ? "打开文件夹" : "打开"); 
        actOpen->setData(ActionOpen); 
        if (!isFolder) { 
            menu.addAction("用系统默认程序打开")->setData(ActionOpenDefault); 
        } 
        menu.addAction("在“资源管理器”中显示")->setData(ActionShowInExplorer); 
 
        menu.addSeparator(); 
 
        // [归类与标记区] 
        // 2026-07-xx 按照 Plan-117：语义分流。判定当前是否为“镜像源”
        // 镜像源定义：侧边栏分类模式 (isMirrorSource() 为真) 
        // 或 物理导航模式下已进入资源库内部 (镜像加速态)
        // 🚨 [双轨不隔离违规点-3 物理隔离修复]: 磁盘模式右键菜单 100% 与内存数据库模式隔离，
        // 表现等同于 Windows 资源管理器。普通物理磁盘导航下的项绝对不提供“归类/设置颜色/设置评分”等任何逻辑库特权操作。
        bool isMirror = isMirrorSource();

        if (isMirror) {
            // [镜像源：归类与元数据编辑区]
            QMenu* categorizeMenu = menu.addMenu("归类到..."); 
            UiHelper::applyMenuStyle(categorizeMenu); 
            auto categories = CategoryRepo::getRecentlyUsed(15); 
            if (categories.empty()) categories = CategoryRepo::getAll();
            if (categories.size() > 15) categories.resize(15);

            QAction* actToUncat = categorizeMenu->addAction(UiHelper::getIcon("uncategorized", QColor("#95a5a6"), 16), "回归“未分类”");
            actToUncat->setData(ActionCategorize);
            actToUncat->setProperty("catId", -2); 
            categorizeMenu->addSeparator();

            if (categories.empty()) { 
                categorizeMenu->addAction("（暂无分类）")->setEnabled(false); 
            } else { 
                for (const auto& cat : categories) { 
                    QAction* act = categorizeMenu->addAction(QString::fromStdWString(cat.name)); 
                    act->setData(ActionCategorize); 
                    act->setProperty("catId", cat.id); 
                } 
            }

            // 直接在主菜单上呈现“设定颜色标签”快捷色块栏
            QString currentColorStr = currentIndex.data(ColorRole).toString();

            QWidgetAction* pickerAction = new QWidgetAction(&menu);
            ColorStripPicker* pickerWidget = new ColorStripPicker(currentColorStr, &menu);
            pickerAction->setDefaultWidget(pickerWidget);
            menu.addAction(pickerAction);

            connect(pickerWidget, &ColorStripPicker::colorSelected, this, [this, view, &menu](const QString& hexColor) {
                struct SelectedItemInfo {
                    QString type;
                    QString path;
                    int categoryId = 0;
                };
                QList<SelectedItemInfo> selectedItems;
                auto indexes = view->selectionModel()->selectedIndexes();  
                for (const auto& idx : indexes) {  
                    if (idx.column() == 0) {  
                        SelectedItemInfo info;
                        info.type = idx.data(TypeRole).toString();
                        info.path = idx.data(PathRole).toString();
                        info.categoryId = idx.data(CategoryIdRole).toInt();
                        selectedItems.append(info);
                    }  
                }

                for (const auto& idx : indexes) {  
                    if (idx.column() == 0) {  
                        m_proxyModel->setData(idx, hexColor, ColorRole);  
                    }  
                } 

                for (const auto& info : selectedItems) {
                    selectAndScrollToItem(info.type, info.path, info.categoryId);
                }
                menu.close(); 
            });
 
            bool isPinned = currentIndex.data(IsLockedRole).toBool(); 
            menu.addAction(isPinned ? "取消置顶" : "置顶")->setData(isPinned ? ActionUnpin : ActionPin); 
        } else {
            // [物理源：显示“迁移”]
            if (!m_currentPath.isEmpty() && m_currentPath != "computer://") {
                std::wstring wp = path.toStdWString();
                std::wstring volSerial = MetadataManager::getVolumeSerialNumber(wp);

                // 2026-07-xx 按照 Plan-121：统一复用 AutoImportManager 的路径计算逻辑，
                // 不再自行拼接，确保使用完全一致的路径来源。
                std::wstring managedRootW = AutoImportManager::getManagedLibraryPath(wp);
                QString managedRoot = QString::fromStdWString(managedRootW);

                QMenu* migrateMenu = menu.addMenu(UiHelper::getIcon("add", QColor("#FF8C00"), 18), "迁移");
                UiHelper::applyMenuStyle(migrateMenu);

                if (managedRoot.isEmpty()) {
                    // Library 文件夹尚未创建，给出明确提示而非显示错误路径
                    migrateMenu->addAction("该盘库存未创建")->setEnabled(false);
                } else {
                    QAction* actRoot = migrateMenu->addAction(managedRoot);
                    actRoot->setData(ActionAddToCategory);
                    actRoot->setProperty("targetPath", managedRoot);

                    migrateMenu->menuAction()->setData(ActionAddToCategory);
                    migrateMenu->menuAction()->setProperty("targetPath", managedRoot);
                }

                migrateMenu->addSeparator();
                QStringList recentFolders = NavigationHistoryService::getRecentVisitedFolders(volSerial);
                if (recentFolders.isEmpty()) {
                    migrateMenu->addAction("迁移至最近活跃位置...")->setEnabled(false);
                } else {
                    for (const QString& folder : recentFolders) {
                        QAction* act = migrateMenu->addAction(folder);
                        act->setData(ActionAddToCategory);
                        act->setProperty("targetPath", folder);
                    }
                }
            }
        }

        menu.addSeparator(); 
 
        // 2026-06-xx 逻辑解耦修复：解除批量重命名的类型硬编码锁定 (架构升级)。
        // 核心规则：多选有效项目 (PathRole 不为空) 或 单选文件夹时，均解锁批量重命名入口。
        int selectedCount = 0;
        for (const auto& selIdx : view->selectionModel()->selectedIndexes()) {
            if (selIdx.column() == 0 && !selIdx.data(PathRole).toString().isEmpty()) {
                selectedCount++;
            }
        }

        // [批量与加密区] 
        if (isFolder || selectedCount > 1) { 
            menu.addAction("批量重命名 (Ctrl+Shift+R)")->setData(ActionBatchRename); 
        }

        if (!isFolder) { 
            QMenu* cryptoMenu = menu.addMenu("加密保护"); 
            UiHelper::applyMenuStyle(cryptoMenu); 
            cryptoMenu->addAction("执行加密保护")->setData(ActionEncrypt); 
            cryptoMenu->addAction("解除加密")->setData(ActionDecrypt); 
            cryptoMenu->addAction("修改加密密码")->setData(ActionChangePwd); 
        } 
 
        menu.addSeparator(); 
 
        // [通用编辑区] 
        if (selectedCount <= 1) {
            menu.addAction("重命名")->setData(ActionRename); 
        }
        menu.addAction("复制")->setData(ActionCopy); 
        menu.addAction("剪切")->setData(ActionCut); 
        menu.addAction("粘贴")->setData(ActionPaste); 
        
        // 2026-06-xx 按照用户要求：在回收站中不显示二级删除菜单
        if (m_currentCategoryType != "trash") {
            QMenu* delMenu = menu.addMenu("删除");
            UiHelper::applyMenuStyle(delMenu);
            delMenu->addAction("移入回收站")->setData(ActionDelete);
            // 2026-07-xx 物理级精简：移除普通彻底删除，仅保留并更名为“永久删除”（采用安全抹除逻辑）
            delMenu->addAction("永久删除")->setData(ActionSecureDelete);
        } else {
            // 回收站模式下，原位置不显示删除
        }
 
        menu.addSeparator(); 
        menu.addAction("复制路径")->setData(ActionCopyPath); 
        menu.addAction("添加至收藏夹")->setData(ActionAddToFavorites); 
        menu.addAction("刷新")->setData(ActionRefresh); 
        menu.addAction("属性")->setData(ActionProperties); 

        // 2026-07-xx 按照 Development_Plan 2.1：始终显示“重新扫描”选项 (仅限资源库内项目)
        if (currentIndex.data(ManagedRole).toBool()) {
            menu.addSeparator();
            menu.addAction(UiHelper::getIcon("sync", QColor("#378ADD"), 18), "重新扫描")->setData(ActionRescan);
        }

        // 2026-07-27 按照 Plan-107：仅对已在资源库中登记的文件夹，增加“取消导入并清除数据”菜单项
        if (currentIndex.data(TypeRole).toString() == "folder" && currentIndex.data(ManagedRole).toBool()) {
            menu.addAction(UiHelper::getIcon("close", QColor("#e81123"), 18), "取消导入并清除数据")->setData(ActionCancelImport);
        }

        // 2026-06-xx 按照用户要求：在回收站分类中，最底部增加“永久删除”选项
        if (m_currentCategoryType == "trash") {
            menu.addSeparator();
            // 2026-07-xx 物理一致性：回收站内的永久删除统一采用 ActionSecureDelete
            menu.addAction(UiHelper::getIcon("trash", QColor("#e81123"), 18), "永久删除")->setData(ActionSecureDelete);
        }
 
    } else { 
        // [空白处菜单] 
        QMenu* newMenu = menu.addMenu("新建..."); 
        UiHelper::applyMenuStyle(newMenu); 
        newMenu->addAction(UiHelper::getIcon("folder_filled", QColor("#EEEEEE")), "创建文件夹")->setData(ActionNewFolder); 
        newMenu->addAction(UiHelper::getIcon("text", QColor("#EEEEEE")), "创建 Markdown")->setData(ActionNewMd); 
        newMenu->addAction(UiHelper::getIcon("text", QColor("#EEEEEE")), "创建纯文本文件 (txt)")->setData(ActionNewTxt); 
 
        menu.addSeparator(); 
        QAction* actPaste = menu.addAction("粘贴"); 
        actPaste->setData(ActionPaste); 
        actPaste->setEnabled(!m_currentPath.isEmpty() && m_currentPath != "computer://"); 
 
        menu.addSeparator(); 
        menu.addAction("刷新")->setData(ActionRefresh);

        menu.addSeparator(); 
        QAction* actProp = menu.addAction("当前文件夹属性"); 
        actProp->setData(ActionProperties); 
        actProp->setEnabled(!m_currentPath.isEmpty() && m_currentPath != "computer://"); 
 
        // 2026-07-xx 按照 Plan-63：如果是空白处点击，直接在这里注入并在下方 exec
    } 

    menu.addSeparator();

    // 注入“排序”二级子菜单
    QMenu* sortMenu = menu.addMenu("排序");
    UiHelper::applyMenuStyle(sortMenu);

    // 属性单选组
    QActionGroup* typeGroup = new QActionGroup(this);
    auto addTypeAct = [&](const QString& label, ContentPanel::SortType type) {
        QAction* act = sortMenu->addAction(label);
        act->setCheckable(true);
        act->setChecked(m_sortType == type);
        typeGroup->addAction(act);
        connect(act, &QAction::triggered, [this, type]() {
            m_sortType = type;
            AppConfig::instance().setValue("ContentPanel/RightClickSortType", static_cast<int>(type));
            
            // 实时触发全量无效化与排序重计算
            m_proxyModel->invalidate();
            m_proxyModel->sort(0, m_sortOrder);
        });
    };

    addTypeAct("名称", ContentPanel::SortByName);
    addTypeAct("创建日期", ContentPanel::SortByCreateDate);
    addTypeAct("修改日期", ContentPanel::SortByModifyDate);
    addTypeAct("扩展名", ContentPanel::SortByExtension);
    addTypeAct("大小", ContentPanel::SortBySize);
    addTypeAct("尺寸", ContentPanel::SortByDimension);
    addTypeAct("评分", ContentPanel::SortByRating);
    addTypeAct("添加日期", ContentPanel::SortByAddedDate);

    sortMenu->addSeparator();

    // 方向单选组
    QActionGroup* orderGroup = new QActionGroup(this);
    auto addOrderAct = [&](const QString& label, Qt::SortOrder order) {
        QAction* act = sortMenu->addAction(label);
        act->setCheckable(true);
        act->setChecked(m_sortOrder == order);
        orderGroup->addAction(act);
        connect(act, &QAction::triggered, [this, order]() {
            m_sortOrder = order;
            AppConfig::instance().setValue("ContentPanel/RightClickSortOrder", static_cast<int>(order));
            
            m_proxyModel->invalidate();
            m_proxyModel->sort(0, order);
        });
    };

    addOrderAct("升序", Qt::AscendingOrder);
    addOrderAct("降序", Qt::DescendingOrder);

    // 2026-07-xx 按照 Plan-63：注入布局显示控制菜单
    menu.addSeparator();
    QMenu* layoutMenu = menu.addMenu("布局显示");
    UiHelper::applyMenuStyle(layoutMenu);
    
    // 通过向上寻道获取 MainWindow 实例以复用菜单逻辑
    MainWindow* mw = nullptr;
    QWidget* parentWin = window();
    while (parentWin) {
        if ((mw = qobject_cast<MainWindow*>(parentWin))) break;
        parentWin = parentWin->parentWidget();
    }
    if (mw) {
        mw->populatePanelMenu(layoutMenu);
    }
 
    QAction* selectedAction = menu.exec(view->viewport()->mapToGlobal(pos)); 
=======
void ContentPanel::onCustomContextMenuRequested(const QPoint& pos) { 
    QAbstractItemView* view = qobject_cast<QAbstractItemView*>(sender()); 
    if (!view) return; 
 
    QModelIndex currentIndex = view->indexAt(pos); 
    bool onItem = currentIndex.isValid(); 
    bool isFolder = onItem && (currentIndex.data(TypeRole).toString() == "folder"); 
    QString path = currentIndex.data(PathRole).toString(); 
 
    QMenu menu(this); 
    UiHelper::applyMenuStyle(&menu); 
 
    if (onItem) { 
        // 2026-06-xx 物理修复：在回收站分类中，顶部增加“还原”选项
        if (m_currentCategoryType == "trash") {
            menu.addAction(UiHelper::getIcon("sync", QColor("#2ecc71"), 18), "还原")->setData(ActionRestore);
            menu.addSeparator();
        }

        // ================= 【第一组】 =================
        // 1. 打开 / 打开文件夹
        QAction* actOpen = menu.addAction(isFolder ? "打开文件夹" : "打开"); 
        actOpen->setData(ActionOpen); 
        if (!isFolder) { 
            menu.addAction("用系统默认程序打开")->setData(ActionOpenDefault); 
        } 
        
        // 2. 在“资源管理器”中显示
        menu.addAction("在“资源管理器”中显示")->setData(ActionShowInExplorer); 
 
        // 3. 归类到..（或迁移）
        bool isMirror = isMirrorSource();
        if (isMirror) {
            QMenu* categorizeMenu = menu.addMenu("归类到..."); 
            UiHelper::applyMenuStyle(categorizeMenu); 
            auto categories = CategoryRepo::getRecentlyUsed(15); 
            if (categories.empty()) categories = CategoryRepo::getAll();
            if (categories.size() > 15) categories.resize(15);

            QAction* actToUncat = categorizeMenu->addAction(UiHelper::getIcon("uncategorized", QColor("#95a5a6"), 16), "回归“未分类”");
            actToUncat->setData(ActionCategorize);
            actToUncat->setProperty("catId", -2); 
            categorizeMenu->addSeparator();

            if (categories.empty()) { 
                categorizeMenu->addAction("（暂无分类）")->setEnabled(false); 
            } else { 
                for (const auto& cat : categories) { 
                    QAction* act = categorizeMenu->addAction(QString::fromStdWString(cat.name)); 
                    act->setData(ActionCategorize); 
                    act->setProperty("catId", cat.id); 
                } 
            }

            // 直接在主菜单上呈现“设定颜色标签”快捷色块栏
            QString currentColorStr = currentIndex.data(ColorRole).toString();

            QWidgetAction* pickerAction = new QWidgetAction(&menu);
            ColorStripPicker* pickerWidget = new ColorStripPicker(currentColorStr, &menu);
            pickerAction->setDefaultWidget(pickerWidget);
            menu.addAction(pickerAction);

            connect(pickerWidget, &ColorStripPicker::colorSelected, this, [this, view, &menu](const QString& hexColor) {
                struct SelectedItemInfo {
                    QString type;
                    QString path;
                    int categoryId = 0;
                };
                QList<SelectedItemInfo> selectedItems;
                auto indexes = view->selectionModel()->selectedIndexes();  
                for (const auto& idx : indexes) {  
                    if (idx.column() == 0) {  
                        SelectedItemInfo info;
                        info.type = idx.data(TypeRole).toString();
                        info.path = idx.data(PathRole).toString();
                        info.categoryId = idx.data(CategoryIdRole).toInt();
                        selectedItems.append(info);
                    }  
                }

                for (const auto& idx : indexes) {  
                    if (idx.column() == 0) {  
                        m_proxyModel->setData(idx, hexColor, ColorRole);  
                    }  
                } 

                for (const auto& info : selectedItems) {
                    selectAndScrollToItem(info.type, info.path, info.categoryId);
                }
                menu.close(); 
            });
 
            // 4. 置顶
            bool isPinned = currentIndex.data(IsLockedRole).toBool(); 
            menu.addAction(isPinned ? "取消置顶" : "置顶")->setData(isPinned ? ActionUnpin : ActionPin); 
        } else {
            // [物理源：显示“迁移”]
            if (!m_currentPath.isEmpty() && m_currentPath != "computer://") {
                std::wstring wp = path.toStdWString();
                std::wstring volSerial = MetadataManager::getVolumeSerialNumber(wp);

                std::wstring managedRootW = AutoImportManager::getManagedLibraryPath(wp);
                QString managedRoot = QString::fromStdWString(managedRootW);

                QMenu* migrateMenu = menu.addMenu(UiHelper::getIcon("add", QColor("#FF8C00"), 18), "迁移");
                UiHelper::applyMenuStyle(migrateMenu);

                if (managedRoot.isEmpty()) {
                    migrateMenu->addAction("该盘库存未创建")->setEnabled(false);
                } else {
                    QAction* actRoot = migrateMenu->addAction(managedRoot);
                    actRoot->setData(ActionAddToCategory);
                    actRoot->setProperty("targetPath", managedRoot);

                    migrateMenu->menuAction()->setData(ActionAddToCategory);
                    migrateMenu->menuAction()->setProperty("targetPath", managedRoot);
                }

                migrateMenu->addSeparator();
                QStringList recentFolders = NavigationHistoryService::getRecentVisitedFolders(volSerial);
                if (recentFolders.isEmpty()) {
                    migrateMenu->addAction("迁移至最近活跃位置...")->setEnabled(false);
                } else {
                    for (const QString& folder : recentFolders) {
                        QAction* act = migrateMenu->addAction(folder);
                        act->setData(ActionAddToCategory);
                        act->setProperty("targetPath", folder);
                    }
                }
            }
        }

        // 5. 添加至收藏夹
        menu.addAction("添加至收藏夹")->setData(ActionAddToFavorites); 
 
        menu.addSeparator(); 
 
        // ================= 【第二组】 =================
        // 0. 重命名 / 批量重命名 (移至编辑组头部)
        int selectedCount = 0;
        for (const auto& selIdx : view->selectionModel()->selectedIndexes()) {
            if (selIdx.column() == 0 && !selIdx.data(PathRole).toString().isEmpty()) {
                selectedCount++;
            }
        }

        if (selectedCount <= 1) {
            menu.addAction("重命名")->setData(ActionRename); 
        } else if (isFolder || selectedCount > 1) { 
            menu.addAction("批量重命名 (Ctrl+Shift+R)")->setData(ActionBatchRename); 
        }

        // 1. 复制、剪切、粘贴
        menu.addAction("复制")->setData(ActionCopy); 
        menu.addAction("剪切")->setData(ActionCut); 
        menu.addAction("粘贴")->setData(ActionPaste); 
        
        // 2. 复制名称 (新增项)、复制路径
        menu.addAction("复制名称")->setData(ActionCopyName); 
        menu.addAction("复制路径")->setData(ActionCopyPath); 

        menu.addSeparator(); 

        // ================= 【第三组】 =================
        // 1. 刷新、属性
        menu.addAction("刷新")->setData(ActionRefresh); 
        menu.addAction("属性")->setData(ActionProperties); 

        // 2. 重新扫描
        if (currentIndex.data(ManagedRole).toBool()) {
            menu.addAction(UiHelper::getIcon("sync", QColor("#378ADD"), 18), "重新扫描")->setData(ActionRescan);
        }

        // 3. 取消导入并清除数据
        if (currentIndex.data(TypeRole).toString() == "folder" && currentIndex.data(ManagedRole).toBool()) {
            menu.addAction(UiHelper::getIcon("close", QColor("#e81123"), 18), "取消导入并清除数据")->setData(ActionCancelImport);
        }

        // 4. 加密保护
        if (!isFolder) { 
            QMenu* cryptoMenu = menu.addMenu("加密保护"); 
            UiHelper::applyMenuStyle(cryptoMenu); 
            cryptoMenu->addAction("执行加密保护")->setData(ActionEncrypt); 
            cryptoMenu->addAction("解除加密")->setData(ActionDecrypt); 
            cryptoMenu->addAction("修改加密密码")->setData(ActionChangePwd); 
        } 

        // 5. 排序级联子菜单 (嵌入并连接)
        QMenu* sortMenu = menu.addMenu("排序");
        UiHelper::applyMenuStyle(sortMenu);

        QActionGroup* typeGroup = new QActionGroup(this);
        auto addTypeAct = [&](const QString& label, ContentPanel::SortType type) {
            QAction* act = sortMenu->addAction(label);
            act->setCheckable(true);
            act->setChecked(m_sortType == type);
            typeGroup->addAction(act);
            connect(act, &QAction::triggered, [this, type]() {
                m_sortType = type;
                AppConfig::instance().setValue("ContentPanel/RightClickSortType", static_cast<int>(type));
                m_proxyModel->invalidate();
                m_proxyModel->sort(0, m_sortOrder);
            });
        };
        addTypeAct("名称", ContentPanel::SortByName);
        addTypeAct("创建日期", ContentPanel::SortByCreateDate);
        addTypeAct("修改日期", ContentPanel::SortByModifyDate);
        addTypeAct("扩展名", ContentPanel::SortByExtension);
        addTypeAct("大小", ContentPanel::SortBySize);
        addTypeAct("尺寸", ContentPanel::SortByDimension);
        addTypeAct("评分", ContentPanel::SortByRating);
        addTypeAct("添加日期", ContentPanel::SortByAddedDate);

        sortMenu->addSeparator();

        QActionGroup* orderGroup = new QActionGroup(this);
        auto addOrderAct = [&](const QString& label, Qt::SortOrder order) {
            QAction* act = sortMenu->addAction(label);
            act->setCheckable(true);
            act->setChecked(m_sortOrder == order);
            orderGroup->addAction(act);
            connect(act, &QAction::triggered, [this, order]() {
                m_sortOrder = order;
                AppConfig::instance().setValue("ContentPanel/RightClickSortOrder", static_cast<int>(order));
                m_proxyModel->invalidate();
                m_proxyModel->sort(0, order);
            });
        };
        addOrderAct("升序", Qt::AscendingOrder);
        addOrderAct("降序", Qt::DescendingOrder);

        // 6. 删除 (二级级联)
        if (m_currentCategoryType != "trash") {
            QMenu* delMenu = menu.addMenu("删除");
            UiHelper::applyMenuStyle(delMenu);
            delMenu->addAction("移入回收站")->setData(ActionDelete);
            delMenu->addAction("永久删除")->setData(ActionSecureDelete);
        }

        // 7. 回收站模式下的最底部永久删除
        if (m_currentCategoryType == "trash") {
            menu.addSeparator();
            menu.addAction(UiHelper::getIcon("trash", QColor("#e81123"), 18), "永久删除")->setData(ActionSecureDelete);
        }
 
    } else { 
        // [空白处菜单] 
        QMenu* newMenu = menu.addMenu("新建..."); 
        UiHelper::applyMenuStyle(newMenu); 
        newMenu->addAction(UiHelper::getIcon("folder_filled", QColor("#EEEEEE")), "创建文件夹")->setData(ActionNewFolder); 
        newMenu->addAction(UiHelper::getIcon("text", QColor("#EEEEEE")), "创建 Markdown")->setData(ActionNewMd); 
        newMenu->addAction(UiHelper::getIcon("text", QColor("#EEEEEE")), "创建纯文本文件 (txt)")->setData(ActionNewTxt); 
 
        menu.addSeparator(); 
        QAction* actPaste = menu.addAction("粘贴"); 
        actPaste->setData(ActionPaste); 
        actPaste->setEnabled(!m_currentPath.isEmpty() && m_currentPath != "computer://"); 
 
        menu.addSeparator(); 
        menu.addAction("刷新")->setData(ActionRefresh);

        menu.addSeparator(); 
        QAction* actProp = menu.addAction("当前文件夹属性"); 
        actProp->setData(ActionProperties); 
        actProp->setEnabled(!m_currentPath.isEmpty() && m_currentPath != "computer://"); 

        menu.addSeparator();

        // 注入“排序”二级子菜单
        QMenu* sortMenu = menu.addMenu("排序");
        UiHelper::applyMenuStyle(sortMenu);

        QActionGroup* typeGroup = new QActionGroup(this);
        auto addTypeAct = [&](const QString& label, ContentPanel::SortType type) {
            QAction* act = sortMenu->addAction(label);
            act->setCheckable(true);
            act->setChecked(m_sortType == type);
            typeGroup->addAction(act);
            connect(act, &QAction::triggered, [this, type]() {
                m_sortType = type;
                AppConfig::instance().setValue("ContentPanel/RightClickSortType", static_cast<int>(type));
                m_proxyModel->invalidate();
                m_proxyModel->sort(0, m_sortOrder);
            });
        };
        addTypeAct("名称", ContentPanel::SortByName);
        addTypeAct("创建日期", ContentPanel::SortByCreateDate);
        addTypeAct("修改日期", ContentPanel::SortByModifyDate);
        addTypeAct("扩展名", ContentPanel::SortByExtension);
        addTypeAct("大小", ContentPanel::SortBySize);
        addTypeAct("尺寸", ContentPanel::SortByDimension);
        addTypeAct("评分", ContentPanel::SortByRating);
        addTypeAct("添加日期", ContentPanel::SortByAddedDate);

        sortMenu->addSeparator();

        QActionGroup* orderGroup = new QActionGroup(this);
        auto addOrderAct = [&](const QString& label, Qt::SortOrder order) {
            QAction* act = sortMenu->addAction(label);
            act->setCheckable(true);
            act->setChecked(m_sortOrder == order);
            orderGroup->addAction(act);
            connect(act, &QAction::triggered, [this, order]() {
                m_sortOrder = order;
                AppConfig::instance().setValue("ContentPanel/RightClickSortOrder", static_cast<int>(order));
                m_proxyModel->invalidate();
                m_proxyModel->sort(0, order);
            });
        };
        addOrderAct("升序", Qt::AscendingOrder);
        addOrderAct("降序", Qt::DescendingOrder);
    } 
 
    QAction* selectedAction = menu.exec(view->viewport()->mapToGlobal(pos)); 
>>>>>>> REPLACE
```

```diff
<<<<<<< SEARCH
        case ActionProperties: { 
            ShellHelper::showProperties(onItem ? path : m_currentPath); 
            break; 
        } 
        case ActionRefresh: {
            refreshAll();
            break;
        }
        default: break; 
    } 
} 
=======
        case ActionCopyName: {
            QModelIndexList indexes = getSelectedIndexes();
            QStringList targetNames;
            for (const auto& idx : indexes) {
                if (idx.column() == 0) {
                    QString p = idx.data(PathRole).toString();
                    if (!p.isEmpty()) {
                        targetNames << QFileInfo(p).fileName();
                    } else {
                        QString display = idx.data(Qt::DisplayRole).toString();
                        if (!display.isEmpty()) targetNames << display;
                    }
                }
            }
            if (targetNames.isEmpty() && onItem) {
                targetNames << QFileInfo(path).fileName();
            }
            if (!targetNames.isEmpty()) {
                QApplication::clipboard()->setText(targetNames.join("\n"));
            }
            break;
        }
        case ActionProperties: { 
            ShellHelper::showProperties(onItem ? path : m_currentPath); 
            break; 
        } 
        case ActionRefresh: {
            refreshAll();
            break;
        }
        default: break; 
    } 
} 
>>>>>>> REPLACE
```

## 5. 修改边界声明【范围】
- [x] 修改 `src/ui/ContentPanel.h`
- [x] 修改 `src/ui/ContentPanel.cpp`

**明确禁止越界修改的范围：**
- [ ] 分类面板 (CategoryPanel) 的右键菜单选项定义 —— 不修改
- [ ] 复制、剪切、粘贴、删除等后台业务物理执行逻辑 —— 不修改

## 6. 实现准则与预警【核心】
1. 需要确保包含头文件 `<QFileInfo>`、`<QApplication>` 和 `<QClipboard>`，以正常进行名称解析和剪贴板操作。
2. 重排序时，将空处菜单单独隔离维护，在彻底保障功能集不受损害的同时满足了极其完美的职责单一度（SRP）。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 右键菜单组装 | 右键菜单选项应当具备高度清晰的三段式结构特征且组装时序准确无误 | ✅ 符合 |

## 8. 待确认事项
（无）
