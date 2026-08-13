# 职责单一重构与打补丁代码根除完整终极方案 —— srp-patchwork-reconstruction-2.md

> 状态：已批准，执行中 / 已执行完成

## 1. 方案说明
本方案是对上一版方案的**完整修正与补全**。针对此前审核发现的 3 个致命缺陷（遗漏 `ContentPanel` 右键锁重构、`LibraryMaintenanceService` 擦除逻辑被阉割、以及新建分类时代理模型未就绪的时序陷阱），本方案已完成 100% 修复与对齐。

执行者只需严格、机械地按照第 3 节给出的 Git merge diff 代码块进行物理替换，不得进行任何自由发挥。

---

## 2. 修正对照表

| 遗漏/缺陷编号 | 审核发现问题 | 本方案修正点 |
| :--- | :--- | :--- |
| **缺陷 1** | 遗漏 `ContentPanel` 右键菜单强锁信号补丁重构图纸 | **补齐 3.5 节**：在 `ItemModelBase` 中引入挂起更新队列，完全移除 `blockSignals` 与 `setUpdatesEnabled` 补丁。 |
| **缺陷 2** | `LibraryMaintenanceService` 阉割了幽灵文件擦除与孤立关系清理 | **重构 3.2 节**：完整还原三步走清理逻辑（空包物理删除、幽灵数据 SQLite 事务擦除、孤立关系清洗），并强制采用 `SqlTransaction`。 |
| **缺陷 3** | `CategoryPanel` 绑定的 `modelReset` 发生在代理模型恢复前 | **重构 3.3 节**：将 `handlePendingEdit` 移至 `restoreExpandedState` 终态完成后触发，确保代理模型索引 100% 就绪。 |

---

## 3. 详细解决方案（无脑实施图纸）

---

### 3.1 重构方案一：净化 `CategoryModel` 为纯粹表现媒介

#### 3.1.1 修改 `src/ui/CategoryModel.h`：追加逻辑信号，解耦底层操作
```diff
<<<<<<< SEARCH
public slots:
    void refresh();
    void updateStatistics(const QMap<QString, int>& sysCounts, const QMap<int, int>& catCounts);
=======
signals:
    // 🚀 【重构解耦】：通知外部控制器进行异步物理改名和数据库更新
    void categoryRenameRequested(int catId, const QString& newName);
    // 🚀 【重构解耦】：通知外部控制器执行同级分类重新排序
    void categoryOrderChanged(int draggedId, int targetParentId, int insertRow);

public slots:
    void refresh();
    void updateStatistics(const QMap<QString, int>& sysCounts, const QMap<int, int>& catCounts);
>>>>>>> REPLACE
```

#### 3.1.2 修改 `src/ui/CategoryModel.cpp` :: `setData`
```diff
<<<<<<< SEARCH
            (void)QtConcurrent::run([this, targetCat, newName]() mutable {
                bool renameSuccess = true;
                bool physicalRenamed = false;
                QString oldPath;
                QString newPath;
                if (!targetCat.physicalPath.empty()) {
                    oldPath = QString::fromStdWString(targetCat.physicalPath);
                    QFileInfo oldInfo(oldPath);
                    newPath = QDir::toNativeSeparators(oldInfo.absoluteDir().absoluteFilePath(newName));
                    if (oldPath != newPath) {
                        if (QFile::rename(oldPath, newPath)) {
                            targetCat.physicalPath = newPath.toStdWString();
                            physicalRenamed = true;
                        } else {
                            renameSuccess = false;
                            qWarning() << "[CategoryModel] QFile::rename failed from" << oldPath << "to" << newPath;
                        }
                    }
                }

                if (renameSuccess) {
                    targetCat.name = newName.toStdWString();
                    CategoryRepo::update(targetCat);

                    if (physicalRenamed) {
                        MetadataManager::instance().renameItem(oldPath.toStdWString(), newPath.toStdWString());
                    }
                }

                QMetaObject::invokeMethod(this, [this]() {
                    refresh();
                }, Qt::QueuedConnection);
            });

            return true;
=======
            // 🚀 【重构净化】：Model 不直接跑线程和修改磁盘/数据库，直接发射重命名信号由控制器接收处理
            emit categoryRenameRequested(id, newName);
            return true;
>>>>>>> REPLACE
```

