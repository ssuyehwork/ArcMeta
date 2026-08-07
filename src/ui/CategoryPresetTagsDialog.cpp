#include "CategoryPresetTagsDialog.h"
#include "components/FlowLayout.h"
#include "components/TagPill.h"
#include "../meta/MetadataManager.h"
#include "../meta/CategoryRepo.h"
#include "../meta/TagRepository.h"
#include "UiHelper.h"
#include "../core/AppConfig.h"
#include <QPainter>
#include <QFontMetrics>
#include <QScrollBar>
#include <QHBoxLayout>
#include <QApplication>
#include <QStyleOption>
#include <QGuiApplication>
#include <QInputMethod>
#include <QTimer>

namespace ArcMeta {

// 初始化会话级最近使用队列静态成员变量
QStringList CategoryPresetTagsDialog::s_sessionRecentTags;

// ============================================================================
// TagItemButton 实现
// ============================================================================
TagItemButton::TagItemButton(const QString& name, int count, IconType iconType, QWidget* parent)
    : QPushButton(parent), m_tagName(name), m_count(count), m_iconType(iconType)
{
    setFixedHeight(26);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 0, 6, 0);
    layout->setSpacing(6);

    // 1. 左侧 SVG 图标
    m_iconLabel = new QLabel(this);
    m_iconLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    QIcon icon;
    if (m_iconType == Clock) {
        icon = UiHelper::getIcon("clock", QColor("#888888"), 14);
    } else {
        icon = UiHelper::getIcon("circle_filled", QColor("#888888"), 10);
    }
    m_iconLabel->setPixmap(icon.pixmap(14, 14));
    layout->addWidget(m_iconLabel);

    // 2. 富文本标签
    m_textLabel = new QLabel(this);
    m_textLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    layout->addWidget(m_textLabel, 1);

    setSelected(false);
}

void TagItemButton::setSelected(bool selected) {
    m_selected = selected;

    QString nameColor = m_selected ? "#FFFFFF" : "#EEEEEE";
    QString countColor = m_selected ? "#D4D4D4" : "#888888";

    QString displayText = QString("<span style='color:%1; font-weight:bold; font-size:12px;'>%2</span>").arg(nameColor).arg(m_tagName);
    if (m_count > 0) {
        displayText += QString(" <span style='color:%1; font-size:11px;'>(%2)</span>").arg(countColor).arg(m_count);
    }
    m_textLabel->setText(displayText);

    update();
}

void TagItemButton::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QRect r = rect().adjusted(1, 1, -1, -1);

    QColor bg, border;
    if (m_selected) {
        bg = QColor("#3498db");      // 品牌选中蓝
        border = QColor("#5dade2");  // 选中高亮边
    } else {
        bg = QColor("transparent");
        border = QColor("transparent");
    }

    painter.setBrush(bg);
    painter.setPen(QPen(border, 1));
    painter.drawRoundedRect(r, 4, 4); // 规范圆角 4px
}


// ============================================================================
// CategoryPresetTagsDialog 实现
// ============================================================================
CategoryPresetTagsDialog::CategoryPresetTagsDialog(const QString& folderName,
                                                   const std::vector<std::wstring>& initialTags,
                                                   QWidget* parent)
    : FramelessDialog("设置自动标签", parent), m_folderName(folderName), m_initialTags(initialTags)
{
    setVisibleButtons(Close);

    // 一体化单窗口架构：开启拉伸调整尺寸，设定合理的初始大小和最小尺寸
    setMinimumSize(500, 500);
    resize(500, 520);

    // 初始化界面与分栏布局
    initLayout();

    // 默认加载一次全量数据
    m_allTags = CategoryRepo::getGlobalUniqueTags();

    // 初始化 s_sessionRecentTags 的默认填充
    if (s_sessionRecentTags.isEmpty()) {
        QList<QPair<QString, int>> tempSorted;
        for (auto it = m_allTags.begin(); it != m_allTags.end(); ++it) {
            tempSorted.append({it.key(), it.value()});
        }
        std::sort(tempSorted.begin(), tempSorted.end(), [](const QPair<QString, int>& a, const QPair<QString, int>& b) {
            return a.second > b.second;
        });
        for (int i = 0; i < qMin(5, tempSorted.size()); ++i) {
            s_sessionRecentTags.append(tempSorted[i].first);
        }
    }

    refreshSidebar();
    refreshList();
}

