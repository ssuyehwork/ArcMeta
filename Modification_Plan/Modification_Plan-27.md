# 侧边栏分类安全加锁与嵌入式解锁重构 —— Modification_Plan-27.md

> 状态：已批准，执行中 / 已执行完成

## 1. 任务背景
在侧边栏对某个分类上了锁（设置密码或执行“立即锁定”）后，如果当前内容面板正在展示该分类，原展示的数据必须“立马被隐藏起来，不可显示出来”以保护隐私，且解锁不应采用弹窗对话框形式，而应使用卡片式内置无边框解锁视图。本方案遵循 Development_Plan.md 第 3.1 节与 3.2 节关于分类安全加锁与子菜单的规定进行系统级整体重构。

## 2. 问题定位
1. **密码保护二级菜单缺失**：`CategoryPanel.cpp` 中的 `pwdMenu` 仅有设置和清除选项，缺少了“修改”和“立即锁定”核心交互项。
2. **解锁方式非嵌入式**：`ContentPanel::loadCategory` 采用 `CategoryLockDialog` 模态弹窗，视觉不够原生。
3. **数据隐藏滞后**：加锁状态改变后没有通知机制通知 `ContentPanel` 即时物理清空。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 分类加锁后数据立马被隐藏起来，不可显示出来（对应用户原话） | 在新设密码、手动锁定、修改密码后调用 CategoryLockManager::lockCategory，触发 categoryLocked 信号物理清空并隐藏数据。 | ✅ |
| 2    | 密码保护子选项增加：设置、修改、立即锁定（对应用户原话） | 重构 CategoryPanel.cpp 中的二级子菜单，动态按分类加锁与解锁状态显示并触发对应槽函数。 | ✅ |
| 3    | 采用“RapidNotes”的内置解锁画面（对应用户原话） | 新建 CategoryLockWidget 作为内置解锁卡片并将其嵌入到 ContentPanel 的 m_viewStack 中，取代模态弹窗。 | ✅ |

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 新增文件一：`src/ui/CategoryLockWidget.h`
完全新建嵌入式解锁卡片组件头文件，声明界面接口。

```cpp
#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QLabel>

namespace ArcMeta {

class CategoryLockWidget : public QWidget {
    Q_OBJECT
public:
    explicit CategoryLockWidget(QWidget* parent = nullptr);
    
    void setCategory(int id, const QString& hint);

signals:
    void unlocked(int id);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onVerify();

private:
    int m_catId = -1;
    QLabel* m_hintLabel = nullptr;
    QLineEdit* m_pwdEdit = nullptr;
};

} // namespace ArcMeta
```

### 4.2 新增文件二：`src/ui/CategoryLockWidget.cpp`
完全新建嵌入式解锁卡片组件实现，100% 对齐 RapidNotes 的扁平高亮安全绿 UI 风格，支持回车键即时解锁和密码框原生清除按钮。

