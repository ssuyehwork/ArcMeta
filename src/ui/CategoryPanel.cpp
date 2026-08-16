#include "CategoryPanel.h"
#include "../meta/TrashRepository.h"
#include "MainWindow.h"
#include "PresetTagsDialog.h"
#include "CategoryModel.h"
#include "../meta/StatisticsService.h"
#include "ContentPanel.h"
#include "../core/DiskTrashService.h"
#include "../core/LibraryMaintenanceService.h"
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
#include <QCoreApplication>

namespace ArcMeta {

/**
 * @brief 获取默认分类颜色：深灰色 (#555555)
 * 2026-06-xx 按照用户要求：废除随机色，统一默认使用深灰色（对应用户原话：“自定义文件夹（分类）的颜色只能为#555555”）
 */
static std::wstring getDefaultCategoryColor() {
    return L"#555555";
}

CategoryPanel::~CategoryPanel() {
    // 1. 在面板被析构前，将控制标志设为内部更新态，彻底屏蔽 QTreeView 卸载时的折叠信号回流
    m_isInternalUpdating = true;
     
    // 2. 物理断开这些高危信号，确保高枕无忧
    if (m_categoryTree) {
        disconnect(m_categoryTree, &QTreeView::expanded, this, &CategoryPanel::saveExpandedStateToSettings);
        disconnect(m_categoryTree, &QTreeView::collapsed, this, &CategoryPanel::saveExpandedStateToSettings);
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
            refreshFullTree();
            m_isFirstLoad = false;
            m_refreshTimer->setProperty("fullRebuild", false); // 消费完重置
        } else {
            refreshCountsOnly();
        }
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
    // 理由：系统启动时的 initFromDatabase 是异步进行的，完成后必须强制刷新侧边栏
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

void CategoryPanel::refreshCountsOnly() {
    if (!m_categoryModel) return;
    QPointer<CategoryPanel> weakThis(this);
    StatisticsService::instance().requestFullRecountAsync([weakThis](const StatisticsSnapshot& snapshot) {
        if (weakThis && weakThis->m_categoryModel) {
            bool isSysUnready = !MetadataManager::instance().isLoaded();
            bool allCountsZero = (snapshot.systemCounts.value("all", 0) == 0 && snapshot.systemCounts.value("trash", 0) == 0);
            if (isSysUnready && allCountsZero) {
                return;
            }
            weakThis->m_categoryModel->updateStatisticsWithSnapshot(snapshot);
        }
    });
}

void CategoryPanel::refreshFullTree() {
    if (!m_categoryModel) return;
    m_categoryModel->refresh();
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
        // 2026-xx-xx 按照 Plan-98：映射至代理模型索引
        QModelIndex proxyIdx = m_proxyModel->mapFromSource(target);
        if (proxyIdx.isValid()) {
            // 2026-07-26 极致重构：利用 DataFlowGuard 优雅控制，彻底消灭 blockSignals
            DataFlowGuard guard(m_isInternalUpdating);
            m_categoryTree->setCurrentIndex(proxyIdx);
            m_categoryTree->scrollTo(proxyIdx);
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
        if (proxyIdx.isValid()) {
            // 2026-07-26 极致重构：利用 DataFlowGuard 优雅控制，彻底消灭 blockSignals
            DataFlowGuard guard(m_isInternalUpdating);
            m_categoryTree->setCurrentIndex(proxyIdx);
            m_categoryTree->scrollTo(proxyIdx);
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
    m_categoryTree->setContextMenuPolicy(Qt::CustomContextMenu);
    // 2026-05-27 物理加固：补全 this 上下文
    connect(m_categoryTree, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QModelIndex proxyIndex = m_categoryTree->indexAt(pos);
        QModelIndex index = m_proxyModel->mapToSource(proxyIndex);
        
        // 2026-03-xx 按照用户要求：实现右键点击即选中，解决“分类与其子分类”交互一致性问题
        // 多选防护：若右键所在的节点已在当前多选集合中，绝对禁止重置，以保全批量删除操作（对应用户原话：“当我在侧边栏选中多个之后，再来执行‘删除’，结果只能删除其中一个”）
        if (proxyIndex.isValid()) {
            if (!m_categoryTree->selectionModel()->isSelected(proxyIndex)) {
                m_categoryTree->setCurrentIndex(proxyIndex);
            }
        }

        QMenu menu(this);
        UiHelper::applyMenuStyle(&menu);

        // 基于规范逻辑：如果没有选中项
        QString itemName = index.data(NameRole).toString();
        QString itemType = index.data(TypeRole).toString();

        if (itemType == "trash") {
            // 2026-06-xx 物理级 1:1 还原：回收站专属右键菜单
            menu.addAction(UiHelper::getIcon("trash", ErrorRed, 18), "清空回收站", this, &CategoryPanel::onEmptyTrash);
            menu.addAction(UiHelper::getIcon("sync", PrimaryBlue, 18), "还原全部项目", this, &CategoryPanel::onRestoreAllFromTrash);
        } else if (!index.isValid()) {
            menu.addAction(UiHelper::getIcon("folder_filled", QColor("#aaaaaa"), 18), "新建文件夹", this, &CategoryPanel::onCreateCategory);
            
            auto* sortMenu = menu.addMenu(UiHelper::getIcon("list_ul", QColor("#aaaaaa"), 18), "排列");
            sortMenu->setStyleSheet(menu.styleSheet());
            sortMenu->addAction("标题(全部) (A→Z)", this, &CategoryPanel::onSortAllByNameAsc);
            sortMenu->addAction("标题(全部) (Z→A)", this, &CategoryPanel::onSortAllByNameDesc);
        } else {
            // 2026-03-xx 按照用户要求：补全子层级（子分类、文件、文件夹）的右键菜单
            // 物理修复：移除重复声明，使用统一的 itemType 变量
            
            // 只要不是系统根节点，都弹出完整菜单
            if (itemType == "category" || itemType == "file" || itemType == "folder") {
                int catId = index.data(IdRole).toInt();
                Category cat = CategoryRepo::getById(catId);
                int kindVal = index.data(CategoryKindRole).toInt();
                bool isManagedLibraryRoot = (kindVal == static_cast<int>(CategoryKind::SystemLibrary)) ||
                                            (cat.kind == CategoryKind::SystemLibrary) ||
                                            (cat.id > 0 && cat.parentId == 0 && !cat.physicalPath.empty());

                if (!isManagedLibraryRoot) {
                    QString colorStr = index.data(ColorRole).toString();
                    
                    // 使用 ColorStripPicker 快捷菜单项，直接展露在主菜单上
                    QWidgetAction* colorPickerAction = new QWidgetAction(&menu);
                    ColorStripPicker* colorPickerWidget = new ColorStripPicker(colorStr, &menu);
                    colorPickerAction->setDefaultWidget(colorPickerWidget);
                    menu.addAction(colorPickerAction);

                    connect(colorPickerWidget, &ColorStripPicker::colorSelected, this, [this, catId, &menu](const QString& hexColor) {
                        auto all = CategoryRepo::getAll();
                        for (auto& cat : all) {
                            if (cat.id == catId) {
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
                        saveExpandedState(QModelIndex(), expandedIds, expandedNames);

                        m_categoryModel->refresh();

                        restoreExpandedState(QModelIndex(), expandedIds, expandedNames);
                        menu.close();
                    });

                    menu.addAction(UiHelper::getIcon("tag_filled", QColor("#9b59b6"), 18), "设置预设标签", this, &CategoryPanel::onSetPresetTags);

                    // [Plan-6] 创建主选项“文件夹图标”
                    QMenu* iconMenu = menu.addMenu(UiHelper::getIcon("folder_filled", WarningOrange, 18), "文件夹图标");
                    UiHelper::applyMenuStyle(iconMenu);

                    QColor catColor = colorStr.isEmpty() ? QColor("#555555") : QColor(colorStr);

                    // 使用 QWidgetAction 构建纯图标选择器
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

                        connect(btn, &QPushButton::clicked, this, [this, catId, iconKey, iconMenu]() {
                            auto cats = CategoryRepo::getAll();
                            for (auto& cat : cats) {
                                if (cat.id == catId) {
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
                }

                menu.addAction(UiHelper::getIcon("folder_filled", TextMuted, 18), "新建文件夹", this, &CategoryPanel::onCreateCategory);

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

                // 2026-03-xx 按照用户要求：补全排列与密码保护逻辑
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
            menu.exec(m_categoryTree->viewport()->mapToGlobal(pos));
        }
    });
}

/**
 * @brief 递归保存 QTreeView 的展开状态
 */
void CategoryPanel::saveExpandedState(const QModelIndex& parent, QSet<int>& expandedIds, QStringList& expandedNames) {
    if (!m_categoryTree || !m_categoryTree->model()) return;
    int rowCount = m_categoryTree->model()->rowCount(parent);
    for (int i = 0; i < rowCount; ++i) {
        QModelIndex idx = m_categoryTree->model()->index(i, 0, parent);
        if (m_categoryTree->isExpanded(idx)) {
            int id = idx.data(IdRole).toInt();
            QString name = idx.data(NameRole).toString();
            if (id != 0) {
                expandedIds.insert(id);
            } else {
                expandedNames << name;
            }
            saveExpandedState(idx, expandedIds, expandedNames);
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
}

/**
 * @brief 递归恢复 QTreeView 的展开状态
 * 2026-03-xx 物理拦截：加密且未解锁的分类在恢复时强制跳过展开
 */
void CategoryPanel::restoreExpandedState(const QModelIndex& parent, const QSet<int>& expandedIds, const QStringList& expandedNames) {
    if (!m_categoryTree || !m_categoryTree->model()) return;
    
    bool hasHistory = m_categoryTree->property("hasHistoryRecord").toBool();
    int rowCount = m_categoryTree->model()->rowCount(parent);
    
    for (int i = 0; i < rowCount; ++i) {
        QModelIndex idx = m_categoryTree->model()->index(i, 0, parent);
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
            m_categoryTree->setExpanded(idx, true);
            restoreExpandedState(idx, expandedIds, expandedNames);
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
    saveExpandedState(QModelIndex(), expandedIds, expandedNames);

    if (CategoryRepo::add(cat)) {
        m_categoryModel->refresh();
        restoreExpandedState(QModelIndex(), expandedIds, expandedNames);

        // 🚀 【时序对齐】：记录待编辑 ID，待代理模型完全恢复展开状态后精密触发
        m_pendingEditCategoryId = cat.id;
        m_categoryModel->refresh();
    }
}

void CategoryPanel::onCreateSubCategory() {
    QModelIndex index = m_categoryTree->currentIndex();
    int parentId = getTargetCategoryId(index);
    // 🚨 修正：若选中的是“文件夹”根组节点 (ID: -9) 或系统项 (<0)，强制归为顶级分类 (parentId = 0)
    if (parentId <= 0) {
        parentId = 0;
    }

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
    saveExpandedState(QModelIndex(), expandedIds, expandedNames);
    expandedIds.insert(parentId);

    if (CategoryRepo::add(cat)) {
        m_categoryModel->refresh();
        restoreExpandedState(QModelIndex(), expandedIds, expandedNames);

        // 🚀 【时序对齐】：记录待编辑 ID，待代理模型完全恢复展开状态后精密触发
        m_pendingEditCategoryId = cat.id;
        m_categoryModel->refresh();
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
    saveExpandedState(QModelIndex(), expandedIds, expandedNames);

    m_categoryModel->refresh();

    restoreExpandedState(QModelIndex(), expandedIds, expandedNames);
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
        saveExpandedState(QModelIndex(), expandedIds, expandedNames);

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

        restoreExpandedState(QModelIndex(), expandedIds, expandedNames);
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
            saveExpandedState(QModelIndex(), expandedIds, expandedNames);

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

            restoreExpandedState(QModelIndex(), expandedIds, expandedNames);
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#00A650;'>[OK] 验证成功，分类已解除加密</b>", 1500, QColor("#00A650"));
        } else {
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#e81123;'>旧密码错误，无法解除密码保护！</b>", 2000, QColor("#e81123"));
        }
    }
}

void CategoryPanel::onRenameCategory() {
    QModelIndex index = m_categoryTree->currentIndex();
    if (index.isValid()) {
        int catId = getTargetCategoryId(index);
        if (catId > 0) {
            Category cat = CategoryRepo::getById(catId);
            if (cat.kind == CategoryKind::SystemLibrary || (!cat.physicalPath.empty() && cat.parentId == 0)) {
                ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#e81123;'>受保护的托管库分类禁止编辑/删除！</b>", 2000, QColor("#e81123"));
                return;
            }
            if (cat.encrypted && !CategoryLockManager::instance().isUnlocked(catId)) {
                ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#e81123;'>分类处于锁定状态，请先解锁后再执行操作！</b>", 2000, QColor("#e81123"));
                return;
            }
        }
        QString type = index.data(TypeRole).toString();
        // 2026-03-xx 物理兼容：允许重命名分类或文件项 (逻辑处理见 Model)
        if (type == "category" || type == "file" || type == "folder") {
            m_categoryTree->edit(index);
        }
    }
}

void CategoryPanel::onDeleteCategory() {
    // 2026-06-xx 彻底重构：支持多选批量删除分类，杜绝单项操作的低效
    QModelIndexList selectedRows = m_categoryTree->selectionModel()->selectedRows();
    if (selectedRows.isEmpty()) {
        // 如果没有整行选中，尝试回退到 currentIndex
        QModelIndex current = m_categoryTree->currentIndex();
        if (current.isValid()) selectedRows << current;
    }

    if (selectedRows.isEmpty()) return;

    // 🚨 核心修复：检查任何选中的分类是否包含受保护托管库或处于锁定状态
    for (const QModelIndex& index : selectedRows) {
        int id = getTargetCategoryId(index);
        if (id > 0) {
            Category cat = CategoryRepo::getById(id);
            if (cat.kind == CategoryKind::SystemLibrary || (!cat.physicalPath.empty() && cat.parentId == 0)) {
                ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#e81123;'>受保护的托管库分类禁止编辑/删除！</b>", 2000, QColor("#e81123"));
                return;
            }
            if (cat.encrypted && !CategoryLockManager::instance().isUnlocked(id)) {
                ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#e81123;'>分类处于锁定状态，请先解锁后再执行操作！</b>", 2000, QColor("#e81123"));
                return;
            }
        }
    }

    QSet<int> idsToDelete;
    
    // 递归收集分类及其所有子分类 ID 的辅助函数。在第一时序完全运行于 Source Index 的无损索引之上，彻底修复 rowCount() 失效导致的级联删除中断
    std::function<void(const QModelIndex&)> collectIds;
    collectIds = [&](const QModelIndex& srcIndex) {
        if (!srcIndex.isValid()) return;
        QString type = srcIndex.data(TypeRole).toString();
        int id = srcIndex.data(IdRole).toInt();
        
        if (type == "category" && id > 0) {
            idsToDelete.insert(id);
            // 递归收集子分类
            for (int i = 0; i < m_categoryModel->rowCount(srcIndex); ++i) {
                collectIds(m_categoryModel->index(i, 0, srcIndex));
            }
        }
    };

    for (const QModelIndex& proxyIdx : selectedRows) {
        QModelIndex srcIndex = m_proxyModel->mapToSource(proxyIdx);
        collectIds(srcIndex);
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
    bool hasDiskTrash = ArcMeta::TrashRepository::instance().hasTrashItems();

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
    // 🚀 【重构解耦】：UI 仅处理界面的 loading 与交互，不执行任何 I/O 与数据库事务
    m_btnScan->setEnabled(false);
    m_btnScan->setIcon(UiHelper::getIcon("scan", QColor("#888888"), 16));

    connect(&LibraryMaintenanceService::instance(), &LibraryMaintenanceService::cleanFinished,
            this, [this](int cleanCount, int ghostCount, int orphanCount) {
        m_btnScan->setEnabled(true);
        m_btnScan->setIcon(UiHelper::getIcon("scan", QColor("#B0B0B0"), 16));

        int totalCleaned = cleanCount + ghostCount;
        if (totalCleaned > 0 || orphanCount > 0) {
            requestRefresh(true);
            QWidget* mw = window();
            if (mw) QMetaObject::invokeMethod(mw, "refreshAll", Qt::QueuedConnection);

            QString msg = QString("<b style='color:#00A650;'>已成功清理 %1 个空白/幽灵资产</b>").arg(totalCleaned);
            if (orphanCount > 0) {
                msg += QString("<br/><span style='color:#00A650; font-size:11px;'>同步剔除 %1 条孤立分类关系</span>").arg(orphanCount);
            }
            ToolTipOverlay::instance()->showText(QCursor::pos(), msg, 3500, QColor("#00A650"));
        } else {
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#CCCCCC;'>未检测到多余的空白托管包与幽灵数据</b>", 2000, QColor("#2D2D2D"));
        }
    }, Qt::UniqueConnection);

    LibraryMaintenanceService::instance().scanAndCleanEmptyArcsAsync();
}

void CategoryPanel::onRestoreAllFromTrash() {
    // 1. 获取回收站内所有 FID
    // 物理修复：明确作用域标识符 CategoryRepo::TRASH_CATEGORY_ID
    std::vector<std::string> trashItems = CategoryRepo::getFolderIdsInCategory(CategoryRepo::TRASH_CATEGORY_ID);
    
    // 双轨回收站：检测是否有物理磁盘删除项目
    bool hasDiskTrash = ArcMeta::TrashRepository::instance().hasTrashItems();

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

    // 物理还原：单树架构，合并系统项与用户分类
    m_categoryTree = new DropTreeView(this);
    m_categoryTree->setStyleSheet(treeStyle); 
    m_categoryTree->setItemDelegate(new CategoryDelegate(this));
    
    // 2026-04-12 关键修复：延迟初始化模型数据（仅构造空壳）
    m_categoryModel = new CategoryModel(CategoryModel::Both, this);

    // 🚀 【重构解耦】：在中介接收器中统一处理物理改名与排序写库，保持 Model 100% 只读 (生命周期安全保护)
    connect(m_categoryModel, &CategoryModel::categoryRenameRequested, this, [this](int catId, const QString& newName) {
        auto cat = CategoryRepo::getById(catId);
        if (cat.id <= 0) return;
        
        QPointer<CategoryModel> safeModel(m_categoryModel);
        (void)QtConcurrent::run([safeModel, cat, newName]() mutable {
            bool renameSuccess = true;
            bool physicalRenamed = false;
            QString oldPath;
            QString newPath;
            if (!cat.physicalPath.empty()) {
                oldPath = QString::fromStdWString(cat.physicalPath);
                QFileInfo oldInfo(oldPath);
                newPath = QDir::toNativeSeparators(oldInfo.absoluteDir().absoluteFilePath(newName));
                if (oldPath != newPath) {
                    if (QFile::rename(oldPath, newPath)) {
                        cat.physicalPath = newPath.toStdWString();
                        physicalRenamed = true;
                    } else {
                        renameSuccess = false;
                    }
                }
            }

            if (renameSuccess) {
                cat.name = newName.toStdWString();
                CategoryRepo::update(cat);
                
                if (physicalRenamed) {
                    MetadataManager::instance().renameItem(oldPath.toStdWString(), newPath.toStdWString());
                }
            }

            if (safeModel) {
                QMetaObject::invokeMethod(safeModel.data(), [safeModel]() {
                    if (safeModel) {
                        safeModel->refresh();
                    }
                }, Qt::QueuedConnection);
            }
        });
    });

    connect(m_categoryModel, &CategoryModel::categoryOrderChanged, this, [this](int draggedId, int targetParentId, int insertRow) {
        auto allCats = CategoryRepo::getAll();
        std::vector<Category> siblings;
        Category draggedCat;
        bool foundDragged = false;

        for (const auto& cat : allCats) {
            if (cat.id == draggedId) {
                draggedCat = cat;
                foundDragged = true;
            } else if (cat.parentId == targetParentId && cat.kind != CategoryKind::SystemLibrary) {
                siblings.push_back(cat);
            }
        }

        if (!foundDragged) return;

        // 按已有的 sortOrder 升序排列同级项
        std::sort(siblings.begin(), siblings.end(), [](const Category& a, const Category& b) {
            return a.sortOrder < b.sortOrder;
        });

        draggedCat.parentId = targetParentId;
        if (insertRow < 0 || insertRow > static_cast<int>(siblings.size())) {
            insertRow = static_cast<int>(siblings.size());
        }
        siblings.insert(siblings.begin() + insertRow, draggedCat);

        // 100% 物理写盘：批量重新计算并更新 SQLite 中的 sortOrder 序号与 parentId
        for (size_t i = 0; i < siblings.size(); ++i) {
            siblings[i].sortOrder = static_cast<int>(i);
            CategoryRepo::update(siblings[i]);
        }

        m_categoryModel->refresh();
    });
    
    // 2026-xx-xx 按照 Plan-98：注入代理模型
    m_proxyModel = new CategoryFilterProxyModel(this);
    m_proxyModel->setSourceModel(m_categoryModel);
    m_categoryTree->setModel(m_proxyModel);
    
    m_categoryTree->setHeaderHidden(true);
    m_categoryTree->setRootIsDecorated(true);
    m_categoryTree->setIndentation(20);
    m_categoryTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_categoryTree->setDragEnabled(true);
    m_categoryTree->setAcceptDrops(true);
    m_categoryTree->setDropIndicatorShown(true);
    // 核心修正：解除 InternalMove 模式封锁，允许接收外部容器（NavPanel/ContentPanel）的拖拽
    m_categoryTree->setDragDropMode(QAbstractItemView::DragDrop);
    m_categoryTree->setDefaultDropAction(Qt::MoveAction);
    m_categoryTree->setSelectionMode(QAbstractItemView::ExtendedSelection);

    // 监听多选改变信号，抛出联合信号
    connect(m_categoryTree->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this]() {
        if (m_isInternalUpdating) return;
         
        QModelIndexList selectedRows = m_categoryTree->selectionModel()->selectedRows();
        if (selectedRows.isEmpty()) return;

        QList<int> catIds;
        QStringList catNames;
        QString type;

        for (const QModelIndex& proxyIdx : selectedRows) {
            QModelIndex index = m_proxyModel->mapToSource(proxyIdx);
            if (!index.isValid()) continue;

            int id = index.data(IdRole).toInt();
            QString t = index.data(TypeRole).toString();
            QString name = index.data(NameRole).toString();

            if (t == "category" && id > 0) {
                catIds.append(id);
                catNames.append(name);
                type = "category";
            } else if (id < 0) {
                // 如果用户选中了系统桶（如回收站、全部数据等），则强制单选该项，杜绝系统项与分类项多选混淆
                catIds = {id};
                catNames = {name};
                type = t;
                break;  
            }
        }

        if (!catIds.isEmpty()) {
            if (catIds.size() == 1) {
                // 仅有一项时，依然兼容发射单选信号，确保单项定制操作（如颜色设定/重命名）对齐
                QModelIndex idx = m_proxyModel->mapToSource(selectedRows.first());
                emit categorySelected(catIds.first(), catNames.first(), type, idx.data(PathRole).toString());
            } else {
                emit categoriesSelected(catIds, catNames, "category");
            }
        }
    });
    
    // 2026-06-xx 按照用户要求：支持 Delete 键物理删除选中分类，使用 Action 提升快捷键响应等级
    QAction* deleteCatAction = new QAction(this);
    deleteCatAction->setShortcut(QKeySequence::Delete);
    deleteCatAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(deleteCatAction, &QAction::triggered, this, &CategoryPanel::onDeleteCategory);
    m_categoryTree->addAction(deleteCatAction);

    m_categoryTree->installEventFilter(this);

    // 2026-03-xx 物理拦截：严禁加密分类在未解锁时被展开，直接触发内容面板卡片密码输入
    // 2026-05-27 物理加固：补全 this 上下文
    connect(m_categoryTree, &QTreeView::expanded, this, [this](const QModelIndex& index) {
        int id = index.data(IdRole).toInt();
        bool isEncrypted = index.data(EncryptedRole).toBool();
        
        // 物理修复：加密校验仅针对数据库分类（ID > 0），跳过系统项（ID < 0）
        if (isEncrypted && id > 0 && !m_unlockedIds.contains(id)) {
            // 物理阻断：立即折叠，防止其在未解锁时显示子项
            m_categoryTree->collapse(index);
            m_categoryTree->setCurrentIndex(index);
            emit categorySelected(id, index.data(NameRole).toString(), index.data(TypeRole).toString(), index.data(PathRole).toString());
        } else {
            // 2026-05-27 物理修复：展开时按需动态加载分类关联的文件，杜绝启动挂起
            m_categoryModel->loadCategoryItems(index);
        }
    });

    // 2026-03-xx 物理兼容：监听模型重置信号，在刷新后尝试恢复展开状态
    // 2026-05-27 物理加固：补全 this 上下文
    connect(m_categoryModel, &QAbstractItemModel::modelAboutToBeReset, this, [this]() {
        // 同步解锁 ID 到模型
        m_categoryModel->setUnlockedIds(m_unlockedIds);
        
        // 物理防护：只有当模型确实有真实数据时，才暂存当前 UI 状态。
        // 如果当前是“加载中”或者为空，则不覆盖暂存值，保留从 Settings 加载或上一次有效的记录。
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
            saveExpandedState(QModelIndex(), expandedIds, expandedNames);
            
            QList<int> idList;
            for (int id : expandedIds) idList << id;
            m_categoryTree->setProperty("expandedIds", QVariant::fromValue(idList));
            m_categoryTree->setProperty("expandedNames", expandedNames);
        }

        // 开启数据流拦截锁，防止接下来 beginResetModel / removeRows 触发大量的 collapsed 虚假信号泄露覆写
        m_isInternalUpdating = true;
    });

    connect(m_categoryModel, &QAbstractItemModel::modelReset, this, [this]() {
        QVariant varIds = m_categoryTree->property("expandedIds");
        QStringList expandedNames = m_categoryTree->property("expandedNames").toStringList();
        
        // 兼容性读取：同时支持 QList<int> 与 QVariantList 两种变体
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
            restoreExpandedState(QModelIndex(), expandedIds, expandedNames);
        }
        m_isRestoringState = false;
        m_isInternalUpdating = false;

        // 🚀 【完美时序】：代理模型展开状态处理完毕后，精确触发新节点编辑
        if (m_pendingEditCategoryId > 0) {
            int targetId = m_pendingEditCategoryId;
            m_pendingEditCategoryId = 0; // 及时重置
            selectCategory(targetId);
            QModelIndex proxyIdx = m_categoryTree->currentIndex();
            if (proxyIdx.isValid()) {
                m_categoryTree->edit(proxyIdx);
            }
        }
    });

    // 彻底重构点击事件。由于多选点击会触发多选改变信号（selectionChanged），点击事件仅承担锁屏验证拦截工作，杜绝信号二次激增造成的死锁
    connect(m_categoryTree, &QTreeView::clicked, this, [this](const QModelIndex& proxyIndex) {
        QModelIndex index = m_proxyModel->mapToSource(proxyIndex);
        QString type = index.data(TypeRole).toString();
        QString name = index.data(NameRole).toString();
        int id = index.data(IdRole).toInt();
        QString path = index.data(PathRole).toString();
        bool isEncrypted = index.data(EncryptedRole).toBool();

        // 2026-03-xx 物理防御：加密分类点击时直接进入，内容面板内置卡片接管验证
        if (isEncrypted && id > 0 && !m_unlockedIds.contains(id)) {
            emit categorySelected(id, name, type, path);
            return;
        }
    });

    connect(m_categoryTree, &DropTreeView::pathsDropped, this, [this](const QStringList& paths, const QModelIndex& proxyIndex) {
        QModelIndex index = m_proxyModel->mapToSource(proxyIndex);
        // 2026-06-xx 彻底重构：物理递归遍历 + 分类镜像创建 + SHA-256 物理加固
        // 🚨 修正：拖到空白处时，目标分类 ID 必须为 0（顶级根分类），绝不能是 -2！
        int targetCatId = 0; 

        if (index.isValid()) {
            QString type = index.data(TypeRole).toString();
            QString name = index.data(NameRole).toString();

            // 2026-06-xx 物理联动：拖拽到回收站
            if (type == "trash") {
                if (ShellHelper::moveToTrash(paths)) {
                    m_categoryModel->refresh();
                    MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::FullRebuild);
                }
                return;
            }

            // 拖到具体的子分类上 (ID > 0)
            if (type == "category" && index.data(IdRole).toInt() > 0) {
                targetCatId = index.data(IdRole).toInt();
            } else {
                targetCatId = 0; // 其余全部归为顶级分类 (0)
            }
        } else {
            targetCatId = 0; // 拖到空白处归为顶级分类 (0)
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
    });
    
    sbContentLayout->addWidget(m_categoryTree);
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
}

void CategoryPanel::saveExpandedStateToSettings() {
    // 1. 状态还原或内部更新中，绝不保存
    if (m_isRestoringState || m_isInternalUpdating) {
        return;
    }
    
    // 2. 模型为空或正在重置，绝不保存
    if (!m_categoryModel || m_categoryModel->rowCount() <= 0) {
        return;
    }

    // 3. 占位符防护
    if (m_categoryModel->rowCount() == 1) {
        QModelIndex first = m_categoryModel->index(0, 0);
        QString type = first.data(TypeRole).toString();
        if (type == "placeholder" || first.data(Qt::DisplayRole).toString().contains("正在统计")) {
            return;
        }
    }

    QSet<int> ids;
    QStringList names;
    saveExpandedState(QModelIndex(), ids, names);

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
}

void CategoryPanel::loadExpandedStateFromSettings() {
    bool hasRecord = !AppConfig::instance().getValue("Category/ExpandedIds").isNull() || 
                     !AppConfig::instance().getValue("Category/ExpandedNames").isNull();
    
    QVariantList idVarList = AppConfig::instance().getValue("Category/ExpandedIds").toList();
    QStringList names = AppConfig::instance().getValue("Category/ExpandedNames").toStringList();

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

    // 尝试立即恢复一次
    m_isRestoringState = true;
    {
        DataFlowGuard guard(m_isInternalUpdating);
        restoreExpandedState(QModelIndex(), ids, names);
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
        if (proxyIdx.isValid()) {
            m_categoryTree->expand(proxyIdx);
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
        if (obj == m_categoryTree && keyEvent->modifiers() == Qt::ControlModifier && keyEvent->key() == Qt::Key_A) {
            return true; 
        }

        // 2026-06-xx 按照用户要求：支持 Delete 键物理删除选中分类
        if (obj == m_categoryTree && keyEvent->key() == Qt::Key_Delete) {
            onDeleteCategory();
            return true;
        }

        // 2026-xx-xx 按照 Plan-63：按 F2 同步进入行内编辑状态
        if (obj == m_categoryTree && keyEvent->key() == Qt::Key_F2) {
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

} // namespace ArcMeta
