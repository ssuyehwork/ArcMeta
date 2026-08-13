#include "CategoryPanel.h"
#include "MainWindow.h"
#include "PresetTagsDialog.h"
#include "CategoryModel.h"
#include "ContentPanel.h"
#include "../core/DiskTrashService.h"
#include "ColorPicker.h"
#include "CategoryFilterProxyModel.h"
#include "CategoryLockDialog.h"
#include "CategorySetPasswordDialog.h"
#include "CategoryDelegate.h"
#include <QCryptographicHash>
#include "DropTreeView.h"
#include "UiHelper.h"
#include "StyleLibrary.h"
using namespace ArcMeta::Style;
#include "ToolTipOverlay.h"
#include "FramelessDialog.h"
#include "TagManagerDialog.h"
#include "BatchProgressDialog.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QRegularExpression>
#include "../meta/CategoryRepo.h"
#include "../meta/DatabaseManager.h"
#include "../util/ShellHelper.h"


#include "../meta/MetadataManager.h"
#include "../core/CoreController.h"
#include "../core/OperationSnapshotEngine.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QHeaderView>
#include <QScrollBar>
#include <QMenu>
#include <QAction>
#include <QWidgetAction>
#include <QGridLayout>
#include <QApplication>
#include <QRandomGenerator>
#include <QSet>
#include <QDirIterator>
#include "../core/AppConfig.h"


#include "Logger.h"
#include <QtConcurrent>

