#include "TagManagerView.h"
#include "UiHelper.h"
#include "StyleLibrary.h"
#include "../meta/MetadataManager.h"
#include "../meta/DatabaseManager.h"
#include "sqlite3.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QFrame>
#include <QEvent>
#include <QContextMenuEvent>
#include <QGridLayout>
#include <QLineEdit>
#include <QInputDialog>

using namespace ArcMeta::Style;

namespace ArcMeta {

TagManagerView::TagManagerView(QWidget* parent) : QWidget(parent) {
    initUi();
}

void TagManagerView::initUi() {
    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setHandleWidth(1);
    m_splitter->setStyleSheet("QSplitter::handle { background-color: #333; }");

    setupSidebar();
    setupContentArea();

    m_splitter->addWidget(m_sidebar);
    m_splitter->addWidget(m_scrollArea);

    // 设置初始宽度
    m_splitter->setSizes({230, 1000});
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);

    mainLayout->addWidget(m_splitter);
}

void TagManagerView::setupSidebar() {
    m_sidebar = new QWidget(this);
    m_sidebar->setObjectName("TagSidebar");
    m_sidebar->setFixedWidth(230);
    m_sidebar->setStyleSheet("QWidget#TagSidebar { background-color: #1E1E1E; border-right: 1px solid #333; }");

    m_sidebarLayout = new QVBoxLayout(m_sidebar);
    m_sidebarLayout->setContentsMargins(0, 0, 0, 0);
    m_sidebarLayout->setSpacing(0);

    // 1. 顶部标题栏
    QWidget* header = new QWidget(m_sidebar);
    header->setFixedHeight(32);
    header->setStyleSheet("background-color: #252526; border-bottom: 1px solid #333;");
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(15, 0, 5, 0);

    QLabel* iconLabel = new QLabel(header);
    iconLabel->setPixmap(UiHelper::getIcon("tag_filled", PrimaryBlue, 18).pixmap(18, 18));
    headerLayout->addWidget(iconLabel);

    QLabel* titleLabel = new QLabel("标签管理", header);
    titleLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #3498db;");
    headerLayout->addWidget(titleLabel);

    m_tagCountLabel = new QLabel("(0)", header);
    m_tagCountLabel->setStyleSheet("color: #888; font-size: 11px;");
    headerLayout->addWidget(m_tagCountLabel);
    headerLayout->addStretch();

    m_sidebarLayout->addWidget(header);

    // 2. 侧边栏列表内容 (简化版)
    auto createSidebarItem = [this](const QString& icon, const QString& name, const QString& countText) {
        QWidget* item = new QWidget(m_sidebar);
        item->setFixedHeight(32);
        item->setCursor(Qt::PointingHandCursor);
        auto* layout = new QHBoxLayout(item);
        layout->setContentsMargins(15, 0, 15, 0);

        QLabel* iconLabel = new QLabel(item);
        iconLabel->setPixmap(UiHelper::getIcon(icon, TextDim, 16).pixmap(16, 16));
        layout->addWidget(iconLabel);

        QLabel* nameLabel = new QLabel(name, item);
        nameLabel->setStyleSheet("color: #CCC; font-size: 12px;");
        layout->addWidget(nameLabel);
        layout->addStretch();

        QLabel* countLabel = new QLabel(countText, item);
        countLabel->setStyleSheet("color: #666; font-size: 11px;");
        layout->addWidget(countLabel);

        item->setStyleSheet("QWidget:hover { background-color: #2A2A2A; }");
        return item;
    };

    auto* btnAll = createSidebarItem("all_data", "全部", "0");
    auto* btnUncategorized = createSidebarItem("uncategorized", "未分类", "0");
    auto* btnFrequent = createSidebarItem("star_filled", "常用标签", "0");

    m_sidebarLayout->addWidget(btnAll);
    m_sidebarLayout->addWidget(btnUncategorized);
    m_sidebarLayout->addWidget(btnFrequent);

    QFrame* line = new QFrame(m_sidebar);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet("background-color: #333; margin: 10px 15px;");
    m_sidebarLayout->addWidget(line);

    // 2026-07-xx 按照用户要求：标签组动态容器
    m_groupContainer = new QWidget(m_sidebar);
    auto* groupLayout = new QVBoxLayout(m_groupContainer);
    groupLayout->setContentsMargins(0, 0, 0, 0);
    groupLayout->setSpacing(0);
    m_sidebarLayout->addWidget(m_groupContainer);

    m_sidebarLayout->addStretch();

    // 底部新建按钮
    QPushButton* btnNewGroup = new QPushButton("+ 新建标签组", m_sidebar);
    btnNewGroup->setFixedHeight(40);
    btnNewGroup->setStyleSheet(
        "QPushButton { background: transparent; border: none; color: #888; text-align: left; padding-left: 15px; }"
        "QPushButton:hover { color: #3498db; background: #2A2A2A; }"
    );
    m_sidebarLayout->addWidget(btnNewGroup);
}