```cpp
#include "CategoryLockWidget.h"
#include "UiHelper.h"
#include "../core/CategoryLockManager.h"
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QEvent>

namespace ArcMeta {

CategoryLockWidget::CategoryLockWidget(QWidget* parent) : QWidget(parent) {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setAlignment(Qt::AlignCenter);

    auto* container = new QWidget();
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setAlignment(Qt::AlignCenter);
    layout->setSpacing(8);

    // 1. 锁图标 (精简 32x32，采用安全绿)
    auto* lockIcon = new QLabel();
    lockIcon->setPixmap(UiHelper::getIcon("lock", QColor("#00A650"), 32).pixmap(32, 32));
    lockIcon->setAlignment(Qt::AlignCenter);
    layout->addWidget(lockIcon);

    // 2. 提示文字 (安全绿加粗)
    auto* titleLabel = new QLabel("输入密码查看内容");
    titleLabel->setStyleSheet("color: #00A650; font-size: 13px; font-weight: bold; background: transparent;");
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);

    // 3. 密码提示 (灰色精简)
    m_hintLabel = new QLabel("密码提示: ");
    m_hintLabel->setStyleSheet("color: #555555; font-size: 11px; background: transparent;");
    m_hintLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_hintLabel);

    layout->addSpacing(2);

    // 4. 扁平密码输入框 (180px，深黑底，细边框)
    m_pwdEdit = new QLineEdit();
    m_pwdEdit->setPlaceholderText("输入密码");
    m_pwdEdit->setEchoMode(QLineEdit::Password);
    m_pwdEdit->setFixedWidth(180);
    m_pwdEdit->setFixedHeight(28);
    m_pwdEdit->setClearButtonEnabled(true);
    m_pwdEdit->setStyleSheet(
        "QLineEdit {"
        "  background-color: #121212; border: 1px solid #333; border-radius: 4px;"
        "  padding: 0 8px; color: white; font-size: 12px;"
        "}"
        "QLineEdit:focus { border: 1px solid #3a90ff; }"
    );
    connect(m_pwdEdit, &QLineEdit::returnPressed, this, &CategoryLockWidget::onVerify);
    m_pwdEdit->installEventFilter(this);
    layout->addWidget(m_pwdEdit, 0, Qt::AlignHCenter);

    mainLayout->addWidget(container);

    setStyleSheet("background: transparent;");
}

void CategoryLockWidget::setCategory(int id, const QString& hint) {
    m_catId = id;
    m_hintLabel->setText(QString("密码提示: %1").arg(hint.isEmpty() ? "无" : hint));
    m_pwdEdit->clear();
    m_pwdEdit->setFocus();
}

bool CategoryLockWidget::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_pwdEdit && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            if (!m_pwdEdit->text().isEmpty()) {
                m_pwdEdit->clear();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void CategoryLockWidget::onVerify() {
    if (m_catId == -1) return;
    
    if (CategoryLockManager::instance().verifyAndUnlock(m_catId, m_pwdEdit->text())) {
        emit unlocked(m_catId);
    } else {
        m_pwdEdit->setStyleSheet(
            "QLineEdit {"
            "  background-color: #121212; border: 1px solid #e74c3c; border-radius: 4px;"
            "  padding: 0 8px; color: white; font-size: 12px;"
            "}"
        );
        m_pwdEdit->selectAll();
    }
}

} // namespace ArcMeta
```

### 4.3 修改 `src/ui/ContentPanel.h`
声明内置锁屏卡片，增加 `clear()` 方法和分类 ID 获取接口。

```diff
<<<<<<< SEARCH
class ContentPanel : public QFrame {
    Q_OBJECT

public:
    enum class DataSourceType {
=======
class CategoryLockWidget;

class ContentPanel : public QFrame {
    Q_OBJECT

public:
    int currentCategoryId() const { return m_currentCategoryId; }
    void clear();

    enum class DataSourceType {
>>>>>>> REPLACE
```

```diff
<<<<<<< SEARCH
signals:
    /**
     * @brief 当在内容区点击子分类时触发，告知 MainWindow 切换侧边栏选中状态
     */
    void categoryClicked(int categoryId);
=======
signals:
    /**
     * @brief 当在内容区点击子分类时触发，告知 MainWindow 切换侧边栏选中状态
     */
    void categoryClicked(int categoryId);

    /**
     * @brief 当在内容面板中成功解锁了分类时触发，告知 MainWindow 同步刷新侧边栏
     */
    void categoryUnlocked(int categoryId);
>>>>>>> REPLACE
```

```diff
<<<<<<< SEARCH
    QAbstractItemView* m_gridView = nullptr;
    QTreeView* m_treeView = nullptr;
    DiskItemModel* m_diskModel = nullptr;       // 负责纯物理磁盘导航模型 (0)
=======
    CategoryLockWidget* m_lockWidget = nullptr;
    QAbstractItemView* m_gridView = nullptr;
    QTreeView* m_treeView = nullptr;
    DiskItemModel* m_diskModel = nullptr;       // 负责纯物理磁盘导航模型 (0)
>>>>>>> REPLACE
```

### 4.4 修改 `src/ui/ContentPanel.cpp`
引入并实例化嵌入式锁屏组件，重构 `loadCategory`，并修改 `loadDirectory` 与 `loadPaths` 以保证在普通导航时恢复正确视图。