namespace ArcMeta {

/**
 * @brief 获取默认分类颜色：深灰色 (#555555)
 * 2026-06-xx 按照用户要求：废除随机色，统一默认使用深灰色（对应用户原话：“自定义文件夹（分类）的颜色只能为#555555”）
 */
static std::wstring getDefaultCategoryColor() {
    return L"#555555";
}

CategoryPanel::~CategoryPanel() {
    Logger::log("[CategoryPanel] ~CategoryPanel called, m_isInternalUpdating set to true, disconnecting signals");
    // 1. 在面板被析构前，将控制标志设为内部更新态，彻底屏蔽 QTreeView 卸载时的折叠信号回流
    m_isInternalUpdating = true;
     
    // 2. 物理断开这些高危信号，确保高枕无忧
    if (m_categoryTree) {
        disconnect(m_categoryTree, &QTreeView::expanded, this, &CategoryPanel::saveExpandedStateToSettings);
        disconnect(m_categoryTree, &QTreeView::collapsed, this, &CategoryPanel::saveExpandedStateToSettings);
    }
    if (m_categoryTreeUser) {
        disconnect(m_categoryTreeUser, &QTreeView::expanded, this, &CategoryPanel::saveExpandedStateToSettings);
        disconnect(m_categoryTreeUser, &QTreeView::collapsed, this, &CategoryPanel::saveExpandedStateToSettings);
    }
}

CategoryPanel::CategoryPanel(QWidget* parent)
    : QFrame(parent) {
    // 2026-07-xx 按照 Plan-63：启用右键菜单策略（容器级）
    setContextMenuPolicy(Qt::CustomContextMenu);

    setObjectName("SidebarContainer");
    setAttribute(Qt::WA_StyledBackground, true);
    setMinimumWidth(230);
    setStyleSheet(QString("color: %1;").arg(qssColor(TextMain)));
    
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    // 2026-06-xx 物理优化：移除此处阻塞主线程的同步初始化。
    // 元数据加载已由 CoreController 在异步线程统一接管。

    // 2026-06-xx 物理削峰：初始化防抖定时器
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    connect(m_refreshTimer, &QTimer::timeout, this, [this]() {
        if (!m_categoryModel) return;
        
        bool needsFullRebuild = m_refreshTimer->property("fullRebuild").toBool();

        if (m_isFirstLoad || needsFullRebuild) {
            m_categoryModel->refresh();
            m_isFirstLoad = false;
            m_refreshTimer->setProperty("fullRebuild", false); // 消费完重置
        }

        // 2026-07-xx 性能优化：执行重建后立即继续统计计算，不再触发 requestRefresh 导致二次等待
        // 2026-06-xx 物理分流：将耗时的统计计算（fullRecount）移出 UI 线程
        // 采用 QPointer 确保线程安全性
        QPointer<CategoryPanel> weakThis(this);
        (void)QtConcurrent::run([weakThis]() {
            CategoryRepo::fullRecount();
            auto sysCounts = CategoryRepo::getSystemCounts();
            auto catCountsVec = CategoryRepo::getCounts();
            
            QMap<int, int> catCounts;
            for (const auto& entry : catCountsVec) catCounts[entry.first] = entry.second;
            
            // 计算完成后，通过消息队列回传主线程执行局部 UI 更新
            QMetaObject::invokeMethod(weakThis.data(), [weakThis, sysCounts, catCounts]() {
                if (weakThis && weakThis->m_categoryModel) {
                    // 物理修复：若统计数据全为0，且系统元数据尚未加载完成，则拒绝执行 UI 更新以防止计数清零
                    bool isSysUnready = !MetadataManager::instance().isLoaded();
                    bool allCountsZero = (sysCounts.value("all", 0) == 0 && sysCounts.value("trash", 0) == 0);
                    if (isSysUnready && allCountsZero) {
                        return;
                    }
                    // 第三阶段：执行局部数据更新，杜绝 beginResetModel 引发全量布局计算
                    weakThis->m_categoryModel->updateStatistics(sysCounts, catCounts);
                }
            });
        });
    });

    // 2026-xx-xx 按照 Plan-106：初始化搜索防抖计时器
    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(300);
    connect(m_searchTimer, &QTimer::timeout, this, [this]() {
        if (m_searchEdit) onSearchTextChanged(m_searchEdit->text());
    });

    initUi();
    setupContextMenu();

    // 2026-06-xx 物理修复：连接 CoreController 的初始化完成信号
    // 理由：系统启动时的 initFromScchMode 是异步进行的，完成后必须强制刷新侧边栏
    // 以解决数据库加载延迟导致的系统项（如“全部数据”、“未分类”）显示为 0 的问题
    connect(&CoreController::instance(), &CoreController::initializationFinished, this, [this]() {
        m_isFirstLoad = true; // 强制执行 refresh() 重建树结构并拉取最新计数
        requestRefresh();
    });

    // 2026-06-xx 物理修复：监听元数据变更信号，确保删除项或标记状态后计数实时更新，且支持全量重建（1:1 同步刷新保障）
    connect(&MetadataManager::instance(), &MetadataManager::metaChanged, this, [this](const QString& path) {
        if (path == "__RELOAD_ALL__" || path == "__RELOAD_CATEGORY_ONLY__") {
            requestRefresh(true);
        } else {
            requestRefresh();
        }
    });
}

void CategoryPanel::requestRefresh(bool fullRebuild) {
    // 2026-07-xx 性能优化：缩短防抖时间至 200ms 以提升 UI 响应灵敏度
    if (fullRebuild) {
        m_refreshTimer->setProperty("fullRebuild", true);
    }
    m_refreshTimer->start(200);
}

void CategoryPanel::selectCategory(int id) {
    if (!m_categoryModel) return;
    
    // 递归查找匹配 ID 的索引
    std::function<QModelIndex(const QModelIndex&)> findId;
    findId = [&](const QModelIndex& parent) -> QModelIndex {
        for (int i = 0; i < m_categoryModel->rowCount(parent); ++i) {
            QModelIndex idx = m_categoryModel->index(i, 0, parent);
            if (idx.data(IdRole).toInt() == id) return idx;
            QModelIndex child = findId(idx);
            if (child.isValid()) return child;
        }
        return QModelIndex();
    };

    QModelIndex target = findId(QModelIndex());
    if (target.isValid()) {
        QModelIndex proxyIdx = m_proxyModel->mapFromSource(target);
        if (proxyIdx.isValid() && m_categoryTree) {
            DataFlowGuard guard(m_isInternalUpdating);
            m_categoryTree->setCurrentIndex(proxyIdx);
            m_categoryTree->scrollTo(proxyIdx);
            if (m_categoryTree->selectionModel()) {
                m_categoryTree->selectionModel()->select(proxyIdx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            }
            if (m_categoryTreeUser && m_categoryTreeUser->selectionModel()) {
                m_categoryTreeUser->selectionModel()->clearSelection();
            }
        } else if (m_proxyModelUser && m_categoryTreeUser) {
            QModelIndex proxyIdxUser = m_proxyModelUser->mapFromSource(target);
            if (proxyIdxUser.isValid()) {
                DataFlowGuard guard(m_isInternalUpdating);
                m_categoryTreeUser->setCurrentIndex(proxyIdxUser);
                m_categoryTreeUser->scrollTo(proxyIdxUser);
                if (m_categoryTreeUser->selectionModel()) {
                    m_categoryTreeUser->selectionModel()->select(proxyIdxUser, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
                }
                if (m_categoryTree && m_categoryTree->selectionModel()) {
                    m_categoryTree->selectionModel()->clearSelection();
                }
            }
        }
    }
}

void CategoryPanel::selectCategoryByType(const QString& type) {
    if (!m_categoryModel) return;

    // 递归查找匹配类型的索引
    std::function<QModelIndex(const QModelIndex&)> findType;
    findType = [&](const QModelIndex& parent) -> QModelIndex {
        for (int i = 0; i < m_categoryModel->rowCount(parent); ++i) {
            QModelIndex idx = m_categoryModel->index(i, 0, parent);
            if (idx.data(TypeRole).toString() == type) return idx;
            QModelIndex child = findType(idx);
            if (child.isValid()) return child;
        }
        return QModelIndex();
    };

    QModelIndex target = findType(QModelIndex());
    if (target.isValid()) {
        QModelIndex proxyIdx = m_proxyModel->mapFromSource(target);
        if (proxyIdx.isValid() && m_categoryTree) {
            DataFlowGuard guard(m_isInternalUpdating);
            m_categoryTree->setCurrentIndex(proxyIdx);
            m_categoryTree->scrollTo(proxyIdx);
            if (m_categoryTree->selectionModel()) {
                m_categoryTree->selectionModel()->select(proxyIdx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            }
            if (m_categoryTreeUser && m_categoryTreeUser->selectionModel()) {
                m_categoryTreeUser->selectionModel()->clearSelection();
            }
        }
    }
}

void CategoryPanel::deferredInit() {

    // 2026-04-12 关键修复：延迟执行数据库数据加载
    if (m_categoryModel) {
        m_categoryModel->deferredRefresh();
    }
}

void CategoryPanel::setupContextMenu() {
    auto setupMenuForTree = [&](DropTreeView* tree, CategoryFilterProxyModel* proxy) {
        tree->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(tree, &QWidget::customContextMenuRequested, this, [this, tree, proxy](const QPoint& pos) {
            QModelIndex proxyIndex = tree->indexAt(pos);
            QModelIndex index = proxy->mapToSource(proxyIndex);

            if (proxyIndex.isValid()) {
                tree->setCurrentIndex(proxyIndex);
            }

            QMenu menu(this);
            UiHelper::applyMenuStyle(&menu);

            QString itemName = index.data(NameRole).toString();
            QString itemType = index.data(TypeRole).toString();

            if (itemType == "trash") {
                menu.addAction(UiHelper::getIcon("trash", ErrorRed, 18), "清空回收站", this, &CategoryPanel::onEmptyTrash);
                menu.addAction(UiHelper::getIcon("sync", PrimaryBlue, 18), "还原全部项目", this, &CategoryPanel::onRestoreAllFromTrash);
            } else if (!index.isValid()) {
                menu.addAction(UiHelper::getIcon("folder_filled", QColor("#aaaaaa"), 18), "新建文件夹", this, &CategoryPanel::onCreateCategory);
                
                auto* sortMenu = menu.addMenu(UiHelper::getIcon("list_ul", QColor("#aaaaaa"), 18), "排列");
                sortMenu->setStyleSheet(menu.styleSheet());
                sortMenu->addAction("标题(全部) (A→Z)", this, &CategoryPanel::onSortAllByNameAsc);
                sortMenu->addAction("标题(全部) (Z→A)", this, &CategoryPanel::onSortAllByNameDesc);
            } else {
                if (itemType == "category" || itemType == "file" || itemType == "folder") {
                    QString colorStr = index.data(ColorRole).toString();

                    QWidgetAction* colorPickerAction = new QWidgetAction(&menu);
                    ColorStripPicker* colorPickerWidget = new ColorStripPicker(colorStr, &menu);
                    colorPickerAction->setDefaultWidget(colorPickerWidget);
                    menu.addAction(colorPickerAction);

                    int id = index.data(IdRole).toInt();
                    connect(colorPickerWidget, &ColorStripPicker::colorSelected, this, [this, id, &menu](const QString& hexColor) {
                        auto all = CategoryRepo::getAll();
                        for (auto& cat : all) {
                            if (cat.id == id) {
                                cat.color = hexColor.toUpper().toStdWString();
                                CategoryRepo::update(cat);
                                if (!cat.physicalPath.empty()) {
                                    MetadataManager::instance().setColor(cat.physicalPath, cat.color, true);
                                }
                                break;
                            }
                        }

                        QSet<int> expandedIds;
                        QStringList expandedNames;
                        saveBothExpandedStates(expandedIds, expandedNames);

                        m_categoryModel->refresh();

                        restoreBothExpandedStates(expandedIds, expandedNames);
                        menu.close();
                    });

                    menu.addAction(UiHelper::getIcon("tag_filled", QColor("#9b59b6"), 18), "设置预设标签", this, &CategoryPanel::onSetPresetTags);

                    QMenu* iconMenu = menu.addMenu(UiHelper::getIcon("folder_filled", WarningOrange, 18), "文件夹图标");
                    UiHelper::applyMenuStyle(iconMenu);

                    QColor catColor = colorStr.isEmpty() ? QColor("#555555") : QColor(colorStr);

                    QWidgetAction* pickerAction = new QWidgetAction(iconMenu);
                    QWidget* pickerWidget = new QWidget(iconMenu);
                    QGridLayout* pickerLayout = new QGridLayout(pickerWidget);
                    pickerLayout->setContentsMargins(6, 6, 6, 6);
                    pickerLayout->setSpacing(6);

                    static const QList<QPair<QString, QString>> builtInIcons = {
                        {"默认文件夹", "folder_filled"},
                        {"层级分类", "category"},
                        {"照片媒体", "image_filled"},
                        {"时钟历史", "clock_filled"},
                        {"星标收藏", "star_filled"},
                        {"爱心常用", "heart_filled"},
                        {"加密安全", "lock_filled"},
                        {"图书文档", "book"},
                        {"配置管理", "settings_filled"},
                        {"网络球体", "globe_filled"}
                    };

                    int row = 0;
                    int col = 0;
                    for (const auto& pair : builtInIcons) {
                        QString label = pair.first;
                        QString iconKey = pair.second;

                        QPushButton* btn = new QPushButton(pickerWidget);
                        btn->setFixedSize(28, 28);
                        btn->setCursor(Qt::PointingHandCursor);
                        btn->setStyleSheet(
                            "QPushButton { "
                            "  background-color: transparent; "
                            "  border: 1px solid transparent; "
                            "  border-radius: 4px; "
                            "}"
                            "QPushButton:hover { "
                            "  background-color: #3E3E42; "
                            "  border: 1px solid #555555; "
                            "}"
                            "QPushButton:pressed { "
                            "  background-color: #4E4E52; "
                            "}"
                        );
                        btn->setIcon(UiHelper::getIcon(iconKey, catColor, 18));
                        btn->setIconSize(QSize(18, 18));
                        btn->setToolTip(label);

                        pickerLayout->addWidget(btn, row, col);

                        connect(btn, &QPushButton::clicked, this, [this, id, iconKey, iconMenu]() {
                            auto cats = CategoryRepo::getAll();
                            for (auto& cat : cats) {
                                if (cat.id == id) {
                                    cat.icon = iconKey.toStdWString();
                                    CategoryRepo::update(cat);
                                    break;
                                }
                            }
                            m_categoryModel->refresh();
                            iconMenu->close();
                        });

                        col++;
                        if (col >= 5) {
                            col = 0;
                            row++;
                        }
                    }

                    pickerWidget->setLayout(pickerLayout);
                    pickerAction->setDefaultWidget(pickerWidget);
                    iconMenu->addAction(pickerAction);

                    menu.addSeparator();

                    menu.addAction(UiHelper::getIcon("folder_filled", TextMuted, 18), "新建文件夹", this, &CategoryPanel::onCreateCategory);

                    int catId = index.data(IdRole).toInt();
                    Category cat = CategoryRepo::getById(catId);
                    bool isManagedLibraryRoot = (cat.id > 0 && cat.parentId == 0 && !cat.physicalPath.empty());

                    if (!isManagedLibraryRoot) {
                        menu.addAction(UiHelper::getIcon("folder_filled", TextMuted, 18), "新建子文件夹", this, &CategoryPanel::onCreateSubCategory);
                    }

                    menu.addSeparator();

                    bool isPinned = index.data(PinnedRole).toBool();
                    menu.addAction(UiHelper::getIcon("pin_vertical", isPinned ? Style::ActiveOrange : TextMuted, 18),
                                   isPinned ? "从“快速访问”中移除" : "添加至“快速访问”", this, &CategoryPanel::onTogglePin);

                    if (!isManagedLibraryRoot) {
                        menu.addAction(UiHelper::getIcon("edit", TextMuted, 18), "重命名", this, &CategoryPanel::onRenameCategory);
                        menu.addAction(UiHelper::getIcon("trash", ErrorRed, 18), "删除", this, &CategoryPanel::onDeleteCategory);
                    }

                    menu.addSeparator();

                    auto* sortMenu = menu.addMenu(UiHelper::getIcon("list_ul", QColor("#aaaaaa"), 18), "排列");
                    sortMenu->setStyleSheet(menu.styleSheet());
                    sortMenu->addAction("标题(当前层级) (A→Z)", this, &CategoryPanel::onSortByNameAsc);
                    sortMenu->addAction("标题(当前层级) (Z→A)", this, &CategoryPanel::onSortByNameDesc);
                    sortMenu->addAction("标题(全部) (A→Z)", this, &CategoryPanel::onSortAllByNameAsc);
                    sortMenu->addAction("标题(全部) (Z→A)", this, &CategoryPanel::onSortAllByNameDesc);

                    auto* pwdMenu = menu.addMenu(UiHelper::getIcon("lock_secure", QColor("#EEEEEE"), 16), "密码保护");
                    UiHelper::applyMenuStyle(pwdMenu);

                    bool isEncrypted = index.data(EncryptedRole).toBool();
                    bool isUnlocked = CategoryLockManager::instance().isUnlocked(catId);

                    if (!isEncrypted) {
                        QAction* setAct = pwdMenu->addAction("设置密码");
                        connect(setAct, &QAction::triggered, this, &CategoryPanel::onSetPassword);
                    } else {
                        QAction* changeAct = pwdMenu->addAction("修改密码");
                        connect(changeAct, &QAction::triggered, this, [this, index, catId]() {
                            QString storedData = index.data(EncryptHintRole).toString();
                            QString realHint = storedData.contains(":::") ? storedData.section(":::", 1) : storedData;
                            CategoryLockDialog dlg(realHint, this);
                            if (dlg.exec() == QDialog::Accepted) {
                                CategorySetPasswordDialog setDlg(this);
                                if (setDlg.exec() == QDialog::Accepted) {
                                    QString newPwd = setDlg.password();
                                    QString newHint = setDlg.hint();

                                    if (newPwd.isEmpty()) {
                                        ToolTipOverlay::instance()->showText(QCursor::pos(), "密码不能为空！", 1500, QColor("#e81123"));
                                        return;
                                    }

                                    QString pwdHash = QCryptographicHash::hash(newPwd.toUtf8(), QCryptographicHash::Sha256).toHex();
                                    QString combinedData = pwdHash + ":::" + newHint;

                                    auto all = CategoryRepo::getAll();
                                    for (auto& cat : all) {
                                        if (cat.id == catId) {
                                            cat.encrypted = true;
                                            cat.encryptHint = combinedData.toStdWString();
                                            CategoryRepo::update(cat);
                                            break;
                                        }
                                    }
                                    syncUnlockedIds();
                                    ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#00A650;'>[OK] 密码修改成功</b>", 1000, QColor("#00A650"));
                                }
                            }
                        });

                        QAction* clearAct = pwdMenu->addAction("清除密码");
                        connect(clearAct, &QAction::triggered, this, &CategoryPanel::onClearPassword);

                        pwdMenu->addSeparator();

                        QAction* lockNowAct = pwdMenu->addAction("立即锁定");
                        lockNowAct->setEnabled(isUnlocked);
                        connect(lockNowAct, &QAction::triggered, this, [this, catId]() {
                            CategoryLockManager::instance().lockCategory(catId);
                            syncUnlockedIds();

                            MainWindow* mw = nullptr;
                            QWidget* parentWin = window();
                            while (parentWin) {
                                if ((mw = qobject_cast<MainWindow*>(parentWin))) break;
                                parentWin = parentWin->parentWidget();
                            }
                            if (mw) {
                                ContentPanel* cp = mw->findChild<ContentPanel*>();
                                if (cp) {
                                    cp->loadCategory(catId);
                                }
                            }
                            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#00A650;'>[OK] 分类已重新锁定</b>", 1000, QColor("#00A650"));
                        });
                    }
                }
            }

            if (!menu.isEmpty()) {
                menu.exec(tree->viewport()->mapToGlobal(pos));
            }
        });
    };

    setupMenuForTree(m_categoryTree, m_proxyModel);
    if (m_categoryTreeUser) {
        setupMenuForTree(m_categoryTreeUser, m_proxyModelUser);
    }
}

void CategoryPanel::saveBothExpandedStates(QSet<int>& expandedIds, QStringList& expandedNames) {
    saveExpandedState(m_categoryTree, QModelIndex(), expandedIds, expandedNames);
    saveExpandedState(m_categoryTreeUser, QModelIndex(), expandedIds, expandedNames);
}

void CategoryPanel::restoreBothExpandedStates(const QSet<int>& expandedIds, const QStringList& expandedNames) {
    restoreExpandedState(m_categoryTree, QModelIndex(), expandedIds, expandedNames);
    restoreExpandedState(m_categoryTreeUser, QModelIndex(), expandedIds, expandedNames);
}

/**
 * @brief 递归保存 QTreeView 的展开状态
 */
void CategoryPanel::saveExpandedState(QTreeView* tree, const QModelIndex& parent, QSet<int>& expandedIds, QStringList& expandedNames) {
    if (!tree || !tree->model()) return;
    int rowCount = tree->model()->rowCount(parent);
    for (int i = 0; i < rowCount; ++i) {
        QModelIndex idx = tree->model()->index(i, 0, parent);
        if (tree->isExpanded(idx)) {
            int id = idx.data(IdRole).toInt();
            QString name = idx.data(NameRole).toString();
            if (id != 0) {
                expandedIds.insert(id);
            } else {
                expandedNames << name;
            }
            saveExpandedState(tree, idx, expandedIds, expandedNames);
        }
    }
}

void CategoryPanel::onSearchTextChanged(const QString& text) {
    if (m_proxyModel) {
        m_proxyModel->setFilterText(text);
        if (!text.isEmpty()) {
            m_categoryTree->expandAll();
        } else {
            // 搜索清除时，恢复常规展开状态
            loadExpandedStateFromSettings();
        }
    }
    if (m_proxyModelUser) {
        m_proxyModelUser->setFilterText(text);
        if (!text.isEmpty()) {
            m_categoryTreeUser->expandAll();
        } else {
            // 搜索清除时，恢复常规展开状态
            loadExpandedStateFromSettings();
        }
    }
}

/**
 * @brief 递归恢复 QTreeView 的展开状态
 * 2026-03-xx 物理拦截：加密且未解锁的分类在恢复时强制跳过展开
 */
void CategoryPanel::restoreExpandedState(QTreeView* tree, const QModelIndex& parent, const QSet<int>& expandedIds, const QStringList& expandedNames) {
    if (!tree || !tree->model()) return;
    
    bool hasHistory = tree->property("hasHistoryRecord").toBool();
    int rowCount = tree->model()->rowCount(parent);
    
    for (int i = 0; i < rowCount; ++i) {
        QModelIndex idx = tree->model()->index(i, 0, parent);
        int id = idx.data(IdRole).toInt();
        QString name = idx.data(NameRole).toString();
        bool isEncrypted = idx.data(EncryptedRole).toBool();
        
        bool shouldExpand = false;
        
        if (expandedNames.contains(name) || (id != 0 && expandedIds.contains(id))) {
            shouldExpand = true;
        }
        else if (name == "快速访问") {
            shouldExpand = true;
        }
        else if (!hasHistory) {
            QModelIndex pIdx = idx.parent();
            if (!pIdx.isValid() && idx.data(TypeRole).toString() == "category") {
                shouldExpand = true;
            }
        }

        if (shouldExpand && isEncrypted && id > 0 && !m_unlockedIds.contains(id)) {
            shouldExpand = false;
        }

        if (shouldExpand) {
            tree->setExpanded(idx, true);
            restoreExpandedState(tree, idx, expandedIds, expandedNames);
        }
    }
}

void CategoryPanel::onCreateCategory() {
    // 1. 扫描当前所有的分类，计算出在顶级（parentId = 0）不冲突的默认名字："新建文件夹"、"新建文件夹 (1)"、"新建文件夹 (2)"...
    auto allCats = CategoryRepo::getAll();
    QString baseName = "新建文件夹";
    QString finalName = baseName;
    int suffix = 1;
    bool conflict = true;
    while (conflict) {
        conflict = false;
        for (const auto& c : allCats) {
            if (c.parentId == 0 && QString::fromStdWString(c.name) == finalName) {
                conflict = true;
                break;
            }
        }
        if (conflict) {
            finalName = QString("%1 (%2)").arg(baseName).arg(suffix++);
        }
    }

    // 2. 构造实体并持久化
    Category cat;
    cat.name = finalName.toStdWString();
    cat.parentId = 0;
    cat.color = getDefaultCategoryColor();

    QSet<int> expandedIds;
    QStringList expandedNames;
    saveBothExpandedStates(expandedIds, expandedNames);

    if (CategoryRepo::add(cat)) {
        m_categoryModel->refresh();
        restoreBothExpandedStates(expandedIds, expandedNames);

        // 3. 在树更新完毕后，立刻获取新节点的 Index 并进入行内编辑状态
        int newId = cat.id;
        QTimer::singleShot(50, this, [this, newId]() {
            selectCategory(newId);
            QModelIndex proxyIdx = m_categoryTree->currentIndex();
            DropTreeView* activeTree = m_categoryTree;
            if (!proxyIdx.isValid() && m_categoryTreeUser) {
                proxyIdx = m_categoryTreeUser->currentIndex();
                activeTree = m_categoryTreeUser;
            }
            if (proxyIdx.isValid()) {
                activeTree->edit(proxyIdx);
            }
        });
    }
}

void CategoryPanel::onCreateSubCategory() {
    QModelIndex index = m_categoryTree->currentIndex();
    if (!index.isValid() && m_categoryTreeUser) {
        index = m_categoryTreeUser->currentIndex();
    }
    int parentId = getTargetCategoryId(index);
    if (parentId <= 0) return;

    Category catObj = CategoryRepo::getById(parentId);
    if (catObj.encrypted && !CategoryLockManager::instance().isUnlocked(parentId)) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#e81123;'>分类处于锁定状态，请先解锁后再执行操作！</b>", 2000, QColor("#e81123"));
        return;
    }

    // 1. 扫描同级分类，计算出在 parentId 下不冲突的默认子分类名字
    auto allCats = CategoryRepo::getAll();
    QString baseName = "新建文件夹";
    QString finalName = baseName;
    int suffix = 1;
    bool conflict = true;
    while (conflict) {
        conflict = false;
        for (const auto& c : allCats) {
            if (c.parentId == parentId && QString::fromStdWString(c.name) == finalName) {
                conflict = true;
                break;
            }
        }
        if (conflict) {
            finalName = QString("%1 (%2)").arg(baseName).arg(suffix++);
        }
    }

    // 2. 构造子分类实体并持久化
    Category cat;
    cat.name = finalName.toStdWString();
    cat.parentId = parentId;
    cat.color = getDefaultCategoryColor();

    QSet<int> expandedIds;
    QStringList expandedNames;
    saveBothExpandedStates(expandedIds, expandedNames);
    expandedIds.insert(parentId);

    if (CategoryRepo::add(cat)) {
        m_categoryModel->refresh();
        restoreBothExpandedStates(expandedIds, expandedNames);

        // 3. 展开父节点并自动对新子节点进入行内编辑状态
        int newId = cat.id;
        QTimer::singleShot(50, this, [this, newId]() {
            selectCategory(newId);
            QModelIndex proxyIdx = m_categoryTree->currentIndex();
            DropTreeView* activeTree = m_categoryTree;
            if (!proxyIdx.isValid() && m_categoryTreeUser) {
                proxyIdx = m_categoryTreeUser->currentIndex();
                activeTree = m_categoryTreeUser;
            }
            if (proxyIdx.isValid()) {
                activeTree->edit(proxyIdx);
            }
        });
    }
}

void CategoryPanel::onSetPresetTags() {
    QModelIndex index = m_categoryTree->currentIndex();
    int catId = getTargetCategoryId(index);
    if (catId <= 0) return;

    PresetTagsDialog dlg(catId, this);
    if (dlg.exec() == QDialog::Accepted) {
        m_categoryModel->refresh();
    }
}

void CategoryPanel::onTogglePin() {
    QModelIndex index = m_categoryTree->currentIndex();
    if (!index.isValid() && m_categoryTreeUser) {
        index = m_categoryTreeUser->currentIndex();
    }
    int id = getTargetCategoryId(index);
    if (id <= 0) return;
    
    bool isPinned = index.data(PinnedRole).toBool();

    auto all = CategoryRepo::getAll();
    for(auto& cat : all) {
        if(cat.id == id) {
            cat.pinned = !isPinned;
            CategoryRepo::update(cat);
            break;
        }
    }

    QSet<int> expandedIds;
    QStringList expandedNames;
    saveBothExpandedStates(expandedIds, expandedNames);

    m_categoryModel->refresh();

    restoreBothExpandedStates(expandedIds, expandedNames);
}

void CategoryPanel::onSetPassword() {
    QModelIndex index = m_categoryTree->currentIndex();
    int id = getTargetCategoryId(index);
    if (id <= 0) return;

    // 2026-03-xx 物理级 1:1 还原：废弃通用输入框，调用三字段密码对话框
    CategorySetPasswordDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        QString pwd = dlg.password();
        QString hint = dlg.hint();

        if (pwd.isEmpty()) {
            ToolTipOverlay::instance()->showText(QCursor::pos(), "密码不能为空！", 1500, QColor("#e81123"));
            return;
        }

        // 🚨 1. 计算真实密码的 SHA-256 哈希密文
        QString pwdHash = QCryptographicHash::hash(pwd.toUtf8(), QCryptographicHash::Sha256).toHex();
        
        // 🚨 2. 将“密文”与“提示”组合存储 (格式: "SHA256密文:::提示文本")
        QString combinedData = pwdHash + ":::" + hint;

        QSet<int> expandedIds;
        QStringList expandedNames;
        saveBothExpandedStates(expandedIds, expandedNames);

        auto all = CategoryRepo::getAll();
        for(auto& cat : all) {
            if(cat.id == id) {
                cat.encrypted = true;
                cat.encryptHint = combinedData.toStdWString();
                CategoryRepo::update(cat);
                break;
            }
        }
        
        // 🚨 3. 核心修复：设置密码后，立刻显式触发强制重锁！
        CategoryLockManager::instance().lockCategory(id);
        syncUnlockedIds(); // 侧边栏图标瞬间变成 lock_filled！

        // 🚨 4. 如果内容面板当前正好停留在该分类，立刻刷新切回 CategoryLockWidget 锁屏！
        MainWindow* mw = nullptr;
        QWidget* parentWin = window();
        while (parentWin) {
            if ((mw = qobject_cast<MainWindow*>(parentWin))) break;
            parentWin = parentWin->parentWidget();
        }
        if (mw) {
            ContentPanel* cp = mw->findChild<ContentPanel*>();
            if (cp && cp->currentCategoryId() == id) {
                cp->loadCategory(id); // 触发 loadCategory，检测到已被锁，直接切到 CategoryLockWidget！
            }
        }

        restoreBothExpandedStates(expandedIds, expandedNames);
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#00A650;'>[OK] 分类已加密并立即锁定</b>", 1500, QColor("#00A650"));
    }
}

void CategoryPanel::onClearPassword() {
    QModelIndex index = m_categoryTree->currentIndex();
    int id = getTargetCategoryId(index);
    if (id <= 0) return;

    Category cat = CategoryRepo::getById(id);
    QString storedData = QString::fromStdWString(cat.encryptHint);
    QString realHint = storedData.contains(":::") ? storedData.section(":::", 1) : storedData;

    // 2026-03-xx 物理级还原：清除密码需先通过旧版验证界面校验身份
    CategoryLockDialog dlg(realHint, this);
    if (dlg.exec() == QDialog::Accepted) {
        // 🚨 核心修复：必须经过 SHA-256 真实密文校验
        if (CategoryLockManager::instance().verifyAndUnlock(id, dlg.password())) {
            QSet<int> expandedIds;
            QStringList expandedNames;
            saveBothExpandedStates(expandedIds, expandedNames);

            auto all = CategoryRepo::getAll();
            for(auto& c : all) {
                if(c.id == id) {
                    c.encrypted = false;
                    c.encryptHint = L"";
                    CategoryRepo::update(c);
                    break;
                }
            }

            syncUnlockedIds();

            restoreBothExpandedStates(expandedIds, expandedNames);
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#00A650;'>[OK] 验证成功，分类已解除加密</b>", 1500, QColor("#00A650"));
        } else {
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#e81123;'>旧密码错误，无法解除密码保护！</b>", 2000, QColor("#e81123"));
        }
    }
}

void CategoryPanel::onRenameCategory() {
    QModelIndex index = m_categoryTree->currentIndex();
    DropTreeView* activeTree = m_categoryTree;
    if (!index.isValid() && m_categoryTreeUser) {
        index = m_categoryTreeUser->currentIndex();
        activeTree = m_categoryTreeUser;
    }
    if (index.isValid()) {
        int catId = getTargetCategoryId(index);
        if (catId > 0) {
            Category cat = CategoryRepo::getById(catId);
            if (cat.encrypted && !CategoryLockManager::instance().isUnlocked(catId)) {
                ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#e81123;'>分类处于锁定状态，请先解锁后再执行操作！</b>", 2000, QColor("#e81123"));
                return;
            }
        }
        QString type = index.data(TypeRole).toString();
        // 2026-03-xx 物理兼容：允许重命名分类或文件项 (逻辑处理见 Model)
        if (type == "category" || type == "file" || type == "folder") {
            activeTree->edit(index);
        }
    }
}

void CategoryPanel::onDeleteCategory() {
    // 2026-06-xx 彻底重构：支持多选批量删除分类，杜绝单项操作的低效
    QModelIndexList selectedRows = m_categoryTree->selectionModel()->selectedRows();
    if (selectedRows.isEmpty() && m_categoryTreeUser) {
        selectedRows = m_categoryTreeUser->selectionModel()->selectedRows();
    }
    if (selectedRows.isEmpty()) {
        // 如果没有整行选中，尝试回退到 currentIndex
        QModelIndex current = m_categoryTree->currentIndex();
        if (!current.isValid() && m_categoryTreeUser) {
            current = m_categoryTreeUser->currentIndex();
        }
        if (current.isValid()) selectedRows << current;
    }

    if (selectedRows.isEmpty()) return;

    // 🚨 核心修复：检查任何选中的分类是否被加密并处于锁定状态
    for (const QModelIndex& index : selectedRows) {
        int id = getTargetCategoryId(index);
        if (id > 0) {
            Category cat = CategoryRepo::getById(id);
            if (cat.encrypted && !CategoryLockManager::instance().isUnlocked(id)) {
                ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#e81123;'>分类处于锁定状态，请先解锁后再执行操作！</b>", 2000, QColor("#e81123"));
                return;
            }
        }
    }

    QSet<int> idsToDelete;
    
    // 递归收集分类及其所有子分类 ID 的辅助函数
    std::function<void(const QModelIndex&)> collectIds;
    collectIds = [&](const QModelIndex& index) {
        QString type = index.data(TypeRole).toString();
        int id = index.data(IdRole).toInt();
        
        if (type == "category" && id > 0) {
            idsToDelete.insert(id);
            // 递归收集子分类
            for (int i = 0; i < m_categoryModel->rowCount(index); ++i) {
                collectIds(m_categoryModel->index(i, 0, index));
            }
        }
    };

    for (const QModelIndex& index : selectedRows) {
        collectIds(index);
    }

    if (idsToDelete.isEmpty()) return;

    // 2. 后台批量异步落库
    int totalCount = idsToDelete.size();
    QList<int> idList = idsToDelete.values();
    
    (void)QThreadPool::globalInstance()->start([this, idList, totalCount]() {
        for (int id : idList) {
            ::ArcMeta::CategoryRepo::remove(id);
        }
        
        // 删除完成后回到主线程刷新 UI
        QMetaObject::invokeMethod(this, [this, totalCount]() {
            m_categoryModel->refresh();
            ToolTipOverlay::instance()->showText(QCursor::pos(), 
                QString("<b style='color:%1;'>已成功删除 %2 个分类</b>").arg(qssColor(ErrorRed)).arg(QString::number(totalCount)), 1500, ErrorRed);
        }, Qt::QueuedConnection);
    });
}

int CategoryPanel::getTargetCategoryId(const QModelIndex& index) {
    if (!index.isValid()) return 0;
    
    int id = index.data(IdRole).toInt();
    // 2026-06-xx 物理修复：允许识别负数 ID（系统项），解除 ID > 0 的硬编码限制
    if (id != 0) return id;
    
    // 递归查找父节点，直到找到 category 类型
    return getTargetCategoryId(index.parent());
}

void CategoryPanel::onSortByNameAsc() {
    QModelIndex index = m_categoryTree->currentIndex();
    if (!index.isValid() && m_categoryTreeUser) {
        index = m_categoryTreeUser->currentIndex();
    }
    // 逻辑：获取该项的父级分类 ID，执行重排
    int parentCatId = 0;
    QModelIndex pIdx = index.parent();
    if (pIdx.isValid()) parentCatId = pIdx.data(IdRole).toInt();

    if (CategoryRepo::reorder(parentCatId, true)) {
        m_categoryModel->refresh();
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#2ecc71;'>[OK] 已按 A→Z 排列</b>");
    }
}

void CategoryPanel::onSortByNameDesc() {
    QModelIndex index = m_categoryTree->currentIndex();
    if (!index.isValid() && m_categoryTreeUser) {
        index = m_categoryTreeUser->currentIndex();
    }
    int parentCatId = 0;
    QModelIndex pIdx = index.parent();
    if (pIdx.isValid()) parentCatId = pIdx.data(IdRole).toInt();

    if (CategoryRepo::reorder(parentCatId, false)) {
        m_categoryModel->refresh();
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#2ecc71;'>[OK] 已按 Z→A 排列</b>");
    }
}

void CategoryPanel::onSortAllByNameAsc() {
    if (CategoryRepo::reorderAll(true)) {
        m_categoryModel->refresh();
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#2ecc71;'>[OK] 全部已按 A→Z 排列</b>");
    }
}

void CategoryPanel::onSortAllByNameDesc() {
    if (CategoryRepo::reorderAll(false)) {
        m_categoryModel->refresh();
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#2ecc71;'>[OK] 全部已按 Z→A 排列</b>");
    }
}

void CategoryPanel::onEmptyTrash() {
    // 1. 获取回收站内所有 FID
    // 物理修复：明确作用域标识符 CategoryRepo::TRASH_CATEGORY_ID
    std::vector<std::string> trashItems = CategoryRepo::getFolderIdsInCategory(CategoryRepo::TRASH_CATEGORY_ID);
    
    // 双轨回收站：检测是否有物理磁盘删除项目
    bool hasDiskTrash = false;
    auto dbs = DatabaseManager::instance().getActiveMemoryDbs();
    for (sqlite3* db : dbs) {
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT COUNT(*) FROM disk_trash";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                if (sqlite3_column_int(stmt, 0) > 0) hasDiskTrash = true;
            }
            sqlite3_finalize(stmt);
        }
    }