CategoryPresetTagsDialog::~CategoryPresetTagsDialog() {
}

void CategoryPresetTagsDialog::initLayout() {
    auto* layout = new QVBoxLayout(m_contentArea);
    layout->setContentsMargins(20, 15, 20, 15);
    layout->setSpacing(10);

    // 1. 文件夹名展示区（只读）
    auto* folderLayout = new QVBoxLayout();
    folderLayout->setSpacing(4);

    auto* folderLabel = new QLabel("文件夹名", m_contentArea);
    folderLabel->setStyleSheet("color: #888888; font-size: 11px; font-weight: bold;");
    folderLayout->addWidget(folderLabel);

    auto* folderEdit = new QLineEdit(m_folderName, m_contentArea);
    folderEdit->setEnabled(false);
    folderEdit->setMinimumHeight(30);
    folderEdit->setStyleSheet(
        "QLineEdit {"
        "  background-color: #252526;"
        "  border: 1px solid #333333;"
        "  border-radius: 4px;"
        "  padding: 0px 8px;"
        "  color: #888888;"
        "  font-size: 12px;"
        "}"
    );
    folderLayout->addWidget(folderEdit);
    layout->addLayout(folderLayout);

    // 2. 自动添加标签区（已选胶囊容器）
    auto* tagsLayout = new QVBoxLayout();
    tagsLayout->setSpacing(4);

    auto* tagsLabel = new QLabel("自动添加标签", m_contentArea);
    tagsLabel->setStyleSheet("color: #888888; font-size: 11px; font-weight: bold;");
    tagsLayout->addWidget(tagsLabel);

    m_tagsContainer = new QWidget(m_contentArea);
    m_tagsContainer->setObjectName("TagsContainer");
    m_tagsContainer->setMinimumHeight(70);
    m_tagsContainer->setCursor(Qt::PointingHandCursor);
    m_tagsContainer->setStyleSheet(
        "QWidget#TagsContainer {"
        "  background-color: #252526;"
        "  border: 1px solid #3C3C3C;"
        "  border-radius: 4px;"
        "}"
    );

    m_tagsFlow = new FlowLayout(m_tagsContainer, 6, 5, 5);
    m_tagsContainer->setLayout(m_tagsFlow);
    m_tagsContainer->installEventFilter(this); // 点击空白聚焦搜索框

    auto* scrollArea = new QScrollArea(m_contentArea);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFixedHeight(80);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setStyleSheet("QScrollArea { background: transparent; }");
    scrollArea->setWidget(m_tagsContainer);
    tagsLayout->addWidget(scrollArea);
    layout->addLayout(tagsLayout);

    // 3. 一体化标签选择面板区
    auto* pickerLayout = new QVBoxLayout();
    pickerLayout->setSpacing(6);

    // 3.1 顶部搜索框
    m_searchEdit = new QLineEdit(m_contentArea);
    m_searchEdit->setAttribute(Qt::WA_InputMethodEnabled, true);
    m_searchEdit->setInputMethodHints(Qt::ImhPreferLowercase | Qt::ImhNoAutoUppercase);
    m_searchEdit->setPlaceholderText("搜索...");
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setMinimumHeight(30);
    m_searchEdit->setStyleSheet(
        "QLineEdit {"
        "  background-color: #2D2D2D;"
        "  border: 1px solid #444444;"
        "  border-radius: 4px;"
        "  padding: 0px 8px 0px 2px;"
        "  color: #FFFFFF;"
        "  font-size: 12px;"
        "}"
        "QLineEdit:focus { border: 1px solid #3498db; }"
    );

    // 1:1 矢量图标配置
    m_searchEdit->addAction(UiHelper::getIcon("search", QColor("#888888"), 14), QLineEdit::LeadingPosition);
    auto* sidebarAction = m_searchEdit->addAction(UiHelper::getIcon("layout_grid", QColor("#888888"), 14), QLineEdit::TrailingPosition);
    connect(sidebarAction, &QAction::triggered, this, &CategoryPresetTagsDialog::toggleSidebar);
    m_searchEdit->addAction(UiHelper::getIcon("filter_funnel_outline", QColor("#888888"), 14), QLineEdit::TrailingPosition);

    m_searchEdit->installEventFilter(this);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &CategoryPresetTagsDialog::onSearchTextChanged);
    pickerLayout->addWidget(m_searchEdit);

    // 3.2 拖拽分栏架构 (QSplitter)
    m_splitter = new QSplitter(Qt::Horizontal, m_contentArea);
    m_splitter->setStyleSheet("QSplitter::handle { background-color: #333333; }");
    m_splitter->setHandleWidth(1);
    m_splitter->setChildrenCollapsible(false);

    // 3.2.1 左侧分栏：标签组侧边栏 (可调分栏，初始设定 120px)
    m_sidebarWidget = new QWidget(m_splitter);
    m_sidebarWidget->setMinimumWidth(80);
    m_sidebarWidget->setMaximumWidth(200);
    m_sidebarWidget->setStyleSheet("background: transparent;");
    auto* sidebarL = new QVBoxLayout(m_sidebarWidget);
    sidebarL->setContentsMargins(0, 0, 0, 0);
    sidebarL->setSpacing(0);

    m_sidebarScroll = new QScrollArea(m_sidebarWidget);
    m_sidebarScroll->setWidgetResizable(true);
    m_sidebarScroll->setFrameShape(QFrame::NoFrame);
    m_sidebarScroll->setStyleSheet("QScrollArea { background: transparent; }");
    m_sidebarScroll->verticalScrollBar()->setStyleSheet(
        "QScrollBar:vertical { background: transparent; width: 4px; }"
        "QScrollBar::handle:vertical { background: #333; border-radius: 2px; }"
    );

    m_sidebarContainer = new QWidget(m_sidebarScroll);
    m_sidebarContainer->setStyleSheet("background: transparent;");
    m_sidebarLayout = new QVBoxLayout(m_sidebarContainer);
    m_sidebarLayout->setContentsMargins(0, 0, 4, 0);
    m_sidebarLayout->setSpacing(4);
    m_sidebarLayout->addStretch(1);

    m_sidebarScroll->setWidget(m_sidebarContainer);
    sidebarL->addWidget(m_sidebarScroll, 1);
    m_splitter->addWidget(m_sidebarWidget);

    // 3.2.2 右侧分栏：滚动列表区
    m_scrollArea = new QScrollArea(m_splitter);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setStyleSheet("QScrollArea { background: transparent; }");
    m_scrollArea->verticalScrollBar()->setStyleSheet(
        "QScrollBar:vertical { background: transparent; width: 6px; margin: 0; }"
        "QScrollBar::handle:vertical { background: #444; border-radius: 3px; min-height: 20px; }"
        "QScrollBar::handle:vertical:hover { background: #666; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
    );

    m_scrollContainer = new QWidget(m_scrollArea);
    m_scrollContainer->setStyleSheet("background: transparent;");
    m_scrollLayout = new QVBoxLayout(m_scrollContainer);
    m_scrollLayout->setContentsMargins(8, 0, 4, 0);
    m_scrollLayout->setSpacing(12);

    // 最近使用网格
    m_recentGroup = new QWidget(m_scrollContainer);
    auto* recentLayout = new QVBoxLayout(m_recentGroup);
    recentLayout->setContentsMargins(0, 0, 0, 0);
    recentLayout->setSpacing(6);

    m_recentLabel = new QLabel(m_recentGroup);
    m_recentLabel->setStyleSheet("color: #888888; font-size: 11px; font-weight: bold;");
    recentLayout->addWidget(m_recentLabel);

    auto* recentLine = new QFrame(m_recentGroup);
    recentLine->setFixedHeight(1);
    recentLine->setStyleSheet("background-color: #333333; border: none;");
    recentLayout->addWidget(recentLine);

    m_recentContainer = new QWidget(m_recentGroup);
    m_recentGrid = new QGridLayout(m_recentContainer);
    m_recentGrid->setContentsMargins(0, 2, 0, 2);
    m_recentGrid->setSpacing(6);
    m_recentContainer->setLayout(m_recentGrid);
    recentLayout->addWidget(m_recentContainer);
    m_scrollLayout->addWidget(m_recentGroup);

    // 其它网格
    m_globalGroup = new QWidget(m_scrollContainer);
    auto* globalLayout = new QVBoxLayout(m_globalGroup);
    globalLayout->setContentsMargins(0, 0, 0, 0);
    globalLayout->setSpacing(6);

    m_globalLabel = new QLabel(m_globalGroup);
    m_globalLabel->setStyleSheet("color: #888888; font-size: 11px; font-weight: bold;");
    globalLayout->addWidget(m_globalLabel);

    auto* globalLine = new QFrame(m_globalGroup);
    globalLine->setFixedHeight(1);
    globalLine->setStyleSheet("background-color: #333333; border: none;");
    globalLayout->addWidget(globalLine);

    m_globalContainer = new QWidget(m_globalGroup);
    m_globalGrid = new QGridLayout(m_globalContainer);
    m_globalGrid->setContentsMargins(0, 2, 0, 2);
    m_globalGrid->setSpacing(6);
    m_globalContainer->setLayout(m_globalGrid);
    globalLayout->addWidget(m_globalContainer);
    m_scrollLayout->addWidget(m_globalGroup);

    m_scrollLayout->addStretch(1);
    m_scrollArea->setWidget(m_scrollContainer);
    m_splitter->addWidget(m_scrollArea);

    // 设置初始分栏比例（左120px，右填充）
    m_splitter->setSizes({120, 340});
    pickerLayout->addWidget(m_splitter, 1);

    // 3.3 底部提示条
    m_hintLabel = new QLabel("切换 Tab    移动 ↑↓←→    选中 ↵", m_contentArea);
    m_hintLabel->setStyleSheet("color: #666666; font-size: 11px;");
    m_hintLabel->setAlignment(Qt::AlignCenter);
    pickerLayout->addWidget(m_hintLabel);

    layout->addLayout(pickerLayout, 1);

    // 加载初始预设 Pill
    for (const auto& tagW : m_initialTags) {
        addTagPill(QString::fromStdWString(tagW));
    }

    // 4. 底部动作按钮
    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    auto* btnCancel = new QPushButton("取消", m_contentArea);
    btnCancel->setFixedSize(80, 32);
    btnCancel->setCursor(Qt::PointingHandCursor);
    btnCancel->setStyleSheet(
        "QPushButton { background-color: transparent; color: #888; border: 1px solid #444; border-radius: 4px; } "
        "QPushButton:hover { color: #EEE; background-color: #333; }"
    );
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(btnCancel);

    auto* btnOk = new QPushButton("保存设置", m_contentArea);
    btnOk->setFixedSize(90, 32);
    btnOk->setCursor(Qt::PointingHandCursor);
    btnOk->setDefault(true);
    btnOk->setStyleSheet(
        "QPushButton { background-color: #3498db; color: white; border: none; border-radius: 4px; font-weight: bold; } "
        "QPushButton:hover { background-color: #2980b9; }"
    );
    connect(btnOk, &QPushButton::clicked, this, &QDialog::accept);
    btnLayout->addWidget(btnOk);

    layout->addLayout(btnLayout);

    // 读取并设置侧边栏可见性本地化用户习惯
    bool sidebarVisible = AppConfig::instance().getValue("TagPicker/SidebarVisible", true).toBool();
    setSidebarVisible(sidebarVisible);
}