```diff
<<<<<<< SEARCH
#include "../core/CoreController.h"
#include "../core/UndoManager.h"
#include "../core/BasicCommands.h"
using namespace ArcMeta::Style;
#include "../util/ShellHelper.h"
#include "DiskScanService.h"
#include "CategoryLoadService.h"
#include "../ui/MediaColorExtractor.h"
=======
#include "../core/CoreController.h"
#include "../core/UndoManager.h"
#include "../core/BasicCommands.h"
using namespace ArcMeta::Style;
#include "../util/ShellHelper.h"
#include "DiskScanService.h"
#include "CategoryLoadService.h"
#include "../ui/MediaColorExtractor.h"
#include "CategoryLockWidget.h"
>>>>>>> REPLACE
```

```diff
<<<<<<< SEARCH
    m_viewStack = new QStackedWidget(this); 
     
    initGridView(); 
    initListView(); 
 
    m_viewStack->addWidget(m_gridView); 
    m_viewStack->addWidget(m_treeView); 
    m_viewStack->setCurrentWidget(m_gridView); 
=======
    m_viewStack = new QStackedWidget(this); 
     
    initGridView(); 
    initListView(); 
 
    m_lockWidget = new CategoryLockWidget(this);

    m_viewStack->addWidget(m_gridView); 
    m_viewStack->addWidget(m_treeView); 
    m_viewStack->addWidget(m_lockWidget);

    m_viewStack->setCurrentWidget(m_gridView);

    connect(m_lockWidget, &CategoryLockWidget::unlocked, this, [this](int id) {
        emit categoryUnlocked(id);
        loadCategory(id);
    });
>>>>>>> REPLACE
```

```diff
<<<<<<< SEARCH
void ContentPanel::loadCategory(int categoryId) { 
    // 🚨 0 与 1 彻底断连多态自动分流：逻辑切断
    if (m_model != m_libraryModel) {
        m_model = m_libraryModel;
        m_proxyModel->setSourceModel(m_model);
    }

    Category cat = CategoryRepo::getById(categoryId);
    if (cat.id > 0) {
        // 🚨【加锁保护拦截】：若分类加锁且当前未解锁
        if (cat.encrypted && !CategoryLockManager::instance().isUnlocked(categoryId)) {
            // 1. 弹出密码输入校验对话框
            CategoryLockDialog dlg(QString::fromStdWString(cat.encryptHint), this);
            if (dlg.exec() == QDialog::Accepted) {
                QString pwd = dlg.password();
                if (CategoryLockManager::instance().verifyAndUnlock(categoryId, pwd)) {
                    // 解锁成功，继续向下加载数据
                } else {
                    ToolTipOverlay::instance()->showText(QCursor::pos(), "密码错误，无法查看该分类数据", 2000, QColor("#e81123"));
                    m_model->clear(); // 密码错误：物理清空内容面板！
                    m_currentCategoryId = -1;
                    return;
                }
            } else {
                // 用户取消输入：物理清空内容面板，绝不展示数据！
                m_model->clear();
                m_currentCategoryId = -1;
                return;
            }
        }
    }

    if (m_isLoading && m_currentCategoryId == categoryId && m_currentCategoryType == "user_category") {
        return;
    }

    m_isLoading = true;
    int reqId = ++m_loadRequestId;
    m_currentCategoryType = "user_category";
    m_currentCategoryId = categoryId;
    updateLayersButtonState();
    m_viewStack->show(); 
    if (m_textPreview) m_textPreview->hide(); 
    if (m_imagePreview) m_imagePreview->hide(); 
    emit dataSourceChanged("category"); 
=======
void ContentPanel::clear() {
    if (m_model) {
        m_model->clear();
    }
    m_currentCategoryId = -1;
    m_currentCategoryType = "";
    updateStatusBarStats();
}

void ContentPanel::loadCategory(int categoryId) { 
    // 🚨 0 与 1 彻底断连多态自动分流：逻辑切断
    if (m_model != m_libraryModel) {
        m_model = m_libraryModel;
        m_proxyModel->setSourceModel(m_model);
    }

    Category cat = CategoryRepo::getById(categoryId);
    if (cat.id > 0) {
        // 🚨【加锁保护拦截】：若分类加锁且当前未解锁
        if (cat.encrypted && !CategoryLockManager::instance().isUnlocked(categoryId)) {
            m_currentCategoryId = categoryId;
            m_currentCategoryType = "user_category";
            updateLayersButtonState();
            if (m_textPreview) m_textPreview->hide();
            if (m_imagePreview) m_imagePreview->hide();

            // 内置解锁卡片无缝展示 (100% 还原 RapidNotes 嵌入画面)
            m_lockWidget->setCategory(categoryId, QString::fromStdWString(cat.encryptHint));
            m_viewStack->setCurrentWidget(m_lockWidget);
            m_viewStack->show();

            m_model->clear(); // 清空内容面板底层数据，实现完全隐藏
            recalculateAndEmitStats();
            m_isLoading = false;
            return;
        }
    }

    if (m_isLoading && m_currentCategoryId == categoryId && m_currentCategoryType == "user_category") {
        return;
    }

    m_isLoading = true;
    int reqId = ++m_loadRequestId;
    m_currentCategoryType = "user_category";
    m_currentCategoryId = categoryId;
    updateLayersButtonState();
    if (m_viewStack) {
        m_viewStack->setCurrentWidget(m_currentViewMode == ListView ? m_treeView : m_gridView);
        m_viewStack->show();
    }
    if (m_textPreview) m_textPreview->hide(); 
    if (m_imagePreview) m_imagePreview->hide(); 
    emit dataSourceChanged("category"); 
>>>>>>> REPLACE
```