    if (trashItems.empty() && !hasDiskTrash) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "回收站已空", 1000);
        return;
    }

    // 2. 物理彻底删除 (双轨隔离清空)
    bool ok1 = true;
    if (!trashItems.empty()) {
        ok1 = CategoryRepo::permanentlyDeleteBatch(trashItems);
    }
    bool ok2 = DiskTrashService::emptyDiskTrash();

    if (ok1 && ok2) {
        m_categoryModel->refresh();
        // 强制刷新当前内容面板以更新视图
        MainWindow* win = qobject_cast<MainWindow*>(window());
        if (win && win->findChild<ContentPanel*>()) {
            win->findChild<ContentPanel*>()->refreshAll();
        }
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#e74c3c;'>[OK] 已清空回收站</b>", 1500, ErrorRed);
    }
}

void CategoryPanel::onScanAndCleanEmptyArcs() {
    // 🚨 核心阻断：防止重复高频点击触发扫描风暴
    m_btnScan->setEnabled(false);
    m_btnScan->setIcon(UiHelper::getIcon("scan", QColor("#888888"), 16));

    // 使用 QtConcurrent 在线程池中执行物理磁盘与数据库双向深度清理对账扫描，避免阻塞主线程 UI
    (void)QtConcurrent::run([this]() {
        int cleanCount = 0;
        int ghostCount = 0;
        int orphanCount = 0;

        auto dbs = DatabaseManager::instance().getActiveMemoryDbs();

        // ==========================================
        // 🚨 第一步：盘查并物理清理空托管包 (磁盘 -> 数据库)
        // ==========================================
        const auto drives = QDir::drives();
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

                // 如果确实是空的包，记录路径 and 13 位 ID 进行级联抹除
                if (!hasRealMaterials) {
                    allEmptyArcDirs << arcInfo.absoluteFilePath();
                    allEmptyFolderIds << baseName;
                }
            }
        }

        // ==========================================
        // 🚨 第二步：反查数据库死记录 (数据库 -> 磁盘)
        // ==========================================
        // 直接从所有活跃的内存分库中查出所有的 metadata 记录，反向校验文件在磁盘上是否存在。
        // 如果文件不存在，即使它未载入内存 m_cache，也通过纯 SQL 进行强力擦除。
        QStringList allGhostFolderIds;
        QStringList allGhostPaths;

        for (sqlite3* db : dbs) {
            sqlite3_stmt* stmt = nullptr;
            const char* sqlQuery = "SELECT folder_id, path FROM metadata";
            if (sqlite3_prepare_v2(db, sqlQuery, -1, &stmt, nullptr) == SQLITE_OK) {
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    const char* fidText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                    const wchar_t* pathText = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 1));
                    if (fidText && pathText) {
                        QString qPath = QString::fromStdWString(pathText);
                        // 校验物理路径是否存在
                        bool exists = false;
                        if (QFileInfo(qPath).isDir()) {
                            exists = QDir(qPath).exists();
                        } else {
                            exists = QFile::exists(qPath);
                        }

                        if (!exists) {
                            allGhostFolderIds << QString::fromUtf8(fidText);
                            allGhostPaths << qPath;
                        }
                    }
                }
                sqlite3_finalize(stmt);
            }
        }

        // 合并空包和幽灵文件的 folderIds & paths 进行强力物理+数据库级联删除
        QStringList targetsToRemovePaths = allEmptyArcDirs + allGhostPaths;
        QStringList targetsToRemoveFolderIds = allEmptyFolderIds + allGhostFolderIds;

        if (!targetsToRemovePaths.isEmpty()) {
            // 1. 先通过常规 removeMetadataBatchSync 进行内存缓存/索引同步清理及总计数调整
            // 这个操作会在 MetadataManager 内自动清理已经加载到 m_cache/m_folderIdToPath 的内存条目，并安全微调总计数
            MetadataManager::instance().removeMetadataBatchSync(targetsToRemovePaths);

            // 2. 数据库强力后备死角兜底：对所有可能未载入内存的幽灵数据进行纯 SQL 直接落盘删除
            for (sqlite3* db : dbs) {
                SqlTransaction trans(db);
                sqlite3_stmt* stmtMeta = nullptr;
                sqlite3_stmt* stmtItems = nullptr;
                sqlite3_stmt* stmtStats = nullptr;

                if (sqlite3_prepare_v2(db, "DELETE FROM metadata WHERE folder_id = ?", -1, &stmtMeta, nullptr) == SQLITE_OK &&
                    sqlite3_prepare_v2(db, "DELETE FROM category_items WHERE folder_id = ?", -1, &stmtItems, nullptr) == SQLITE_OK) {
                    
                    for (const QString& fid : targetsToRemoveFolderIds) {
                        std::string stdFid = fid.toStdString();
                        
                        // 从 metadata 删除
                        sqlite3_bind_text(stmtMeta, 1, stdFid.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_step(stmtMeta);
                        sqlite3_reset(stmtMeta);

                        // 从 category_items 删除
                        sqlite3_bind_text(stmtItems, 1, stdFid.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_step(stmtItems);
                        sqlite3_reset(stmtItems);
                    }
                }
                if (stmtMeta) sqlite3_finalize(stmtMeta);
                if (stmtItems) sqlite3_finalize(stmtItems);

                // 同时清理关联的 PROGRESS 进度记录
                for (const QString& qp : targetsToRemovePaths) {
                    std::string progressKey = "PROGRESS:" + qp.toUtf8().toStdString();
                    if (sqlite3_prepare_v2(db, "DELETE FROM system_stats WHERE key = ?", -1, &stmtStats, nullptr) == SQLITE_OK) {
                        sqlite3_bind_text(stmtStats, 1, progressKey.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_step(stmtStats);
                        sqlite3_finalize(stmtStats);
                    }
                }
                trans.commit();
            }

            // 3. 物理彻底擦除磁盘空目录（仅针对第一步判定为空的包）
            for (const QString& path : allEmptyArcDirs) {
                QDir(path).removeRecursively();
            }

            cleanCount = allEmptyArcDirs.size();
            ghostCount = allGhostFolderIds.size();
        }

        // ==========================================
        // 🚨 第三步：清洗孤立关联 (category_items -> metadata)
        // ==========================================
        // 清理所有 category_items（分类关系表）中那些其 folder_id 已经在 metadata（主元数据表）中不存在的断线幽灵关联
        for (sqlite3* db : dbs) {
            SqlTransaction trans(db);
            char* errMsg = nullptr;
            const char* sqlCleanOrphans = "DELETE FROM category_items WHERE folder_id NOT IN (SELECT folder_id FROM metadata)";
            int rc = sqlite3_exec(db, sqlCleanOrphans, nullptr, nullptr, &errMsg);
            if (rc == SQLITE_OK) {
                int affected = sqlite3_changes(db);
                if (affected > 0) {
                    orphanCount += affected;
                }
            } else {
                if (errMsg) {
                    qWarning() << "[Cleanup] Clean orphans error:" << errMsg;
                    sqlite3_free(errMsg);
                }
            }
            trans.commit();
        }

        // 强力对账与同步通知
        if (cleanCount > 0 || ghostCount > 0 || orphanCount > 0) {
            CategoryRepo::s_countsDirty.store(true);
        }

        // 4. 在主线程同步 UI 数据、播放反馈通知并恢复按钮状态
        QMetaObject::invokeMethod(this, [this, cleanCount, ghostCount, orphanCount]() {
            m_btnScan->setEnabled(true);
            m_btnScan->setIcon(UiHelper::getIcon("scan", QColor("#B0B0B0"), 16));

            int totalCleaned = cleanCount + ghostCount;
            if (totalCleaned > 0 || orphanCount > 0) {
                // 重新计数对账以更新侧边栏和主界面
                CategoryRepo::fullRecount();
                requestRefresh(true);

                // 尝试寻找主面板进行内容区全局自愈重构刷新
                QWidget* mw = window();
                if (mw) {
                    QMetaObject::invokeMethod(mw, "refreshAll", Qt::QueuedConnection);
                }

                QString msg = QString("<b style='color:#00A650;'>已成功清理 %1 个空白/幽灵资产</b>").arg(totalCleaned);
                if (orphanCount > 0) {
                    msg += QString("<br/><span style='color:#00A650; font-size:11px;'>同步剔除 %1 条孤立分类关系</span>").arg(orphanCount);
                }

                ToolTipOverlay::instance()->showText(QCursor::pos(), msg, 3500, QColor("#00A650"));
            } else {
                ToolTipOverlay::instance()->showText(QCursor::pos(), 
                    "<b style='color:#CCCCCC;'>未检测到多余的空白托管包与幽灵数据</b>", 
                    2000, QColor("#2D2D2D"));
            }
        });
    });
}