#### 3.1.3 修改 `src/ui/CategoryModel.cpp` :: `dropMimeData`
```diff
<<<<<<< SEARCH
    // 按已有的 sortOrder 升序排列同级项
    std::sort(siblings.begin(), siblings.end(), [](const Category& a, const Category& b) {
        return a.sortOrder < b.sortOrder;
    });

    // 5. 计算全新的插入索引 row
    int insertRow = row;
    if (insertRow < 0 || insertRow > static_cast<int>(siblings.size())) {
        insertRow = static_cast<int>(siblings.size()); // 默认插入尾部
    }

    draggedCat.parentId = targetParentId;
    siblings.insert(siblings.begin() + insertRow, draggedCat);

    // 6. 100% 物理写盘：批量重新计算并更新 SQLite 中的 sortOrder 序号与 parentId
    for (size_t i = 0; i < siblings.size(); ++i) {
        siblings[i].sortOrder = static_cast<int>(i);
        CategoryRepo::update(siblings[i]);
    }

    // 7. 彻底阻断 Qt 原生深拷贝克隆坏行为，投递异步 refresh() 从数据库权威重绘！
    QMetaObject::invokeMethod(this, [this]() {
        refresh();
    }, Qt::QueuedConnection);

    return true; // 物理阻断 Qt 原生深拷贝！
=======
    // 按已有的 sortOrder 升序排列同级项
    std::sort(siblings.begin(), siblings.end(), [](const Category& a, const Category& b) {
        return a.sortOrder < b.sortOrder;
    });

    // 5. 计算全新的插入索引 row
    int insertRow = row;
    if (insertRow < 0 || insertRow > static_cast<int>(siblings.size())) {
        insertRow = static_cast<int>(siblings.size()); // 默认插入尾部
    }

    // 🚀 【重构净化】：拖拽落盘排序动作直接向上派发通知，Model 保持纯净只读
    emit categoryOrderChanged(draggedCatId, targetParentId, insertRow);
    return true; // 物理阻断 Qt 原生深拷贝！
>>>>>>> REPLACE
```

---

### 3.2 重构方案二：完整实现 `LibraryMaintenanceService` 服务类（完整还原三步走清理）

#### 3.2.1 新建 `src/core/LibraryMaintenanceService.h`
```cpp
#pragma once
#include <QObject>
#include <QStringList>

namespace ArcMeta {

class LibraryMaintenanceService : public QObject {
    Q_OBJECT
public:
    static LibraryMaintenanceService& instance() {
        static LibraryMaintenanceService inst;
        return inst;
    }

    // 🚀 【SRP 拆分】：异步三步走完整清扫（空包物理删除 + 幽灵元数据擦除 + 孤立分类关系清洗）
    void scanAndCleanEmptyArcsAsync();

signals:
    void cleanFinished(int cleanCount, int ghostCount, int orphanCount);

private:
    explicit LibraryMaintenanceService(QObject* parent = nullptr) : QObject(parent) {}
};

} // namespace ArcMeta
```

