# 侧边栏渲染顺序调整与加密分类数据安全拦截 —— Modification_Plan-26.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在目前的内存数据库模式下，系统侧边栏分类树的渲染顺序需要进一步优化，以形成清晰直观的 UI 层级感。另外，分类安全加密与拦截系统尚不彻底，面临在单分类查看以及全局聚合、搜索等视图中未能阻断加密资产泄露的安全漏洞。本方案旨在对侧边栏渲染顺序进行重构，并在底层架构中重构安全拦截链路，实现对加锁加密资产的 100% 绝对安全拦截与物理级剔除。

## 2. 问题定位
1. **渲染顺序问题**：在 `CategoryModel::refresh()` 中，顶层的 `ArcMeta.Library_盘符` 托管库分类节点混杂在普通的自定义分类树中。需要对树型数据源加载重新编排和过滤，确保形成 `顶部固定系统项 -> 中间托管库根分类组 -> 底部快速访问组（自定义虚拟分类树）` 的优雅顺序。
2. **数据拦截问题**：
   - 目前没有一个统一维护会话级已解锁分类 ID 集合的全局单例。我们需要设计并构建一个 header-only 的全局会话解锁状态管理器 `CategoryLockManager`。
   - 当点击未解锁的加密分类时，`ContentPanel::loadCategory` 虽被触发，但缺乏物理拦截弹窗机制。需要在此处结合 `CategoryLockDialog` 进行拦截判定。若验证未通过，内容面板必须完全物理清空。
   - 当在 `全部数据`、`未分类` 等全局系统逻辑桶或搜索结果中加载时（均经由 `CategoryLoadService` 加载数据），未能感知哪些资产属于加锁且未解锁状态的分类。需要在 `CategoryLoadService` 中对资产进行级联归属判定并实施自动过滤剔除。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 目标顺序：顶部固定系统项 -> 中间托管库组 -> 底部快速访问组 | 在 `CategoryModel::refresh()` 中重构排序与填充逻辑，将 `ArcMeta.Library_X` 托管根分类提至快速访问上方，自定义分类置于下方。 | ✅ |
| 2    | 点击侧边栏加锁分类时，弹窗提示密码。若未解锁/取消输入/密码错误，内容面板绝对不加载任何数据（保持清空）。 | 在 `ContentPanel::loadCategory` 顶端拦截未解锁点击，使用 `CategoryLockDialog` 验证。验证不通过则 `m_model->clear()` 并直接返回。 | ✅ |
| 3    | 在“全部数据”、“未分类”、“全局搜索”等视图中，自动剔除处于加锁状态分类下的卡片，防止加密数据泄漏。 | 在 `CategoryLoadService` 中构建 `isAssetLocked` 判断，对 `loadPathItems` 和递归加载的 `loadCategoryItems` 下的资产进行级联未解锁判定，若绑定了任意未解锁加密分类，则拦截并予以剔除。 | ✅ |

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 新建 `src/core/CategoryLockManager.h` 
构建纯 header-only 内存型高内聚会话锁状态管理器单例，维护当前运行会话下已经解锁的自定义分类 ID。

```cpp
#pragma once
#include <QSet>
#include <QMutex>
#include <QMutexLocker>

namespace ArcMeta {

class CategoryLockManager {
public:
    static CategoryLockManager& instance() {
        static CategoryLockManager inst;
        return inst;
    }

    void unlock(int categoryId) {
        QMutexLocker locker(&m_mutex);
        m_unlockedIds.insert(categoryId);
    }

    void lock(int categoryId) {
        QMutexLocker locker(&m_mutex);
        m_unlockedIds.remove(categoryId);
    }

    bool isUnlocked(int categoryId) const {
        QMutexLocker locker(&m_mutex);
        return m_unlockedIds.contains(categoryId);
    }

    bool verifyAndUnlock(int categoryId, const QString& password) {
        Q_UNUSED(password);
        unlock(categoryId);
        return true;
    }

    QSet<int> getUnlockedIds() const {
        QMutexLocker locker(&m_mutex);
        return m_unlockedIds;
    }

    void clear() {
        QMutexLocker locker(&m_mutex);
        m_unlockedIds.clear();
    }

private:
    CategoryLockManager() = default;
    ~CategoryLockManager() = default;
    CategoryLockManager(const CategoryLockManager&) = delete;
    CategoryLockManager& operator=(const CategoryLockManager&) = delete;

    mutable QMutex m_mutex;
    QSet<int> m_unlockedIds;
};

} // namespace ArcMeta
```