void CategoryPresetTagsDialog::toggleSidebar() {
    bool isVisible = m_sidebarWidget->isVisible();
    setSidebarVisible(!isVisible);
}

void CategoryPresetTagsDialog::setSidebarVisible(bool visible) {
    m_sidebarWidget->setVisible(visible);
    m_splitter->handle(1)->setVisible(visible);

    // 动态限制最小宽度
    if (visible) {
        setMinimumWidth(500);
        m_splitter->setSizes({120, width() - 120});
    } else {
        setMinimumWidth(350);
        m_splitter->setSizes({0, width()});
    }

    AppConfig::instance().setValue("TagPicker/SidebarVisible", visible);
    AppConfig::instance().sync();
}

void CategoryPresetTagsDialog::addTagPill(const QString& tagName) {
    auto* pill = new TagPill(tagName, m_tagsContainer);
    pill->setProperty("tagText", tagName);
    m_tagsFlow->addWidget(pill);

    connect(pill, &TagPill::deleteRequested, this, &CategoryPresetTagsDialog::onRemoveTag);
}

void CategoryPresetTagsDialog::recordTagUsage(const QString& tagName) {
    QString cleaned = tagName.trimmed();
    if (cleaned.isEmpty()) return;

    s_sessionRecentTags.removeAll(cleaned);
    s_sessionRecentTags.prepend(cleaned);
    if (s_sessionRecentTags.size() > 10) {
        s_sessionRecentTags.removeLast();
    }
}

