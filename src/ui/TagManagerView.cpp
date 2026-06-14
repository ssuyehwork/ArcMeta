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
    m_splitter->setHandleWidth(1);
    m_splitter->setStyleSheet("QSplitter::handle { background-color: #333; }");

    setupSidebar();
    setupContentArea();

    m_splitter->addWidget(m_sidebar);
    m_splitter->addWidget(m_scrollArea);

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
    layout->setContentsMargins(2, 1, 2, 1);
    layout->setSpacing(0);

    QWidget* inner = new QWidget(item);
    inner->setObjectName("SidebarItemInner");
    inner->setStyleSheet(
        "QWidget#SidebarItemInner { background: transparent; border-radius: 4px; }"
        "QWidget#SidebarItemInner:hover { background-color: #2a2d2e; }"
    );
    auto* innerLayout = new QHBoxLayout(inner);
    innerLayout->setContentsMargins(13, 0, 13, 0);
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
    m_sidebar = new QWidget(this);
    m_sidebar->setObjectName("TagSidebar");
    m_sidebar->setFixedWidth(230);
    m_sidebar->setStyleSheet("QWidget#TagSidebar { background-color: #1E1E1E; border-right: 1px solid #333; }");

    m_sidebarLayout = new QVBoxLayout(m_sidebar);
    m_sidebarLayout->setContentsMargins(0, 0, 0, 0);
    m_sidebarLayout->setSpacing(0);

    // 标题栏
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

    // 静态项
    m_sidebarLayout->addWidget(createSidebarItem("all_data", "全部标签", "0", &m_allTagsCountLabel));
    m_sidebarLayout->addWidget(createSidebarItem("uncategorized", "未分类", "0", &m_uncategorizedTagsCountLabel));
    m_sidebarLayout->addWidget(createSidebarItem("star_filled", "常用标签", "0", &m_frequentTagsCountLabel));

    QFrame* line = new QFrame(m_sidebar);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet("background-color: #333; margin: 10px 15px;");
    m_sidebarLayout->addWidget(line);

    // 标签组容器
    m_groupContainer = new QWidget(m_sidebar);
    auto* groupLayout = new QVBoxLayout(m_groupContainer);
    groupLayout->setContentsMargins(0, 0, 0, 0);
    groupLayout->setSpacing(0);
    m_sidebarLayout->addWidget(m_groupContainer);

    m_sidebarLayout->addStretch();

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
    m_contentWidget->setObjectName("TagContentContainer");
    m_contentWidget->setStyleSheet("QWidget#TagContentContainer { background-color: #1E1E1E; }");

    auto* contentLayout = new QVBoxLayout(m_contentWidget);
    contentLayout->setContentsMargins(20, 20, 20, 20);
    contentLayout->setSpacing(20);

    m_scrollArea->setWidget(m_contentWidget);
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
    m_tagCountLabel->setText(QString("(%1)").arg(allCount));
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

    QLayout* groupLayout = m_groupContainer->layout();
    QLayoutItem* child;
    while ((child = groupLayout->takeAt(0)) != nullptr) {
        delete child->widget();
        delete child;
    }
    for (const auto& group : m_tagGroups) {
        groupLayout->addWidget(createSidebarItem("folder_filled", group.name, QString::number(group.tags.size())));
    }

    // 重建内容区
    QLayout* contentLayout = m_contentWidget->layout();
    while ((child = contentLayout->takeAt(0)) != nullptr) {
        delete child->widget();
        delete child;
    }

    // 添加标题
    QWidget* contentHeader = new QWidget(m_contentWidget);
    contentHeader->setFixedHeight(40);
    auto* hLayout = new QHBoxLayout(contentHeader);
    hLayout->setContentsMargins(0, 0, 0, 0);
    QLabel* lblTitle = new QLabel("标签", contentHeader);
    lblTitle->setStyleSheet("font-size: 18px; font-weight: bold; color: #EEEEEE;");
    hLayout->addWidget(lblTitle);
    hLayout->addStretch();
    contentLayout->addWidget(contentHeader);

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
        auto* grid = new QGridLayout(tagsContainer);
        grid->setContentsMargins(0, 10, 0, 10);
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

            grid->addWidget(tagBtn, row, col);
            col++;
            if (col >= maxCols) { col = 0; row++; }
        }
        vLayout->addWidget(tagsContainer);
        contentLayout->addWidget(groupWidget);
    }
    contentLayout->addStretch();
}

} // namespace ArcMeta
