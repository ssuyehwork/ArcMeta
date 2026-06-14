#include "TagManagementPanel.h"
#include "UiHelper.h"
#include "StyleLibrary.h"
#include "../meta/TagRepo.h"
#include <QContextMenuEvent>
#include <QMenu>
#include <QAction>
#include <QInputDialog>
#include <QMessageBox>
#include <QApplication>
#include <QStyle>
#include <QSplitter>

namespace ArcMeta {

TagManagementPanel::TagManagementPanel(QWidget* parent) : QWidget(parent) {
    initUi();
    refresh();
}

void TagManagementPanel::initUi() {
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    // 标题栏 (物理对标 MainWindow 32px)
    QWidget* titleBar = new QWidget(this);
    titleBar->setObjectName("ContainerHeader");
    titleBar->setFixedHeight(32);
    titleBar->setStyleSheet("QWidget#ContainerHeader { background-color: #252526; border-bottom: 1px solid #333; }");
    QHBoxLayout* titleL = new QHBoxLayout(titleBar);
    titleL->setContentsMargins(15, 0, 15, 0);
    titleL->setSpacing(8);

    QLabel* iconLabel = new QLabel(titleBar);
    iconLabel->setPixmap(UiHelper::getIcon("tag", Style::AccentCyan, 16).pixmap(16, 16));
    titleL->addWidget(iconLabel);

    QLabel* titleLabel = new QLabel("标签管理", titleBar);
    titleLabel->setStyleSheet(QString("font-size: 12px; color: %1; background: transparent; border: none;").arg(Style::AccentCyan.name()));
    titleL->addWidget(titleLabel);
    titleL->addStretch();

    m_mainLayout->addWidget(titleBar);

    // 主体区域：使用 QSplitter 隔离导航和内容
    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setHandleWidth(1);
    splitter->setStyleSheet("QSplitter::handle { background-color: #333; }");

    setupSidebar();
    splitter->addWidget(m_sidebar);

    QWidget* rightContent = new QWidget();
    setupMainArea();

    // 把 setupMainArea 创建的组件放入 rightContent
    QVBoxLayout* rightL = new QVBoxLayout(rightContent);
    rightL->setContentsMargins(0, 0, 0, 0);
    rightL->setSpacing(0);

    // 顶部搜索栏
    QWidget* searchBar = new QWidget();
    searchBar->setFixedHeight(48);
    searchBar->setStyleSheet("background-color: #1E1E1E; border-bottom: 1px solid #333;");
    QHBoxLayout* searchL = new QHBoxLayout(searchBar);
    searchL->setContentsMargins(20, 0, 20, 0);

    m_searchEdit = new QLineEdit();
    m_searchEdit->setPlaceholderText("搜索标签...");
    m_searchEdit->setFixedWidth(300);
    m_searchEdit->setFixedHeight(28);
    m_searchEdit->setStyleSheet(
        "QLineEdit { "
        "  background-color: #2D2D2D; "
        "  border: 1px solid #333; "
        "  border-radius: 4px; "
        "  color: #EEE; "
        "  padding-left: 8px; "
        "  font-size: 12px; "
        "} "
        "QLineEdit:focus { border: 1px solid #3498db; }"
    );
    connect(m_searchEdit, &QLineEdit::textChanged, [this](const QString& text) {
        m_currentSearch = text;
        renderFilteredTags();
    });
    searchL->addWidget(m_searchEdit);
    searchL->addStretch();

    rightL->addWidget(searchBar);
    rightL->addWidget(m_scrollArea);

    splitter->addWidget(rightContent);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({230, 800});

    m_mainLayout->addWidget(splitter);
}

void TagManagementPanel::setupSidebar() {
    m_sidebar = new QListWidget();
    m_sidebar->setFixedWidth(230);
    m_sidebar->setObjectName("NavPanel"); // 复用样式
    m_sidebar->setFrameShape(QFrame::NoFrame);
    m_sidebar->setStyleSheet(
        "QListWidget { "
        "  background-color: #252526; "
        "  border-right: 1px solid #333; "
        "  outline: none; "
        "  padding-top: 10px; "
        "} "
        "QListWidget::item { "
        "  height: 32px; "
        "  color: #BBB; "
        "  padding-left: 15px; "
        "  border-left: 3px solid transparent; "
        "} "
        "QListWidget::item:hover { background-color: #2A2D2E; } "
        "QListWidget::item:selected { "
        "  background-color: #37373D; "
        "  color: #FFF; "
        "  border-left: 3px solid #3498db; "
        "}"
    );

    connect(m_sidebar, &QListWidget::currentRowChanged, [this](int row) {
        if (row < 0) return;
        QListWidgetItem* item = m_sidebar->item(row);
        m_currentFilterType = item->data(Qt::UserRole).toInt();
        renderFilteredTags();
    });
}

void TagManagementPanel::setupMainArea() {
    m_scrollArea = new QScrollArea();
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setStyleSheet("background-color: #1E1E1E;");

    m_scrollContainer = new QWidget();
    m_scrollContainer->setObjectName("TagScrollContainer");
    m_scrollContainer->setStyleSheet("QWidget#TagScrollContainer { background-color: #1E1E1E; }");
    m_containerLayout = new QVBoxLayout(m_scrollContainer);
    m_containerLayout->setContentsMargins(20, 20, 20, 20);
    m_containerLayout->setSpacing(30);

    m_scrollArea->setWidget(m_scrollContainer);
}

void TagManagementPanel::refresh() {
    // 1. 获取数据
    m_allTags = TagRepo::getAllTags();
    m_groups = TagRepo::getGroups();

    // 2. 更新侧边栏
    m_sidebar->blockSignals(true);
    m_sidebar->clear();

    auto* allItem = new QListWidgetItem(UiHelper::getIcon("list_ul", Style::TextDim), "所有标签");
    allItem->setData(Qt::UserRole, 0);
    m_sidebar->addItem(allItem);

    auto* favItem = new QListWidgetItem(UiHelper::getIcon("star", QColor("#f1c40f")), "常用标签");
    favItem->setData(Qt::UserRole, 1);
    m_sidebar->addItem(favItem);

    if (!m_groups.empty()) {
        auto* groupHeader = new QListWidgetItem(m_sidebar);
        groupHeader->setFlags(Qt::NoItemFlags);
        groupHeader->setData(Qt::UserRole, -1);
        groupHeader->setSizeHint(QSize(0, 30));

        QLabel* headerLabel = new QLabel(" 标签分组");
        headerLabel->setStyleSheet("color: #666; font-size: 11px; padding-top: 10px; background: transparent;");
        m_sidebar->setItemWidget(groupHeader, headerLabel);

        for (const auto& g : m_groups) {
            auto* item = new QListWidgetItem(UiHelper::getIcon("folder", Style::PrimaryBlue), g.name);
            item->setData(Qt::UserRole, 100 + g.id);
            m_sidebar->addItem(item);
        }
    }

    m_sidebar->setCurrentRow(0);
    m_sidebar->blockSignals(false);

    renderFilteredTags();
}

void TagManagementPanel::renderFilteredTags() {
    // 清理界面
    QLayoutItem* item;
    while ((item = m_containerLayout->takeAt(0)) != nullptr) {
        if (item->widget()) delete item->widget();
        delete item;
    }

    std::vector<TagDef> filtered;
    for (const auto& tag : m_allTags) {
        // 搜索过滤
        if (!m_currentSearch.isEmpty() && !tag.name.contains(m_currentSearch, Qt::CaseInsensitive)) {
            continue;
        }

        // 类型过滤
        if (m_currentFilterType == 1) { // Favorites
            if (!tag.isFavorite) continue;
        } else if (m_currentFilterType >= 100) { // Group
            if (tag.groupId != m_currentFilterType - 100) continue;
        }

        filtered.push_back(tag);
    }

    if (filtered.empty()) {
        QLabel* empty = new QLabel("未找到匹配的标签");
        empty->setAlignment(Qt::AlignCenter);
        empty->setStyleSheet("color: #666; font-size: 14px; margin-top: 100px;");
        m_containerLayout->addWidget(empty);
        m_containerLayout->addStretch();
        return;
    }

    // 按字母分组渲染
    QMap<QString, std::vector<TagDef>> letterMap;
    for (const auto& tag : filtered) {
        QString letter = "#";
        if (!tag.name.isEmpty()) {
            QChar first = tag.name.at(0).toUpper();
            if (first >= 'A' && first <= 'Z') letter = QString(first);
            else if (tag.name.at(0).unicode() >= 0x4E00 && tag.name.at(0).unicode() <= 0x9FA5) {
                // 中文处理，这里简化，实际可引入拼音库
                letter = "中";
            }
        }
        letterMap[letter].push_back(tag);
    }

    for (auto it = letterMap.begin(); it != letterMap.end(); ++it) {
        QWidget* section = new QWidget();
        QVBoxLayout* sectionL = new QVBoxLayout(section);
        sectionL->setContentsMargins(0, 0, 0, 0);
        sectionL->setSpacing(10);

        QLabel* header = new QLabel(it.key());
        header->setStyleSheet("font-size: 14px; font-weight: normal; color: #888888; border-bottom: 1px solid #333333; padding-bottom: 3px;");
        sectionL->addWidget(header);

        FlowLayout* flow = new FlowLayout(nullptr, 0, 10, 10);
        for (const auto& tag : it.value()) {
            flow->addWidget(createTagWidget(tag));
        }
        sectionL->addLayout(flow);

        m_containerLayout->addWidget(section);
    }

    m_containerLayout->addStretch();
}

QWidget* TagManagementPanel::createTagWidget(const TagDef& tag) {
    QPushButton* btn = new QPushButton();
    QString displayText = QString("%1 (%2)").arg(tag.name).arg(tag.usageCount);
    btn->setText(displayText);

    // 样式规范：物理对标 image.png (无边框, #333333 背景)
    QString style = "QPushButton { "
                    "  background-color: #333333; "
                    "  color: #CCCCCC; "
                    "  border: none; "
                    "  padding: 4px 12px; "
                    "  border-radius: 4px; "
                    "  font-size: 12px; "
                    "} "
                    "QPushButton:hover { background-color: #3E3E42; color: white; }";

    if (tag.isFavorite) {
        style += "QPushButton { border-left: 3px solid #f1c40f; }";
    }

    btn->setStyleSheet(style);
    btn->setProperty("tagName", tag.name);

    connect(btn, &QPushButton::clicked, [this, tag]() {
        emit tagSearchRequested(tag.name);
    });

    // 右键菜单支持
    btn->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(btn, &QPushButton::customContextMenuRequested, [this, btn](const QPoint& pos) {
        QString tagName = btn->property("tagName").toString();

        QMenu menu(this);
        UiHelper::applyMenuStyle(&menu);

        menu.addAction(UiHelper::getIcon("search", QColor("#EEE")), "搜索包含此标签的项目")->setData(1);

        // 查找当前标签定义
        TagDef currentDef;
        for(const auto& t : m_allTags) if(t.name == tagName) { currentDef = t; break; }

        menu.addAction(currentDef.isFavorite ? "取消常用标签" : "设为常用标签")->setData(2);
        menu.addAction("重命名")->setData(3);

        QMenu* groupMenu = menu.addMenu("添加至分组...");
        UiHelper::applyMenuStyle(groupMenu);
        for (const auto& g : m_groups) {
            QAction* act = groupMenu->addAction(g.name);
            act->setData(100 + g.id);
        }
        groupMenu->addSeparator();
        groupMenu->addAction("新建分组...")->setData(4);

        menu.addSeparator();
        menu.addAction(UiHelper::getIcon("trash", QColor("#e81123")), "删除标签")->setData(5);

        QAction* selected = menu.exec(btn->mapToGlobal(pos));
        if (!selected) return;

        int actionId = selected->data().toInt();
        if (actionId == 1) {
            emit tagSearchRequested(tagName);
        } else if (actionId == 2) {
            TagRepo::setTagFavorite(tagName, !currentDef.isFavorite);
            refresh();
        } else if (actionId == 3) {
            bool ok;
            QString newName = QInputDialog::getText(this, "重命名标签", "请输入新标签名称:", QLineEdit::Normal, tagName, &ok);
            if (ok && !newName.isEmpty() && newName != tagName) {
                TagRepo::renameTagGlobal(tagName, newName);
                refresh();
                emit tagMetadataChanged();
            }
        } else if (actionId == 4) {
            bool ok;
            QString gName = QInputDialog::getText(this, "新建标签组", "分组名称:", QLineEdit::Normal, "", &ok);
            if (ok && !gName.isEmpty()) {
                int gid = TagRepo::addGroup(gName);
                if (gid > 0) TagRepo::setTagGroup(tagName, gid);
                refresh();
            }
        } else if (actionId == 5) {
            if (QMessageBox::question(this, "删除标签", QString("确定要永久删除标签 \"%1\" 吗？此操作将从所有文件中移除该标签。").arg(tagName)) == QMessageBox::Yes) {
                TagRepo::deleteTagGlobal(tagName);
                refresh();
                emit tagMetadataChanged();
            }
        } else if (actionId >= 100) {
            int gid = actionId - 100;
            TagRepo::setTagGroup(tagName, gid);
            refresh();
        }
    });

    return btn;
}

void TagManagementPanel::contextMenuEvent(QContextMenuEvent* event) {
    QMenu menu(this);
    UiHelper::applyMenuStyle(&menu);

    menu.addAction("新建标签组...")->setData(1);
    menu.addAction("刷新数据")->setData(2);

    QAction* selected = menu.exec(event->globalPos());
    if (!selected) return;

    if (selected->data().toInt() == 1) {
        bool ok;
        QString name = QInputDialog::getText(this, "新建标签组", "分组名称:", QLineEdit::Normal, "", &ok);
        if (ok && !name.isEmpty()) {
            TagRepo::addGroup(name);
            refresh();
        }
    } else if (selected->data().toInt() == 2) {
        refresh();
    }
}


} // namespace ArcMeta
