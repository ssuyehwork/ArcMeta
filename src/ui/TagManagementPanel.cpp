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
    iconLabel->setPixmap(UiHelper::getIcon("tag", QColor("#41F2F2"), 16).pixmap(16, 16));
    titleL->addWidget(iconLabel);

    QLabel* titleLabel = new QLabel("标签管理", titleBar);
    titleLabel->setStyleSheet("font-size: 12px; color: #41F2F2; background: transparent; border: none;");
    titleL->addWidget(titleLabel);
    titleL->addStretch();

    m_mainLayout->addWidget(titleBar);

    // 滚动区
    m_scrollArea = new QScrollArea(this);
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
    m_mainLayout->addWidget(m_scrollArea);
}

void TagManagementPanel::refresh() {
    // 1. 获取数据
    m_allTags = TagRepo::getAllTags();
    m_groups = TagRepo::getGroups();

    // 2. 清理界面
    QLayoutItem* item;
    while ((item = m_containerLayout->takeAt(0)) != nullptr) {
        if (item->widget()) delete item->widget();
        delete item;
    }

    // 3. 渲染视图 (目前默认渲染 A-Z 视图，后续可增加切换)
    renderAlphabeticalView();

    // 如果有自定义分组，也渲染出来
    if (!m_groups.empty()) {
        renderGroupedView();
    }

    m_containerLayout->addStretch();
}

void TagManagementPanel::renderAlphabeticalView() {
    QMap<QString, std::vector<TagDef>> letterMap;
    for (const auto& tag : m_allTags) {
        QString letter = "#";
        if (!tag.name.isEmpty()) {
            QChar first = tag.name.at(0).toUpper();
            if (first >= 'A' && first <= 'Z') letter = QString(first);
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
}

void TagManagementPanel::renderGroupedView() {
    // 建立 ID -> 分组名 映射
    QMap<int, QString> groupNames;
    for (const auto& g : m_groups) groupNames[g.id] = g.name;

    // 建立 ID -> 标签列表 映射
    QMap<int, std::vector<TagDef>> groupMap;
    for (const auto& tag : m_allTags) {
        if (tag.groupId > 0) {
            groupMap[tag.groupId].push_back(tag);
        }
    }

    if (groupMap.isEmpty()) return;

    // 分隔线
    QFrame* line = new QFrame();
    line->setFrameShape(QFrame::HLine);
    line->setFixedHeight(1);
    line->setStyleSheet("background-color: #333; margin-top: 20px; margin-bottom: 20px;");
    m_containerLayout->addWidget(line);

    QLabel* groupTitle = new QLabel("按分组查看");
    groupTitle->setStyleSheet("font-size: 22px; font-weight: bold; color: #EEE; margin-bottom: 10px;");
    m_containerLayout->addWidget(groupTitle);

    for (auto it = groupMap.begin(); it != groupMap.end(); ++it) {
        QWidget* section = new QWidget();
        QVBoxLayout* sectionL = new QVBoxLayout(section);
        sectionL->setContentsMargins(0, 0, 0, 0);
        sectionL->setSpacing(10);

        QString gName = groupNames.value(it.key(), "未知分组");
        QLabel* header = new QLabel(gName);
        header->setStyleSheet("font-size: 18px; font-weight: bold; color: #3498db; border-bottom: 1px solid #333; padding-bottom: 5px;");
        sectionL->addWidget(header);

        FlowLayout* flow = new FlowLayout(nullptr, 0, 10, 10);
        for (const auto& tag : it.value()) {
            flow->addWidget(createTagWidget(tag));
        }
        sectionL->addLayout(flow);

        m_containerLayout->addWidget(section);
    }
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
        style += "QPushButton { border-left: 3px solid #f1c40f; }"; // 常用标签黄色边框
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