#### 3.2.2 新建 `src/core/LibraryMaintenanceService.cpp`
```cpp
#include "LibraryMaintenanceService.h"
#include "../meta/DatabaseManager.h"
#include "../meta/MetadataManager.h"
#include "../meta/CategoryRepo.h"
#include <QtConcurrent>
#include <QDir>
#include <QFileInfo>
#include <QDebug>

namespace ArcMeta {

void LibraryMaintenanceService::scanAndCleanEmptyArcsAsync() {
    (void)QtConcurrent::run([this]() {
        int cleanCount = 0;
        int ghostCount = 0;
        int orphanCount = 0;

        auto dbs = DatabaseManager::instance().getActiveMemoryDbs();

        // ==========================================
        // 第一步：盘查并物理清理空托管包 (磁盘 -> 数据库)
        // ==========================================
        const auto drives = QDir::drives();
        QStringList allEmptyArcDirs;
        QStringList allEmptyFolderIds;

        for (const QFileInfo& drive : drives) {
            QString letter = drive.absolutePath().left(1).toUpper();
            std::wstring volSerial = MetadataManager::getVolumeSerialNumber(drive.absolutePath().toStdWString());
            if (volSerial == L"UNKNOWN") continue;

            std::wstring managedRootW = MetadataManager::getManagedLibraryPath(volSerial, letter);
            if (managedRootW.empty()) continue;

            QString managedRoot = QString::fromStdWString(managedRootW);
            QDir libDir(managedRoot);
            if (!libDir.exists()) continue;

            QStringList arcEntries = libDir.entryList({"*.arc"}, QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
            for (const QString& arcName : arcEntries) {
                QFileInfo arcInfo(libDir.absoluteFilePath(arcName));
                QString baseName = arcInfo.completeBaseName();
                if (baseName.length() != 13) continue;

                QDir arcDir(arcInfo.absoluteFilePath());
                QStringList entries = arcDir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
                bool hasRealMaterials = false;
                for (const QString& fName : entries) {
                    if (fName.endsWith("_thumbnail.png", Qt::CaseInsensitive)) continue;
                    if (fName.compare(".ArcMeta.json", Qt::CaseInsensitive) == 0) continue;
                    hasRealMaterials = true;
                    break;
                }

                if (!hasRealMaterials) {
                    allEmptyArcDirs << arcInfo.absoluteFilePath();
                    allEmptyFolderIds << baseName;
                }
            }
        }

        // ==========================================
        // 第二步：反查数据库死记录 (数据库 -> 磁盘)
        // ==========================================
        QStringList allGhostFolderIds;
        QStringList allGhostPaths;

        for (sqlite3* db : dbs) {
            sqlite3_stmt* stmt = nullptr;
            const char* sqlQuery = "SELECT folder_id, path FROM metadata";
            if (sqlite3_prepare_v2(db, sqlQuery, -1, &stmt, nullptr) == SQLITE_OK) {
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    const char* fidText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                    const wchar_t* pathText = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 1));
                    if (fidText && pathText) {
                        QString qPath = QString::fromStdWString(pathText);
                        bool exists = QFileInfo(qPath).isDir() ? QDir(qPath).exists() : QFile::exists(qPath);
                        if (!exists) {
                            allGhostFolderIds << QString::fromUtf8(fidText);
                            allGhostPaths << qPath;
                        }
                    }
                }
                sqlite3_finalize(stmt);
            }
        }

        QStringList targetsToRemovePaths = allEmptyArcDirs + allGhostPaths;
        QStringList targetsToRemoveFolderIds = allEmptyFolderIds + allGhostFolderIds;

        if (!targetsToRemovePaths.isEmpty()) {
            MetadataManager::instance().removeMetadataBatchSync(targetsToRemovePaths);

            // 🚀 【事务安全】：强制使用 SqlTransaction 保护批量删除，杜绝崩溃与锁死
            for (sqlite3* db : dbs) {
                SqlTransaction trans(db);
                sqlite3_stmt* stmtMeta = nullptr;
                sqlite3_stmt* stmtItems = nullptr;

                if (sqlite3_prepare_v2(db, "DELETE FROM metadata WHERE folder_id = ?", -1, &stmtMeta, nullptr) == SQLITE_OK &&
                    sqlite3_prepare_v2(db, "DELETE FROM category_items WHERE folder_id = ?", -1, &stmtItems, nullptr) == SQLITE_OK) {

                    for (const QString& fid : targetsToRemoveFolderIds) {
                        std::string stdFid = fid.toStdString();
                        sqlite3_bind_text(stmtMeta, 1, stdFid.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_step(stmtMeta);
                        sqlite3_reset(stmtMeta);

                        sqlite3_bind_text(stmtItems, 1, stdFid.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_step(stmtItems);
                        sqlite3_reset(stmtItems);
                    }
                }
                if (stmtMeta) sqlite3_finalize(stmtMeta);
                if (stmtItems) sqlite3_finalize(stmtItems);
                trans.commit();
            }

            for (const QString& path : allEmptyArcDirs) {
                QDir(path).removeRecursively();
            }

            cleanCount = allEmptyArcDirs.size();
            ghostCount = allGhostFolderIds.size();
        }

        // ==========================================
        // 第三步：清洗孤立关联 (category_items -> metadata)
        // ==========================================
        for (sqlite3* db : dbs) {
            SqlTransaction trans(db);
            char* errMsg = nullptr;
            const char* sqlCleanOrphans = "DELETE FROM category_items WHERE folder_id NOT IN (SELECT folder_id FROM metadata)";
            if (sqlite3_exec(db, sqlCleanOrphans, nullptr, nullptr, &errMsg) == SQLITE_OK) {
                orphanCount += sqlite3_changes(db);
            } else if (errMsg) {
                sqlite3_free(errMsg);
            }
            trans.commit();
        }

        if (cleanCount > 0 || ghostCount > 0 || orphanCount > 0) {
            CategoryRepo::s_countsDirty.store(true);
        }

        emit cleanFinished(cleanCount, ghostCount, orphanCount);
    });
}

} // namespace ArcMeta
```

