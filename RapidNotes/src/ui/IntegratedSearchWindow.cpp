#include "IntegratedSearchWindow.h"
#include "FileSearchWidget.h"
#include "KeywordSearchWidget.h"
#include "IconHelper.h"
#include "ResizeHandle.h"
#include "ToolTipOverlay.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabBar>
#include <QLabel>
#include <QPushButton>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QMenu>
#include <QFileDialog>
#include <QApplication>
#include <QDateTime>

IntegratedSearchWindow::IntegratedSearchWindow(QWidget* parent)
    : FramelessDialog("搜索中心", parent)
{
    setObjectName("IntegratedSearchWindow");
    loadWindowSettings();
    resize(1200, 750);
    setupStyles();
    initUI();
    loadFavorites();
    loadCollection();
    m_resizeHandle = new ResizeHandle(this, this);
}

IntegratedSearchWindow::~IntegratedSearchWindow() {}

void IntegratedSearchWindow::setupStyles() {
    setStyleSheet(R"(
        #IntegratedSearchWindow { background-color: #1E1E1E; }
        QTabWidget::pane { border: none; background-color: #1E1E1E; }
        QTabBar::tab {
            background-color: transparent;
            color: #888888;
            padding: 12px 24px;
            margin-right: 15px;
            font-size: 14px;
            border-bottom: 3px solid transparent;
        }
        QTabBar::tab:selected {
            color: #007ACC;
            border-bottom: 3px solid #007ACC;
            font-weight: bold;
        }
        QTabBar::tab:hover { color: #CCCCCC; }
        QListWidget {
            background-color: #252526; border: 1px solid #333; border-radius: 6px; padding: 4px; color: #CCC;
        }
        QListWidget::item { min-height: 28px; padding-left: 10px; border-radius: 4px; }
        QListWidget::item:selected { background-color: #3e3e42; border-left: 4px solid #007ACC; color: #FFF; } // 2026-03-xx 统一选中色
        #SideButton {
            background-color: #2D2D30; border: 1px solid #444; color: #AAA; border-radius: 4px; font-size: 12px;
        }
        #SideButton:hover { background-color: #3e3e42; color: #FFF; border-color: #666; } // 2026-03-xx 统一悬停色
    )");
}

void IntegratedSearchWindow::initUI() {
    auto* mainLayout = new QHBoxLayout(m_contentArea);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(0);

    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setHandleWidth(1);
    mainLayout->addWidget(splitter);

    // 左侧
    auto* leftSide = new QWidget();
    auto* leftLayout = new QVBoxLayout(leftSide);
    leftLayout->setContentsMargins(0, 0, 15, 0);
    leftLayout->setSpacing(10);
    auto* leftHeader = new QHBoxLayout();
    auto* leftIcon = new QLabel(); leftIcon->setPixmap(IconHelper::getIcon("folder", "#007ACC").pixmap(16, 16));
    leftHeader->addWidget(leftIcon);
    auto* leftTitle = new QLabel("收藏夹 (可拖入)");
    leftTitle->setStyleSheet("color: #888; font-weight: bold; font-size: 12px;");
    leftHeader->addWidget(leftTitle);
    leftHeader->addStretch();
    leftLayout->addLayout(leftHeader);
    m_sidebar = new QListWidget();
    m_sidebar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_sidebar, &QListWidget::itemClicked, this, &IntegratedSearchWindow::onSidebarItemClicked);
    connect(m_sidebar, &QListWidget::customContextMenuRequested, this, &IntegratedSearchWindow::showSidebarContextMenu);
    leftLayout->addWidget(m_sidebar);
    auto* btnAddFav = new QPushButton("收藏当前路径"); btnAddFav->setObjectName("SideButton"); btnAddFav->setFixedHeight(34);
    leftLayout->addWidget(btnAddFav);
    splitter->addWidget(leftSide);

    // 中间
    auto* center = new QWidget();
    auto* centerLayout = new QVBoxLayout(center);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    m_tabWidget = new QTabWidget();
    m_tabWidget->setDocumentMode(true);
    m_fileSearchWidget = new FileSearchWidget();
    m_keywordSearchWidget = new KeywordSearchWidget();
    m_tabWidget->addTab(m_fileSearchWidget, IconHelper::getIcon("folder", "#007ACC", 18), QString("文件查找"));
    m_tabWidget->addTab(m_keywordSearchWidget, IconHelper::getIcon("find_keyword", "#007ACC", 18), QString("关键字查找"));
    centerLayout->addWidget(m_tabWidget);
    splitter->addWidget(center);

    // 右侧
    auto* rightSide = new QWidget();
    auto* rightLayout = new QVBoxLayout(rightSide);
    rightLayout->setContentsMargins(15, 0, 0, 0);
    rightLayout->setSpacing(10);
    auto* rightHeader = new QHBoxLayout();
    auto* rightIcon = new QLabel(); rightIcon->setPixmap(IconHelper::getIcon("file", "#007ACC").pixmap(16, 16));
    rightHeader->addWidget(rightIcon);
    auto* rightTitle = new QLabel("文件收藏 (可多选/拖入)");
    rightTitle->setStyleSheet("color: #888; font-weight: bold; font-size: 12px;");
    rightHeader->addWidget(rightTitle);
    rightHeader->addStretch();
    rightLayout->addLayout(rightHeader);
    m_collectionSidebar = new QListWidget();
    m_collectionSidebar->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_collectionSidebar->setContextMenuPolicy(Qt::CustomContextMenu);
    rightLayout->addWidget(m_collectionSidebar);
    auto* btnMerge = new QPushButton("合并收藏内容"); btnMerge->setObjectName("SideButton"); btnMerge->setFixedHeight(34);
    connect(btnMerge, &QPushButton::clicked, this, &IntegratedSearchWindow::onMergeCollectionFiles);
    rightLayout->addWidget(btnMerge);
    splitter->addWidget(rightSide);

    splitter->setStretchFactor(0, 0); splitter->setStretchFactor(1, 1); splitter->setStretchFactor(2, 0);
}

void IntegratedSearchWindow::setCurrentTab(SearchType type) { m_tabWidget->setCurrentIndex(static_cast<int>(type)); }
void IntegratedSearchWindow::resizeEvent(QResizeEvent* event) {
    FramelessDialog::resizeEvent(event);
    if (m_resizeHandle) m_resizeHandle->move(width() - 20, height() - 20);
}
void IntegratedSearchWindow::onSidebarItemClicked(QListWidgetItem* item) {
    if (!item) return;
    QString p = item->data(Qt::UserRole).toString();
    m_fileSearchWidget->setSearchPath(p);
    m_keywordSearchWidget->setSearchPath(p);
}
void IntegratedSearchWindow::showSidebarContextMenu(const QPoint& pos) {
    QListWidgetItem* item = m_sidebar->itemAt(pos); if (!item) return;
    QMenu menu(this);
    menu.addAction("取消收藏", [this, item](){ delete m_sidebar->takeItem(m_sidebar->row(item)); saveFavorites(); });
    menu.exec(m_sidebar->mapToGlobal(pos));
}
void IntegratedSearchWindow::addFavorite(const QString& path) {
    auto* item = new QListWidgetItem(IconHelper::getIcon("folder", "#F1C40F"), QFileInfo(path).fileName());
    item->setData(Qt::UserRole, path); m_sidebar->addItem(item);
}
void IntegratedSearchWindow::addCollectionItem(const QString& path) {
    auto* item = new QListWidgetItem(IconHelper::getIcon("file", "#2ECC71"), QFileInfo(path).fileName());
    item->setData(Qt::UserRole, path); m_collectionSidebar->addItem(item);
}
void IntegratedSearchWindow::loadFavorites() {}
void IntegratedSearchWindow::saveFavorites() {}
void IntegratedSearchWindow::loadCollection() {}
void IntegratedSearchWindow::saveCollection() {}
void IntegratedSearchWindow::onMergeCollectionFiles() {}
void IntegratedSearchWindow::onMergeFiles(const QStringList&, const QString&, bool) {}
