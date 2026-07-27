#include "ContentContextMenuManager.h"
#include "ContentPanel.h"
#include "MainWindow.h"
#include "UiHelper.h"
#include "ColorPicker.h"
#include "core/AppConfig.h"
#include "core/AutoImportManager.h"
#include "core/NavigationHistoryService.h"
#include "core/FileSystemService.h"
#include "core/BasicCommands.h"
#include "core/UndoManager.h"
#include "meta/CategoryRepo.h"
#include "meta/MetadataManager.h"
#include "meta/MediaExtractorPipeline.h"
#include "crypto/EncryptionManager.h"
#include "util/ShellHelper.h"
#include "util/ImportHelper.h"
#include "BatchRenameDialog.h"
#include "ToolTipOverlay.h"
#include "FramelessDialog.h"
#include "BatchProgressDialog.h"

#include <QMenu>
#include <QActionGroup>
#include <QWidgetAction>
#include <QApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QDir>
#include <QPointer>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrent>

namespace ArcMeta {

ContentContextMenuManager& ContentContextMenuManager::instance() {
    static ContentContextMenuManager inst;
    return inst;
}

ContentContextMenuManager::ContentContextMenuManager(QObject* parent) : QObject(parent) {}

void ContentContextMenuManager::showContextMenu(ContentPanel* panel, const QPoint& pos) {
    QAbstractItemView* view = panel->currentActiveView(); 
    if (!view) return; 
 
    QModelIndex currentIndex = view->indexAt(pos); 
    bool onItem = currentIndex.isValid(); 
    bool isFolder = onItem && (currentIndex.data(ContentPanel::TypeRole).toString() == "folder"); 
    QString path = currentIndex.data(ContentPanel::PathRole).toString(); 
 
    QMenu menu(panel); 
    UiHelper::applyMenuStyle(&menu); 
 
    if (onItem) { 
        if (panel->m_currentCategoryType == "trash") {
            menu.addAction(UiHelper::getIcon("sync", QColor("#2ecc71"), 18), "还原")->setData(ContentPanel::ActionRestore);
            menu.addSeparator();
        }

        QAction* actOpen = menu.addAction(isFolder ? "打开文件夹" : "打开"); 
        actOpen->setData(ContentPanel::ActionOpen); 
        if (!isFolder) { 
            menu.addAction("用系统默认程序打开")->setData(ContentPanel::ActionOpenDefault); 
        } 
        menu.addAction("在“资源管理器”中显示")->setData(ContentPanel::ActionShowInExplorer); 
 
        menu.addSeparator(); 
 
        bool isMirrorSource = !panel->m_currentCategoryType.isEmpty();
        if (!isMirrorSource && onItem) {
            bool isManaged = currentIndex.data(ContentPanel::ManagedRole).toBool();
            bool isInsideLib = MetadataManager::instance().isInsideManagedLibrary(path.toStdWString());
            isMirrorSource = isManaged || isInsideLib;
        }

        if (isMirrorSource) {
            QMenu* categorizeMenu = menu.addMenu("归类到..."); 
            UiHelper::applyMenuStyle(categorizeMenu); 
            auto categories = CategoryRepo::getRecentlyUsed(15); 
            if (categories.empty()) categories = CategoryRepo::getAll();
            if (categories.size() > 15) categories.resize(15);

            QAction* actToUncat = categorizeMenu->addAction(UiHelper::getIcon("uncategorized", QColor("#95a5a6"), 16), "回归“未分类”");
            actToUncat->setData(ContentPanel::ActionCategorize);
            actToUncat->setProperty("catId", -2); 
            categorizeMenu->addSeparator();

            if (categories.empty()) { 
                categorizeMenu->addAction("（暂无分类）")->setEnabled(false); 
            } else { 
                for (const auto& cat : categories) { 
                    QAction* act = categorizeMenu->addAction(QString::fromStdWString(cat.name)); 
                    act->setData(ContentPanel::ActionCategorize); 
                    act->setProperty("catId", cat.id); 
                } 
            }

            QString currentColorStr = currentIndex.data(ContentPanel::ColorRole).toString();

            QWidgetAction* pickerAction = new QWidgetAction(&menu);
            ColorStripPicker* pickerWidget = new ColorStripPicker(currentColorStr, &menu);
            pickerAction->setDefaultWidget(pickerWidget);
            menu.addAction(pickerAction);

            connect(pickerWidget, &ColorStripPicker::colorSelected, panel, [panel, view, &menu](const QString& hexColor) {
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
                        info.type = idx.data(ContentPanel::TypeRole).toString();
                        info.path = idx.data(ContentPanel::PathRole).toString();
                        info.categoryId = idx.data(ContentPanel::CategoryIdRole).toInt();
                        selectedItems.append(info);
                    }  
                }

                for (const auto& idx : indexes) {  
                    if (idx.column() == 0) {  
                        panel->m_proxyModel->setData(idx, hexColor, ContentPanel::ColorRole);  
                    }  
                } 

                for (const auto& info : selectedItems) {
                    panel->selectAndScrollToItem(info.type, info.path, info.categoryId);
                }
                menu.close(); 
            });
 
            bool isPinned = currentIndex.data(ContentPanel::IsLockedRole).toBool(); 
            menu.addAction(isPinned ? "取消置顶" : "置顶")->setData(isPinned ? ContentPanel::ActionUnpin : ContentPanel::ActionPin); 
        } else {
            if (!panel->m_currentPath.isEmpty() && panel->m_currentPath != "computer://") {
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
                    actRoot->setData(ContentPanel::ActionAddToCategory);
                    actRoot->setProperty("targetPath", managedRoot);

                    migrateMenu->menuAction()->setData(ContentPanel::ActionAddToCategory);
                    migrateMenu->menuAction()->setProperty("targetPath", managedRoot);
                }

                migrateMenu->addSeparator();
                QStringList recentFolders = NavigationHistoryService::getRecentVisitedFolders(volSerial);
                if (recentFolders.isEmpty()) {
                    migrateMenu->addAction("迁移至最近活跃位置...")->setEnabled(false);
                } else {
                    for (const QString& folder : recentFolders) {
                        QAction* act = migrateMenu->addAction(folder);
                        act->setData(ContentPanel::ActionAddToCategory);
                        act->setProperty("targetPath", folder);
                    }
                }
            }
        }

        menu.addSeparator(); 
 
        int selectedCount = 0;
        for (const auto& selIdx : view->selectionModel()->selectedIndexes()) {
            if (selIdx.column() == 0 && !selIdx.data(ContentPanel::PathRole).toString().isEmpty()) {
                selectedCount++;
            }
        }

        if (isFolder || selectedCount > 1) { 
            menu.addAction("批量重命名 (Ctrl+Shift+R)")->setData(ContentPanel::ActionBatchRename); 
        }

        if (!isFolder) { 
            QMenu* cryptoMenu = menu.addMenu("加密保护"); 
            UiHelper::applyMenuStyle(cryptoMenu); 
            cryptoMenu->addAction("执行加密保护")->setData(ContentPanel::ActionEncrypt); 
            cryptoMenu->addAction("解除加密")->setData(ContentPanel::ActionDecrypt); 
            cryptoMenu->addAction("修改加密密码")->setData(ContentPanel::ActionChangePwd); 
        } 
 
        menu.addSeparator(); 
 
        if (selectedCount <= 1) {
            menu.addAction("重命名")->setData(ContentPanel::ActionRename); 
        }
        menu.addAction("复制")->setData(ContentPanel::ActionCopy); 
        menu.addAction("剪切")->setData(ContentPanel::ActionCut); 
        menu.addAction("粘贴")->setData(ContentPanel::ActionPaste); 
        
        if (panel->m_currentCategoryType != "trash") {
            QMenu* delMenu = menu.addMenu("删除");
            UiHelper::applyMenuStyle(delMenu);
            delMenu->addAction("移入回收站")->setData(ContentPanel::ActionDelete);
            delMenu->addAction("永久删除")->setData(ContentPanel::ActionSecureDelete);
        }
 
        menu.addSeparator(); 
        menu.addAction("复制路径")->setData(ContentPanel::ActionCopyPath); 
        menu.addAction("添加至收藏夹")->setData(ContentPanel::ActionAddToFavorites); 
        menu.addAction("刷新")->setData(ContentPanel::ActionRefresh); 
        menu.addAction("属性")->setData(ContentPanel::ActionProperties); 

        if (currentIndex.data(ContentPanel::ManagedRole).toBool()) {
            menu.addSeparator();
            menu.addAction(UiHelper::getIcon("sync", QColor("#378ADD"), 18), "重新扫描")->setData(ContentPanel::ActionRescan);
        }

        if (currentIndex.data(ContentPanel::TypeRole).toString() == "folder" && currentIndex.data(ContentPanel::ManagedRole).toBool()) {
            menu.addAction(UiHelper::getIcon("close", QColor("#e81123"), 18), "取消导入并清除数据")->setData(ContentPanel::ActionCancelImport);
        }

        if (panel->m_currentCategoryType == "trash") {
            menu.addSeparator();
            menu.addAction(UiHelper::getIcon("trash", QColor("#e81123"), 18), "永久删除")->setData(ContentPanel::ActionSecureDelete);
        }
 
    } else { 
        QMenu* newMenu = menu.addMenu("新建..."); 
        UiHelper::applyMenuStyle(newMenu); 
        newMenu->addAction(UiHelper::getIcon("folder_filled", QColor("#EEEEEE")), "创建文件夹")->setData(ContentPanel::ActionNewFolder); 
        newMenu->addAction(UiHelper::getIcon("text", QColor("#EEEEEE")), "创建 Markdown")->setData(ContentPanel::ActionNewMd); 
        newMenu->addAction(UiHelper::getIcon("text", QColor("#EEEEEE")), "创建纯文本文件 (txt)")->setData(ContentPanel::ActionNewTxt); 
 
        menu.addSeparator(); 
        QAction* actPaste = menu.addAction("粘贴"); 
        actPaste->setData(ContentPanel::ActionPaste); 
        actPaste->setEnabled(!panel->m_currentPath.isEmpty() && panel->m_currentPath != "computer://"); 
 
        menu.addSeparator(); 
        menu.addAction("刷新")->setData(ContentPanel::ActionRefresh);

        menu.addSeparator(); 
        QAction* actProp = menu.addAction("当前文件夹属性"); 
        actProp->setData(ContentPanel::ActionProperties); 
        actProp->setEnabled(!panel->m_currentPath.isEmpty() && panel->m_currentPath != "computer://"); 
    } 

    menu.addSeparator();

    QMenu* sortMenu = menu.addMenu("排序");
    UiHelper::applyMenuStyle(sortMenu);

    QActionGroup* typeGroup = new QActionGroup(panel);
    auto addTypeAct = [&](const QString& label, ContentPanel::SortType type) {
        QAction* act = sortMenu->addAction(label);
        act->setCheckable(true);
        act->setChecked(panel->m_sortType == type);
        typeGroup->addAction(act);
        connect(act, &QAction::triggered, panel, [panel, type]() {
            panel->m_sortType = type;
            AppConfig::instance().setValue("ContentPanel/RightClickSortType", static_cast<int>(type));
            panel->m_proxyModel->invalidate();
            panel->m_proxyModel->sort(0, panel->m_sortOrder);
        });
    };

    addTypeAct("名称", ContentPanel::SortByName);
    addTypeAct("创建日期", ContentPanel::SortByCreateDate);
    addTypeAct("修改日期", ContentPanel::SortByModifyDate);
    addTypeAct("扩展名", ContentPanel::SortByExtension);
    addTypeAct("大小", ContentPanel::SortBySize);
    addTypeAct("尺寸", ContentPanel::SortByDimension);
    addTypeAct("评分", ContentPanel::SortByRating);

    sortMenu->addSeparator();

    QActionGroup* orderGroup = new QActionGroup(panel);
    auto addOrderAct = [&](const QString& label, Qt::SortOrder order) {
        QAction* act = sortMenu->addAction(label);
        act->setCheckable(true);
        act->setChecked(panel->m_sortOrder == order);
        orderGroup->addAction(act);
        connect(act, &QAction::triggered, panel, [panel, order]() {
            panel->m_sortOrder = order;
            AppConfig::instance().setValue("ContentPanel/RightClickSortOrder", static_cast<int>(order));
            panel->m_proxyModel->invalidate();
            panel->m_proxyModel->sort(0, order);
        });
    };

    addOrderAct("升序", Qt::AscendingOrder);
    addOrderAct("降序", Qt::DescendingOrder);

    menu.addSeparator();
    QMenu* layoutMenu = menu.addMenu("布局显示");
    UiHelper::applyMenuStyle(layoutMenu);
    
    MainWindow* mw = nullptr;
    QWidget* parentWin = panel->window();
    while (parentWin) {
        if ((mw = qobject_cast<MainWindow*>(parentWin))) break;
        parentWin = parentWin->parentWidget();
    }
    if (mw) {
        mw->populatePanelMenu(layoutMenu);
    }
 
    QAction* selectedAction = menu.exec(view->viewport()->mapToGlobal(pos)); 
    if (!selectedAction || !selectedAction->data().isValid()) return; 
 
    ContentPanel::ContextAction action = static_cast<ContentPanel::ContextAction>(selectedAction->data().toInt()); 
 
    switch (action) { 
        case ContentPanel::ActionOpen: 
        case ContentPanel::ActionOpenDefault: 
            panel->onDoubleClicked(currentIndex); 
            break; 
        case ContentPanel::ActionShowInExplorer: { 
            ShellHelper::openInExplorer(onItem ? path : panel->m_currentPath); 
            break; 
        } 
        case ContentPanel::ActionNewFolder: panel->createNewItem("folder"); break; 
        case ContentPanel::ActionNewMd: panel->createNewItem("md"); break; 
        case ContentPanel::ActionNewTxt: panel->createNewItem("txt"); break; 
        case ContentPanel::ActionCategorize: { 
            int catId = selectedAction->property("catId").toInt(); 
            auto indexes = view->selectionModel()->selectedIndexes(); 
             
            for (const auto& idx : indexes) { 
                if (idx.column() == 0) { 
                    QString itemPath = idx.data(ContentPanel::PathRole).toString(); 
                    std::wstring wPath = itemPath.toStdWString();
                    std::string fid = MetadataManager::instance().getFileIdSync(wPath); 
 
                    if (catId == -2) { 
                        MetadataManager::instance().removeCategoryAssoc(fid); 
                    } else if (catId >= 0) { 
                        MetadataManager::instance().setCategoryAssoc(fid, catId); 
                    } 
                } 
            } 
            panel->refreshAll(); 
            break; 
        } 
        case ContentPanel::ActionPin: 
        case ContentPanel::ActionUnpin: { 
            auto indexes = view->selectionModel()->selectedIndexes(); 
            bool pin = (action == ContentPanel::ActionPin); 
            for (const QModelIndex& idx : indexes) { 
                if (idx.column() == 0) { 
                    panel->m_proxyModel->setData(idx, pin, ContentPanel::IsLockedRole); 
                } 
            } 
            panel->m_proxyModel->invalidate();
            panel->m_proxyModel->sort(0, panel->m_proxyModel->sortOrder());
            break; 
        } 
        case ContentPanel::ActionEncrypt: { 
            FramelessInputDialog dlg("加密保护", "设置加密密码:", "", panel);
            dlg.setEchoMode(QLineEdit::Password);
            if (dlg.exec() == QDialog::Accepted) { 
                QString pwd = dlg.text();
                if (pwd.isEmpty()) break;
                auto indexes = view->selectionModel()->selectedIndexes(); 
                QStringList targets; 
                for (const auto& idx : indexes) if (idx.column() == 0) targets << idx.data(ContentPanel::PathRole).toString(); 
                 
                ToolTipOverlay::instance()->showText(QCursor::pos(), "加密任务已在后台启动...", 2000); 
                 
                std::string stdPwd = pwd.toStdString(); 
                QPointer<ContentPanel> self(panel); 
                QString currentDir = panel->m_currentPath; 
 
                (void)QThreadPool::globalInstance()->start([self, targets, stdPwd, currentDir]() { 
                    for (const QString& src : targets) { 
                        QString dest = src + ".amenc"; 
                        if (EncryptionManager::instance().encryptFile(src.toStdWString(), dest.toStdWString(), stdPwd)) { 
                            QFile::remove(src); 
                            MetadataManager::instance().setEncrypted(dest.toStdWString(), true); 
                        } 
                    } 
                    QMetaObject::invokeMethod(QCoreApplication::instance(), [self, currentDir]() { 
                        if (self && self->m_currentPath == currentDir) self->loadDirectory(currentDir, self->m_isRecursive); 
                        ToolTipOverlay::instance()->showText(QCursor::pos(), "加密任务处理完成", 1500, QColor("#2ecc71")); 
                    }); 
                }); 
            } 
            break; 
        } 
        case ContentPanel::ActionDecrypt: { 
            FramelessInputDialog dlg("解除加密", "输入加密密码:", "", panel);
            dlg.setEchoMode(QLineEdit::Password);
            if (dlg.exec() == QDialog::Accepted) { 
                QString pwd = dlg.text();
                if (!pwd.isEmpty()) { 
                    ToolTipOverlay::instance()->showText(QCursor::pos(), "解除加密逻辑已触发", 1500); 
                }
            } 
            break; 
        } 
        case ContentPanel::ActionBatchRename: panel->performBatchRename(); break; 
        case ContentPanel::ActionAddToCategory: {
            QStringList paths;
            auto indexes = view->selectionModel()->selectedIndexes();
            for (const auto& idx : indexes) {
                if (idx.column() == 0) {
                    QString p = idx.data(ContentPanel::PathRole).toString();
                    if (!p.isEmpty()) paths << p;
                }
            }
            
            if (paths.isEmpty() && !path.isEmpty()) paths << path;

            QString target = selectedAction->property("targetPath").toString();
            if (target.isEmpty()) {
                std::wstring wp = path.toStdWString();
                std::wstring volSerial = MetadataManager::getVolumeSerialNumber(wp);
                QString key = QString("ManagedFolder/Volume_%1").arg(QString::fromStdWString(volSerial));
                QString relPath = AppConfig::instance().getValue(key, "").toString();
                target = QDir::toNativeSeparators(path.left(3) + relPath);
            }

            if (!paths.isEmpty() && !target.isEmpty()) {
                QPointer<ContentPanel> weakThis(panel);
                ImportHelper::importPaths(paths, target, panel, [weakThis]() {
                    if (weakThis) {
                        weakThis->refreshAll(); 
                    }
                });
            }
            break;
        }
        case ContentPanel::ActionRename: view->edit(currentIndex); break; 
        case ContentPanel::ActionCopy: panel->performCopy(false); break; 
        case ContentPanel::ActionCut: panel->performCopy(true); break; 
        case ContentPanel::ActionPaste: panel->performPaste(); break; 
        case ContentPanel::ActionRescan: {
            auto indexes = view->selectionModel()->selectedIndexes();
            QStringList targetPaths;
            for (const auto& idx : indexes) {
                if (idx.column() == 0) {
                    QString p = idx.data(ContentPanel::PathRole).toString();
                    if (!p.isEmpty()) targetPaths << p;
                }
            }
            if (targetPaths.isEmpty() && !path.isEmpty()) targetPaths << path;
 
            if (!targetPaths.isEmpty()) {
                MetadataManager::instance().registerItemsAsync(targetPaths, true);
                ToolTipOverlay::instance()->showText(QCursor::pos(), "已启动物理状态同步", 1500, QColor("#378ADD"));
            }
            break;
        }
        case ContentPanel::ActionCancelImport: {
            auto indexes = view->selectionModel()->selectedIndexes();
            QStringList targetPaths;
            for (const auto& idx : indexes) {
                if (idx.column() == 0) {
                    QString p = idx.data(ContentPanel::PathRole).toString();
                    if (!p.isEmpty()) targetPaths << p;
                }
            }
            if (targetPaths.isEmpty() && !path.isEmpty()) targetPaths << path;

            if (!targetPaths.isEmpty()) {
                std::vector<std::wstring> stdPaths;
                for (const QString& tp : targetPaths) {
                    stdPaths.push_back(tp.toStdWString());
                    panel->clearFolderCache(tp);
                }
                MediaExtractorPipeline::instance().cancelBatch(stdPaths);
                MetadataManager::instance().removeMetadataBatchSync(targetPaths);
                ToolTipOverlay::instance()->showText(QCursor::pos(), "已取消自动导入并彻底擦除相关元数据", 2000, QColor("#e81123"));
                panel->refreshAll();
            }
            break;
        }
        case ContentPanel::ActionRestore: {
            auto indexes = view->selectionModel()->selectedIndexes();
            for (const auto& idx : indexes) {
                if (idx.column() == 0) {
                    QString itemPath = idx.data(ContentPanel::PathRole).toString();
                    auto meta = MetadataManager::instance().getMeta(itemPath.toStdWString());
                    if (meta.isTrash && !meta.originalPath.empty()) {
                        QString dest = QString::fromStdWString(meta.originalPath);
                        QDir().mkpath(QFileInfo(dest).absolutePath());
                        if (QFile::rename(itemPath, dest)) {
                            MetadataManager::instance().markAsTrash(dest.toStdWString(), false);
                        }
                    }
                }
            }
            panel->loadDirectory(panel->m_currentPath);
            MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::FullRebuild);
            break;
        }
        case ContentPanel::ActionDelete: 
        case ContentPanel::ActionSecureDelete: {
            auto indexes = view->selectionModel()->selectedIndexes();
            QStringList targetPaths;
            for (const auto& idx : indexes) {
                if (idx.column() == 0) targetPaths << idx.data(ContentPanel::PathRole).toString();
            }
            if (targetPaths.isEmpty() && !path.isEmpty()) targetPaths << path;

            if (targetPaths.isEmpty()) break;

            if (action == ContentPanel::ActionDelete) {
                if (ShellHelper::moveToTrash(targetPaths)) panel->loadDirectory(panel->m_currentPath);
            } else {
                QString msg = "确定要永久删除选中的项目吗？数据将被物理覆写并彻底抹除，此操作不可恢复。";
                if (!FramelessMessageBox::question(panel, "确认删除", msg)) break;

                BatchProgressDialog* progress = new BatchProgressDialog("正在执行永久删除（深层抹除）...", panel);
                progress->show();

                QPointer<ContentPanel> weakThis(panel);
                QPointer<BatchProgressDialog> weakProgress(progress);

                DiskIoService::asyncDeletePaths(
                    targetPaths,
                    action == ContentPanel::ActionSecureDelete,
                    weakThis,
                    [weakProgress](int percent) {
                        if (weakProgress) {
                            weakProgress->setValue(percent);
                        }
                    },
                    [weakThis, weakProgress]() {
                        if (weakProgress) {
                            weakProgress->accept();
                            weakProgress->deleteLater();
                        }
                        if (weakThis) {
                            weakThis->loadDirectory(weakThis->m_currentPath);
                            ToolTipOverlay::instance()->showText(QCursor::pos(), "深层抹除已完成，关联记录已物理清空", 1500, QColor("#2ecc71"));
                        }
                    }
                );
            }
            break;
        }
        case ContentPanel::ActionAddToFavorites: {
            QStringList selectedPaths;
            QModelIndexList indexes = panel->getSelectedIndexes();
            for (const auto& idx : indexes) {
                if (idx.column() == 0) {
                    QString p = idx.data(ContentPanel::PathRole).toString();
                    if (!p.isEmpty()) {
                        selectedPaths << p;
                    }
                }
            }
            if (!selectedPaths.isEmpty()) {
                emit panel->requestAddFavorite(selectedPaths);
                ToolTipOverlay::instance()->showText(QCursor::pos(), "已成功添加至收藏夹", 1500, QColor("#2ecc71"));
            }
            break;
        }
        case ContentPanel::ActionCopyPath: {
            QModelIndexList indexes = panel->getSelectedIndexes();
            QStringList targetPaths;
            for (const auto& idx : indexes) {
                if (idx.column() == 0) {
                    QString p = idx.data(ContentPanel::PathRole).toString();
                    if (!p.isEmpty()) targetPaths << QDir::toNativeSeparators(p);
                }
            }
            if (targetPaths.isEmpty() && !path.isEmpty()) {
                targetPaths << QDir::toNativeSeparators(path);
            }
            if (!targetPaths.isEmpty()) {
                QApplication::clipboard()->setText(targetPaths.join("\n"));
            }
            break;
        }
        case ContentPanel::ActionProperties: { 
            ShellHelper::showProperties(onItem ? path : panel->m_currentPath); 
            break; 
        } 
        case ContentPanel::ActionRefresh: {
            panel->refreshAll();
            break;
        }
        default: break; 
    } 
}

} // namespace ArcMeta