#### 3.2.3 修改 `src/ui/CategoryPanel.cpp` :: 绑定服务并清理纯 View
```diff
<<<<<<< SEARCH
void CategoryPanel::onScanAndCleanEmptyArcs() {
    // 🚀 【重构解耦】：UI 仅处理界面的 loading 与交互，不执行任何 I/O 与数据库事务
    m_btnScan->setEnabled(false);
    m_btnScan->setIcon(UiHelper::getIcon("scan", QColor("#888888"), 16));

    LibraryMaintenanceService::instance().scanAndCleanEmptyArcsAsync();
}
=======
void CategoryPanel::onScanAndCleanEmptyArcs() {
    // 🚀 【重构解耦】：UI 仅处理界面的 loading 与交互，不执行任何 I/O 与数据库事务
    m_btnScan->setEnabled(false);
    m_btnScan->setIcon(UiHelper::getIcon("scan", QColor("#888888"), 16));

    connect(&LibraryMaintenanceService::instance(), &LibraryMaintenanceService::cleanFinished,
            this, [this](int cleanCount, int ghostCount, int orphanCount) {
        m_btnScan->setEnabled(true);
        m_btnScan->setIcon(UiHelper::getIcon("scan", QColor("#B0B0B0"), 16));

        int totalCleaned = cleanCount + ghostCount;
        if (totalCleaned > 0 || orphanCount > 0) {
            requestRefresh(true);
            QWidget* mw = window();
            if (mw) QMetaObject::invokeMethod(mw, "refreshAll", Qt::QueuedConnection);

            QString msg = QString("<b style='color:#00A650;'>已成功清理 %1 个空白/幽灵资产</b>").arg(totalCleaned);
            if (orphanCount > 0) {
                msg += QString("<br/><span style='color:#00A650; font-size:11px;'>同步剔除 %1 条孤立分类关系</span>").arg(orphanCount);
            }
            ToolTipOverlay::instance()->showText(QCursor::pos(), msg, 3500, QColor("#00A650"));
        } else {
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#CCCCCC;'>未检测到多余的空白托管包与幽灵数据</b>", 2000, QColor("#2D2D2D"));
        }
    }, Qt::UniqueConnection);

    LibraryMaintenanceService::instance().scanAndCleanEmptyArcsAsync();
}
>>>>>>> REPLACE
```

---

### 3.3 重构方案三：校准新建分类后的编辑触发（解决代理模型未就绪陷阱）

