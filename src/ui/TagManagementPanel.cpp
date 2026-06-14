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

TagChip::TagChip(const TagDef& tag, QWidget* parent) : QWidget(parent), m_tag(tag) {
    setCursor(Qt::PointingHandCursor);
    setFixedHeight(32);

    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 0, 8, 0);
    layout->setSpacing(8);

    m_nameLabel = new QLabel(tag.name, this);
    m_nameLabel->setStyleSheet(QString("color: %1; font-size: 12px; background: transparent;").arg(Style::TextMain.name()));
    layout->addWidget(m_nameLabel);

    m_countLabel = new QLabel(QString::number(tag.usageCount), this);
    m_countLabel->setStyleSheet(QString("color: %1; font-size: 10px; background: transparent;").arg(Style::TextMuted.name()));
    layout->addWidget(m_countLabel);

    m_btnDelete = new QPushButton(this);
    m_btnDelete->setFixedSize(16, 16);
    m_btnDelete->setIcon(UiHelper::getIcon("close", Style::TextMuted, 10));
    m_btnDelete->setStyleSheet("QPushButton { background: transparent; border: none; } QPushButton:hover { background: #E81123; border-radius: 2px; }");
    m_btnDelete->hide();
    connect(m_btnDelete, &QPushButton::clicked, [this]() { emit deleteRequested(m_tag.name); });
    layout->addWidget(m_btnDelete);

    // 初始样式
    QString bg = Style::BackgroundHover.name();
    if (tag.isFavorite) {
        setStyleSheet(QString("TagChip { background-color: %1; border-radius: 16px; border: 1px solid %2; }")
            .arg(bg).arg(Style::AccentCyan.name()));
    } else {
        setStyleSheet(QString("TagChip { background-color: %1; border-radius: 16px; border: 1px solid %2; }")
            .arg(bg).arg(Style::BorderColor.name()));
    }
}

void TagChip::enterEvent(QEnterEvent* event) {
    m_btnDelete->show();
    QWidget::enterEvent(event);
}

void TagChip::leaveEvent(QEvent* event) {
    m_btnDelete->hide();
    QWidget::leaveEvent(event);
}

void TagChip::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        emit clicked(m_tag.name);
    } else if (event->button() == Qt::RightButton) {
        QMenu menu(this);
        UiHelper::applyMenuStyle(&menu);
        menu.addAction(m_tag.isFavorite ? "取消常用" : "设为常用")->setData(1);
        menu.addAction("重命名")->setData(2);
        menu.addSeparator();
        menu.addAction(UiHelper::getIcon("trash", Style::ErrorRed), "删除标签")->setData(3);

        QAction* act = menu.exec(QCursor::pos());
        if (act) {
            int id = act->data().toInt();
            if (id == 1) emit favoriteToggled(m_tag.name, !m_tag.isFavorite);
            else if (id == 2) emit renameRequested(m_tag.name);
            else if (id == 3) emit deleteRequested(m_tag.name);
        }
    }
    QWidget::mouseReleaseEvent(event);
}

TagManagementPanel::TagManagementPanel(QWidget* parent) : QWidget(parent) {
    initUi();
    refresh();
}