void CategoryPanel::onRestoreAllFromTrash() {
    // 1. 获取回收站内所有 FID
    // 物理修复：明确作用域标识符 CategoryRepo::TRASH_CATEGORY_ID
    std::vector<std::string> trashItems = CategoryRepo::getFolderIdsInCategory(CategoryRepo::TRASH_CATEGORY_ID);
    
    // 双轨回收站：检测是否有物理磁盘删除项目
    bool hasDiskTrash = false;
    auto dbs = DatabaseManager::instance().getActiveMemoryDbs();
    for (sqlite3* db : dbs) {
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT COUNT(*) FROM disk_trash";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                if (sqlite3_column_int(stmt, 0) > 0) hasDiskTrash = true;
            }
            sqlite3_finalize(stmt);
        }
    }

    if (trashItems.empty() && !hasDiskTrash) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "回收站内无项目", 1000);
        return;
    }

    // 2. 物理还原至未分类 (双轨分流还原)
    bool ok1 = true;
    if (!trashItems.empty()) {
        ok1 = CategoryRepo::restoreFromTrashBatch(trashItems);
    }
    bool ok2 = DiskTrashService::restoreAllDiskTrash();

    if (ok1 && ok2) {
        m_categoryModel->refresh();
        // 强制刷新当前内容面板以更新视图
        MainWindow* win = qobject_cast<MainWindow*>(window());
        if (win && win->findChild<ContentPanel*>()) {
            win->findChild<ContentPanel*>()->refreshAll();
        }
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#2ecc71;'>[OK] 已还原全部项目</b>", 1500, QColor("#2ecc71"));
    }
}

