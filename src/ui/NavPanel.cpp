#include "NavPanel.h"
#include "UiHelper.h"
#include "TreeItemDelegate.h"
#include "DropTreeView.h"
#include "ContentPanel.h"
#include "../meta/AmMetaJson.h"
#include <QHeaderView>
#include <QScrollBar>
#include <QLabel>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QStandardPaths>
#include <QTimer>
#include <QPointer>
#include <QtConcurrent>
#include <QApplication>

namespace ArcMeta {

/**
 * @brief 构造函数，设置面板属性
 */
NavPanel::NavPanel(QWidget* parent)
    : QFrame(parent) {
    setObjectName("ListContainer");
    setAttribute(Qt::WA_StyledBackground, true);
    // 设置面板宽度（遵循文档：导航面板 230px）
    setMinimumWidth(230);
    
    // 核心修正：显式设置边框和背景，确保“物理切割感”
    setFrameShape(QFrame::StyledPanel);
    setLineWidth(1);
    setStyleSheet("#ListContainer { border: 1px solid #333333; background-color: #1E1E1E; color: #EEEEEE; }");

    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    initUi();
}

/**
 * @brief 初始化 UI 组件
 */
void NavPanel::deferredInit() {
    qDebug() << "[NavPanel] deferredInit 开始执行";
    if (m_model && m_model->rowCount() > 0) {
        qDebug() << "[NavPanel] 模型已存在数据，跳过重复初始化";
        return;
    }

    // 1. 桌面入口 (使用原生系统图标)
    QString desktopPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    QFileIconProvider provider;
    QIcon desktopIcon = provider.icon(QFileInfo(desktopPath));
    QStandardItem* desktopItem = new QStandardItem(desktopIcon, "桌面");
    desktopItem->setData(desktopPath, Qt::UserRole + 1);
    // 增加虚拟子项以便显示展开箭头
    desktopItem->appendRow(new QStandardItem("Loading..."));
    m_model->appendRow(desktopItem);

    // 2. 此电脑入口 (使用原生系统图标)
    QIcon computerIcon = provider.icon(QFileIconProvider::Computer);
    QStandardItem* computerItem = new QStandardItem(computerIcon, "此电脑");
    computerItem->setData("computer://", Qt::UserRole + 1);
    m_model->appendRow(computerItem);

    // 3. 磁盘列表 (逻辑异步预备：先填充基础文字路径)
    const auto drives = QDir::drives();
    for (const QFileInfo& drive : drives) {
        QString driveName = drive.absolutePath();
        QStandardItem* driveItem = new QStandardItem(driveName);
        driveItem->setData(driveName, Qt::UserRole + 1);
        driveItem->appendRow(new QStandardItem("Loading..."));
        m_model->appendRow(driveItem);
    }

    // 2026-03-xx 线程安全修复：图标提取必须在主线程执行。
    QTimer::singleShot(0, [this, drives]() {
        qDebug() << "[NavPanel] 开始异步填充磁盘图标 (回归原生)...";
        QFileIconProvider provider;
        for (int i = 0; i < drives.size(); ++i) {
            if (i + 2 < m_model->rowCount()) {
                QIcon driveIcon = provider.icon(drives[i]);
                m_model->item(i + 2)->setIcon(driveIcon);
            }
        }
        qDebug() << "[NavPanel] 磁盘图标填充完成";
    });
    qDebug() << "[NavPanel] deferredInit 同步部分执行完毕";
}

void NavPanel::setFocusHighlight(bool visible) {
    if (m_focusLine) m_focusLine->setVisible(visible);
}

void NavPanel::initUi() {
    // 2026-05-07 按照用户要求：修改焦点线颜色为蓝色
    m_focusLine = new QWidget(this);
    m_focusLine->setFixedHeight(1);
    m_focusLine->setStyleSheet("background-color: #007ACC;");
    m_focusLine->hide(); // 初始隐藏
    m_mainLayout->addWidget(m_focusLine);

    // 面板标题 (还原旧版架构：Layout + Icon + Text)
    QWidget* header = new QWidget(this);
    header->setObjectName("ContainerHeader");
    header->setFixedHeight(32);
    // 重新注入标题栏样式，确保背景色和边框还原
    header->setStyleSheet(
        "QWidget#ContainerHeader {"
        "  background-color: #252526;"
        "  border-bottom: 1px solid #333;"
        "}"
    );
    QHBoxLayout* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(15, 2, 15, 0); // 严格还原 15px 左右边距，顶部 2px 偏移以垂直居中
    headerLayout->setSpacing(8);

    QLabel* iconLabel = new QLabel(header);
    iconLabel->setPixmap(UiHelper::getIcon("list_ul", QColor("#2ecc71"), 18).pixmap(18, 18));
    headerLayout->addWidget(iconLabel);

    QLabel* titleLabel = new QLabel("目录导航", header);
    titleLabel->setStyleSheet("color: #2ecc71; font-size: 13px; font-weight: bold; background: transparent; border: none;");
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();

    m_mainLayout->addWidget(header);

    // 核心修正：为列表内容包裹容器，恢复旧版 (15, 8, 15, 8) 的呼吸边距
    QWidget* contentWrapper = new QWidget(this);
    contentWrapper->setStyleSheet("background: transparent; border: none;");
    QVBoxLayout* contentLayout = new QVBoxLayout(contentWrapper);
    contentLayout->setContentsMargins(15, 8, 15, 8);
    contentLayout->setSpacing(0);

    // 物理还原：使用自定义视图以支持无快照拖拽
    m_treeView = new DropTreeView(this);
    m_treeView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_treeView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_treeView->setHeaderHidden(true);
    m_treeView->setAnimated(true);
    
    // 物理还原：20px 缩进以对齐三角形图标
    m_treeView->setIndentation(20);
    
    // 物理修正：禁用编辑触发，防止双击重命名
    m_treeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    
    // 物理还原：双击锁定为伸展或折叠
    m_treeView->setExpandsOnDoubleClick(true);
    
    // 增强：开启拖拽收藏功能
    m_treeView->setDragEnabled(true);
    m_treeView->setDragDropMode(QAbstractItemView::DragOnly);
    
    // 按照用户要求：目录导航中的录入/置顶标记不再需要显示
    m_treeView->setItemDelegate(new TreeItemDelegate(this, false));

    m_model = new QStandardItemModel(this);
    m_treeView->setModel(m_model);
    connect(m_treeView, &QTreeView::expanded, this, &NavPanel::onItemExpanded);

    // 树形控件样式美化
    // 2026-03-xx 按照用户要求：同步左侧“数据分类”样式，为三角形图标添加 padding 以实现清秀感，杜绝粗大感
    QString arrowRight = UiHelper::getSvgTempFilePath("arrow_right", QColor("#3498db"));
    QString arrowDown  = UiHelper::getSvgTempFilePath("arrow_down",  QColor("#3498db"));

    m_treeView->setStyleSheet(QString(
        "QTreeView { background-color: transparent; border: none; font-size: 12px; outline: none; }"
        "QTreeView::item { height: 28px; padding-left: 0px; color: #EEEEEE; }"
        
        "QTreeView::branch { width: 20px; }"
        "QTreeView::branch:has-children:closed { image: url(\"%1\"); }"
        "QTreeView::branch:has-children:open   { image: url(\"%2\"); }"
        "QTreeView::branch:has-children:closed:has-siblings { image: url(\"%1\"); }"
        "QTreeView::branch:has-children:open:has-siblings   { image: url(\"%2\"); }"
    ).arg(arrowRight, arrowDown));


    connect(m_treeView, &QTreeView::clicked, this, &NavPanel::onTreeClicked);

    contentLayout->addWidget(m_treeView);
    m_mainLayout->addWidget(contentWrapper, 1);
}

/**
 * @brief 设置当前显示的根路径并自动展开
 */
void NavPanel::setRootPath(const QString& path) {
    Q_UNUSED(path);
    // 由于改为扁平化快捷入口列表，不再支持 setRootPath 的树深度同步
}

void NavPanel::selectPath(const QString& path) {
    for (int i = 0; i < m_model->rowCount(); ++i) {
        QStandardItem* item = m_model->item(i);
        if (item->data(Qt::UserRole + 1).toString() == path) {
            m_treeView->setCurrentIndex(item->index());
            m_treeView->setFocus();
            break;
        }
    }
}

/**
 * @brief 当用户点击目录时，发出信号告知外部组件（如内容面板）
 */
void NavPanel::onTreeClicked(const QModelIndex& index) {
    QString path = index.data(Qt::UserRole + 1).toString();
    if (!path.isEmpty() && path != "computer://") {
        emit directorySelected(path);
    } else if (path == "computer://") {
        emit directorySelected("computer://");
    }
}

void NavPanel::onItemExpanded(const QModelIndex& index) {
    QStandardItem* item = m_model->itemFromIndex(index);
    if (!item) return;

    // 如果只有一个 Loading 子项，则触发真实加载
    if (item->rowCount() == 1 && item->child(0)->text() == "Loading...") {
        fetchChildDirs(item);
    }
}

/**
 * @brief 异步获取子目录，解决展开文件夹时的界面假死 (2026-05-25 物理加速)
 */
void NavPanel::fetchChildDirs(QStandardItem* parent) {
    QString path = parent->data(Qt::UserRole + 1).toString();
    if (path.isEmpty() || path == "computer://") return;

    parent->removeRows(0, parent->rowCount());
    parent->appendRow(new QStandardItem("正在读取..."));

    // 2026-05-25 编译修复：QStandardItem 不继承自 QObject，严禁使用 QPointer。
    // 改用 QPersistentModelIndex 确保异步回调时索引的有效性。
    QPersistentModelIndex pIdx(parent->index());
    (void)QtConcurrent::run([this, pIdx, path]() {
        QDir dir(path);
        // 执行耗时的物理磁盘读取
        QFileInfoList list = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        
        struct DirInfo { QString name; QString absPath; bool hasSub; };
        QList<DirInfo> results;
        for (const QFileInfo& info : list) {
            QDir subDir(info.absoluteFilePath());
            bool hasSub = !subDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty();
            results << DirInfo{info.fileName(), info.absoluteFilePath(), hasSub};
        }

        // 2026-06-xx 按照视觉要求：检测目录下是否存在 .am_meta.json 以点亮 UI
        bool hasJson = QFileInfo(path + "/.am_meta.json").exists();

        // 投递回主线程进行 UI 更新
        QMetaObject::invokeMethod(qApp, [this, pIdx, results, hasJson]() {
            if (!pIdx.isValid()) return;
            QStandardItem* safeParent = m_model->itemFromIndex(pIdx);
            if (!safeParent) return;

            safeParent->removeRows(0, safeParent->rowCount());
            
            for (const auto& info : results) {
                // 2026-06-xx 物理修复：在导航面板应用“受控状态”视觉
                // 逻辑：基于本地 JSON 判定录入状态与置顶状态
                bool isManaged = false;
                bool isPinned = false;
                if (hasJson) {
                    AmMetaJson amJson(QFileInfo(info.absPath).absolutePath().toStdWString());
                    if (amJson.load()) {
                        auto& items = amJson.items();
                        std::wstring wName = info.name.toStdWString();
                        if (items.count(wName)) {
                            isManaged = true;
                            isPinned = items.at(wName).pinned;
                        }
                    }
                }

                QIcon folderIcon = UiHelper::getFileIcon(info.absPath, 18);
                QStandardItem* child = new QStandardItem(folderIcon, info.name);
                child->setData(info.absPath, Qt::UserRole + 1);
                
                // 2026-06-xx 按照要求：注入状态角色。
                // InDatabaseRole 用于对勾逻辑，IsLockedRole 用于置顶逻辑（两者在 Delegate 互斥）
                // 物理修复：校准作用域，ItemRole 位于 ArcMeta 空间而非 ContentPanel 类
                child->setData(isManaged, InDatabaseRole);
                child->setData(isPinned, IsLockedRole);

                if (info.hasSub) {
                    child->appendRow(new QStandardItem("Loading..."));
                }
                safeParent->appendRow(child);
            }
        }, Qt::QueuedConnection);
    });
}

} // namespace ArcMeta
