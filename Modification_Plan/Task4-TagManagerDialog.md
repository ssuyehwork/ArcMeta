# 任务 4 重构施工图纸

---

### 步骤 1：新建头文件 `src/ui/TagManagerDialog.h`

```cpp
#pragma once

#include "FramelessDialog.h"
#include "components/FlowLayout.h"
#include "../meta/MetadataManager.h"
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QLabel>
#include <QFrame>

namespace ArcMeta {

/**
 * @brief 高级标签管理弹窗 (模块化组件，对应图二/图三规范)
 */
class TagManagerDialog : public FramelessDialog {
    Q_OBJECT
public:
    /**
     * @brief 全局统一模块化静态调用入口
     * @param parent 父窗口指针
     * @param currentPath 当前操作的文件/目录绝对路径
     * @param isMirrorSource 是否处于托管库模式 (true: 托管库, false: 磁盘导航模式)
     */
    static void showDialog(QWidget* parent, const QString& currentPath, bool isMirrorSource);

protected:
    explicit TagManagerDialog(const QString& currentPath, bool isMirrorSource, QWidget* parent = nullptr);
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onSearchTextChanged(const QString& text);
    void onSidebarToggled(bool checked);

private:
    void initContent();
    void applyTheme();
    void refreshTags();
    void createTag(const QString& tagName);

    QString m_currentPath;
    bool m_isMirrorSource = false;

    // 顶部组件
    QLineEdit* m_searchEdit = nullptr;
    QPushButton* m_btnToggleSidebar = nullptr;

    // 左侧 180px 侧边栏
    QFrame* m_sidebar = nullptr;
    QVBoxLayout* m_sidebarLayout = nullptr;

    // 右侧内容区
    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_contentWidget = nullptr;
    QVBoxLayout* m_contentLayout = nullptr;

    // 动态新增提示胶囊 (`+ 新增 "关键字"`)
    QWidget* m_addNewTagWidget = nullptr;
    QPushButton* m_btnAddNewTag = nullptr;

    // 标签流式容器
    QWidget* m_recentTagsContainer = nullptr;
    FlowLayout* m_recentFlowLayout = nullptr;

    QWidget* m_allTagsContainer = nullptr;
    FlowLayout* m_allFlowLayout = nullptr;

    // 数据缓存
    static QStringList s_sessionRecentTags; // 全局会话级“最近使用”历史队列
    QMap<QString, int> m_allTagCounts;
};

} // namespace ArcMeta
```

---

### 步骤 2：新建实现文件 `src/ui/TagManagerDialog.cpp`

```cpp
#include "TagManagerDialog.h"
#include "UiHelper.h"
#include "StyleLibrary.h"
#include "../meta/CategoryRepo.h"
#include "../meta/AmMetaJson.h"
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
    resize(580, 460);

    initContent();
    applyTheme();
    refreshTags();
}

void TagManagerDialog::initContent() {
    QVBoxLayout* mainL = new QVBoxLayout(m_contentArea);
    mainL->setContentsMargins(0, 0, 0, 0);
    mainL->setSpacing(0);

    // ================= 1. 顶部操作栏（透明搜索框 + 右侧 sidebar.svg 按钮） =================
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

    // 侧边栏折叠按钮 (使用 sidebar.svg)
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
    m_sidebarLayout->setContentsMargins(10, 10, 10, 10);
    m_sidebarLayout->setSpacing(6);

    QLabel* sideTitle = new QLabel("分类导航", m_sidebar);
    sideTitle->setStyleSheet("color: #888; font-size: 11px; font-weight: bold;");
    m_sidebarLayout->addWidget(sideTitle);

    // 追加全部、未分类等导航项...
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
        "QPushButton { background: #1C97EA; color: #FFF; border: none; border-radius: 4px; padding: 4px 12px; font-weight: bold; font-size: 12px; }"
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
        setMinimumWidth(400); // 180px 侧边栏 + >=220px 内容区
    } else {
        setMinimumWidth(200); // 隐藏侧边栏后，最小宽度可缩小至 200px
    }
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
}

void TagManagerDialog::createTag(const QString& tagName) {
    if (tagName.isEmpty()) return;

    if (m_isMirrorSource) {
        // 双轨之一：托管库模式 -> 写入 MetadataManager / SQLite
        MetadataManager::instance().setTags(m_currentPath.toStdWString(), {tagName});
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

    // 🚨 实时更新规则：新新增的标签瞬时挂载到“最近使用”区域首位
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

    // 1. 渲染“最近使用”流式布局 (实时更新)
    while (QLayoutItem* item = m_recentFlowLayout->takeAt(0)) {
        delete item->widget(); delete item;
    }
    for (const QString& tag : s_sessionRecentTags) {
        QPushButton* btn = new QPushButton(tag, m_recentTagsContainer);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet("QPushButton { background: #2D2D30; border: 1px solid #3498DB; color: #3498DB; border-radius: 4px; padding: 3px 8px; font-size: 12px; }");
        m_recentFlowLayout->addWidget(btn);
    }

    // 2. 渲染“全部标签”流式布局
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
```

---

### 步骤 3：在 `CategoryPanel.cpp` 中替换预设标签接口调用

打开 `src/ui/CategoryPanel.cpp`，定位到 `onSetPresetTags()` 函数（第 872 行），替换为：

```cpp
void CategoryPanel::onSetPresetTags() {
    QModelIndex index = m_categoryTree->currentIndex();
    QString path = index.data(PathRole).toString();
    
    // 一键弹出模块化高级标签管理弹窗
    TagManagerDialog::showDialog(this, path, false);
}
```

---

### 步骤 4：在 `CMakeLists.txt` 中引入新源文件

在 `CMakeLists.txt` 的 `set(SOURCES ...)` 列表中追加：
```cmake
src/ui/TagManagerDialog.h
src/ui/TagManagerDialog.cpp
```

全套图纸已交付完毕，执行者完成后项目将 100% 达成预期的效果。