void CategoryPanel::setFocusHighlight(bool visible) {
    if (m_focusLine) m_focusLine->setVisible(visible);
}

void CategoryPanel::initUi() {
    // 2026-05-07 按照用户要求：修改焦点线颜色为蓝色
    m_focusLine = new QWidget(this);
    m_focusLine->setFixedHeight(1);
    m_focusLine->setStyleSheet(QString("background-color: %1;").arg(qssColor(PrimaryBlue)));
    m_focusLine->hide(); // 初始隐藏
    m_mainLayout->addWidget(m_focusLine);

    // 1. 标题栏
    QWidget* header = new QWidget(this);
    header->setObjectName("ContainerHeader");
    header->setFixedHeight(32);
    header->setStyleSheet(
        "QWidget#ContainerHeader {"
        "  background-color: #252526;"
        "  border-bottom: 1px solid #333;"
        "}"
    );
    QHBoxLayout* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(15, 0, 5, 0); // 2026-xx-xx 按照用户要求：右侧保留 5px 呼吸边距
    headerLayout->setSpacing(5);                  // 2026-05-17 按照用户要求：间距统一为 5px

    QLabel* iconLabel = new QLabel(header);
    iconLabel->setPixmap(UiHelper::getIcon("folder_filled", PrimaryBlue, 18).pixmap(18, 18));
    headerLayout->addWidget(iconLabel);

    QLabel* titleLabel = new QLabel("文件夹", header);
    titleLabel->setStyleSheet(QString("font-size: 13px; font-weight: bold; color: %1; background: transparent; border: none;").arg(qssColor(PrimaryBlue)));
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();

    // 2026-07-xx 按照用户要求 (Modification_Plan-36)：在分类标题栏右侧加入一键扫描空托管包按钮
    m_btnScan = new QPushButton(header);
    m_btnScan->setFixedSize(24, 24);
    m_btnScan->setIcon(UiHelper::getIcon("scan", QColor("#B0B0B0"), 16));
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
    m_btnScan->installEventFilter(this); // 复用既有悬停滤镜以触发 tooltip
    connect(m_btnScan, &QPushButton::clicked, this, &CategoryPanel::onScanAndCleanEmptyArcs);
    headerLayout->addWidget(m_btnScan);

    m_mainLayout->addWidget(header);

    // 2. 内容区包裹容器 (物理还原 8, 8, 0, 8 呼吸边距)
    // 2026-06-xx 物理对齐：右侧边距设为 0，使滚动条贴合容器边缘
    QWidget* sbContent = new QWidget(this);
    sbContent->setStyleSheet("background: transparent; border: none;");
    auto* sbContentLayout = new QVBoxLayout(sbContent);
    sbContentLayout->setContentsMargins(8, 8, 0, 8);
    sbContentLayout->setSpacing(0);

    QString arrowRight = UiHelper::getSvgTempFilePath("arrow_right", PrimaryBlue);
    QString arrowDown  = UiHelper::getSvgTempFilePath("arrow_down",  PrimaryBlue);

    QString treeStyle = QString(R"(
        QTreeView { background-color: transparent; border: none; color: #CCC; outline: none; }
        
        QTreeView::branch {
            background-color: transparent;
            width: 20px;
        }

        QTreeView::branch:has-children:closed { image: url("%1"); }
        QTreeView::branch:has-children:open   { image: url("%2"); }
        QTreeView::branch:has-children:closed:has-siblings { image: url("%1"); }
        QTreeView::branch:has-children:open:has-siblings   { image: url("%2"); }

        QTreeView::item { height: 26px; padding-left: 0px; }
    )").arg(arrowRight).arg(arrowDown);

    // 2026-04-12 关键修复：延迟初始化模型数据（仅构造空壳），由两棵树共用一个数据源
    m_categoryModel = new CategoryModel(CategoryModel::Both, this);

    // 2.1 构造上半部分：系统项与托管库 QTreeView
    m_categoryTree = new DropTreeView(this);
    m_categoryTree->setStyleSheet(treeStyle); 
    m_categoryTree->setItemDelegate(new CategoryDelegate(this));
    m_proxyModel = new CategoryFilterProxyModel(this);
    m_proxyModel->setFilterMode(CategoryFilterProxyModel::FilterMode::SystemAndManaged);
    m_proxyModel->setSourceModel(m_categoryModel);
    m_categoryTree->setModel(m_proxyModel);

    // 2.2 构造下半部分：自定义分类 QTreeView
    m_categoryTreeUser = new DropTreeView(this);
    m_categoryTreeUser->setStyleSheet(treeStyle);
    m_categoryTreeUser->setItemDelegate(new CategoryDelegate(this));
    m_proxyModelUser = new CategoryFilterProxyModel(this);
    m_proxyModelUser->setFilterMode(CategoryFilterProxyModel::FilterMode::CustomCategoriesOnly);
    m_proxyModelUser->setSourceModel(m_categoryModel);
    m_categoryTreeUser->setModel(m_proxyModelUser);

    auto configureTree = [&](DropTreeView* tree) {
        tree->setHeaderHidden(true);
        tree->setRootIsDecorated(true);
        tree->setIndentation(20);
        tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
        tree->setDragEnabled(true);
        tree->setAcceptDrops(true);
        tree->setDropIndicatorShown(true);
        tree->setDragDropMode(QAbstractItemView::DragDrop);
        tree->setDefaultDropAction(Qt::MoveAction);
        tree->setSelectionMode(QAbstractItemView::ExtendedSelection);

        QAction* deleteAction = new QAction(this);
        deleteAction->setShortcut(QKeySequence::Delete);
        deleteAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        connect(deleteAction, &QAction::triggered, this, &CategoryPanel::onDeleteCategory);
        tree->addAction(deleteAction);

        tree->installEventFilter(this);
    };

    configureTree(m_categoryTree);
    configureTree(m_categoryTreeUser);

    auto connectTreeExpanded = [&](DropTreeView* tree, CategoryFilterProxyModel* proxy) {
        connect(tree, &QTreeView::expanded, this, [this, tree, proxy](const QModelIndex& proxyIndex) {
            QModelIndex index = proxy->mapToSource(proxyIndex);
            int id = index.data(IdRole).toInt();
            bool isEncrypted = index.data(EncryptedRole).toBool();

            if (isEncrypted && id > 0 && !m_unlockedIds.contains(id)) {
                tree->collapse(proxyIndex);
                tree->setCurrentIndex(proxyIndex);
                emit categorySelected(id, index.data(NameRole).toString(), index.data(TypeRole).toString(), index.data(PathRole).toString());
            } else {
                m_categoryModel->loadCategoryItems(index);
            }
        });
    };

    connectTreeExpanded(m_categoryTree, m_proxyModel);
    connectTreeExpanded(m_categoryTreeUser, m_proxyModelUser);

    // 2.3 绑定 AboutToBeReset 与 Reset 逻辑以安全暂存/恢复展开状态
    connect(m_categoryModel, &QAbstractItemModel::modelAboutToBeReset, this, [this]() {
        Logger::log(QString("[CategoryPanel] modelAboutToBeReset: rowCount before reset: %1").arg(m_categoryModel->rowCount()));
        m_categoryModel->setUnlockedIds(m_unlockedIds);
        
        bool hasRealData = false;
        if (m_categoryModel->rowCount() > 1) {
            hasRealData = true;
        } else if (m_categoryModel->rowCount() == 1) {
            QString type = m_categoryModel->index(0, 0).data(TypeRole).toString();
            if (type != "placeholder" && !m_categoryModel->index(0,0).data(Qt::DisplayRole).toString().contains("正在统计")) {
                hasRealData = true;
            }
        }

        if (hasRealData) {
            QSet<int> expandedIds;
            QStringList expandedNames;
            saveBothExpandedStates(expandedIds, expandedNames);
            
            QList<int> idList;
            for (int id : expandedIds) idList << id;
            m_categoryTree->setProperty("expandedIds", QVariant::fromValue(idList));
            m_categoryTree->setProperty("expandedNames", expandedNames);
        }

        m_isInternalUpdating = true;
    });

    connect(m_categoryModel, &QAbstractItemModel::modelReset, this, [this]() {
        Logger::log(QString("[CategoryPanel] modelReset: rowCount after reset: %1").arg(m_categoryModel->rowCount()));
        QVariant varIds = m_categoryTree->property("expandedIds");
        QStringList expandedNames = m_categoryTree->property("expandedNames").toStringList();
        
        QSet<int> expandedIds;
        if (varIds.canConvert<QList<int>>()) {
            QList<int> list = varIds.value<QList<int>>();
            for (int id : list) expandedIds.insert(id);
        } else if (varIds.canConvert<QVariantList>()) {
            QVariantList list = varIds.toList();
            for (const auto& v : list) expandedIds.insert(v.toInt());
        }

        m_isRestoringState = true;
        {
            DataFlowGuard guard(m_isInternalUpdating);
            restoreBothExpandedStates(expandedIds, expandedNames);
        }
        m_isRestoringState = false;
        m_isInternalUpdating = false;

        int count = m_categoryModel ? m_categoryModel->allUserFolderCount() : 0;
        updateFolderGroupButtonText(count);

        if (m_categoryTreeUser) {
            m_categoryTreeUser->setVisible(m_isFolderGroupExpanded);
        }
    });

    // 2.4 绑定点击选择事件
    connect(m_categoryTree, &QTreeView::clicked, this, [this](const QModelIndex& proxyIndex) {
        if (m_categoryTreeUser && m_categoryTreeUser->selectionModel()) {
            m_categoryTreeUser->selectionModel()->clearSelection();
        }
        QModelIndex index = m_proxyModel->mapToSource(proxyIndex);
        QString type = index.data(TypeRole).toString();
        QString name = index.data(NameRole).toString();
        int id = index.data(IdRole).toInt();
        QString path = index.data(PathRole).toString();
        bool isEncrypted = index.data(EncryptedRole).toBool();

        if (isEncrypted && id > 0 && !m_unlockedIds.contains(id)) {
            emit categorySelected(id, name, type, path);
            return;
        }

        if (!type.isEmpty()) {
             emit categorySelected(id, name, type, path);
        }
    });

    connect(m_categoryTreeUser, &QTreeView::clicked, this, [this](const QModelIndex& proxyIndex) {
        if (m_categoryTree && m_categoryTree->selectionModel()) {
            m_categoryTree->selectionModel()->clearSelection();
        }
        QModelIndex index = m_proxyModelUser->mapToSource(proxyIndex);
        QString type = index.data(TypeRole).toString();
        QString name = index.data(NameRole).toString();
        int id = index.data(IdRole).toInt();
        QString path = index.data(PathRole).toString();
        bool isEncrypted = index.data(EncryptedRole).toBool();

        if (isEncrypted && id > 0 && !m_unlockedIds.contains(id)) {
            emit categorySelected(id, name, type, path);
            return;
        }

        if (!type.isEmpty()) {
             emit categorySelected(id, name, type, path);
        }
    });

    // 2.5 绑定拖拽事件
    auto handlePathsDropped = [this](const QStringList& paths, const QModelIndex& srcIndex) {
        int targetCatId = 0; 

        if (srcIndex.isValid()) {
            QString type = srcIndex.data(TypeRole).toString();
            QString name = srcIndex.data(NameRole).toString();

            if (type == "trash") {
                if (ShellHelper::moveToTrash(paths)) {
                    m_categoryModel->refresh();
                    MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::FullRebuild);
                }
                return;
            }

            if (type == "category" && srcIndex.data(IdRole).toInt() > 0) {
                targetCatId = srcIndex.data(IdRole).toInt();
            } else {
                targetCatId = 0;
            }
        } else {
            targetCatId = 0;
        }

        if (!paths.isEmpty()) {
            OperationSnapshotEngine::instance().executeWithSnapshot(
                this,
                SnapshotOperationType::DragCategorize,
                paths,
                "已完成拖拽分类操作",
                [this, paths, targetCatId]() {
                    emit pathsDroppedToCategory(paths, targetCatId);
                    return true;
                },
                [this](const QVector<AssetItemSnapshot>& beforeState) {
                    for (const auto& snap : beforeState) {
                        std::wstring wPath = snap.path.toStdWString();
                        std::string fid = MetadataManager::instance().getFolderIdSync(wPath);
                        if (!fid.empty()) {
                            CategoryRepo::removeAllCategories(fid);
                            for (int catId : snap.categoryIds) {
                                CategoryRepo::addItemToCategory(catId, fid, wPath);
                            }
                        }
                    }
                    m_categoryModel->refresh();
                    MainWindow* win = qobject_cast<MainWindow*>(window());
                    if (win) {
                        ContentPanel* cp = win->findChild<ContentPanel*>();
                        if (cp) cp->refreshAll();
                    }
                    return true;
                }
            );
        }
    };

    connect(m_categoryTree, &DropTreeView::pathsDropped, this, [this, handlePathsDropped](const QStringList& paths, const QModelIndex& proxyIndex) {
        QModelIndex index = m_proxyModel->mapToSource(proxyIndex);
        handlePathsDropped(paths, index);
    });

    connect(m_categoryTreeUser, &DropTreeView::pathsDropped, this, [this, handlePathsDropped](const QStringList& paths, const QModelIndex& proxyIndex) {
        QModelIndex index = m_proxyModelUser->mapToSource(proxyIndex);
        handlePathsDropped(paths, index);
    });
    
    // 2.6 构造“文件夹 (N)”专用组按钮
    m_btnFolderGroup = new QPushButton(this);
    m_btnFolderGroup->setFixedHeight(28);
    m_btnFolderGroup->setCursor(Qt::PointingHandCursor);
    m_btnFolderGroup->setStyleSheet(
        "QPushButton { "
        "  background: transparent; "
        "  border: none; "
        "  color: #EEEEEE; "
        "  font-weight: bold; "
        "  font-size: 12px; "
        "  text-align: left; "
        "  padding-left: 15px; " // 左侧对齐边距：使其图标和文字完美对齐系统分类
        "  margin-right: 5px; "
        "} "
        "QPushButton:hover { background-color: #2D2D30; border-radius: 4px; color: #FFFFFF; }"
        "QPushButton:pressed { background-color: #3E3E40; border-radius: 4px; }"
    );

    // 2.7 点击按钮：折叠/展开自定义分类列表
    connect(m_btnFolderGroup, &QPushButton::clicked, this, [this]() {
        m_isFolderGroupExpanded = !m_isFolderGroupExpanded;
        
        if (m_categoryTreeUser) {
            m_categoryTreeUser->setVisible(m_isFolderGroupExpanded);
        }

        int count = m_categoryModel ? m_categoryModel->allUserFolderCount() : 0;
        updateFolderGroupButtonText(count);
    });

    sbContentLayout->addWidget(m_categoryTree);
    sbContentLayout->addWidget(m_btnFolderGroup);
    sbContentLayout->addWidget(m_categoryTreeUser);
    m_mainLayout->addWidget(sbContent, 1);

    // 2026-xx-xx 按照 Plan-98：新增底部搜索过滤框
    QWidget* searchContainer = new QWidget(this);
    searchContainer->setFixedHeight(40);
    // 2026-06-xx 视觉优化：移除冗余 border-top 分割线
    searchContainer->setStyleSheet("background: transparent; border-top: none;");
    QHBoxLayout* searchLayout = new QHBoxLayout(searchContainer);
    searchLayout->setContentsMargins(10, 0, 10, 0);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("筛选分类...");
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setFixedHeight(32); // 2026-06-xx 物理归一化：与主搜索框对齐至 32px
    
    m_searchEdit->setStyleSheet(QString(
        "QLineEdit {"
        "  background: #1E1E1E;"
        "  color: #EEEEEE;"
        "  border: 1px solid #444;"
        "  border-radius: 6px;"        // 2026-06-xx 规范修正：从 8px 回归至全局 6px 规范
        "  padding: 0 8px 0 1px;"     // 2026-06-xx 物理修正：1px Padding + 约 7px 系统预留 = 8px 视觉间距
        "  font-size: 12px;"
        "}"
        "QLineEdit:focus { border-color: %1; }"
    ).arg(qssColor(PrimaryBlue)));

    // 2026-06-xx 视觉优化：将 select 图标替换为更符合语境的 filter_funnel_outline
    QAction* leadingIcon = m_searchEdit->addAction(UiHelper::getIcon("filter_funnel_outline", QColor("#888888"), 16), QLineEdit::LeadingPosition);
    Q_UNUSED(leadingIcon);

    searchLayout->addWidget(m_searchEdit);
    m_mainLayout->addWidget(searchContainer);

    // 2026-xx-xx 按照 Plan-106：防抖处理
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (text.isEmpty()) {
            m_searchTimer->stop();
            onSearchTextChanged(""); // 清空时立即响应
            return;
        }
        m_searchTimer->start();
    });

    // 2026-03-xx 物理记忆：初始化后加载持久化的展开状态
    QTimer::singleShot(100, this, &CategoryPanel::loadExpandedStateFromSettings);

    // 2026-03-xx 物理记忆：连接展开/折叠信号，实时持久化
    connect(m_categoryTree, &QTreeView::expanded, this, &CategoryPanel::saveExpandedStateToSettings);
    connect(m_categoryTree, &QTreeView::collapsed, this, &CategoryPanel::saveExpandedStateToSettings);
    // 2026-06-xx 物理同步：支持内部拖拽重排持久化
    connect(m_categoryModel, &QAbstractItemModel::rowsMoved, this, [this](const QModelIndex&, int, int, const QModelIndex&, int) {
        // 核心逻辑：深度优先遍历分类树，根据 UI 层级物理同步 DB 中的 parent_id 与 sort_order
        std::function<void(const QModelIndex&, int)> syncSubtree;
        syncSubtree = [&](const QModelIndex& parentIdx, int parentIdInDb) {
            for (int i = 0; i < m_categoryModel->rowCount(parentIdx); ++i) {
                QModelIndex childIdx = m_categoryModel->index(i, 0, parentIdx);
                int id = childIdx.data(IdRole).toInt();
                QString type = childIdx.data(TypeRole).toString();
                bool isPinned = childIdx.data(PinnedRole).toBool();

                // 物理阻断：严禁处理“镜像节点”（即 Pinned 为 true 的节点）。
                // 理由：镜像节点仅作为 UI 快捷方式，其移动不应改写原始数据库中的 parentId 关系。
                if (isPinned) {
                    continue;
                }

                if (type == "category" && id > 0) {
                    int actualParentId = parentIdx.isValid() ? parentIdInDb : 0;
                    // 只有在数据真正发生位移时才触发数据库 UPDATE，优化性能
                    auto all = CategoryRepo::getAll();
                    for (auto& cat : all) {
                        if (cat.id == id) {
                            if (cat.parentId != actualParentId || cat.sortOrder != i) {
                                cat.parentId = actualParentId;
                                cat.sortOrder = i;
                                CategoryRepo::update(cat);
                            }
                            break;
                        }
                    }
                    // 递归同步子分类
                    syncSubtree(childIdx, id);
                }
            }
        };
        syncSubtree(QModelIndex(), 0); // 从隐式根开始，0 表示顶层
    });
}