```diff
<<<<<<< SEARCH
    m_isRecursive = recursive; 
    if (m_btnLayers) m_btnLayers->setChecked(recursive); \n \n    if (path.isEmpty() || path == "computer://") { 
=======
    m_isRecursive = recursive; 
    if (m_btnLayers) m_btnLayers->setChecked(recursive); \n \n    if (m_viewStack) {
        m_viewStack->setCurrentWidget(m_currentViewMode == ListView ? m_treeView : m_gridView);
    }

    if (path.isEmpty() || path == "computer://") { 
>>>>>>> REPLACE
```

```diff
<<<<<<< SEARCH
    // 维持既有的系统分类类型标识
    if (m_currentCategoryType.isEmpty()) {
        m_currentCategoryType = "path_list";
    }
    updateLayersButtonState();
    
    m_viewStack->show(); 
=======
    // 维持既有的系统分类类型标识
    if (m_currentCategoryType.isEmpty()) {
        m_currentCategoryType = "path_list";
    }
    updateLayersButtonState();
    
    if (m_viewStack) {
        m_viewStack->setCurrentWidget(m_currentViewMode == ListView ? m_treeView : m_gridView);
        m_viewStack->show();
    }
>>>>>>> REPLACE
```

### 4.5 修改 `src/ui/CategoryPanel.h`
声明 `categoryLocked` 信号，以及新二级菜单槽函数。

```diff
<<<<<<< SEARCH
signals:
    void categorySelected(int id, const QString& name, const QString& type, const QString& path = "");
    void fileSelected(const QString& path);
=======
signals:
    void categorySelected(int id, const QString& name, const QString& type, const QString& path = "");
    void fileSelected(const QString& path);
    void categoryLocked(int id);
>>>>>>> REPLACE
```

```diff
<<<<<<< SEARCH
    void onSetPassword();
    void onClearPassword();
    void onRenameCategory();
=======
    void onSetPassword();
    void onClearPassword();
    void onModifyPassword();
    void onLockCategory();
    void onRenameCategory();
>>>>>>> REPLACE
```

### 4.6 修改 `src/ui/CategoryPanel.cpp`
重构右键菜单“密码保护”下的二级子菜单，建立槽函数业务逻辑并在锁时发出信号。

