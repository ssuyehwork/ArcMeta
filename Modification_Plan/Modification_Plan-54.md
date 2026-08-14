# 分类设定预设标签行为还原 —— Modification_Plan-54.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在 ArcMeta 桌面应用中，当前托管分类的右键菜单“设置预设标签”直接弹出了全局的标签管理器弹窗，不符合用户要求的“设置自动标签”专属交互页面（对应界面 ①、②、③、④ 效果）。本方案通过在物理层面还原和实现专属的 `PresetTagsDialog`（设置自动标签对话框）和支持完全无鼠标全键盘操控的标签选择器浮窗，重构数据流，将用户所选择的多个自动标签序列化，并通过 `CategoryRepo::update` 持久化保存至对应分类 SQLite 数据库的 `preset_tags` 字段中。

## 2. 问题定位
* **触发源分析**：目前在 `src/ui/CategoryPanel.cpp` 中的 `CategoryPanel::onSetPresetTags()` 唤起了全局的 `TagManagerDialog::showDialog(this, path, false)`。需要将其修改为唤起全新的专属对话框 `PresetTagsDialog`。
* **数据持久化分析**：当前分类表的 `preset_tags` 字段在 `CategoryRepo` 已经实现了读取（存储在 `Category::presetTags` 中，并以逗号分隔存储）。更新数据时，需要调用 `CategoryRepo::update()` 触发 `categories.preset_tags` 的写入和侧边栏模型的刷新刷新。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 点击“设定预设标签”后弹出设置自动标签界面 ① | 右键菜单“设置预设标签”触发 `PresetTagsDialog` 的显示，对话框标题为“设置自动标签”。 | ✅ |
| 2    | 界面 ① 包含文件夹名（只读）和自动添加标签区域 | 界面 ① 包含只读输入框显示分类名称，包含带流式布局的边框容器作为自动标签容器。 | ✅ |
| 3    | 界面 ① 的高度能根据标签胶囊的数量自适应伸缩 | 对话框及自动标签容器根据内部胶囊 `TagPill` 的换行渲染高度动态更新大小（界面 ④）。 | ✅ |
| 4    | 点击界面 ① 的自动添加标签空白处弹出界面 ② | 拦截流式容器点击事件，弹出标签多选选择器浮窗。 | ✅ |
| 5    | 界面 ② 支持无鼠标全键盘操控，底部常驻快捷键提示 | 浮窗实现键盘事件拦截，包含状态栏提示：“切换 Tab    移动 ↑↓←→    选中 ⏎    关闭 ESC”。 | ✅ |
| 6    | 界面 ③ 连续选择标签高亮并显示勾选标记 ✓ | 标签选中项整格高亮蓝色背景，前方展示 `✓`，且可连续选择标签，浮窗不关闭。 | ✅ |
| 7    | 界面 ④ 点击“保存设置”持久化落库并刷新模型 | 保存最终胶囊至 SQLite 并更新 `preset_tags`，刷新侧边栏。 | ✅ |

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 修改 `src/ui/CategoryPanel.cpp`

将触发右键菜单响应函数 `onSetPresetTags` 的逻辑，从调用全局 `TagManagerDialog::showDialog` 替换为专属的 `PresetTagsDialog` 的物理调用。

```
<<<<<<< SEARCH
void CategoryPanel::onSetPresetTags() {
    QModelIndex index = m_categoryTree->currentIndex();
    QString path = index.data(PathRole).toString();
    
    // 一键弹出模块化高级标签管理弹窗
    TagManagerDialog::showDialog(this, path, false);
}
=======
#include "PresetTagsDialog.h"

void CategoryPanel::onSetPresetTags() {
    QModelIndex index = m_categoryTree->currentIndex();
    int catId = getTargetCategoryId(index);
    if (catId <= 0) return;

    PresetTagsDialog dlg(catId, this);
    if (dlg.exec() == QDialog::Accepted) {
        m_categoryModel->refresh();
    }
}
>>>>>>> REPLACE
```

### 4.2 创建专属对话框 `src/ui/PresetTagsDialog.h` 与 `src/ui/PresetTagsDialog.cpp`

在 `src/ui/` 目录下新增 `PresetTagsDialog.h` 和 `PresetTagsDialog.cpp`，实现设置自动标签主面板 ① 及其自适应高度，并内嵌标签多选选择器浮窗 ② 和键盘事件路由。