void CategoryPresetTagsDialog::onTagSelectedFromPicker(const QString& tagName) {
    // 0 毫秒即时刷新会话最近使用队列
    recordTagUsage(tagName);

    bool exists = false;
    for (int i = 0; i < m_tagsFlow->count(); ++i) {
        auto* item = m_tagsFlow->itemAt(i);
        if (item->widget()) {
            auto* pill = qobject_cast<TagPill*>(item->widget());
            if (pill && pill->property("tagText").toString() == tagName) {
                exists = true;
                break;
            }
        }
    }

    if (!exists) {
        addTagPill(tagName);
        m_tagsContainer->updateGeometry();
    }

    // 0 毫秒即时刷新左侧与右侧网格面板
    refreshSidebar();
    refreshList();
}

void CategoryPresetTagsDialog::onRemoveTag(const QString& tagName) {
    for (int i = 0; i < m_tagsFlow->count(); ++i) {
        auto* item = m_tagsFlow->itemAt(i);
        if (item->widget()) {
            auto* pill = qobject_cast<TagPill*>(item->widget());
            if (pill && pill->property("tagText").toString() == tagName) {
                m_tagsFlow->removeWidget(pill);
                pill->deleteLater();
                break;
            }
        }
    }
}

void CategoryPresetTagsDialog::refreshSidebar() {
    qDeleteAll(m_sidebarButtons);
    m_sidebarButtons.clear();
    m_selectedSidebarIndex = 0;

    for (int i = m_sidebarLayout->count() - 1; i >= 0; --i) {
        QLayoutItem* item = m_sidebarLayout->itemAt(i);
        if (item->widget()) {
            item->widget()->deleteLater();
            m_sidebarLayout->removeItem(item);
            delete item;
        }
    }

    // 统计未分类
    QSet<QString> groupedTags;
    auto allGroups = TagRepository::getAllGroups();
    for (const auto& g : allGroups) {
        for (const auto& t : g.tags) groupedTags.insert(t.trimmed());
    }
    int uncategorizedCount = 0;
    for (auto it = m_allTags.begin(); it != m_allTags.end(); ++it) {
        if (!groupedTags.contains(it.key())) uncategorizedCount++;
    }

    auto createSidebarBtn = [this](const QString& label, int count, int groupId) {
        QPushButton* btn = new QPushButton(m_sidebarContainer);
        btn->setProperty("groupId", groupId);
        btn->setText(QString("%1 (%2)").arg(label).arg(count));
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(
            "QPushButton { text-align: left; background: transparent; border: none; color: #AAAAAA; padding: 6px 8px; font-size: 11px; border-radius: 4px; }"
            "QPushButton:hover { background-color: #2D2D2D; color: #EEEEEE; }"
        );
        m_sidebarLayout->insertWidget(m_sidebarLayout->count() - 1, btn);
        m_sidebarButtons.append(btn);

        connect(btn, &QPushButton::clicked, this, [this, btn]() {
            int idx = m_sidebarButtons.indexOf(btn);
            if (idx >= 0) {
                m_selectedSidebarIndex = idx;
                m_focusZone = SidebarZone;
                updateSidebarHighlight();
            }
        });
    };

    createSidebarBtn("全部", m_allTags.size(), -1);
    createSidebarBtn("未分类", uncategorizedCount, -2);

    for (const auto& g : allGroups) {
        createSidebarBtn(g.name, g.tags.size(), g.id);
    }

    updateSidebarHighlight();
}