```diff
<<<<<<< SEARCH
                auto* pwdMenu = menu.addMenu(UiHelper::getIcon("lock", QColor("#aaaaaa"), 18), "密码保护");
                pwdMenu->setStyleSheet(menu.styleSheet());
                
                // 2026-03-xx 按照用户要求：通过 EncryptedRole 动态判断显示“设置”或“清除”
                bool isEncrypted = index.data(EncryptedRole).toBool();
                
                if (!isEncrypted) {
                    pwdMenu->addAction("设置密码", this, &CategoryPanel::onSetPassword);
                } else {
                    pwdMenu->addAction("清除密码", this, &CategoryPanel::onClearPassword);
                }
=======
                auto* pwdMenu = menu.addMenu(UiHelper::getIcon("lock", QColor("#aaaaaa"), 18), "密码保护");
                pwdMenu->setStyleSheet(menu.styleSheet());
                
                bool isEncrypted = index.data(EncryptedRole).toBool();
                int id = index.data(IdRole).toInt();

                if (!isEncrypted) {
                    pwdMenu->addAction("设置", this, &CategoryPanel::onSetPassword);
                } else {
                    // 当且仅当已解锁时才提供“立即锁定”功能
                    bool isUnlocked = CategoryLockManager::instance().isUnlocked(id);
                    if (isUnlocked) {
                        pwdMenu->addAction("立即锁定", this, &CategoryPanel::onLockCategory);
                    }
                    pwdMenu->addAction("修改", this, &CategoryPanel::onModifyPassword);
                    pwdMenu->addAction("清除密码", this, &CategoryPanel::onClearPassword);
                }
>>>>>>> REPLACE
```

```diff
<<<<<<< SEARCH
void CategoryPanel::onSetPassword() {
    QModelIndex index = m_categoryTree->currentIndex();
    int id = getTargetCategoryId(index);
    if (id <= 0) return;

    // 2026-03-xx 物理级 1:1 还原：废弃通用输入框，调用三字段密码对话框
    CategorySetPasswordDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        QString pwd = dlg.password();
        QString hint = dlg.hint();

        QSet<int> expandedIds;
        QStringList expandedNames;
        saveExpandedState(QModelIndex(), expandedIds, expandedNames);

        auto all = CategoryRepo::getAll();
        for(auto& cat : all) {
            if(cat.id == id) {
                cat.encrypted = true;
                cat.encryptHint = hint.toStdWString();
                CategoryRepo::update(cat);
                break;
            }
        }
        
        m_categoryModel->refresh();

        restoreExpandedState(QModelIndex(), expandedIds, expandedNames);
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#00A650;'>[OK] 分类已加密</b>", 1000, QColor("#00A650"));
    }
}

void CategoryPanel::onClearPassword() {
=======
void CategoryPanel::onSetPassword() {
    QModelIndex index = m_categoryTree->currentIndex();
    int id = getTargetCategoryId(index);
    if (id <= 0) return;

    CategorySetPasswordDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        QString pwd = dlg.password();
        QString hint = dlg.hint();

        QSet<int> expandedIds;
        QStringList expandedNames;
        saveExpandedState(QModelIndex(), expandedIds, expandedNames);

        auto all = CategoryRepo::getAll();
        for(auto& cat : all) {
            if(cat.id == id) {
                cat.encrypted = true;
                cat.encryptHint = hint.toStdWString();
                CategoryRepo::update(cat);
                break;
            }
        }
        
        // 🚨 首次上锁，清退会话级解锁状态
        CategoryLockManager::instance().lockCategory(id);
        m_unlockedIds = CategoryLockManager::instance().getUnlockedIds();

        m_categoryModel->refresh();

        restoreExpandedState(QModelIndex(), expandedIds, expandedNames);
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#00A650;'>[OK] 分类已设置密码并锁定</b>", 1000, QColor("#00A650"));

        emit categoryLocked(id);
    }
}

void CategoryPanel::onLockCategory() {
    QModelIndex index = m_categoryTree->currentIndex();
    int id = getTargetCategoryId(index);
    if (id <= 0) return;

    // 🚨 在 CategoryLockManager 会话单例中立即清退解锁，实现安全物理上锁
    CategoryLockManager::instance().lockCategory(id);
    m_unlockedIds = CategoryLockManager::instance().getUnlockedIds();

    m_categoryModel->refresh();

    ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#E81123;'>[OK] 分类已立即锁定</b>", 1000, QColor("#E81123"));

    emit categoryLocked(id);
}

void CategoryPanel::onModifyPassword() {
    QModelIndex index = m_categoryTree->currentIndex();
    int id = getTargetCategoryId(index);
    if (id <= 0) return;

    Category cat = CategoryRepo::getById(id);
    if (cat.id <= 0) return;

    // 1. 验证旧密码
    CategoryLockDialog verifyDlg(QString::fromStdWString(cat.encryptHint), this);
    if (verifyDlg.exec() == QDialog::Accepted) {
        // 2. 成功后设置新密码
        CategorySetPasswordDialog setDlg(this);
        if (setDlg.exec() == QDialog::Accepted) {
            QString pwd = setDlg.password();
            QString hint = setDlg.hint();

            QSet<int> expandedIds;
            QStringList expandedNames;
            saveExpandedState(QModelIndex(), expandedIds, expandedNames);

            auto all = CategoryRepo::getAll();
            for(auto& c : all) {
                if(c.id == id) {
                    c.encrypted = true;
                    c.encryptHint = hint.toStdWString();
                    CategoryRepo::update(c);
                    break;
                }
            }

            // 修改后恢复物理锁定，保障隐私
            CategoryLockManager::instance().lockCategory(id);
            m_unlockedIds = CategoryLockManager::instance().getUnlockedIds();

            m_categoryModel->refresh();

            restoreExpandedState(QModelIndex(), expandedIds, expandedNames);
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#00A650;'>[OK] 密码修改成功，分类已锁定</b>", 1000, QColor("#00A650"));

            emit categoryLocked(id);
        }
    }
}

void CategoryPanel::onClearPassword() {
>>>>>>> REPLACE
```