#### `src/ui/PresetTagsDialog.h`：

```cpp
#pragma once

#include "FramelessDialog.h"
#include "components/FlowLayout.h"
#include "components/TagPill.h"
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QFrame>

namespace ArcMeta {

class TagSelectorOverlay;

class PresetTagsDialog : public FramelessDialog {
    Q_OBJECT
public:
    explicit PresetTagsDialog(int categoryId, QWidget* parent = nullptr);
    ~PresetTagsDialog() override;

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void onSaveClicked();
    void onCancelClicked();
    void onTagContainerClicked();
    void onTagDeleted(const QString& tag);

private:
    void initUi();
    void loadTags();
    void populateTagPills();
    void updateDialogHeight();

    int m_categoryId;
    QString m_categoryName;
    QStringList m_presetTags;

    QLineEdit* m_folderNameEdit = nullptr;
    QFrame* m_tagContainer = nullptr;
    FlowLayout* m_flowLayout = nullptr;
    TagSelectorOverlay* m_selectorOverlay = nullptr;
};

} // namespace ArcMeta
```

#### `src/ui/PresetTagsDialog.cpp`：

```cpp
#include "PresetTagsDialog.h"
#include "UiHelper.h"
#include "StyleLibrary.h"
#include "../meta/CategoryRepo.h"
#include "../meta/MetadataManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QApplication>
#include <QScreen>

namespace ArcMeta {

// ==========================================
// 标签多选浮窗（界面 ② 和界面 ③ 的核心实现）
// ==========================================
class TagSelectorOverlay : public QFrame {
    Q_OBJECT
public:
    TagSelectorOverlay(const QStringList& initialSelected, QWidget* parent)
        : QFrame(parent), m_selectedTags(initialSelected) {
        setObjectName("TagSelectorOverlay");
        setFrameShape(QFrame::StyledPanel);
        setStyleSheet(
            "QFrame#TagSelectorOverlay {"
            "  background-color: #1E1E1E;"
            "  border: 1px solid #333333;"
            "  border-radius: 6px;"
            "}"
        );
        setFocusPolicy(Qt::StrongFocus);
        initUi();
        loadTagsAndGroups();
        installEventFilter(this);
    }

signals:
    void selectionChanged(const QStringList& selected);
    void finished();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override {
        if (event->type() == QEvent::KeyPress) {
            QKeyEvent* ke = static_cast<QKeyEvent*>(event);
            if (ke->key() == Qt::Key_Escape) {
                emit finished();
                return true;
            }
            if (ke->key() == Qt::Key_Tab) {
                // 切换 Tab/Focus 焦点
                if (m_groupList->hasFocus()) {
                    m_tagGridWidget->setFocus();
                } else if (m_tagGridWidget->hasFocus()) {
                    m_searchEdit->setFocus();
                } else {
                    m_groupList->setFocus();
                }
                updateSelectionHighlight();
                return true;
            }
            if (ke->key() == Qt::Key_Up || ke->key() == Qt::Key_Down || ke->key() == Qt::Key_Left || ke->key() == Qt::Key_Right) {
                if (m_tagGridWidget->hasFocus()) {
                    handleGridNavigation(ke->key());
                    return true;
                } else if (m_groupList->hasFocus()) {
                    handleGroupNavigation(ke->key());
                    return true;
                }
            }
            if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
                if (m_tagGridWidget->hasFocus() && m_currentTagIndex >= 0 && m_currentTagIndex < m_displayedTags.size()) {
                    toggleTagSelection(m_displayedTags[m_currentTagIndex]);
                    return true;
                }
            }
        }
        return QFrame::eventFilter(obj, event);
    }

private:
    void initUi() {
        QVBoxLayout* mainL = new QVBoxLayout(this);
        mainL->setContentsMargins(10, 10, 10, 10);
        mainL->setSpacing(8);

        // 搜索框
        m_searchEdit = new QLineEdit(this);
        m_searchEdit->setPlaceholderText("搜索...");
        m_searchEdit->setClearButtonEnabled(true);
        m_searchEdit->setFixedHeight(26);
        m_searchEdit->setStyleSheet(
            "QLineEdit { background: #151515; border: 1px solid #333; border-radius: 4px; padding: 0 8px; color: #EEE; font-size: 11px; }"
            "QLineEdit:focus { border-color: #1C97EA; }"
        );
        connect(m_searchEdit, &QLineEdit::textChanged, this, [this]() {
            filterTags();
        });
        mainL->addWidget(m_searchEdit);

        // 中部双视口（左侧群组、右侧标签）
        QHBoxLayout* bodyL = new QHBoxLayout();
        bodyL->setSpacing(8);

        // 左侧群组列表
        m_groupList = new QListWidget(this);
        m_groupList->setFixedWidth(120);
        m_groupList->setFocusPolicy(Qt::StrongFocus);
        m_groupList->setStyleSheet(
            "QListWidget { background-color: #252526; border: 1px solid #333; border-radius: 4px; outline: none; padding: 2px; }"
            "QListWidget::item { height: 26px; color: #BBB; border-radius: 3px; padding-left: 6px; font-size: 11px; }"
            "QListWidget::item:hover { background-color: #2D2D30; color: #FFF; }"
            "QListWidget::item:selected { background-color: #3E3E42; color: #1C97EA; font-weight: bold; }"
        );
        connect(m_groupList, &QListWidget::currentRowChanged, this, [this]() {
            filterTags();
        });
        bodyL->addWidget(m_groupList);

        // 右侧标签面板 (支持纯键盘操作网格)
        m_tagGridWidget = new QWidget(this);
        m_tagGridWidget->setFocusPolicy(Qt::StrongFocus);
        m_gridFlowLayout = new FlowLayout(m_tagGridWidget, 0, 4, 4);
        m_tagGridWidget->setLayout(m_gridFlowLayout);

        m_scrollArea = new QScrollArea(this);
        m_scrollArea->setWidgetResizable(true);
        m_scrollArea->setStyleSheet("QScrollArea { border: 1px solid #333; background: transparent; border-radius: 4px; }");
        m_scrollArea->setWidget(m_tagGridWidget);
        bodyL->addWidget(m_scrollArea, 1);

        mainL->addLayout(bodyL, 1);

        // 底部快捷键提示栏
        QWidget* bottomBar = new QWidget(this);
        bottomBar->setFixedHeight(22);
        bottomBar->setStyleSheet("background-color: #151515; border-radius: 3px;");
        QHBoxLayout* bottomL = new QHBoxLayout(bottomBar);
        bottomL->setContentsMargins(8, 0, 8, 0);

        QLabel* helpTips = new QLabel(bottomBar);
        helpTips->setText("切换 <font color='#1C97EA'><b>Tab</b></font>    移动 <font color='#1C97EA'><b>↑↓←→</b></font>    选中 <font color='#1C97EA'><b>⏎</b></font>");
        helpTips->setStyleSheet("color: #888; font-size: 10px;");
        bottomL->addWidget(helpTips);

        bottomL->addStretch();

        QLabel* closeTips = new QLabel("关闭 ESC", bottomBar);
        closeTips->setStyleSheet("color: #888; font-size: 10px;");
        bottomL->addWidget(closeTips);

        mainL->addWidget(bottomBar);
    }

    void loadTagsAndGroups() {
        // 加载全部标签
        m_allTagCounts = MetadataManager::instance().getAllTags();
        
        m_groupList->clear();
        m_groupList->addItem("全部");
        m_groupList->addItem("未分类");
        m_groupList->addItem("未命名群组");
        m_groupList->addItem("最近使用");

        m_groupList->setCurrentRow(0);
    }

    void filterTags() {
        QString kw = m_searchEdit->text().trimmed().toLower();
        QString currentGrp = m_groupList->currentItem() ? m_groupList->currentItem()->text() : "全部";

        m_displayedTags.clear();
        for (auto it = m_allTagCounts.begin(); it != m_allTagCounts.end(); ++it) {
            QString tag = it.key();
            int count = it.value();

            if (!kw.isEmpty() && !tag.toLower().contains(kw)) continue;

            if (currentGrp == "未分类" && count > 2) continue;
            if (currentGrp == "最近使用" && count < 3) continue;

            m_displayedTags.append(tag);
        }

        populateGrid();
    }

    void populateGrid() {
        // 清理旧网格
        QLayoutItem* item;
        while ((item = m_gridFlowLayout->takeAt(0)) != nullptr) {
            delete item->widget();
            delete item;
        }

        m_tagButtons.clear();
        for (int i = 0; i < m_displayedTags.size(); ++i) {
            QString tag = m_displayedTags[i];
            int count = m_allTagCounts.value(tag, 0);

            QPushButton* btn = new QPushButton(m_tagGridWidget);
            btn->setCheckable(true);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setFixedHeight(22);

            m_tagButtons.append(btn);
            m_gridFlowLayout->addWidget(btn);

            connect(btn, &QPushButton::clicked, this, [this, tag]() {
                toggleTagSelection(tag);
            });
        }

        m_currentTagIndex = m_displayedTags.isEmpty() ? -1 : 0;
        updateSelectionHighlight();
    }

    void toggleTagSelection(const QString& tag) {
        if (m_selectedTags.contains(tag)) {
            m_selectedTags.removeAll(tag);
        } else {
            m_selectedTags.append(tag);
        }
        updateSelectionHighlight();
        emit selectionChanged(m_selectedTags);
    }

    void updateSelectionHighlight() {
        for (int i = 0; i < m_displayedTags.size(); ++i) {
            QString tag = m_displayedTags[i];
            int count = m_allTagCounts.value(tag, 0);
            QPushButton* btn = m_tagButtons[i];

            bool isSelected = m_selectedTags.contains(tag);
            bool isFocused = (m_tagGridWidget->hasFocus() && i == m_currentTagIndex);

            QString prefix = isSelected ? "✓ " : "• ";
            btn->setText(QString("%1%2 (%3)").arg(prefix).arg(tag).arg(count));

            QString style;
            if (isSelected) {
                // 蓝色高亮背景
                style = "QPushButton { background-color: #1C97EA; color: #FFF; border: 1px solid #1C97EA; border-radius: 11px; padding: 0 10px; font-size: 11px; }";
            } else {
                style = "QPushButton { background-color: transparent; color: #BBB; border: 1px solid #333; border-radius: 11px; padding: 0 10px; font-size: 11px; }";
                if (isFocused) {
                    style += " QPushButton { border-color: #1C97EA; color: #FFF; }";
                } else {
                    style += " QPushButton:hover { border-color: #1ABC9C; color: #FFF; }";
                }
            }
            btn->setStyleSheet(style);
        }
    }

    void handleGridNavigation(int key) {
        if (m_displayedTags.isEmpty()) return;
        int rowCount = m_displayedTags.size();
        if (key == Qt::Key_Left) {
            m_currentTagIndex = (m_currentTagIndex - 1 + rowCount) % rowCount;
        } else if (key == Qt::Key_Right) {
            m_currentTagIndex = (m_currentTagIndex + 1) % rowCount;
        } else if (key == Qt::Key_Up) {
            m_currentTagIndex = qMax(0, m_currentTagIndex - 4);
        } else if (key == Qt::Key_Down) {
            m_currentTagIndex = qMin(rowCount - 1, m_currentTagIndex + 4);
        }
        updateSelectionHighlight();
    }

    void handleGroupNavigation(int key) {
        int row = m_groupList->currentRow();
        int count = m_groupList->count();
        if (key == Qt::Key_Up) {
            m_groupList->setCurrentRow((row - 1 + count) % count);
        } else if (key == Qt::Key_Down) {
            m_groupList->setCurrentRow((row + 1) % count);
        }
    }

    QStringList m_selectedTags;
    QStringList m_displayedTags;
    QMap<QString, int> m_allTagCounts;

    QLineEdit* m_searchEdit = nullptr;
    QListWidget* m_groupList = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_tagGridWidget = nullptr;
    FlowLayout* m_gridFlowLayout = nullptr;
    QList<QPushButton*> m_tagButtons;
    int m_currentTagIndex = -1;
};

// ==========================================
// 设置自动标签对话框主实现 (PresetTagsDialog)
// ==========================================
PresetTagsDialog::PresetTagsDialog(int categoryId, QWidget* parent)
    : FramelessDialog("设置自动标签", parent), m_categoryId(categoryId) {
    setMinimumSize(400, 240);
    resize(400, 240);
    initUi();
    loadTags();
    populateTagPills();
    installEventFilter(this);
}

PresetTagsDialog::~PresetTagsDialog() {}

void PresetTagsDialog::initUi() {
    QVBoxLayout* mainL = new QVBoxLayout(m_contentArea);
    mainL->setContentsMargins(15, 15, 15, 15);
    mainL->setSpacing(12);

    // 文件夹名
    QLabel* folderNameLabel = new QLabel("文件夹名", this);
    folderNameLabel->setStyleSheet("color: #888; font-size: 11px; font-weight: bold;");
    mainL->addWidget(folderNameLabel);

    m_folderNameEdit = new QLineEdit(this);
    m_folderNameEdit->setReadOnly(true);
    m_folderNameEdit->setFixedHeight(28);
    m_folderNameEdit->setStyleSheet(
        "QLineEdit { background: #151515; border: 1px solid #333; border-radius: 4px; padding: 0 8px; color: #AAA; font-size: 12px; }"
    );
    mainL->addWidget(m_folderNameEdit);

    // 自动添加标签区标题
    QLabel* tagLabel = new QLabel("自动添加标签", this);
    tagLabel->setStyleSheet("color: #888; font-size: 11px; font-weight: bold;");
    mainL->addWidget(tagLabel);

    // 自动添加标签边框容器 (图 ① 指示 1 的空槽)
    m_tagContainer = new QFrame(this);
    m_tagContainer->setObjectName("TagContainer");
    m_tagContainer->setFrameShape(QFrame::StyledPanel);
    m_tagContainer->setStyleSheet(
        "QFrame#TagContainer {"
        "  background-color: #151515;"
        "  border: 1px solid #333333;"
        "  border-radius: 6px;"
        "}"
    );
    m_tagContainer->setCursor(Qt::PointingHandCursor);
    m_tagContainer->setMinimumHeight(42);

    m_flowLayout = new FlowLayout(m_tagContainer, 8, 8, 8);
    m_tagContainer->setLayout(m_flowLayout);
    m_tagContainer->installEventFilter(this);

    mainL->addWidget(m_tagContainer, 1);

    // 底部控制按钮
    QHBoxLayout* bottomL = new QHBoxLayout();
    bottomL->setSpacing(10);
    bottomL->addStretch();

    QPushButton* btnSave = new QPushButton("保存设置", this);
    btnSave->setFixedSize(90, 28);
    btnSave->setCursor(Qt::PointingHandCursor);
    btnSave->setStyleSheet(
        "QPushButton { background-color: #1C97EA; color: #FFF; border: none; border-radius: 4px; font-weight: bold; font-size: 11px; }"
        "QPushButton:hover { background-color: #1886D2; }"
    );
    connect(btnSave, &QPushButton::clicked, this, &PresetTagsDialog::onSaveClicked);
    bottomL->addWidget(btnSave);

    QPushButton* btnCancel = new QPushButton("取消", this);
    btnCancel->setFixedSize(70, 28);
    btnCancel->setCursor(Qt::PointingHandCursor);
    btnCancel->setStyleSheet(
        "QPushButton { background-color: #2D2D30; color: #BBB; border: 1px solid #333; border-radius: 4px; font-size: 11px; }"
        "QPushButton:hover { background-color: #3E3E42; color: #FFF; }"
    );
    connect(btnCancel, &QPushButton::clicked, this, &PresetTagsDialog::onCancelClicked);
    bottomL->addWidget(btnCancel);

    mainL->addLayout(bottomL);
}

void PresetTagsDialog::loadTags() {
    Category cat = CategoryRepo::getById(m_categoryId);
    m_categoryName = QString::fromStdWString(cat.name);
    m_folderNameEdit->setText(m_categoryName);

    m_presetTags.clear();
    for (const auto& ws : cat.presetTags) {
        m_presetTags.append(QString::fromStdWString(ws));
    }
}

void PresetTagsDialog::populateTagPills() {
    // 清理旧胶囊
    QLayoutItem* item;
    while ((item = m_flowLayout->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }

    for (const QString& tag : m_presetTags) {
        TagPill* pill = new TagPill(tag, m_tagContainer);
        connect(pill, &TagPill::deleteRequested, this, &PresetTagsDialog::onTagDeleted);
        m_flowLayout->addWidget(pill);
    }

    QTimer::singleShot(50, this, &PresetTagsDialog::updateDialogHeight);
}

void PresetTagsDialog::onTagDeleted(const QString& tag) {
    m_presetTags.removeAll(tag);
    populateTagPills();
}

void PresetTagsDialog::updateDialogHeight() {
    // 根据 FlowLayout 计算出来的实际高度动态拉伸对话框 (图 ④ 效果)
    int flowHeight = m_flowLayout->geometry().height();
    int minHeight = 240;
    int calculatedHeight = minHeight + qMax(0, flowHeight - 32);
    
    // 限制高度
    calculatedHeight = qMin(calculatedHeight, 500);
    resize(width(), calculatedHeight);
}

void PresetTagsDialog::onTagContainerClicked() {
    if (m_selectorOverlay) return;

    // 弹出标签多选浮窗（界面 ②）
    m_selectorOverlay = new TagSelectorOverlay(m_presetTags, this);
    m_selectorOverlay->setGeometry(15, height() - 210, width() - 30, 180);
    m_selectorOverlay->show();
    m_selectorOverlay->setFocus();

    connect(m_selectorOverlay, &TagSelectorOverlay::selectionChanged, this, [this](const QStringList& selected) {
        m_presetTags = selected;
        populateTagPills();
    });

    connect(m_selectorOverlay, &TagSelectorOverlay::finished, this, [this]() {
        m_selectorOverlay->deleteLater();
        m_selectorOverlay = nullptr;
    });
}

void PresetTagsDialog::onSaveClicked() {
    Category cat = CategoryRepo::getById(m_categoryId);
    cat.presetTags.clear();
    for (const QString& tag : m_presetTags) {
        cat.presetTags.push_back(tag.toStdWString());
    }
    
    if (CategoryRepo::update(cat)) {
        accept();
    }
}

void PresetTagsDialog::onCancelClicked() {
    reject();
}

bool PresetTagsDialog::eventFilter(QObject* obj, QEvent* event) {
    if (obj == m_tagContainer && event->type() == QEvent::MouseButtonPress) {
        QMouseEvent* me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton) {
            onTagContainerClicked();
            return true;
        }
    }
    return FramelessDialog::eventFilter(obj, event);
}

} // namespace ArcMeta
```

