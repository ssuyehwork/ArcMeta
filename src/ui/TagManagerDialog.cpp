#include "TagManagerDialog.h"
#include "UiHelper.h"
#include "StyleLibrary.h"
#include "SvgIconRenderer.h"
#include "../meta/CategoryRepo.h"
#include "../meta/AmMetaJson.h"
#include <QApplication>
#include <QScreen>
#include <QFileInfo>

namespace ArcMeta {

void TagManagerDialog::showDialog(QWidget* parent, const QString& currentPath, bool isMirrorSource) {
    TagManagerDialog* dlg = new TagManagerDialog(currentPath, isMirrorSource, parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->exec();
}

TagManagerDialog::TagManagerDialog(const QString& currentPath, bool isMirrorSource, QWidget* parent)
    : FramelessDialog("标签管理", parent), m_currentPath(currentPath), m_isMirrorSource(isMirrorSource) {

    // 默认最小尺寸硬约束：有侧边栏时 400px 宽，固定 180px 侧边栏
    setMinimumSize(400, 350);
    resize(550, 450);

    initContent();
    applyTheme();
    refreshTags();
}

void TagManagerDialog::initContent() {
    QVBoxLayout* mainL = new QVBoxLayout(m_contentArea);
    mainL->setContentsMargins(0, 0, 0, 0);
    mainL->setSpacing(0);

    // ================= 1. 顶部操作栏（透明搜索框 + 右侧侧边栏切换按钮） =================
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
        "QLineEdit { background: transparent; border: 1px solid #444; border-radius: 4px; padding: 0 8px; color: #EEE; }"
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

    // 折叠侧边栏按钮 (使用 sidebar)
    m_btnToggleSidebar = new QPushButton(topBar);
    m_btnToggleSidebar->setFixedSize(24, 24);
    m_btnToggleSidebar->setCheckable(true);
    m_btnToggleSidebar->setChecked(true);
    m_btnToggleSidebar->setIcon(UiHelper::getIcon("sidebar", QColor("#AAAAAA"), 16));
    m_btnToggleSidebar->setStyleSheet(
        "QPushButton { background: transparent; border: none; border-radius: 3px; }"
        "QPushButton:hover { background-color: #3E3E42; }"
    );
    connect(m_btnToggleSidebar, &QPushButton::toggled, this, &TagManagerDialog::onSidebarToggled);
    topL->addWidget(m_btnToggleSidebar);

    mainL->addWidget(topBar);

    // ================= 2. 中部核心分割区域（左 180px 侧边栏 + 右内容区） =================
    QWidget* bodyWidget = new QWidget(this);
    QHBoxLayout* bodyL = new QHBoxLayout(bodyWidget);
    bodyL->setContentsMargins(0, 0, 0, 0);
    bodyL->setSpacing(0);

    // A. 固定 180px 侧边栏
    m_sidebar = new QFrame(bodyWidget);
    m_sidebar->setFixedWidth(180);
    m_sidebar->setStyleSheet("background-color: #252526; border-right: 1px solid #333;");
    m_sidebarLayout = new QVBoxLayout(m_sidebar);
    m_sidebarLayout->setContentsMargins(10, 10, 10, 10);
    m_sidebarLayout->setSpacing(6);

    QLabel* sideTitle = new QLabel("分类导航", m_sidebar);
    sideTitle->setStyleSheet("color: #888; font-size: 11px; font-weight: bold;");
    m_sidebarLayout->addWidget(sideTitle);

    // 追加系统与侧边栏选择项...
    m_sidebarLayout->addStretch();
    bodyL->addWidget(m_sidebar);

    // B. 右侧标签显示区
    m_scrollArea = new QScrollArea(bodyWidget);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setStyleSheet("QScrollArea { border: none; background: transparent; }");

    m_contentWidget = new QWidget();
    m_contentLayout = new QVBoxLayout(m_contentWidget);
    m_contentLayout->setContentsMargins(15, 15, 15, 15);
    m_contentLayout->setSpacing(15);

    // 动态新增提示胶囊 (默认隐藏)
    m_addNewTagWidget = new QWidget(m_contentWidget);
    QHBoxLayout* addL = new QHBoxLayout(m_addNewTagWidget);
    addL->setContentsMargins(0, 0, 0, 0);
    m_btnAddNewTag = new QPushButton(m_addNewTagWidget);
    m_btnAddNewTag->setCursor(Qt::PointingHandCursor);
    m_btnAddNewTag->setStyleSheet(
        "QPushButton { background: #1C97EA; color: #FFF; border: none; border-radius: 4px; padding: 4px 12px; font-weight: bold; }"
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

    // 最近使用区块
    QLabel* lblRecent = new QLabel("最近使用", m_contentWidget);
    lblRecent->setStyleSheet("color: #1ABC9C; font-size: 12px; font-weight: bold;");
    m_contentLayout->addWidget(lblRecent);

    m_recentTagsContainer = new QWidget(m_contentWidget);
    m_recentFlowLayout = new FlowLayout(m_recentTagsContainer, 0, 8, 6);
    m_contentLayout->addWidget(m_recentTagsContainer);

    // 全部标签区块
    QLabel* lblAll = new QLabel("全部标签", m_contentWidget);
    lblAll->setStyleSheet("color: #888; font-size: 12px; font-weight: bold;");
    m_contentLayout->addWidget(lblAll);

    m_allTagsContainer = new QWidget(m_contentWidget);
    m_allFlowLayout = new FlowLayout(m_allTagsContainer, 0, 8, 6);
    m_contentLayout->addWidget(m_allTagsContainer);

    m_contentLayout->addStretch();
    m_scrollArea->setWidget(m_contentWidget);
    bodyL->addWidget(m_scrollArea, 1);

    mainL->addWidget(bodyWidget, 1);
}

void TagManagerDialog::onSidebarToggled(bool checked) {
    m_sidebar->setVisible(checked);
    if (checked) {
        setMinimumWidth(400); // 180px 侧边栏 + 220px 内容区
    } else {
        setMinimumWidth(200); // 隐藏侧边栏后调小最小宽度限制
    }
}

void TagManagerDialog::onSearchTextChanged(const QString& text) {
    QString kw = text.trimmed();
    if (kw.isEmpty()) {
        m_addNewTagWidget->hide();
        m_scrollArea->show();
    } else {
        bool exactMatch = m_allTagCounts.contains(kw);
        if (!exactMatch) {
            m_btnAddNewTag->setText(QString("+ 新增 \"%1\"").arg(kw));
            m_addNewTagWidget->show();
        } else {
            m_addNewTagWidget->hide();
        }
    }
}

void TagManagerDialog::createTag(const QString& tagName) {
    if (tagName.isEmpty()) return;

    if (m_isMirrorSource) {
        // 托管库模式：直接存入 MetadataManager / SQLite
        MetadataManager::instance().setTags(m_currentPath.toStdWString(), QStringList() << tagName);
    } else {
        // 磁盘导航模式：直接写入本地 .ArcMeta.json
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

    // 实时挂载到“最近使用”首位
    m_recentTags.removeAll(tagName);
    m_recentTags.prepend(tagName);

    refreshTags();
}

void TagManagerDialog::refreshTags() {
    // 双轨拉取标签库...
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

    // 渲染最近使用流式布局
    while (QLayoutItem* item = m_recentFlowLayout->takeAt(0)) {
        delete item->widget(); delete item;
    }
    for (const QString& tag : m_recentTags) {
        QPushButton* btn = new QPushButton(tag, m_recentTagsContainer);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet("QPushButton { background: #2D2D30; border: 1px solid #3498DB; color: #3498DB; border-radius: 4px; padding: 3px 8px; font-size: 12px; }");
        m_recentFlowLayout->addWidget(btn);
    }

    // 渲染全部标签流式布局...
    while (QLayoutItem* item = m_allFlowLayout->takeAt(0)) {
        delete item->widget(); delete item;
    }
    for (auto it = m_allTagCounts.begin(); it != m_allTagCounts.end(); ++it) {
        QPushButton* btn = new QPushButton(QString("%1 (%2)").arg(it.key()).arg(it.value()), m_allTagsContainer);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet("QPushButton { background: transparent; border: 1px solid #444; color: #AAA; border-radius: 4px; padding: 3px 8px; font-size: 12px; }");
        m_allFlowLayout->addWidget(btn);
    }
}

void TagManagerDialog::resizeEvent(QResizeEvent* event) {
    FramelessDialog::resizeEvent(event);
    if (m_recentFlowLayout) m_recentFlowLayout->activate();
    if (m_allFlowLayout) m_allFlowLayout->activate();
}

void TagManagerDialog::applyTheme() {
    setStyleSheet("QDialog { background-color: #1E1E1E; color: #BBB; }");
}

} // namespace ArcMeta