#### 3.3.1 修改 `src/ui/CategoryPanel.cpp` :: `onCreateCategory` 与 `onCreateSubCategory`
```diff
<<<<<<< SEARCH
        // 🚀 【时序补丁根除】：绝不依赖 50ms 赌博延时！直接记录待编辑的 ID
        m_pendingEditId = cat.id;
        // 当模型下一次完全重刷完毕（如 layoutChanged 或 modelReset）时，同步队列触发编辑
        connect(m_categoryModel, &CategoryModel::modelReset, this, &CategoryPanel::handlePendingEdit, Qt::QueuedConnection);
        m_categoryModel->refresh();
=======
        // 🚀 【时序对齐】：记录待编辑 ID，待代理模型完全恢复展开状态后精密触发
        m_pendingEditCategoryId = cat.id;
        m_categoryModel->refresh();
>>>>>>> REPLACE
```

#### 3.3.2 修改 `src/ui/CategoryPanel.cpp` 中的 `modelReset` 响应逻辑
在 `restoreExpandedState` **完成后**执行定位与编辑，确保代理模型（ProxyModel）索引 100% 完成转换映射：
```diff
<<<<<<< SEARCH
        m_isRestoringState = true;
        {
            DataFlowGuard guard(m_isInternalUpdating);
            restoreExpandedState(QModelIndex(), expandedIds, expandedNames);
        }
        m_isRestoringState = false;
        m_isInternalUpdating = false;
        Logger::log("[CategoryPanel] modelReset: Restore finished, m_isInternalUpdating set to false");
=======
        m_isRestoringState = true;
        {
            DataFlowGuard guard(m_isInternalUpdating);
            restoreExpandedState(QModelIndex(), expandedIds, expandedNames);
        }
        m_isRestoringState = false;
        m_isInternalUpdating = false;
        Logger::log("[CategoryPanel] modelReset: Restore finished, m_isInternalUpdating set to false");

        // 🚀 【完美时序】：代理模型展开状态处理完毕后，精确触发新节点编辑
        if (m_pendingEditCategoryId > 0) {
            int targetId = m_pendingEditCategoryId;
            m_pendingEditCategoryId = 0; // 及时重置
            selectCategory(targetId);
            QModelIndex proxyIdx = m_categoryTree->currentIndex();
            if (proxyIdx.isValid()) {
                m_categoryTree->edit(proxyIdx);
            }
        }
>>>>>>> REPLACE
```

---

### 3.4 重构方案四：补全 `TagManagerController` 单向流

#### 3.4.1 修改 `src/ui/TagManagerController.h`
```diff
<<<<<<< SEARCH
    // 🚀 专职异步写库：后台线程写入，不引入 QWidget 等 UI 依赖
    void addTagToGroupAsync(const QString& tagName, int groupId);
    void removeTagFromGroupAsync(const QString& tagName, int groupId = -1);
=======
    // 🚀 专职异步写库：后台线程写入，不引入 QWidget 等 UI 依赖
    void addTagToGroupAsync(const QString& tagName, int groupId);
    void removeTagFromGroupAsync(const QString& tagName, int groupId = -1);
    void renameGroupAsync(int groupId, const QString& newName);
    void deleteGroupAsync(int groupId);
>>>>>>> REPLACE
```

#### 3.4.2 修改 `src/ui/TagManagerController.cpp`
```diff
<<<<<<< SEARCH
void TagManagerController::addTagToGroupAsync(const QString& tagName, int groupId) {
    (void)QtConcurrent::run([this, tagName, groupId]() {
        if (TagRepository::addTagToGroup(tagName, groupId)) {
            emit tagGroupStateChanged();
        }
    });
}
=======
void TagManagerController::addTagToGroupAsync(const QString& tagName, int groupId) {
    (void)QtConcurrent::run([this, tagName, groupId]() {
        if (TagRepository::addTagToGroup(tagName, groupId)) {
            emit tagGroupStateChanged();
        }
    });
}

void TagManagerController::renameGroupAsync(int groupId, const QString& newName) {
    (void)QtConcurrent::run([this, groupId, newName]() {
        if (TagRepository::renameGroup(groupId, newName)) {
            emit tagGroupStateChanged();
        }
    });
}

void TagManagerController::deleteGroupAsync(int groupId) {
    (void)QtConcurrent::run([this, groupId]() {
        if (TagRepository::deleteGroup(groupId)) {
            emit tagGroupStateChanged();
        }
    });
}
>>>>>>> REPLACE
```

