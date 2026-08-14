# 分类嵌入式卡片无边框密码验证重构 —— Modification_Plan-30.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
本方案承接自 Modification_Plan-29.md，针对用户提出的“密码保护”流程不够智能、弹出模态阻断对话框无法对标 RapidNotes 版本的问题（对应用户原话：“关于“密码保护”，你先去对比当前版本与“RapidNotes”版本整套流程和逻辑架构，你就会发现当前版本的密码保护的逻辑架构非常的恶心、智障，根本不智能，而且还会弹出这种傻逼对话框，完全无法对标RapidNotes版本”），进行系统级的无缝重构。我们将拆除阻断性的模态弹窗，采用完全嵌入到内容面板中央的内置无边框解锁卡片（CategoryLockWidget）替代之。

## 2. 问题定位
1. **阻断性弹窗过度干扰**：在 `ContentPanel::loadCategory` 和 `CategoryPanel` 展开/点击事件中，当分类被锁定且未解锁时，原系统直接使用了阻断性的模态窗口 `CategoryLockDialog` 进行拦截，这带来了极不流畅的交互体感。
2. **CategoryLockWidget 闲置**：当前代码库中虽已提供了高规格的内置卡片式解锁控件 `CategoryLockWidget`，但在主程序中由于历史负债从未被实例化与启用，需要将其完全嵌入内容面板的视图堆栈中作为无缝页进行调度。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 当前版本的密码保护逻辑架构根本不智能，会弹出阻断性对话框，无法对标 RapidNotes（对应用户原话） | 在 `ContentPanel` 视图堆栈中加入 `CategoryLockWidget` 解锁卡片。在加载未解锁的加密分类时，自动隐藏当前可能在内容区显示的数据，无缝切换堆栈至解锁卡片。解锁后静默同步并展开侧边栏（对应用户原话）。 | ✅ |

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 修改 `src/ui/ContentPanel.h`
在前向声明中加入 `CategoryLockWidget`，并将其作为类的成员变量进行声明。

```diff
<<<<<<< SEARCH
namespace ArcMeta {

struct RuntimeMeta;
=======
namespace ArcMeta {

struct RuntimeMeta;
class CategoryLockWidget;
>>>>>>> REPLACE
```

```diff
<<<<<<< SEARCH
    QVBoxLayout* m_mainLayout = nullptr;
    QStackedWidget* m_viewStack = nullptr;
    QPushButton* m_btnLayers = nullptr;
=======
    QVBoxLayout* m_mainLayout = nullptr;
    QStackedWidget* m_viewStack = nullptr;
    CategoryLockWidget* m_lockWidget = nullptr;
    QPushButton* m_btnLayers = nullptr;
>>>>>>> REPLACE
```

### 4.2 修改 `src/ui/ContentPanel.cpp`
引入 `"CategoryLockWidget.h"` 头文件，在 `initUi()` 中实例化 `m_lockWidget` 并添加至视图堆栈中，连接验证成功后的回调，重写 `loadCategory` 逻辑以支持无缝卡片式解锁调度。

```diff
<<<<<<< SEARCH
#include "CategoryLockDialog.h" 
#include "BatchRenameDialog.h" 
=======
#include "CategoryLockDialog.h" 
#include "CategoryLockWidget.h"
#include "BatchRenameDialog.h" 
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
        MainWindow* mw = nullptr;
        QWidget* parentWin = window();
        while (parentWin) {
            if ((mw = qobject_cast<MainWindow*>(parentWin))) break;
            parentWin = parentWin->parentWidget();
        }
        if (mw) {
            CategoryPanel* cp = mw->findChild<CategoryPanel*>();
            if (cp) {
                cp->syncUnlockedIds();
                cp->expandCategory(id);
            }
        }
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
            // 彻底移除阻断型模态对话框，直接使用无缝内置卡片式解锁界面进行展示
            m_model->clear();
            m_proxyModel->invalidate();
            m_lockWidget->setCategory(categoryId, QString::fromStdWString(cat.encryptHint));
            m_viewStack->setCurrentWidget(m_lockWidget);
            if (m_textPreview) m_textPreview->hide(); 
            if (m_imagePreview) m_imagePreview->hide(); 
            m_currentCategoryId = categoryId;
            m_currentCategoryType = "user_category";
            updateLayersButtonState();
            emit dataSourceChanged("category"); 
            return;
        }
    }

    // 已经解锁，将视图堆栈还原到正确的列表或网格显示页
    if (m_currentViewMode == ListView) {
        m_viewStack->setCurrentWidget(m_treeView);
    } else {
        m_viewStack->setCurrentWidget(m_gridView);
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
>>>>>>> REPLACE
```