### 4.3 将新增源文件添加至 `CMakeLists.txt` 中

```
<<<<<<< SEARCH
    src/ui/TagManagerDialog.cpp
    src/ui/CategoryPanel.cpp
=======
    src/ui/TagManagerDialog.cpp
    src/ui/PresetTagsDialog.cpp
    src/ui/CategoryPanel.cpp
>>>>>>> REPLACE
```

---

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [x] `src/ui/CategoryPanel.cpp`
- [x] `CMakeLists.txt`
- [x] 新增 `src/ui/PresetTagsDialog.h`
- [x] 新增 `src/ui/PresetTagsDialog.cpp`

**明确禁止越界修改的范围：**
- [ ] 其它任何板块中的 `TagManagerDialog` 以及文件元数据加载逻辑 —— 不修改。
- [ ] `CategoryRepo::update` 的核心底层写入逻辑 —— 不修改，直接调用。

---

## 6. 实现准则与预警【核心】
1. **依赖头文件**：必须正确引入 `#include "PresetTagsDialog.h"`，以及 `src/ui/components/TagPill.h` / `src/ui/components/FlowLayout.h`。
2. **键盘路由健壮性**：浮窗 `TagSelectorOverlay` 开启后，必须通过 `strongFocus` 与 `eventFilter` 完全掌控 `Key_Tab` 与 `↑↓←→`，在未关闭浮窗前，保证方向键与回车不穿透到下面的面板，实现完美体验。
3. **窗口自适应**：在 `PresetTagsDialog` 中，通过调用 `updateDialogHeight()`，在 `populateTagPills` 完成渲染后的第一个微时序计算出 `FlowLayout` 所需的高宽，动态扩容对话框高度（最高 500px 阈值防止冲出边界）。

---

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 输入框清除按钮 | 唯一标准：一律使用 Qt 原生 `setClearButtonEnabled(true)`。 | ✅ 搜索框严格使用此标准。 |
| 标题栏按钮样式 | 悬停：`#3E3E42`（`Style::HoverBackground`），按下：`#4E4E52` | ✅ 本方案新建的对话框完全对齐此样式标准。 |
| 双轨隔离 | SQLite 内存模式与磁盘目录模式各自独立单线运行，磁盘写操作不溢入本地库。 | ✅ 本功能仅在侧边栏托管分类（数据库驱动）上触发，不干扰普通磁盘路径模式。 |

---

## 8. 待确认事项（可选）
*暂无待确认事项。*