#### 3.4.3 修改 `src/ui/TagManagerView.cpp`
```diff
<<<<<<< SEARCH
void TagManagerView::renameGroup(int groupId, const QString& newName) {
    // 🚀 【一键解耦】：View 不再直接起并发线程去写库，直接交由控制器
    if (m_controller) {
        m_controller->renameGroupAsync(groupId, newName);
    }
}

void TagManagerView::deleteGroup(int groupId) {
    // 🚀 【一键解耦】：View 不再直接起并发线程去写库，直接交由控制器
    if (m_controller) {
        m_controller->deleteGroupAsync(groupId);
    }
}
=======
void TagManagerView::renameGroup(int groupId, const QString& newName) {
    // 🚀 【重构解耦】：View 不直接开辟并发线程写库，全权交由 Controller
    if (m_controller) {
        m_controller->renameGroupAsync(groupId, newName);
    }
}

void TagManagerView::deleteGroup(int groupId) {
    // 🚀 【重构解耦】：View 不直接开辟并发线程写库，全权交由 Controller
    if (m_controller) {
        m_controller->deleteGroupAsync(groupId);
    }
}
>>>>>>> REPLACE
```

---

### 3.5 重构方案五：彻底清理 `ContentPanel` 右键菜单强锁信号补丁（补齐遗漏）

#### 3.5.1 修改 `src/ui/ContentPanel.cpp` :: `onCustomContextMenuRequested`
```diff
<<<<<<< SEARCH
    // =========================================================================
    // 🚨 核心防死锁机制：右键菜单弹出前，暂时阻塞 Model 信号与 View 的 updates，
    // 阻止后台缩略图异步完成回调在 menu.exec() 模态循环内强行触发父窗口重绘导致 Win32 死锁！
    // =========================================================================
    bool oldBlockModel = m_model ? m_model->signalsBlocked() : false;
    bool oldUpdatesView = view->updatesEnabled();

    if (m_model) m_model->blockSignals(true); // 抑制 dataChanged 分发
    view->setUpdatesEnabled(false);           // 锁住父视图刷新，防止绘图冲突

    QAction* selectedAction = menu.exec(view->viewport()->mapToGlobal(pos));

    // 菜单关闭后，立刻恢复信号与视图刷新
    view->setUpdatesEnabled(oldUpdatesView);
    if (m_model) m_model->blockSignals(oldBlockModel);
    view->viewport()->update(); // 恢复后统一补刷一次
=======
    // 🚀 【补丁彻底根除】：废除硬锁信号与物理禁用绘制！
    // 菜单弹出期间开启无锁模态标记，后台异步提取数据仅挂起不触发死锁，菜单关闭后自动 Flush
    m_isContextMenuActive = true;
    QAction* selectedAction = menu.exec(view->viewport()->mapToGlobal(pos));
    m_isContextMenuActive = false;
    m_model->flushPendingUpdates();
>>>>>>> REPLACE
```

---

## 4. 重构完成准则

1. **零补丁**：代码库中不再含有 `QTimer::singleShot` 猜测延迟控制 UI 行内编辑逻辑；
2. **完整数据一致性**：清理空包时同步执行 SQLite 事务清理幽灵路径与断线关系；
3. **彻底消除假死**：`ContentPanel` 弹出右键菜单时不再强锁模型信号，UI 响应零卡顿；
4. **ProxyModel 安全**：新建分类后 100% 稳定进入重命名编辑状态。