### 4.3 修改 `src/ui/CategoryPanel.h`
增加公共同步方法 `syncUnlockedIds()` 和 `expandCategory(int id)`，允许外界对其进行解锁通知和操作。

```diff
<<<<<<< SEARCH
    explicit CategoryPanel(QWidget* parent = nullptr);
    ~CategoryPanel() override;

    void selectCategory(int id);
=======
    explicit CategoryPanel(QWidget* parent = nullptr);
    ~CategoryPanel() override;

    void selectCategory(int id);
    void syncUnlockedIds();
    void expandCategory(int id);
>>>>>>> REPLACE
```

### 4.4 修改 `src/ui/CategoryPanel.cpp`
实现 `syncUnlockedIds()` 与 `expandCategory(int id)`，并重构点击、展开事件的底层响应，移除阻断性模态窗解锁。

```diff
<<<<<<< SEARCH
    // 2026-03-xx 物理拦截：严禁加密分类在未解锁时被展开
    // 2026-05-27 物理加固：补全 this 上下文
    connect(m_categoryTree, &QTreeView::expanded, this, [this](const QModelIndex& index) {
        int id = index.data(IdRole).toInt();
        bool isEncrypted = index.data(EncryptedRole).toBool();
        
        // 物理修复：加密校验仅针对数据库分类（ID > 0），跳过系统项（ID < 0）
        if (isEncrypted && id > 0 && !m_unlockedIds.contains(id)) {
            // 物理阻断：立即折叠，防止闪烁
            m_categoryTree->collapse(index);
            // 异步触发校验，避免在信号回调中处理复杂 UI
            QTimer::singleShot(0, [this, index]() {
                if (tryUnlockCategory(index)) {
                    // 解锁成功后刷新状态并重新展开
                    m_categoryModel->setUnlockedIds(m_unlockedIds);
                    m_categoryModel->refresh();
                    m_categoryTree->expand(index);
                }
            });
        } else {
            // 2026-05-27 物理修复：展开时按需动态加载分类关联的文件，杜绝启动挂起
            m_categoryModel->loadCategoryItems(index);
        }
    });
=======
    // 2026-03-xx 物理拦截：严禁加密分类在未解锁时被展开，直接触发内容面板卡片密码输入
    // 2026-05-27 物理加固：补全 this 上下文
    connect(m_categoryTree, &QTreeView::expanded, this, [this](const QModelIndex& index) {
        int id = index.data(IdRole).toInt();
        bool isEncrypted = index.data(EncryptedRole).toBool();
        
        // 物理修复：加密校验仅针对数据库分类（ID > 0），跳过系统项（ID < 0）
        if (isEncrypted && id > 0 && !m_unlockedIds.contains(id)) {
            // 物理阻断：立即折叠，防止其在未解锁时显示子项
            m_categoryTree->collapse(index);
            m_categoryTree->setCurrentIndex(index);
            emit categorySelected(id, index.data(NameRole).toString(), index.data(TypeRole).toString(), index.data(PathRole).toString());
        } else {
            // 2026-05-27 物理修复：展开时按需动态加载分类关联的文件，杜绝启动挂起
            m_categoryModel->loadCategoryItems(index);
        }
    });
>>>>>>> REPLACE
```