void CategoryPanel::saveExpandedStateToSettings() {
    // 1. 状态还原或内部更新中，绝不保存
    if (m_isRestoringState || m_isInternalUpdating) {
        Logger::log(QString("[CategoryPanel] saveExpandedStateToSettings: Ignored. m_isRestoringState: %1, m_isInternalUpdating: %2")
            .arg(m_isRestoringState).arg(m_isInternalUpdating));
        return;
    }
    
    // 2. 模型为空或正在重置，绝不保存
    if (!m_categoryModel || m_categoryModel->rowCount() <= 0) {
        Logger::log("[CategoryPanel] saveExpandedStateToSettings: Ignored. Model is null or empty rowCount.");
        return;
    }

    // 3. 占位符防护
    if (m_categoryModel->rowCount() == 1) {
        QModelIndex first = m_categoryModel->index(0, 0);
        QString type = first.data(TypeRole).toString();
        if (type == "placeholder" || first.data(Qt::DisplayRole).toString().contains("正在统计")) {
            Logger::log("[CategoryPanel] saveExpandedStateToSettings: Ignored. Only placeholder or statistics pending.");
            return;
        }
    }

    QSet<int> ids;
    QStringList names;
    saveBothExpandedStates(ids, names);

    // 构造标准化的写入列表
    QVariantList idVarList;
    QList<int> idIntList = ids.values();
    for (int id : idIntList) idVarList << id;

    // 物理落盘
    AppConfig::instance().setValue("Category/ExpandedIds", idVarList);
    AppConfig::instance().setValue("Category/ExpandedNames", names);
    AppConfig::instance().sync(); 

    // 关键点：物理同步更新当前 Tree 的 Property，确保后续刷新时能拿到最新的展开记忆
    m_categoryTree->setProperty("expandedIds", QVariant::fromValue(idIntList));
    m_categoryTree->setProperty("expandedNames", names);
    if (m_categoryTreeUser) {
        m_categoryTreeUser->setProperty("expandedIds", QVariant::fromValue(idIntList));
        m_categoryTreeUser->setProperty("expandedNames", names);
    }
    Logger::log(QString("[CategoryPanel] saveExpandedStateToSettings: Successfully saved to Settings and updated Property. ids count: %1, names count: %2")
        .arg(ids.size()).arg(names.size()));
}

