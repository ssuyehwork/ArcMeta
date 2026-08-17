#include "TagManagerDialog.h"
#include "UiHelper.h"
#include "StyleLibrary.h"
#include "../meta/CategoryRepo.h"
#include "../meta/AmMetaJson.h"
#include "components/FlowLayout.h"
#include <QApplication>
#include <QScreen>
#include <QFileInfo>

namespace ArcMeta {

// 初始化静态会话级最近使用标签队列
QStringList TagManagerDialog::s_sessionRecentTags;

void TagManagerDialog::showDialog(QWidget* parent, const QString& currentPath, bool isMirrorSource) {
    TagManagerDialog* dlg = new TagManagerDialog(currentPath, isMirrorSource, parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->exec();
}

TagManagerDialog::TagManagerDialog(const QString& currentPath, bool isMirrorSource, QWidget* parent)
    : FramelessDialog("标签管理", parent), m_currentPath(currentPath), m_isMirrorSource(isMirrorSource) {
    
    // 尺寸硬性约束：显示 180px 侧边栏时最小宽度 400px
    setMinimumSize(400, 350);
    resize(580, 480);

    initContent();
    applyTheme();
    refreshTags();
}

void TagManagerDialog::initContent() {
    QVBoxLayout* mainL = new QVBoxLayout(m_contentArea);
    mainL->setContentsMargins(0, 0, 0, 0);
    mainL->setSpacing(0);

    // ================= 1. 顶部操作栏（透明搜索框 + 右侧 sidebar 按钮） =================
    QWidget* topBar = new QWidget(this);
    topBar->setFixedHeight(40);
    topBar->setStyleSheet("background: transparent; border-bottom: 1px solid #333;");
    QHBoxLayout* topL = new QHBoxLayout(topBar);
    topL->setContentsMargins(15, 0, 10, 0);
    topL->setSpacing(10);

    m_searchEdit = new QLineEdit(topBar);
    m_searchEdit->setPlaceholderText("搜索或新建标签...");
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setFixedHeight(28);
    m_searchEdit->setStyleSheet(
        "QLineEdit { background: transparent; border: 1px solid #444; border-radius: 4px; padding: 0 8px; color: #EEE; font-size: 12px; }"
        "QLineEdit:focus { border-color: #3498DB; }"
    );
    connect(m_searchEdit, &QLineEdit::textChanged, this, &TagManagerDialog::onSearchTextChanged);
    connect(m_searchEdit, &QLineEdit::returnPressed, [this]() {
        QString kw = m_searchEdit->text().trimmed();
        if (!kw.isEmpty()) {
            createTag(kw);
            m_searchEdit->clear();
        }
    });
    topL->addWidget(m_searchEdit, 1);

    // 侧边栏折叠按钮
    m_btnToggleSidebar = new QPushButton(topBar);
    m_btnToggleSidebar->setFixedSize(24, 24);
    m_btnToggleSidebar->setCheckable(true);
    m_btnToggleSidebar->setChecked(true);
    m_btnToggleSidebar->setIcon(UiHelper::getIcon("sidebar", QColor("#AAAAAA"), 16));
    m_btnToggleSidebar->setCursor(Qt::PointingHandCursor);
    m_btnToggleSidebar->setStyleSheet(
        "QPushButton { background: transparent; border: none; border-radius: 3px; }"
        "QPushButton:hover { background-color: #3E3E42; }"
    );
    connect(m_btnToggleSidebar, &QPushButton::toggled, this, &TagManagerDialog::onSidebarToggled);
    topL->addWidget(m_btnToggleSidebar);

    mainL->addWidget(topBar);

    // ================= 2. 中部核心区域（左侧固定 180px 侧边栏 + 右侧流式内容区） =================
    QWidget* bodyWidget = new QWidget(this);
    QHBoxLayout* bodyL = new QHBoxLayout(bodyWidget);
    bodyL->setContentsMargins(0, 0, 0, 0);
    bodyL->setSpacing(0);

    // A. 固定 180px 侧边栏
    m_sidebar = new QFrame(bodyWidget);
    m_sidebar->setFixedWidth(180); // 规则：侧边栏宽度恒定 180px，不可调整
    m_sidebar->setStyleSheet("QFrame { background-color: #252526; border-right: 1px solid #333; }");
    m_sidebarLayout = new QVBoxLayout(m_sidebar);
    m_sidebarLayout->setContentsMargins(10, 15, 10, 10);
    m_sidebarLayout->setSpacing(6);

    QLabel* sideTitle = new QLabel("分类导航", m_sidebar);
    sideTitle->setStyleSheet("color: #888; font-size: 11px; font-weight: bold; margin-bottom: 4px;");
    m_sidebarLayout->addWidget(sideTitle);

    m_sidebarGroup = new QButtonGroup(this);
    m_sidebarGroup->setExclusive(true);

    auto addSidebarBtn = [this](int id, const QString& icon, const QString& name) {
        QPushButton* btn = new QPushButton(m_sidebar);
        btn->setText(" " + name);
        btn->setIcon(UiHelper::getIcon(icon, QColor("#AAAAAA"), 13));
        btn->setIconSize(QSize(13, 13));
        btn->setCheckable(true);
        btn->setFixedSize(160, 28);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(
            "QPushButton { background: transparent; color: #CCC; border: none; text-align: left; padding-left: 10px; border-radius: 4px; font-size: 11px; }"
            "QPushButton:hover { background-color: #2D2D30; color: #FFF; }"
            "QPushButton:checked { background-color: #3E3E42; color: #3498DB; font-weight: bold; }"
        );
        m_sidebarLayout->addWidget(btn);
        m_sidebarGroup->addButton(btn, id);
    };

    addSidebarBtn(0, "all_data", "全部标签");
    addSidebarBtn(1, "uncategorized", "未分类标签");
    addSidebarBtn(2, "star_filled", "常用标签");

    m_sidebarGroup->button(0)->setChecked(true);
    connect(m_sidebarGroup, &QButtonGroup::idClicked, this, &TagManagerDialog::onSidebarItemClicked);

    m_sidebarLayout->addStretch();
    bodyL->addWidget(m_sidebar);

    // B. 右侧标签流式容器区
    m_scrollArea = new QScrollArea(bodyWidget);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setStyleSheet("QScrollArea { border: none; background: transparent; }");

    m_contentWidget = new QWidget();
    m_contentLayout = new QVBoxLayout(m_contentWidget);
    m_contentLayout->setContentsMargins(15, 15, 15, 15);
    m_contentLayout->setSpacing(15);

    // 动态新增提示胶囊 (`+ 新增 "关键字"`)，默认隐藏
    m_addNewTagWidget = new QWidget(m_contentWidget);
    QHBoxLayout* addL = new QHBoxLayout(m_addNewTagWidget);
    addL->setContentsMargins(0, 0, 0, 0);
    m_btnAddNewTag = new QPushButton(m_addNewTagWidget);
    m_btnAddNewTag->setCursor(Qt::PointingHandCursor);
    m_btnAddNewTag->setStyleSheet(
        "QPushButton { background: #1C97EA; color: #FFF; border: none; border-radius: 4px; padding: 4px 12px; font-weight: bold; font-size: 11px; }"
        "QPushButton:hover { background: #1886D2; }"
    );
    connect(m_btnAddNewTag, &QPushButton::clicked, [this]() {
        QString kw = m_searchEdit->text().trimmed();
        if (!kw.isEmpty()) {
            createTag(kw);
            m_searchEdit->clear();
        }
    });
    addL->addWidget(m_btnAddNewTag, 0, Qt::AlignLeft);
    m_addNewTagWidget->hide();
    m_contentLayout->addWidget(m_addNewTagWidget);

    // 标签分组滚动区域主布局
    QWidget* tagsScrollWidget = new QWidget(m_contentWidget);
    m_tagsScrollLayout = new QVBoxLayout(tagsScrollWidget);
    m_tagsScrollLayout->setContentsMargins(0, 0, 0, 0);
    m_tagsScrollLayout->setSpacing(15);
    m_contentLayout->addWidget(tagsScrollWidget);

    m_contentLayout->addStretch();
    m_scrollArea->setWidget(m_contentWidget);
    bodyL->addWidget(m_scrollArea, 1);

    mainL->addWidget(bodyWidget, 1);
}

void TagManagerDialog::onSidebarToggled(bool checked) {
    m_sidebar->setVisible(checked);
    if (checked) {
        setMinimumWidth(400); // 180px 侧边栏 + >=220px 内容区
    } else {
        setMinimumWidth(200); // 隐藏侧边栏后，最小宽度可缩小至 200px
    }
}

void TagManagerDialog::onSidebarItemClicked(int id) {
    if (id == 0) m_currentFilter = "all";
    else if (id == 1) m_currentFilter = "uncategorized";
    else if (id == 2) m_currentFilter = "frequent";

    refreshTags();
}

void TagManagerDialog::onSearchTextChanged(const QString& text) {
    QString kw = text.trimmed();
    if (kw.isEmpty()) {
        m_addNewTagWidget->hide();
    } else {
        bool exactMatch = m_allTagCounts.contains(kw);
        if (!exactMatch) {
            m_btnAddNewTag->setText(QString("+ 新增 \"%1\"").arg(kw));
            m_addNewTagWidget->show();
        } else {
            m_addNewTagWidget->hide();
        }
    }
    refreshTags();
}

void TagManagerDialog::createTag(const QString& tagName) {
    if (tagName.isEmpty()) return;

    if (m_isMirrorSource) {
        // 双轨之一：托管库模式 -> 写入 MetadataManager / SQLite
        MetadataManager::instance().setTags(m_currentPath.toStdWString(), QStringList() << tagName);
    } else {
        // 双轨之二：磁盘导航模式 -> 写入本地 .ArcMeta.json
        QFileInfo info(m_currentPath);
        AmMetaJson amJson(info.absolutePath().toStdWString());
        amJson.load();
        ItemMeta& item = amJson.items()[info.fileName().toStdWString()];
        
        bool exists = false;
        for (const auto& t : item.tags) {
            if (QString::fromStdWString(t) == tagName) { exists = true; break; }
        }
        if (!exists) {
            item.tags.push_back(tagName.toStdWString());
            amJson.save();
        }
    }

    // 实时更新规则：新新增的标签瞬时挂载到“最近使用”区域首位
    s_sessionRecentTags.removeAll(tagName);
    s_sessionRecentTags.prepend(tagName);

    refreshTags();
}

void TagManagerDialog::refreshTags() {
    // 双轨分流拉取标签数据...
    if (m_isMirrorSource) {
        m_allTagCounts = MetadataManager::instance().getAllTags();
    } else {
        QFileInfo info(m_currentPath);
        AmMetaJson amJson(info.absolutePath().toStdWString());
        amJson.load();
        m_allTagCounts.clear();
        for (const auto& [name, item] : amJson.items()) {
            for (const auto& t : item.tags) m_allTagCounts[QString::fromStdWString(t)]++;
        }
    }

    // 清理标签滚动区域
    while (QLayoutItem* item = m_tagsScrollLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    QString searchKeyword = m_searchEdit->text().trimmed().toLower();

    // 过滤出符合搜索关键字的标签
    QMap<QString, int> filteredTagCounts;
    for (auto it = m_allTagCounts.begin(); it != m_allTagCounts.end(); ++it) {
        if (searchKeyword.isEmpty() || it.key().toLower().contains(searchKeyword)) {
            filteredTagCounts.insert(it.key(), it.value());
        }
    }

    // 1. 最近使用标签组
    QStringList filteredRecent;
    for (const QString& tag : s_sessionRecentTags) {
        if (searchKeyword.isEmpty() || tag.toLower().contains(searchKeyword)) {
            filteredRecent.append(tag);
        }
    }

    if (!filteredRecent.isEmpty() && m_currentFilter == "all") {
        QWidget* groupWidget = new QWidget();
        QVBoxLayout* groupL = new QVBoxLayout(groupWidget);
        groupL->setContentsMargins(0, 0, 0, 0);
        groupL->setSpacing(6);

        QLabel* titleLabel = new QLabel("最近使用", groupWidget);
        titleLabel->setStyleSheet("color: #1ABC9C; font-size: 11px; font-weight: bold;");
        groupL->addWidget(titleLabel);

        QWidget* flowContainer = new QWidget(groupWidget);
        FlowLayout* flowL = new FlowLayout(flowContainer, 0, 6, 6);
        for (const QString& tag : filteredRecent) {
            int count = m_allTagCounts.value(tag, 0);
            QPushButton* btn = new QPushButton(QString("• %1 (%2)").arg(tag).arg(count), flowContainer);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setStyleSheet(
                "QPushButton { background: transparent; border: 1px solid #333; color: #3498DB; border-radius: 12px; padding: 3px 10px; font-size: 11px; }"
                "QPushButton:hover { border-color: #3498DB; background-color: #2D2D30; }"
            );
            connect(btn, &QPushButton::clicked, [this, tag]() {
                createTag(tag);
            });
            flowL->addWidget(btn);
        }
        groupL->addWidget(flowContainer);
        m_tagsScrollLayout->addWidget(groupWidget);
    }

    // 2. 字母与其它 A-Z 分组标签
    QMap<QString, QMap<QString, int>> alphabetGroups;
    for (auto it = filteredTagCounts.begin(); it != filteredTagCounts.end(); ++it) {
        QString tag = it.key();
        int count = it.value();

        // 筛选逻辑
        if (m_currentFilter == "uncategorized" && count > 2) {
            continue; // 未分类标签：展现轻量/低频标签
        }
        if (m_currentFilter == "frequent" && count < 3) {
            continue; // 常用标签：展现高频标签
        }

        QChar firstChar = tag.at(0).toUpper();
        QString groupKey = "其它";
        if (firstChar >= 'A' && firstChar <= 'Z') {
            groupKey = QString(firstChar);
        }
        alphabetGroups[groupKey][tag] = count;
    }

    // 排序并绘制分组
    for (auto git = alphabetGroups.begin(); git != alphabetGroups.end(); ++git) {
        QString groupName = git.key();
        const auto& tagMap = git.value();

        QWidget* groupWidget = new QWidget();
        QVBoxLayout* groupL = new QVBoxLayout(groupWidget);
        groupL->setContentsMargins(0, 0, 0, 0);
        groupL->setSpacing(6);

        QLabel* titleLabel = new QLabel(groupName, groupWidget);
        titleLabel->setStyleSheet("color: #888888; font-size: 11px; font-weight: bold;");
        groupL->addWidget(titleLabel);

        QWidget* flowContainer = new QWidget(groupWidget);
        FlowLayout* flowL = new FlowLayout(flowContainer, 0, 6, 6);
        for (auto tit = tagMap.begin(); tit != tagMap.end(); ++tit) {
            QString tag = tit.key();
            int count = tit.value();

            QPushButton* btn = new QPushButton(QString("• %1 (%2)").arg(tag).arg(count), flowContainer);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setStyleSheet(
                "QPushButton { background: transparent; border: 1px solid #333; color: #BBB; border-radius: 12px; padding: 3px 10px; font-size: 11px; }"
                "QPushButton:hover { border-color: #1ABC9C; color: #1ABC9C; background-color: #252526; }"
            );
            connect(btn, &QPushButton::clicked, [this, tag]() {
                createTag(tag);
            });
            flowL->addWidget(btn);
        }
        groupL->addWidget(flowContainer);
        m_tagsScrollLayout->addWidget(groupWidget);
    }
}

void TagManagerDialog::resizeEvent(QResizeEvent* event) {
    FramelessDialog::resizeEvent(event);
    refreshTags(); // 自适应布局刷新流式布局
}

void TagManagerDialog::applyTheme() {
    setStyleSheet("QDialog { background-color: #1E1E1E; color: #BBB; }");
}

} // namespace ArcMeta