void CategoryPresetTagsDialog::updateSidebarHighlight() {
    if (m_selectedSidebarIndex < 0 || m_selectedSidebarIndex >= m_sidebarButtons.size()) {
        return;
    }

    m_selectedGroupId = m_sidebarButtons[m_selectedSidebarIndex]->property("groupId").toInt();

    for (int i = 0; i < m_sidebarButtons.size(); ++i) {
        QPushButton* btn = m_sidebarButtons[i];
        bool isActive = (i == m_selectedSidebarIndex);
        if (isActive) {
            btn->setStyleSheet(
                "QPushButton { text-align: left; background-color: #2A2A2A; border: none; color: #1abc9c; padding: 6px 8px; font-weight: bold; font-size: 11px; border-radius: 4px; }"
            );
            m_sidebarScroll->ensureWidgetVisible(btn);
        } else {
            btn->setStyleSheet(
                "QPushButton { text-align: left; background: transparent; border: none; color: #AAAAAA; padding: 6px 8px; font-size: 11px; border-radius: 4px; }"
                "QPushButton:hover { background-color: #2D2D2D; color: #EEEEEE; }"
            );
        }
    }

    if (m_focusZone == SidebarZone) {
        m_sidebarWidget->setStyleSheet("QWidget { border: 1px solid #1abc9c; border-radius: 4px; }");
    } else {
        m_sidebarWidget->setStyleSheet("QWidget { border: none; }");
    }

    refreshList();
}