void CategoryPanel::loadExpandedStateFromSettings() {
    bool hasRecord = !AppConfig::instance().getValue("Category/ExpandedIds").isNull() || 
                     !AppConfig::instance().getValue("Category/ExpandedNames").isNull();
    
    QVariantList idVarList = AppConfig::instance().getValue("Category/ExpandedIds").toList();
    QStringList names = AppConfig::instance().getValue("Category/ExpandedNames").toStringList();

    Logger::log(QString("[CategoryPanel] loadExpandedStateFromSettings: Loaded settings. ids count: %1, names count: %2, hasRecord: %3")
        .arg(idVarList.size()).arg(names.size()).arg(hasRecord));

    QSet<int> ids;
    QList<int> idIntList;
    for (const auto& v : idVarList) {
        int id = v.toInt();
        ids.insert(id);
        idIntList << id;
    }

    // 核心修复：Property 中统一存储 QList<int>，彻底消除类型强转失败
    m_categoryTree->setProperty("expandedIds", QVariant::fromValue(idIntList));
    m_categoryTree->setProperty("expandedNames", names);
    m_categoryTree->setProperty("hasHistoryRecord", hasRecord);

    if (m_categoryTreeUser) {
        m_categoryTreeUser->setProperty("expandedIds", QVariant::fromValue(idIntList));
        m_categoryTreeUser->setProperty("expandedNames", names);
        m_categoryTreeUser->setProperty("hasHistoryRecord", hasRecord);
    }

    // 尝试立即恢复一次
    m_isRestoringState = true;
    {
        DataFlowGuard guard(m_isInternalUpdating);
        restoreBothExpandedStates(ids, names);
    }
    m_isRestoringState = false;
}

