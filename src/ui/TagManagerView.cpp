#include "TagManagerView.h"
#include "UiHelper.h"
#include "StyleLibrary.h"
#include "MetaPanel.h"
#include "../meta/MetadataManager.h"
#include "../meta/DatabaseManager.h"
#include "sqlite3.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QFrame>
#include <QGridLayout>
#include <QMenu>
#include <QDebug>

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
    m_splitter->setHandleWidth(5);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setStyleSheet(QString(
        "QSplitter { background: transparent; border: none; }"
        "QSplitter::handle { background-color: %1; width: 5px; }"
        "QSplitter::handle:hover { background-color: %2; }"
    ).arg(qssColor(BackgroundDeep)).arg(qssColor(BackgroundHover)));

    setupSidebar();
    setupContentArea();

    m_splitter->addWidget(m_sidebar);
    m_splitter->addWidget(m_contentContainer);

    // 侧边栏固定 230px
    m_splitter->setSizes({230, 1000});
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);

    mainLayout->addWidget(m_splitter);
}

QWidget* TagManagerView::createSidebarItem(const QString& icon, const QString& name, const QString& countText, QLabel** outCountLabel) {
    QWidget* item = new QWidget();
    item->setFixedHeight(26);
    item->setCursor(Qt::PointingHandCursor);

    auto* layout = new QHBoxLayout(item);
    layout->setContentsMargins(0, 1, 0, 1);
    layout->setSpacing(0);

    QWidget* inner = new QWidget(item);
    inner->setObjectName("SidebarItemInner");
    inner->setStyleSheet(
        "QWidget#SidebarItemInner { background: transparent; border-radius: 4px; }"
        "QWidget#SidebarItemInner:hover { background-color: #2a2d2e; }"
    );
    auto* innerLayout = new QHBoxLayout(inner);
    innerLayout->setContentsMargins(0, 0, 15, 0);
    innerLayout->setSpacing(6);

    QLabel* iconLabel = new QLabel(inner);
    iconLabel->setPixmap(UiHelper::getIcon(icon, TextDim, 16).pixmap(16, 16));
    innerLayout->addWidget(iconLabel);

    QLabel* nameLabel = new QLabel(name, inner);
    nameLabel->setStyleSheet("color: #CCC; font-size: 12px; background: transparent;");
    innerLayout->addWidget(nameLabel);
    innerLayout->addStretch();

    QLabel* countLabel = new QLabel(countText, inner);
    countLabel->setStyleSheet("color: #666; font-size: 11px; background: transparent;");
    innerLayout->addWidget(countLabel);
    if (outCountLabel) *outCountLabel = countLabel;

    layout->addWidget(inner);
    return item;
}

void TagManagerView::setupSidebar() {
    m_sidebar = new QFrame(this);
    m_sidebar->setObjectName("ListContainer"); // 物理对标：复用导航栏样式 ID
    m_sidebar->setFixedWidth(230);
    m_sidebar->setAttribute(Qt::WA_StyledBackground, true);

    m_sidebarLayout = new QVBoxLayout(m_sidebar);
    m_sidebarLayout->setContentsMargins(0, 0, 0, 0);
    m_sidebarLayout->setSpacing(0);

    // 标题栏
    QWidget* header = new QWidget(m_sidebar);
    header->setObjectName("ContainerHeader");
    header->setFixedHeight(32);
    header->setAttribute(Qt::WA_StyledBackground, true);
    header->setStyleSheet(
        "QWidget#ContainerHeader {"
        "  background-color: #252526;"
        "  border-bottom: 1px solid #333;"
        "}"
    );
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(15, 0, 5, 0);

    QLabel* iconLabel = new QLabel(header);
    iconLabel->setPixmap(UiHelper::getIcon("tag_filled", QColor("#1abc9c"), 18).pixmap(18, 18));
    headerLayout->addWidget(iconLabel);

    QLabel* titleLabel = new QLabel("标签管理", header);
    titleLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #1abc9c;");
    headerLayout->addWidget(titleLabel);

    headerLayout->addStretch();

    m_sidebarLayout->addWidget(header);

    // 2026-07-xx 按照用户要求：为列表内容包裹容器，恢复旧版 (15, 8, 0, 8) 的呼吸边距
    QWidget* contentWrapper = new QWidget(m_sidebar);
    contentWrapper->setStyleSheet("background: transparent; border: none;");
    QVBoxLayout* sidebarContentLayout = new QVBoxLayout(contentWrapper);
    sidebarContentLayout->setContentsMargins(15, 8, 0, 8);
    sidebarContentLayout->setSpacing(0);

    // 静态项
    sidebarContentLayout->addWidget(createSidebarItem("all_data", "全部标签", "0", &m_allTagsCountLabel));
    sidebarContentLayout->addWidget(createSidebarItem("uncategorized", "未分类", "0", &m_uncategorizedTagsCountLabel));
    sidebarContentLayout->addWidget(createSidebarItem("star_filled", "常用标签", "0", &m_frequentTagsCountLabel));

    QFrame* line = new QFrame(contentWrapper);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet("background-color: #333; margin: 10px 15px;");
    sidebarContentLayout->addWidget(line);

    // 标签组容器
    m_groupContainer = new QWidget(contentWrapper);
    auto* groupLayout = new QVBoxLayout(m_groupContainer);
    groupLayout->setContentsMargins(0, 0, 0, 0);
    groupLayout->setSpacing(0);
    sidebarContentLayout->addWidget(m_groupContainer);

    sidebarContentLayout->addStretch();

    m_sidebarLayout->addWidget(contentWrapper, 1);

    QPushButton* btnNewGroup = new QPushButton("+ 新建标签组", m_sidebar);
    btnNewGroup->setFixedHeight(40);
    btnNewGroup->setStyleSheet(
        "QPushButton { background: transparent; border: none; color: #888; text-align: left; padding-left: 15px; }"
        "QPushButton:hover { color: #3498db; background: #2A2A2A; }"
    );
    m_sidebarLayout->addWidget(btnNewGroup);
}