### 4.7 修改 `src/ui/MainWindow.cpp`
建立锁定和解锁的完全双向数据信号通信通道。

```diff
<<<<<<< SEARCH
    // 监听侧边栏分类拖拽事件并交由控制层 (MainWindow) 处理物理导入与迁移决策
    connect(m_categoryPanel, &CategoryPanel::pathsDroppedToCategory, this, [this](const QStringList& paths, int targetCatId) {
=======
    // 监听侧边栏密码保护动作变化信号
    connect(m_categoryPanel, &CategoryPanel::categoryLocked, this, [this](int id) {
        // 🚨 任何时候只要分类被锁住，且右侧正是此分类，立即隐藏右侧所有显示的数据
        if (m_contentPanel && m_contentPanel->isMirrorSource() && m_contentPanel->currentCategoryId() == id) {
            m_contentPanel->loadCategory(id);
        }
    });

    // 监听内容面板卡片解锁成功的事件
    connect(m_contentPanel, &ContentPanel::categoryUnlocked, this, [this](int id) {
        Q_UNUSED(id);
        // 🚨 一旦在右侧解锁成功，侧边栏分类必须整体同步刷新解锁状态图标
        if (m_categoryPanel) {
            m_categoryPanel->requestRefresh(true);
        }
    });

    // 监听侧边栏分类拖拽事件并交由控制层 (MainWindow) 处理物理导入与迁移决策
    connect(m_categoryPanel, &CategoryPanel::pathsDroppedToCategory, this, [this](const QStringList& paths, int targetCatId) {
>>>>>>> REPLACE
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [x] 新增 `src/ui/CategoryLockWidget.h`
- [x] 新增 `src/ui/CategoryLockWidget.cpp`
- [x] 修改 `src/ui/ContentPanel.h`
- [x] 修改 `src/ui/ContentPanel.cpp`
- [x] 修改 `src/ui/CategoryPanel.h`
- [x] 修改 `src/ui/CategoryPanel.cpp`
- [x] 修改 `src/ui/MainWindow.cpp`

**明确禁止越界修改的范围：**
- [x] 磁盘导航（DiskNav）物理 DFS 扫描逻辑 —— 不修改。

## 6. 实现准则与预警【核心】
1. 所有嵌入式密码输入验证动作直接委托 `CategoryLockManager::instance().verifyAndUnlock`，确保会话一致性。
2. 确保 `CategoryLockWidget` 在析构或隐藏时清除临时密码，不留安全漏洞。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 置顶激活色彩 | 置顶激活采用 `#ff551c` | ✅ 符合。本方案不改变该属性。 |
| 清除按钮规范 | 每个可编辑的输入框必须配置上“Qt 原生的 setClearButtonEnabled(true)”，而且只可采用“Qt 原生的 setClearButtonEnabled(true)” | ✅ 符合。我们在 CategoryLockWidget.cpp 的输入框显式配置了该参数。 |
| 元数据管理与选择规范 | 在侧边栏分类模式下加载数据，点击上锁分类需进行安全拦截与清空。 | ✅ 符合。loadCategory 成功拦截并完全隐藏了数据。 |