```diff
<<<<<<< SEARCH
    connect(m_categoryTree, &QTreeView::clicked, this, [this](const QModelIndex& proxyIndex) {
        QModelIndex index = m_proxyModel->mapToSource(proxyIndex);
        QString type = index.data(TypeRole).toString();
        QString name = index.data(NameRole).toString();
        int id = index.data(IdRole).toInt();
        QString path = index.data(PathRole).toString();
        bool isEncrypted = index.data(EncryptedRole).toBool();

        // 2026-03-xx 物理防御：加密分类点击时触发校验
        if (isEncrypted && id > 0 && !m_unlockedIds.contains(id)) {
            if (tryUnlockCategory(index)) {
                emit categorySelected(id, name, type, path);
            } else {
                emit categorySelected(-1, "", "", "");
            }
            return;
        }

        // 核心联动：如果点击的是有效的分类、系统项或快速访问项
        if (!type.isEmpty()) {
             // 2026-06-xx 重构：点击项不再加载文件到树中，而是直接通过信号触发 ContentPanel 加载
             emit categorySelected(id, name, type, path);
        }
    });
=======
    connect(m_categoryTree, &QTreeView::clicked, this, [this](const QModelIndex& proxyIndex) {
        QModelIndex index = m_proxyModel->mapToSource(proxyIndex);
        QString type = index.data(TypeRole).toString();
        QString name = index.data(NameRole).toString();
        int id = index.data(IdRole).toInt();
        QString path = index.data(PathRole).toString();
        bool isEncrypted = index.data(EncryptedRole).toBool();

        // 2026-03-xx 物理防御：加密分类点击时直接进入，内容面板内置卡片接管验证
        if (isEncrypted && id > 0 && !m_unlockedIds.contains(id)) {
            emit categorySelected(id, name, type, path);
            return;
        }

        // 核心联动：如果点击的是有效的分类、系统项或快速访问项
        if (!type.isEmpty()) {
             // 2026-06-xx 重构：点击项不再加载文件到树中，而是直接通过信号触发 ContentPanel 加载
             emit categorySelected(id, name, type, path);
        }
    });
>>>>>>> REPLACE
```

```diff
<<<<<<< SEARCH
bool CategoryPanel::tryUnlockCategory(const QModelIndex& index) {
=======
void CategoryPanel::syncUnlockedIds() {
    m_unlockedIds = CategoryLockManager::instance().getUnlockedIds();
    if (m_categoryModel) {
        m_categoryModel->setUnlockedIds(m_unlockedIds);
        m_categoryModel->refresh();
    }
}

void CategoryPanel::expandCategory(int id) {
    if (!m_categoryModel || !m_categoryTree) return;
    
    std::function<QModelIndex(const QModelIndex&)> findId;
    findId = [&](const QModelIndex& parent) -> QModelIndex {
        for (int i = 0; i < m_categoryModel->rowCount(parent); ++i) {
            QModelIndex idx = m_categoryModel->index(i, 0, parent);
            if (idx.data(IdRole).toInt() == id) return idx;
            QModelIndex child = findId(idx);
            if (child.isValid()) return child;
        }
        return QModelIndex();
    };

    QModelIndex target = findId(QModelIndex());
    if (target.isValid()) {
        QModelIndex proxyIdx = m_proxyModel->mapFromSource(target);
        if (proxyIdx.isValid()) {
            m_categoryTree->expand(proxyIdx);
        }
    }
}

bool CategoryPanel::tryUnlockCategory(const QModelIndex& index) {
>>>>>>> REPLACE
```

## 5. 修改边界声明【范围】
- [x] 修改 `src/ui/ContentPanel.h`
- [x] 修改 `src/ui/ContentPanel.cpp`
- [x] 修改 `src/ui/CategoryPanel.h`
- [x] 修改 `src/ui/CategoryPanel.cpp`

## 6. 实现准则与预警【核心】
1. 依赖头文件 `"CategoryLockWidget.h"` 用于在 `ContentPanel` 中嵌入式实例化该类。
2. 解锁后会自动向上级寻道获取并触发 `CategoryPanel` 同步，不改变原有极简架构，彻底做到了高内聚和完美的分流。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 卡片式解锁  | 统一采用嵌入在内容面板中央的卡片式内置无边框解锁视图（CategoryLockWidget）进行密码解锁 | ✅ 符合 |

## 8. 待确认事项
（无）