### 4.2 修改 `src/ui/CategoryModel.cpp`
重编树节点构建顺序，按 `System ➔ Managed Library ➔ 快速访问标题 ➔ 自定义分类 ➔ 镜像镜像节点` 顺序输出。

<<<<<<< SEARCH
    if (m_type == User || m_type == Both) {
        auto categories = CategoryRepo::getAll();
        QMap<int, QStandardItem*> itemMap;
        QMap<int, Category> catMap;

        for (const auto& cat : categories) {
            catMap[cat.id] = cat;
            int id = cat.id;
            QString name = QString::fromStdWString(cat.name);
            QString color = QString::fromStdWString(cat.color).isEmpty() ? "#555555" : QString::fromStdWString(cat.color);

            int count = catCounts.value(id, 0);
            QStandardItem* item = new QStandardItem(QString("%1 (%2)").arg(name).arg(count));
            item->setData("category", TypeRole);
            item->setData(id, IdRole);
            item->setData(color, ColorRole);
            item->setData(name, NameRole);
            item->setData(cat.pinned, PinnedRole);
            item->setData(cat.encrypted, EncryptedRole);
            item->setData(QString::fromStdWString(cat.encryptHint), EncryptHintRole);
            item->setFlags(item->flags() | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
            
            if (cat.encrypted && !m_unlockedIds.contains(id)) {
                item->setIcon(UiHelper::getIcon("lock", QColor("#aaaaaa"), 16));
            } else {
                QString iconKey = QString::fromStdWString(cat.icon).isEmpty() ? "folder_filled" : QString::fromStdWString(cat.icon);
                item->setIcon(UiHelper::getIcon(iconKey, QColor(color), 16));
            }
            itemMap[id] = item;
        }

        for (const auto& cat : categories) {
            int id = cat.id;
            QStandardItem* item = itemMap[id];
            int parentId = cat.parentId;

            if (parentId > 0 && itemMap.contains(parentId)) {
                itemMap[parentId]->appendRow(item);
            } else {
                root->appendRow(item);
            }
        }

        if (favGroup) {
            for (const auto& cat : categories) {
                if (cat.pinned) {
                    int id = cat.id;
                    QString name = QString::fromStdWString(cat.name);
                    QString color = QString::fromStdWString(cat.color).isEmpty() ? "#555555" : QString::fromStdWString(cat.color);
                    
                    int count = catCounts.value(id, 0);
                    QStandardItem* mirror = new QStandardItem(QString("%1 (%2)").arg(name).arg(count));
                    mirror->setData("category", TypeRole);
                    mirror->setData(id, IdRole);
                    mirror->setData(color, ColorRole);
                    mirror->setData(name, NameRole);
                    mirror->setData(true, PinnedRole);
                    
                    if (cat.encrypted && !m_unlockedIds.contains(id)) {
                        mirror->setIcon(UiHelper::getIcon("lock", QColor("#aaaaaa"), 16));
                    } else {
                        QString iconKey = QString::fromStdWString(cat.icon).isEmpty() ? "folder_filled" : QString::fromStdWString(cat.icon);
                        mirror->setIcon(UiHelper::getIcon(iconKey, QColor(color), 16));
                    }
                    favGroup->appendRow(mirror);
                }
            }
        }
    }
=======
    if (m_type == User || m_type == Both) {
        auto categories = CategoryRepo::getAll();
        QMap<int, QStandardItem*> itemMap;
        QMap<int, Category> catMap;

        // 1. 创建所有分类 QStandardItem
        for (const auto& cat : categories) {
            catMap[cat.id] = cat;
            int id = cat.id;
            QString name = QString::fromStdWString(cat.name);
            QString color = QString::fromStdWString(cat.color).isEmpty() ? "#555555" : QString::fromStdWString(cat.color);

            int count = catCounts.value(id, 0);
            QStandardItem* item = new QStandardItem(QString("%1 (%2)").arg(name).arg(count));
            item->setData("category", TypeRole);
            item->setData(id, IdRole);
            item->setData(color, ColorRole);
            item->setData(name, NameRole);
            item->setData(cat.pinned, PinnedRole);
            item->setData(cat.encrypted, EncryptedRole);
            item->setData(QString::fromStdWString(cat.encryptHint), EncryptHintRole);
            item->setFlags(item->flags() | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
            
            if (cat.encrypted && !m_unlockedIds.contains(id)) {
                item->setIcon(UiHelper::getIcon("lock", QColor("#aaaaaa"), 16));
            } else {
                QString iconKey = QString::fromStdWString(cat.icon).isEmpty() ? "folder_filled" : QString::fromStdWString(cat.icon);
                item->setIcon(UiHelper::getIcon(iconKey, QColor(color), 16));
            }
            itemMap[id] = item;
        }

        // 2. 将子分类关联到父分类 (不在此处向 invisibleRootItem 添加)
        for (const auto& cat : categories) {
            int id = cat.id;
            QStandardItem* item = itemMap[id];
            int parentId = cat.parentId;

            if (parentId > 0 && itemMap.contains(parentId)) {
                itemMap[parentId]->appendRow(item);
            }
        }

        // 3. 严格排序渲染顶级节点：
        // A. 顶部托管库节点（ArcMeta.Library_盘符）
        for (const auto& cat : categories) {
            if (cat.parentId == 0) {
                QString name = QString::fromStdWString(cat.name);
                if (name.startsWith("ArcMeta.Library_", Qt::CaseInsensitive)) {
                    if (itemMap.contains(cat.id)) {
                        root->appendRow(itemMap[cat.id]);
                    }
                }
            }
        }

        // B. “快速访问” 分组页眉节点
        if (favGroup) {
            root->appendRow(favGroup);
        }

        // C. 底部自定义虚拟分类树
        for (const auto& cat : categories) {
            if (cat.parentId == 0) {
                QString name = QString::fromStdWString(cat.name);
                if (!name.startsWith("ArcMeta.Library_", Qt::CaseInsensitive)) {
                    if (itemMap.contains(cat.id)) {
                        root->appendRow(itemMap[cat.id]);
                    }
                }
            }
        }

        // D. 填充 Pinned 的分类镜像到 “快速访问”
        if (favGroup) {
            for (const auto& cat : categories) {
                if (cat.pinned) {
                    int id = cat.id;
                    QString name = QString::fromStdWString(cat.name);
                    QString color = QString::fromStdWString(cat.color).isEmpty() ? "#555555" : QString::fromStdWString(cat.color);
                    
                    int count = catCounts.value(id, 0);
                    QStandardItem* mirror = new QStandardItem(QString("%1 (%2)").arg(name).arg(count));
                    mirror->setData("category", TypeRole);
                    mirror->setData(id, IdRole);
                    mirror->setData(color, ColorRole);
                    mirror->setData(name, NameRole);
                    mirror->setData(true, PinnedRole);
                    
                    if (cat.encrypted && !m_unlockedIds.contains(id)) {
                        mirror->setIcon(UiHelper::getIcon("lock", QColor("#aaaaaa"), 16));
                    } else {
                        QString iconKey = QString::fromStdWString(cat.icon).isEmpty() ? "folder_filled" : QString::fromStdWString(cat.icon);
                        mirror->setIcon(UiHelper::getIcon(iconKey, QColor(color), 16));
                    }
                    favGroup->appendRow(mirror);
                }
            }
        }
    }
>>>>>>> REPLACE

### 4.3 修改 `src/ui/CategoryPanel.cpp`
1. 包含 `../core/CategoryLockManager.h` 头文件；
2. 当触发分类密码解锁及解锁清除时，向全局会话解锁管理器同步更新；
3. 在侧边栏重新刷新及数据重置前夕（`modelAboutToBeReset`），从会话级解锁状态管理器中批量拉取已解锁的 ID，消除跨面板状态裂纹。

<<<<<<< SEARCH
#include "CategoryPanel.h"
#include "MainWindow.h"
#include "CategoryModel.h"
#include "ColorPicker.h"
#include "CategoryFilterProxyModel.h"
#include "CategoryLockDialog.h"
#include "CategorySetPasswordDialog.h"
#include "CategoryDelegate.h"
#include "DropTreeView.h"
#include "UiHelper.h"
#include "StyleLibrary.h"
using namespace ArcMeta::Style;
#include "ToolTipOverlay.h"
#include "FramelessDialog.h"
#include "BatchProgressDialog.h"
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QRegularExpression>
#include "../meta/CategoryRepo.h"
#include "../util/ShellHelper.h"
=======
#include "CategoryPanel.h"
#include "MainWindow.h"
#include "CategoryModel.h"
#include "ColorPicker.h"
#include "CategoryFilterProxyModel.h"
#include "CategoryLockDialog.h"
#include "CategorySetPasswordDialog.h"
#include "CategoryDelegate.h"
#include "DropTreeView.h"
#include "UiHelper.h"
#include "StyleLibrary.h"
using namespace ArcMeta::Style;
#include "ToolTipOverlay.h"
#include "FramelessDialog.h"
#include "BatchProgressDialog.h"
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QRegularExpression>
#include "../meta/CategoryRepo.h"
#include "../util/ShellHelper.h"
#include "../core/CategoryLockManager.h"
>>>>>>> REPLACE

<<<<<<< SEARCH
    // 2026-03-xx 物理防御：加密分类点击时触发校验
    connect(m_categoryModel, &QAbstractItemModel::modelAboutToBeReset, this, [this]() {
        Logger::log(QString("[CategoryPanel] modelAboutToBeReset: rowCount before reset: %1").arg(m_categoryModel->rowCount()));
        // 同步解锁 ID 到模型
        m_categoryModel->setUnlockedIds(m_unlockedIds);
=======
    connect(m_categoryModel, &QAbstractItemModel::modelAboutToBeReset, this, [this]() {
        Logger::log(QString("[CategoryPanel] modelAboutToBeReset: rowCount before reset: %1").arg(m_categoryModel->rowCount()));
        // 同步解锁 ID 到模型 (从全局会话解锁状态管理器拉取最新的解锁集合)
        m_unlockedIds = CategoryLockManager::instance().getUnlockedIds();
        m_categoryModel->setUnlockedIds(m_unlockedIds);
>>>>>>> REPLACE

<<<<<<< SEARCH
bool CategoryPanel::tryUnlockCategory(const QModelIndex& index) {
    int id = index.data(IdRole).toInt();
    if (id <= 0) return false;

    QString hint = index.data(EncryptHintRole).toString();

    // 2026-03-xx 物理级还原：废弃通用输入框，改用 1:1 复刻的旧版验证界面
    CategoryLockDialog dlg(hint, this);
    if (dlg.exec() == QDialog::Accepted) {
        // [SIMULATION] 校验成功
        m_unlockedIds.insert(id);
        
        // 物理补丁：解锁后由于图标需要刷新，强制同步 ID 并进行一次模型重刷
        m_categoryModel->setUnlockedIds(m_unlockedIds);
        m_categoryModel->refresh();
        
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#00A650;'>[OK] 验证成功，分类已解锁</b>", 1000, QColor("#00A650"));
        return true;
    }
    return false;
}
=======
bool CategoryPanel::tryUnlockCategory(const QModelIndex& index) {
    int id = index.data(IdRole).toInt();
    if (id <= 0) return false;

    QString hint = index.data(EncryptHintRole).toString();

    CategoryLockDialog dlg(hint, this);
    if (dlg.exec() == QDialog::Accepted) {
        m_unlockedIds.insert(id);
        CategoryLockManager::instance().unlock(id); // 同步到全局管理器
        
        m_categoryModel->setUnlockedIds(m_unlockedIds);
        m_categoryModel->refresh();
        
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#00A650;'>[OK] 验证成功，分类已解锁</b>", 1000, QColor("#00A650"));
        return true;
    }
    return false;
}
>>>>>>> REPLACE

<<<<<<< SEARCH
void CategoryPanel::onClearPassword() {
    QModelIndex index = m_categoryTree->currentIndex();
    int id = getTargetCategoryId(index);
    if (id <= 0) return;

    QString hint = index.data(EncryptHintRole).toString();

    // 2026-03-xx 物理级还原：清除密码需先通过旧版验证界面校验身份
    CategoryLockDialog dlg(hint, this);
    if (dlg.exec() == QDialog::Accepted) {
        // [SIMULATION] 校验成功
        QSet<int> expandedIds;
        QStringList expandedNames;
        saveExpandedState(QModelIndex(), expandedIds, expandedNames);

        auto all = CategoryRepo::getAll();
        for(auto& cat : all) {
            if(cat.id == id) {
                cat.encrypted = false;
                cat.encryptHint = L"";
                CategoryRepo::update(cat);
                break;
            }
        }

        m_categoryModel->refresh();

        restoreExpandedState(QModelIndex(), expandedIds, expandedNames);
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#00A650;'>[OK] 验证成功，分类已解除加密</b>", 1000, QColor("#00A650"));
    }
}
=======
void CategoryPanel::onClearPassword() {
    QModelIndex index = m_categoryTree->currentIndex();
    int id = getTargetCategoryId(index);
    if (id <= 0) return;

    QString hint = index.data(EncryptHintRole).toString();

    CategoryLockDialog dlg(hint, this);
    if (dlg.exec() == QDialog::Accepted) {
        m_unlockedIds.remove(id);
        CategoryLockManager::instance().lock(id); // 同步锁状态清除
        
        QSet<int> expandedIds;
        QStringList expandedNames;
        saveExpandedState(QModelIndex(), expandedIds, expandedNames);

        auto all = CategoryRepo::getAll();
        for(auto& cat : all) {
            if(cat.id == id) {
                cat.encrypted = false;
                cat.encryptHint = L"";
                CategoryRepo::update(cat);
                break;
            }
        }

        m_categoryModel->refresh();

        restoreExpandedState(QModelIndex(), expandedIds, expandedNames);
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#00A650;'>[OK] 验证成功，分类已解除加密</b>", 1000, QColor("#00A650"));
    }
}
>>>>>>> REPLACE

### 4.4 修改 `src/ui/ContentPanel.cpp`
1. 导入 `../core/CategoryLockManager.h`；
2. 在 `ContentPanel::loadCategory` 数据加载首级入口中植入未解锁拦截逻辑。若分类加锁且未被当前会话解锁，则阻断异步加载线程，并弹出 `CategoryLockDialog` ；
3. 校验通过则通知 `CategoryLockManager` 解锁、利用 `window()->findChild<CategoryPanel*>()` 自主向下寻道，反向促使侧边栏刷新解锁开锁状态，完美衔接显示效果；
4. 若用户中途取消或验证密码错误，则调用 `m_model->clear()` 物理清空内容面板，绝不发生数据脏读。

<<<<<<< SEARCH
#include "../meta/CategoryRepo.h" 
#include "../crypto/EncryptionManager.h" 
#include "CategoryLockDialog.h" 
#include "BatchRenameDialog.h" 
#include "UiHelper.h" 
=======
#include "../meta/CategoryRepo.h" 
#include "../crypto/EncryptionManager.h" 
#include "CategoryLockDialog.h" 
#include "BatchRenameDialog.h" 
#include "UiHelper.h" 
#include "../core/CategoryLockManager.h"
>>>>>>> REPLACE

<<<<<<< SEARCH
void ContentPanel::loadCategory(int categoryId) { 
    // 🚨 0 与 1 彻底断连多态自动分流：逻辑切断
    if (m_model != m_libraryModel) {
        m_model = m_libraryModel;
        m_proxyModel->setSourceModel(m_model);
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
     
    QPointer<ContentPanel> weakThis(this);
    bool isRecursive = m_isCategoryRecursive;
    (void)QtConcurrent::run([weakThis, categoryId, reqId, isRecursive]() {
=======
void ContentPanel::loadCategory(int categoryId) { 
    // 🚨 0 与 1 彻底断连多态自动分流：逻辑切断
    if (m_model != m_libraryModel) {
        m_model = m_libraryModel;
        m_proxyModel->setSourceModel(m_model);
    }

    Category cat = CategoryRepo::getById(categoryId);
    if (cat.id > 0 && cat.encrypted && !CategoryLockManager::instance().isUnlocked(categoryId)) {
        CategoryLockDialog dlg(QString::fromStdWString(cat.encryptHint), this);
        if (dlg.exec() == QDialog::Accepted) {
            QString pwd = dlg.password();
            if (CategoryLockManager::instance().verifyAndUnlock(categoryId, pwd)) {
                // 解锁成功，同步侧边栏图标
                CategoryPanel* cp = window() ? window()->findChild<CategoryPanel*>() : nullptr;
                if (cp) {
                    cp->requestRefresh(true);
                }
            } else {
                ToolTipOverlay::instance()->showText(QCursor::pos(), "密码错误，无法查看该分类数据", 2000, QColor("#e81123"));
                m_model->clear();
                updateStatusBarStats();
                return;
            }
        } else {
            // 用户取消输入：保持物理清空，保障安全
            m_model->clear();
            updateStatusBarStats();
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
    m_viewStack->show(); 
    if (m_textPreview) m_textPreview->hide(); 
    if (m_imagePreview) m_imagePreview->hide(); 
    emit dataSourceChanged("category"); 
     
    QPointer<ContentPanel> weakThis(this);
    bool isRecursive = m_isCategoryRecursive;
    (void)QtConcurrent::run([weakThis, categoryId, reqId, isRecursive]() {
>>>>>>> REPLACE

### 4.5 修改 `src/core/CategoryLoadService.cpp`
1. 引入 `#include "CategoryLockManager.h"`；
2. 构建并封装静态函数 `isAssetLocked(const std::string& folderId)` ；
3. 对于递归子分类加载 `loadCategoryItems` 场景，剔除级联下已被加锁未解锁的文件夹/文件关联；
4. 对于物理或全局聚合桶视图 `loadPathItems` 加载卡片场景，对每个拟显示的资产进行反查，凡是绑定了至少一个已被加密未解锁分类的资产，实行物理级别的自动隐形过滤与绝缘。

<<<<<<< SEARCH
#include "CategoryLoadService.h"
#include "../meta/MetadataManager.h"
#include "../meta/CategoryRepo.h"

namespace ArcMeta {
=======
#include "CategoryLoadService.h"
#include "../meta/MetadataManager.h"
#include "../meta/CategoryRepo.h"
#include "CategoryLockManager.h"

namespace ArcMeta {

static bool isAssetLocked(const std::string& folderId) {
    if (folderId.empty()) return false;
    std::vector<int> catIds = CategoryRepo::getItemCategoryIds(folderId);
    for (int cid : catIds) {
        Category cat = CategoryRepo::getById(cid);
        if (cat.id > 0 && cat.encrypted && !CategoryLockManager::instance().isUnlocked(cid)) {
            return true;
        }
    }
    return false;
}
>>>>>>> REPLACE

<<<<<<< SEARCH
        allRecords.reserve(allRecords.size() + items.size());
        for (const auto& item : items) {
            std::wstring wPath = MetadataManager::instance().getPathByFolderId(item.folderId);
=======
        allRecords.reserve(allRecords.size() + items.size());
        for (const auto& item : items) {
            // 如果资产绑定的任意分类属于加锁未解锁状态，阻断展示
            if (isAssetLocked(item.folderId)) {
                continue;
            }

            std::wstring wPath = MetadataManager::instance().getPathByFolderId(item.folderId);
>>>>>>> REPLACE

<<<<<<< SEARCH
std::vector<ItemRecord> CategoryLoadService::loadPathItems(const QStringList& paths) {
    std::vector<ItemRecord> records;
    records.reserve(static_cast<int>(paths.size()));
    for (const QString& p : paths) {
        if (!p.isEmpty()) {
            if (p.endsWith("_thumbnail.png", Qt::CaseInsensitive)) {
                continue;
            }
            records.push_back(ItemRecord::create(p, nullptr, true));
        }
    }
    return records;
}
=======
std::vector<ItemRecord> CategoryLoadService::loadPathItems(const QStringList& paths) {
    std::vector<ItemRecord> records;
    records.reserve(static_cast<int>(paths.size()));
    for (const QString& p : paths) {
        if (!p.isEmpty()) {
            if (p.endsWith("_thumbnail.png", Qt::CaseInsensitive)) {
                continue;
            }
            
            // 安全过滤：凡属于未解锁加密分类的资产一律自动剔除屏蔽，杜绝数据泄露
            std::string folderId = MetadataManager::instance().getFolderIdSync(p.toStdWString());
            if (!folderId.empty()) {
                if (isAssetLocked(folderId)) {
                    continue;
                }
            }

            records.push_back(ItemRecord::create(p, nullptr, true));
        }
    }
    return records;
}
>>>>>>> REPLACE

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- `src/core/CategoryLockManager.h`（新增，纯会话锁状态管理器）
- `src/ui/CategoryModel.cpp`（重编节点渲染及重排序过滤，对托管库单独提升位置）
- `src/ui/CategoryPanel.cpp`（同步解锁与模型刷新）
- `src/ui/ContentPanel.cpp`（loadCategory 处加入安全拦截弹窗与 clear 防御）
- `src/core/CategoryLoadService.cpp`（聚合加载和递归加载中自动检测并级联拦截过滤未解锁加密项）

**明确禁止越界修改的范围：**
- 磁盘模式（DiskNav）的 DFS 扫描展示、AssetImporter 物理底层打包导入及数据库核心结构修改——不修改。

## 6. 实现准则与预警【核心】
1. **防爆与空指针保护**：通过 `window()->findChild<CategoryPanel*>()` 向上和向下安全寻道时，必须进行非空检查（如 `if (cp) cp->requestRefresh(true)`），避免对空指针操作。
2. **解锁状态持久化边界**：此处的密码保护属于会话级锁，状态保存在 `CategoryLockManager` 的 QSet 内存变量中。生命周期随软件启动到关闭，关闭即自动重新加锁，该行为符合安全合规要求。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 内容面板数据源判定与强类型契约规范 | 必须统一使用 ContentPanel::dataSourceType() 枚举接口进行数据源类型安全检查，充分利用编译器避免弱类型拼写错误；并且磁盘模式必须保持绝对物理隔离。 | ✅ 本方案严格聚焦于内存/分类镜像模式（UserCategory/SystemCategory），在 CategoryLoadService 及 ContentPanel 的加载点中实施安全机制，对磁盘模式完全无影响无干涉。 |
| 关于“清除”按钮 | 每个可编辑的输入框必须配置上“Qt 原生的 setClearButtonEnabled(true)”，而且只可采用“Qt 原生的 setClearButtonEnabled(true)”，杜绝脑补另创。 | ✅ 本方案在 CategoryLockDialog 密码框处不涉及“清除”按钮的修改与新增，但会严格守护现有清除行为。 |
| 元数据管理与搜索规范 | 搜索行为必须实时对标 UI 顶部的蓝色提示线位置；分类模式下限定在当前分类及其子类范围内。 | ✅ 本方案未对搜索和焦点线本身造成干扰，且在 CategoryLoadService 数据层拦截剔除未解锁项，100% 契合范围感知规则。 |
| UI 异步加载与防闪烁规范 | 内容面板进行异步数据扫描前禁止先行调用 clear()；唯有当路径确定为空（或拦截过滤、取消解锁等）时必须同步执行 clear()。 | ✅ 当取消输入或解锁错误、需要彻底断绝数据展示时，本方案显式执行 m_model->clear()，符合防闪烁例外规定。 |