void TagManagementPanel::initUi() {
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);
    m_mainLayout->setAlignment(Qt::AlignTop);

    // 顶部全屏 Header (物理对标原型：居中大搜索框)
    QWidget* header = new QWidget(this);
    header->setFixedHeight(140);
    header->setStyleSheet(QString("background-color: %1; border-bottom: 1px solid %2;")
        .arg(Style::BackgroundDeep.name()).arg(Style::BorderColor.name()));

    QVBoxLayout* headerL = new QVBoxLayout(header);
    headerL->setContentsMargins(50, 20, 50, 20);

    // 第一行：标题与关闭按钮
    QHBoxLayout* topRow = new QHBoxLayout();
    QLabel* titleLabel = new QLabel("标签管理", header);
    titleLabel->setStyleSheet(QString("font-size: 20px; font-weight: bold; color: %1;").arg(Style::TextMain.name()));
    topRow->addWidget(titleLabel);
    topRow->addStretch();

    QPushButton* btnClose = new QPushButton(header);
    btnClose->setFixedSize(32, 32);
    btnClose->setIcon(UiHelper::getIcon("close", Style::TextDim));
    btnClose->setCursor(Qt::PointingHandCursor);
    btnClose->setStyleSheet(
        "QPushButton { background: transparent; border-radius: 4px; } "
        "QPushButton:hover { background-color: #E81123; } "
    );
    connect(btnClose, &QPushButton::clicked, [this]() {
        emit tagSearchRequested(""); // 触发 MainWindow 返回逻辑
    });
    topRow->addWidget(btnClose);
    headerL->addLayout(topRow);

    headerL->addStretch();

    // 第二行：居中大搜索框
    QHBoxLayout* searchRow = new QHBoxLayout();
    searchRow->addStretch();

    m_searchEdit = new QLineEdit(header);
    m_searchEdit->setPlaceholderText("搜索或创建标签...");
    m_searchEdit->setFixedWidth(500);
    m_searchEdit->setFixedHeight(40);
    m_searchEdit->setStyleSheet(QString(
        "QLineEdit { "
        "  background-color: #2D2D2D; "
        "  border: 1px solid %1; "
        "  border-radius: 20px; "
        "  color: %2; "
        "  padding: 0 20px; "
        "  font-size: 14px; "
        "} "
        "QLineEdit:focus { border: 1px solid %3; }"
    ).arg(Style::BorderColor.name()).arg(Style::TextMain.name()).arg(Style::PrimaryBlue.name()));

    connect(m_searchEdit, &QLineEdit::textChanged, [this](const QString& text) {
        m_currentSearch = text;
        renderFilteredTags();
    });
    searchRow->addWidget(m_searchEdit);
    searchRow->addStretch();
    headerL->addLayout(searchRow);

    m_mainLayout->addWidget(header);

    setupMainArea();
    m_mainLayout->addWidget(m_scrollArea);
}

void TagManagementPanel::setupMainArea() {
    m_scrollArea = new QScrollArea();
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setStyleSheet(QString("background-color: %1;").arg(Style::BackgroundDeep.name()));

    m_scrollContainer = new QWidget();
    m_scrollContainer->setObjectName("TagScrollContainer");
    m_scrollContainer->setStyleSheet(QString("QWidget#TagScrollContainer { background-color: %1; }").arg(Style::BackgroundDeep.name()));
    m_containerLayout = new QVBoxLayout(m_scrollContainer);
    m_containerLayout->setContentsMargins(50, 40, 50, 40); // 增加全屏时的左右内边距
    m_containerLayout->setSpacing(40);

    m_scrollArea->setWidget(m_scrollContainer);
}

void TagManagementPanel::refresh() {
    // 1. 获取数据
    m_allTags = TagRepo::getAllTags();
    m_groups = TagRepo::getGroups();

    // 2. 由于已移除侧边栏，直接进行全量渲染
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
    TagChip* chip = new TagChip(tag, this);

    connect(chip, &TagChip::clicked, this, [this](const QString& name) {
        emit tagSearchRequested(name);
    });

    connect(chip, &TagChip::deleteRequested, this, [this](const QString& name) {
        if (QMessageBox::question(this, "删除标签", QString("确定要永久删除标签 \"%1\" 吗？").arg(name)) == QMessageBox::Yes) {
            TagRepo::deleteTagGlobal(name);
            refresh();
            emit tagMetadataChanged();
        }
    });

    connect(chip, &TagChip::favoriteToggled, this, [this](const QString& name, bool favorite) {
        TagRepo::setTagFavorite(name, favorite);
        refresh();
    });

    connect(chip, &TagChip::renameRequested, this, [this](const QString& name) {
        bool ok;
        QString newName = QInputDialog::getText(this, "重命名标签", "新名称:", QLineEdit::Normal, name, &ok);
        if (ok && !newName.isEmpty() && newName != name) {
            TagRepo::renameTagGlobal(name, newName);
            refresh();
            emit tagMetadataChanged();
        }
    });

    return chip;
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