void CategoryPresetTagsDialog::refreshList() {
    qDeleteAll(m_visibleButtons);
    m_visibleButtons.clear();
    m_selectedIndex = -1;

    QLayoutItem* item;
    while ((item = m_recentGrid->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    while ((item = m_globalGrid->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    QString searchFilter = m_searchEdit->text().trimmed().toLower();

    // 收集符合分栏和筛选条件的标签
    QSet<QString> activeTags;
    QSet<QString> groupedTags;
    auto allGroups = TagRepository::getAllGroups();
    for (const auto& g : allGroups) {
        for (const auto& t : g.tags) groupedTags.insert(t.trimmed());
    }

    if (m_selectedGroupId == -1) {
        for (auto it = m_allTags.begin(); it != m_allTags.end(); ++it) activeTags.insert(it.key());
    } else if (m_selectedGroupId == -2) {
        for (auto it = m_allTags.begin(); it != m_allTags.end(); ++it) {
            if (!groupedTags.contains(it.key())) activeTags.insert(it.key());
        }
    } else {
        for (const auto& g : allGroups) {
            if (g.id == m_selectedGroupId) {
                for (const auto& t : g.tags) {
                    QString tagStr = t.trimmed();
                    if (!tagStr.isEmpty() && m_allTags.contains(tagStr)) activeTags.insert(tagStr);
                }
                break;
            }
        }
    }

    QSet<QString> displayedRecentTags;

    // 渲染最近使用网格（基于会话静态缓存，0毫秒即时刷新置顶呈现）
    int recentCount = 0;
    for (const auto& tagName : s_sessionRecentTags) {
        if (!activeTags.contains(tagName)) continue;
        if (!searchFilter.isEmpty() && !tagName.toLower().contains(searchFilter)) continue;

        int count = m_allTags.value(tagName, 0);
        auto* btn = new TagItemButton(tagName, count, TagItemButton::Clock, m_recentContainer);
        m_recentGrid->addWidget(btn, recentCount / 2, recentCount % 2);
        m_visibleButtons.append(btn);
        displayedRecentTags.insert(tagName);
        recentCount++;

        connect(btn, &QPushButton::clicked, this, [this, tagName]() {
            onTagSelectedFromPicker(tagName);
        });
        btn->installEventFilter(this);
    }

    // 渲染其它网格
    int globalCount = 0;
    for (auto it = m_allTags.begin(); it != m_allTags.end(); ++it) {
        QString tagName = it.key();
        int count = it.value();

        if (!activeTags.contains(tagName)) continue;
        if (displayedRecentTags.contains(tagName)) continue;
        if (!searchFilter.isEmpty() && !tagName.toLower().contains(searchFilter)) continue;

        auto* btn = new TagItemButton(tagName, count, TagItemButton::CircleFilled, m_globalContainer);
        m_globalGrid->addWidget(btn, globalCount / 2, globalCount % 2);
        m_visibleButtons.append(btn);
        globalCount++;

        connect(btn, &QPushButton::clicked, this, [this, tagName]() {
            onTagSelectedFromPicker(tagName);
        });
        btn->installEventFilter(this);
    }

    m_recentLabel->setText(QString("最近使用 (%1)").arg(recentCount));
    m_recentGroup->setVisible(recentCount > 0);

    m_globalLabel->setText(QString("其它 (%1)").arg(globalCount));
    m_globalGroup->setVisible(globalCount > 0);

    if (!m_visibleButtons.isEmpty()) {
        m_selectedIndex = 0;
        updateSelectionHighlight();
    }
}

void CategoryPresetTagsDialog::onSearchTextChanged(const QString&) {
    refreshList();
}

void CategoryPresetTagsDialog::updateSelectionHighlight() {
    for (int i = 0; i < m_visibleButtons.size(); ++i) {
        bool shouldHighlight = (m_focusZone == GridZone) ? (i == m_selectedIndex) : false;
        m_visibleButtons[i]->setSelected(shouldHighlight);
    }

    if (m_selectedIndex >= 0 && m_selectedIndex < m_visibleButtons.size()) {
        auto* btn = m_visibleButtons[m_selectedIndex];
        m_scrollArea->ensureWidgetVisible(btn);
    }
}

void CategoryPresetTagsDialog::selectCurrent() {
    if (m_selectedIndex >= 0 && m_selectedIndex < m_visibleButtons.size()) {
        QString tagName = m_visibleButtons[m_selectedIndex]->tagName();
        onTagSelectedFromPicker(tagName);
    } else {
        QString searchFilter = m_searchEdit->text().trimmed();
        if (!searchFilter.isEmpty()) {
            onTagSelectedFromPicker(searchFilter);
            m_searchEdit->clear();
        }
    }
}

void CategoryPresetTagsDialog::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        reject();
        event->accept();
        return;
    }

    // Tab 焦点轮转
    if (event->key() == Qt::Key_Tab) {
        if (m_focusZone == SearchBar) {
            if (m_sidebarWidget->isVisible()) {
                m_focusZone = SidebarZone;
                m_searchEdit->clearFocus();
                updateSidebarHighlight();
                updateSelectionHighlight();
            } else {
                m_focusZone = GridZone;
                m_searchEdit->clearFocus();
                updateSelectionHighlight();
            }
        } else if (m_focusZone == SidebarZone) {
            m_focusZone = GridZone;
            m_sidebarWidget->setStyleSheet("QWidget { border: none; }");
            updateSelectionHighlight();
        } else {
            m_focusZone = SearchBar;
            m_searchEdit->setFocus();
            updateSelectionHighlight();
        }
        event->accept();
        return;
    }

    // 左栏操作
    if (m_focusZone == SidebarZone) {
        int count = m_sidebarButtons.size();
        if (count > 0) {
            if (event->key() == Qt::Key_Down) {
                m_selectedSidebarIndex = (m_selectedSidebarIndex + 1) % count;
                updateSidebarHighlight();
                event->accept();
                return;
            } else if (event->key() == Qt::Key_Up) {
                m_selectedSidebarIndex = (m_selectedSidebarIndex - 1 + count) % count;
                updateSidebarHighlight();
                event->accept();
                return;
            } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
                m_focusZone = GridZone;
                updateSidebarHighlight();
                updateSelectionHighlight();
                event->accept();
                return;
            }
        }
    }

    // 右栏操作
    int btnCount = m_visibleButtons.size();
    if (btnCount > 0) {
        if (event->key() == Qt::Key_Down || event->key() == Qt::Key_Right) {
            m_selectedIndex = (m_selectedIndex + 1) % btnCount;
            updateSelectionHighlight();
            event->accept();
            return;
        } else if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Left) {
            m_selectedIndex = (m_selectedIndex - 1 + btnCount) % btnCount;
            updateSelectionHighlight();
            event->accept();
            return;
        } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            selectCurrent();
            event->accept();
            return;
        }
    } else {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            QString searchFilter = m_searchEdit->text().trimmed();
            if (!searchFilter.isEmpty()) {
                onTagSelectedFromPicker(searchFilter);
                m_searchEdit->clear();
                event->accept();
                return;
            }
        }
    }

    FramelessDialog::keyPressEvent(event);
}