void TagManagerView::setupContentArea() {
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setStyleSheet("QScrollArea { border: none; background-color: #1E1E1E; }");

    m_contentWidget = new QWidget();
    m_contentWidget->setObjectName("TagContent");
    m_contentWidget->setStyleSheet("QWidget#TagContent { background-color: #1E1E1E; }");

    auto* contentLayout = new QVBoxLayout(m_contentWidget);
    contentLayout->setContentsMargins(20, 20, 20, 20);
    contentLayout->setSpacing(20);

    m_scrollArea->setWidget(m_contentWidget);
}

void TagManagerView::refresh() {
    m_tagCounts = MetadataManager::instance().getAllTags();

    // 2026-07-xx 物理同步：加载标签组数据
    m_tagGroups.clear();
    sqlite3* db = DatabaseManager::instance().getMemoryDb(L"C"); // 默认系统分库
    if (db) {
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, "SELECT id, name, color FROM tag_groups ORDER BY sort_order ASC", -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                TagGroup tg;
                tg.id = sqlite3_column_int(stmt, 0);
                tg.name = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 1));
                tg.color = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 2));

                // 查询关联标签
                sqlite3_stmt* itemStmt;
                if (sqlite3_prepare_v2(db, "SELECT tag_name FROM tag_group_items WHERE group_id = ?", -1, &itemStmt, nullptr) == SQLITE_OK) {
                    sqlite3_bind_int(itemStmt, 1, tg.id);
                    while (sqlite3_step(itemStmt) == SQLITE_ROW) {
                        tg.tags << QString::fromUtf8((const char*)sqlite3_column_text(itemStmt, 0));
                    }
                    sqlite3_finalize(itemStmt);
                }
                m_tagGroups.append(tg);
            }
            sqlite3_finalize(stmt);
        }
    }

    // 更新侧边栏计数
    m_tagCountLabel->setText(QString("(%1)").arg(m_tagCounts.size()));

    // 2026-07-xx 按照用户要求：更新侧边栏标签组列表
    if (m_groupContainer) {
        QLayout* layout = m_groupContainer->layout();
        QLayoutItem* item;
        while ((item = layout->takeAt(0)) != nullptr) {
            delete item->widget();
            delete item;
        }

        auto createSidebarItem = [this](const QString& icon, const QString& name, const QString& countText) {
            QWidget* item = new QWidget();
            item->setFixedHeight(32);
            item->setCursor(Qt::PointingHandCursor);
            auto* layout = new QHBoxLayout(item);
            layout->setContentsMargins(15, 0, 15, 0);

            QLabel* iconLabel = new QLabel(item);
            iconLabel->setPixmap(UiHelper::getIcon(icon, TextDim, 16).pixmap(16, 16));
            layout->addWidget(iconLabel);

            QLabel* nameLabel = new QLabel(name, item);
            nameLabel->setStyleSheet("color: #CCC; font-size: 12px;");
            layout->addWidget(nameLabel);
            layout->addStretch();

            QLabel* countLabel = new QLabel(countText, item);
            countLabel->setStyleSheet("color: #666; font-size: 11px;");
            layout->addWidget(countLabel);

            item->setStyleSheet("QWidget:hover { background-color: #2A2A2A; }");
            return item;
        };

        for (const auto& group : m_tagGroups) {
            layout->addWidget(createSidebarItem("folder_filled", group.name, QString::number(group.tags.size())));
        }
    }

    // 重建内容区
    // 清理旧布局
    QLayout* oldLayout = m_contentWidget->layout();
    if (oldLayout) {
        QLayoutItem* item;
        while ((item = oldLayout->takeAt(0)) != nullptr) {
            delete item->widget();
            delete item;
        }
    } else {
        oldLayout = new QVBoxLayout(m_contentWidget);
        m_contentWidget->setLayout(oldLayout);
    }

    auto* contentLayout = static_cast<QVBoxLayout*>(oldLayout);

    // 按字母分组
    QMap<QChar, QMap<QString, int>> groups;
    for (auto it = m_tagCounts.begin(); it != m_tagCounts.end(); ++it) {
        QString tag = it.key();
        QChar first = tag.at(0).toUpper();
        if (first < 'A' || first > 'Z') first = '#';
        groups[first][tag] = it.value();
    }

    for (auto it = groups.begin(); it != groups.end(); ++it) {
        QWidget* groupWidget = new QWidget(m_contentWidget);
        auto* groupLayout = new QVBoxLayout(groupWidget);
        groupLayout->setContentsMargins(0, 0, 0, 0);
        groupLayout->setSpacing(10);

        QLabel* groupTitle = new QLabel(QString(it.key()), groupWidget);
        groupTitle->setStyleSheet("font-size: 18px; font-weight: bold; color: #555; border-bottom: 1px solid #333; padding-bottom: 5px;");
        groupLayout->addWidget(groupTitle);

        // 使用网格布局模拟分栏
        QWidget* tagsContainer = new QWidget(groupWidget);
        auto* grid = new QGridLayout(tagsContainer);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setSpacing(15);

        int row = 0, col = 0;
        int maxCols = 4;

        auto tagsInGroup = it.value();
        for (auto tagIt = tagsInGroup.begin(); tagIt != tagsInGroup.end(); ++tagIt) {
            QPushButton* tagBtn = new QPushButton(QString("%1 (%2)").arg(tagIt.key()).arg(tagIt.value()), tagsContainer);
            tagBtn->setCursor(Qt::PointingHandCursor);
            tagBtn->setStyleSheet(
                "QPushButton { background: transparent; border: none; color: #AAA; text-align: left; font-size: 13px; }"
                "QPushButton:hover { color: #3498db; text-decoration: underline; }"
            );

            QString tagName = tagIt.key();
            connect(tagBtn, &QPushButton::clicked, this, [this, tagName]() {
                emit requestSearchTag(tagName);
            });

            // 右键菜单
            tagBtn->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(tagBtn, &QWidget::customContextMenuRequested, this, [this, tagName](const QPoint& pos) {
                QMenu menu(this);
                UiHelper::applyMenuStyle(&menu);
                menu.addAction("搜索含此标签的项目", this, [this, tagName](){ emit requestSearchTag(tagName); });
                menu.addSeparator();
                menu.addAction("设为常用标签");

                auto* addToMenu = menu.addMenu("加入标签组");
                for (const auto& group : m_tagGroups) {
                    addToMenu->addAction(group.name);
                }

                auto* moveToMenu = menu.addMenu("移动至标签组");
                for (const auto& group : m_tagGroups) {
                    moveToMenu->addAction(group.name);
                }

                menu.addAction("从标签组中移除");
                menu.addSeparator();
                menu.addAction("重命名");
                menu.addAction("删除标签");
                menu.exec(QCursor::pos());
            });

            grid->addWidget(tagBtn, row, col);
            col++;
            if (col >= maxCols) {
                col = 0;
                row++;
            }
        }

        groupLayout->addWidget(tagsContainer);
        contentLayout->addWidget(groupWidget);
    }

    contentLayout->addStretch();
}

QStringList TagManagerView::getFrequentTags() const {
    // 2026-07-xx 示例逻辑：暂时返回空，后续可从 AppConfig 读取
    return {};
}

bool TagManagerView::eventFilter(QObject* obj, QEvent* event) {
    return QWidget::eventFilter(obj, event);
}

} // namespace ArcMeta