void TagManagerView::setupContentArea() {
    m_contentContainer = new QFrame(this);
    m_contentContainer->setObjectName("EditorContainer"); // 物理对标：复用内容面板 ID
    m_contentContainer->setAttribute(Qt::WA_StyledBackground, true);
    auto* mainL = new QVBoxLayout(m_contentContainer);
    mainL->setContentsMargins(0, 0, 0, 0);
    mainL->setSpacing(0);

    // 1. 标题栏 (物理对齐 ContentPanel)
    QWidget* titleBar = new QWidget(m_contentContainer);
    titleBar->setObjectName("ContainerHeader");
    titleBar->setFixedHeight(32);
    titleBar->setAttribute(Qt::WA_StyledBackground, true);
    titleBar->setStyleSheet(
        "QWidget#ContainerHeader {"
        "  background-color: #252526;"
        "  border-bottom: 1px solid #333;"
        "}"
    );
    QHBoxLayout* titleL = new QHBoxLayout(titleBar);
    titleL->setContentsMargins(15, 0, 5, 0);
    titleL->setSpacing(5);

    QLabel* iconLabel = new QLabel(titleBar);
    iconLabel->setPixmap(UiHelper::getIcon("tag", QColor("#1abc9c"), 18).pixmap(18, 18));
    titleL->addWidget(iconLabel);

    m_contentTitleLabel = new QLabel("标签", titleBar);
    m_contentTitleLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #1abc9c; background: transparent; border: none;");
    titleL->addWidget(m_contentTitleLabel);
    titleL->addStretch();

    mainL->addWidget(titleBar);

    // 2. 滚动区
    m_scrollArea = new QScrollArea(m_contentContainer);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setStyleSheet("QScrollArea { border: none; background-color: transparent; }");

    m_contentWidget = new QWidget();
    m_contentWidget->setObjectName("TagContentContainer");
    m_contentWidget->setStyleSheet("QWidget#TagContentContainer { background-color: transparent; }");

    auto* contentLayout = new QVBoxLayout(m_contentWidget);
    contentLayout->setContentsMargins(20, 20, 20, 20);
    contentLayout->setSpacing(20);

    m_scrollArea->setWidget(m_contentWidget);
    mainL->addWidget(m_scrollArea, 1);
}