void CategoryPanel::syncUnlockedIds() {
    m_unlockedIds = CategoryLockManager::instance().getUnlockedIds();
    if (m_categoryModel) {
        m_categoryModel->setUnlockedIds(m_unlockedIds);
        m_categoryModel->refresh();
        if (m_proxyModel) {
            m_proxyModel->invalidate();
        }
        if (m_proxyModelUser) {
            m_proxyModelUser->invalidate();
        }
    }
}

void CategoryPanel::expandCategory(int id) {
    if (!m_categoryModel || !m_categoryTree) return;
    
    std::function<QModelIndex(const QModelIndex&)> findId;
    findId = [&](const QModelIndex& parent) -> QModelIndex {
        for (int i = 0; i < m_categoryModel->rowCount(parent); ++i) {
            QModelIndex idx = m_categoryModel->index(i, 0, parent);
            if (idx.data(IdRole).toInt() == id) return idx;
            QModelIndex child = findId(idx);
            if (child.isValid()) return child;
        }
        return QModelIndex();
    };

    QModelIndex target = findId(QModelIndex());
    if (target.isValid()) {
        QModelIndex proxyIdx = m_proxyModel->mapFromSource(target);
        if (proxyIdx.isValid() && m_categoryTree) {
            m_categoryTree->expand(proxyIdx);
        } else if (m_proxyModelUser && m_categoryTreeUser) {
            QModelIndex proxyIdxUser = m_proxyModelUser->mapFromSource(target);
            if (proxyIdxUser.isValid()) {
                m_categoryTreeUser->expand(proxyIdxUser);
            }
        }
    }
}

bool CategoryPanel::tryUnlockCategory(const QModelIndex& index) {
    int id = index.data(IdRole).toInt();
    if (id <= 0) return false;

    QString storedData = index.data(EncryptHintRole).toString();
    QString realHint = storedData.contains(":::") ? storedData.section(":::", 1) : storedData;

    // 2026-03-xx 物理级还原：废弃通用输入框，改用 1:1 复刻的旧版验证界面
    CategoryLockDialog dlg(realHint, this);
    if (dlg.exec() == QDialog::Accepted) {
        // 🚨 使用 CategoryLockManager 线程安全会话单例管理解锁状态
        CategoryLockManager::instance().verifyAndUnlock(id, dlg.password());
        
        m_unlockedIds = CategoryLockManager::instance().getUnlockedIds();
        
        // 物理补丁：解锁后由于图标需要刷新，强制同步 ID 并进行一次模型重刷
        m_categoryModel->setUnlockedIds(m_unlockedIds);
        m_categoryModel->refresh();
        
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#00A650;'>[OK] 验证成功，分类已解锁</b>", 1000, QColor("#00A650"));
        return true;
    }
    return false;
}

bool CategoryPanel::eventFilter(QObject* obj, QEvent* event) {
    // 2026-06-xx 按照用户要求：补全对 ToolTipOverlay 的物理拦截与映射逻辑
    // 理由：主窗口无法自动拦截深层嵌套子组件的 Hover 事件，需在组件层手动分发
    if (event->type() == QEvent::HoverEnter || event->type() == QEvent::Enter) {
        QString text = obj->property("tooltipText").toString();
        if (!text.isEmpty()) {
            ToolTipOverlay::instance()->showText(QCursor::pos(), text);
        }
    } else if (event->type() == QEvent::HoverLeave || event->type() == QEvent::Leave) {
        ToolTipOverlay::hideTip();
    }

    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        
        // 2026-06-xx 按照用户要求：禁用 Ctrl+A 全选
        if ((obj == m_categoryTree || obj == m_categoryTreeUser) && keyEvent->modifiers() == Qt::ControlModifier && keyEvent->key() == Qt::Key_A) {
            return true; 
        }

        // 2026-06-xx 按照用户要求：支持 Delete 键物理删除选中分类
        if ((obj == m_categoryTree || obj == m_categoryTreeUser) && keyEvent->key() == Qt::Key_Delete) {
            onDeleteCategory();
            return true;
        }

        // 2026-xx-xx 按照 Plan-63：按 F2 同步进入行内编辑状态
        if ((obj == m_categoryTree || obj == m_categoryTreeUser) && keyEvent->key() == Qt::Key_F2) {
            onRenameCategory();
            return true;
        }

        if (keyEvent->key() == Qt::Key_Escape) {
            // [UX] 两段式：查找对话框内的第一个非空输入框
            QLineEdit* edit = findChild<QLineEdit*>();
            if (edit && !edit->text().isEmpty()) {
                edit->clear();
                return true;
            }
        }
    }
    return QFrame::eventFilter(obj, event);
}

void CategoryPanel::updateFolderGroupButtonText(int count) {
    if (!m_btnFolderGroup) return;
    // 使用统一美观的双态符号（对应用户原话：“(▼ / ▶)”）
    QString arrow = m_isFolderGroupExpanded ? "▼  " : "▶  ";
    m_btnFolderGroup->setText(QString("%1文件夹 (%2)").arg(arrow).arg(count));
    m_btnFolderGroup->setIcon(UiHelper::getIcon("folder_filled", QColor("#378ADD"), 16));
}

} // namespace ArcMeta