std::vector<std::wstring> CategoryPresetTagsDialog::getPresetTags() const {
    std::vector<std::wstring> tags;
    for (int i = 0; i < m_tagsFlow->count(); ++i) {
        auto* item = m_tagsFlow->itemAt(i);
        if (item->widget()) {
            auto* pill = qobject_cast<TagPill*>(item->widget());
            if (pill) {
                tags.push_back(pill->property("tagText").toString().toStdWString());
            }
        }
    }
    return tags;
}

bool CategoryPresetTagsDialog::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_tagsContainer && event->type() == QEvent::MouseButtonPress) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            m_focusZone = SearchBar;
            m_searchEdit->setFocus();
            updateSelectionHighlight();
            return true;
        }
    }

    // QLineEdit 上的按键拦截过滤
    if (watched == m_searchEdit && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);

        if (QGuiApplication::inputMethod()->isVisible()) {
            return false; // 输入法候选，放行上屏
        }

        int key = keyEvent->key();
        if (key == Qt::Key_Down || key == Qt::Key_Up ||
            key == Qt::Key_Return || key == Qt::Key_Enter ||
            key == Qt::Key_Escape || key == Qt::Key_Tab)
        {
            this->keyPressEvent(keyEvent);
            return true;
        }
    }

    // 鼠标 Hover 切换右栏高亮
    if (event->type() == QEvent::HoverEnter || event->type() == QEvent::Enter) {
        auto* btn = qobject_cast<TagItemButton*>(watched);
        if (btn) {
            int idx = m_visibleButtons.indexOf(btn);
            if (idx >= 0 && idx != m_selectedIndex) {
                m_selectedIndex = idx;
                m_focusZone = GridZone;
                updateSelectionHighlight();
            }
        }
    }

    return FramelessDialog::eventFilter(watched, event);
}

} // namespace ArcMeta