void TagManagerView::refresh() {
    m_tagCounts = MetadataManager::instance().getAllTags();

    // 加载标签组
    m_tagGroups.clear();
    sqlite3* db = DatabaseManager::instance().getMemoryDb(L"C");
    if (db) {
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, "SELECT id, name, color FROM tag_groups ORDER BY sort_order ASC", -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                TagGroup tg;
                tg.id = sqlite3_column_int(stmt, 0);
                tg.name = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 1));
                tg.color = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 2));

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

    // 更新侧边栏
    int allCount = m_tagCounts.size();
    if (m_allTagsCountLabel) m_allTagsCountLabel->setText(QString::number(allCount));

    // 统计未分类 (此处逻辑：不在任何 TagGroup 中的标签)
    QSet<QString> groupedTags;
    for (const auto& group : m_tagGroups) {
        for (const auto& tag : group.tags) groupedTags.insert(tag);
    }
    int uncategorizedCount = 0;
    for (auto it = m_tagCounts.begin(); it != m_tagCounts.end(); ++it) {
        if (!groupedTags.contains(it.key())) uncategorizedCount++;
    }
    if (m_uncategorizedTagsCountLabel) m_uncategorizedTagsCountLabel->setText(QString::number(uncategorizedCount));

    QVBoxLayout* groupLayout = qobject_cast<QVBoxLayout*>(m_groupContainer->layout());
    QLayoutItem* child;
    if (groupLayout) {
        while ((child = groupLayout->takeAt(0)) != nullptr) {
            delete child->widget();
            delete child;
        }
        for (const auto& group : m_tagGroups) {
            auto* item = createSidebarItem("folder_filled", group.name, QString::number(group.tags.size()));
            groupLayout->addWidget(item);
        }
    }

    // 重建内容区
    QVBoxLayout* contentLayout = qobject_cast<QVBoxLayout*>(m_contentWidget->layout());
    if (contentLayout) {
        while ((child = contentLayout->takeAt(0)) != nullptr) {
            delete child->widget();
            delete child;
        }
        // 物理对标：移除旧式内联标题，改用 top titleBar
    }

    // 按字母分组
    QMap<QChar, QMap<QString, int>> groups;
    for (auto it = m_tagCounts.begin(); it != m_tagCounts.end(); ++it) {
        QString tag = it.key();
        if (tag.isEmpty()) continue;
        QChar first = tag.at(0).toUpper();
        if (first < 'A' || first > 'Z') first = '#';
        groups[first][tag] = it.value();
    }

    for (auto it = groups.begin(); it != groups.end(); ++it) {
        QWidget* groupWidget = new QWidget(m_contentWidget);
        auto* vLayout = new QVBoxLayout(groupWidget);
        vLayout->setContentsMargins(0, 10, 0, 10);

        QLabel* groupTitle = new QLabel(QString(it.key()), groupWidget);
        groupTitle->setStyleSheet("font-size: 16px; font-weight: bold; color: #555; border-bottom: 1px solid #333; padding-bottom: 5px;");
        vLayout->addWidget(groupTitle);

        QWidget* tagsContainer = new QWidget(groupWidget);
        auto* flow = new FlowLayout(tagsContainer, 0, 10, 8);

        auto tagsInGroup = it.value();
        for (auto tagIt = tagsInGroup.begin(); tagIt != tagsInGroup.end(); ++tagIt) {
            QPushButton* tagBtn = new QPushButton(QString("%1 (%2)").arg(tagIt.key()).arg(tagIt.value()), tagsContainer);
            tagBtn->setCursor(Qt::PointingHandCursor);
            tagBtn->setStyleSheet(
                "QPushButton { background: transparent; border: none; color: #AAA; text-align: left; font-size: 13px; padding: 2px 5px; }"
                "QPushButton:hover { color: #3498db; text-decoration: underline; }"
            );

            QString tagName = tagIt.key();
            connect(tagBtn, &QPushButton::clicked, this, [this, tagName]() {
                emit requestSearchTag(tagName);
            });

            // 右键菜单
            tagBtn->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(tagBtn, &QWidget::customContextMenuRequested, this, [this, tagName](const QPoint&) {
                QMenu menu(this);
                UiHelper::applyMenuStyle(&menu);
                menu.addAction(UiHelper::getIcon("search", TextMain), "搜索含此标签的项目", this, [this, tagName](){ emit requestSearchTag(tagName); });
                menu.addSeparator();
                menu.addAction(UiHelper::getIcon("star_filled", WarningOrange), "设为常用标签");

                auto* addToMenu = menu.addMenu(UiHelper::getIcon("add", SuccessGreen), "加入标签组");
                for (const auto& group : m_tagGroups) {
                    addToMenu->addAction(group.name);
                }

                auto* moveToMenu = menu.addMenu(UiHelper::getIcon("folder_filled", PrimaryBlue), "移动至标签组");
                for (const auto& group : m_tagGroups) {
                    moveToMenu->addAction(group.name);
                }

                menu.addAction(UiHelper::getIcon("close", ErrorRed), "从标签组中移除");
                menu.addSeparator();
                menu.addAction(UiHelper::getIcon("edit", TextMain), "重命名标签");
                menu.addAction(UiHelper::getIcon("trash", ErrorRed), "删除标签");
                menu.exec(QCursor::pos());
            });

            flow->addWidget(tagBtn);
        }
        vLayout->addWidget(tagsContainer);
        if (contentLayout) contentLayout->addWidget(groupWidget);
    }
    if (contentLayout) contentLayout->addStretch();
}

} // namespace ArcMeta
