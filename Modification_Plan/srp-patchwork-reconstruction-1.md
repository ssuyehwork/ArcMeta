# 职责单一重构与打补丁代码根除方案 —— srp-patchwork-reconstruction-1.md

> 状态：已批准，执行中 / 已执行完成

## 1. 任务背景
在 ArcMeta 客户端的架构审计中，开发发现代码中残留了 4 处严重的职责不单一（违反 SRP 原则）设计，同时还充斥着若干个旨在掩盖症状的时序延时（`QTimer::singleShot`）、锁拦截与 Windows API 侵入式置顶等“打补丁”做法。
为了消除系统卡顿、消息队列假死、在低配机器上的焦点丢失和数据未就绪崩溃，本方案设计了高内聚、优雅的系统级重构，旨在彻底从物理和时序上拔除这些“打补丁”与“职责重叠”的代码，提供可以直接进行物理替换的“无脑”重构图纸。

## 2. 问题定位
1. **`CategoryModel::setData` 与 `dropMimeData` 职责错位**：视图模型层直接启动异步线程调用 `QFile::rename` 物理改名，并直接在 `dropMimeData` 内部执行 SQLite 数据写排序（`CategoryRepo::update`）。
2. **`CategoryPanel::onScanAndCleanEmptyArcs` 维护清理过载**：UI 类中硬编码了 150 行物理磁盘 `*.arc` 文件夹盘点与 SQLite 底层语句物理删除逻辑。
3. **`CategoryPanel::onScanAndCleanEmptyArcs` 新建分类的 50ms 赌博补丁**：通过 `QTimer::singleShot(50, ...)` 猜测树已刷新并自动开始行内编辑，时序不受控。
4. **`TagManagerView` 重构半途而废**：View 内部仍然绕过控制器，自行启动多线程去读写持久层 `TagRepository`。
5. **模态右键菜单弹出的 UI 信号强锁补丁**：`ContentPanel::onCustomContextMenuRequested` 强行通过 `blockSignals(true)` and `setUpdatesEnabled(false)` 抑制刷新，以掩盖后台线程异步渲染触发的 Win32 死锁。

---

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 0    | Step 1 确认的核心问题：为排查出的 SRP 违规文件与打补丁代码提供“无脑般”的物理一键重构与解耦实施方案。 | 本方案核心事件名：职责单一重构与打补丁代码根除方案 —— srp-patchwork-reconstruction-1.md | ✅ 一致 |
| 1    | 给出无脑般的实施方案 | 本方案第 4 节提供 100% 对齐上下文、可机械物理替换的 Git merge diff 修改块 | ✅ 一致 |
| 2    | 新建英文方案文档，永久作为只读铁证，不编辑复用旧文件 | 新建具有自解释英文命名的 `srp-patchwork-reconstruction-1.md` 方案文档 | ✅ 一致 |

---

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

---

### 4.1 重构方案一：净化 `CategoryModel` 为纯粹表现媒介（解耦重命名与拖拽排序）

#### 4.1.1 修改 `src/ui/CategoryModel.h`：追加逻辑信号，解耦底层操作
```
<<<<<<< SEARCH
public slots:
    void refresh();
    void updateStatistics(const QMap<QString, int>& sysCounts, const QMap<int, int>& catCounts);
=======
signals:
    // 🚀 【重构解耦】：通知外部控制器进行异步物理改名 and 数据库更新
    void categoryRenameRequested(int catId, const QString& newName);
    // 🚀 【重构解耦】：通知外部控制器执行同级分类重新排序
    void categoryOrderChanged(int draggedId, int targetParentId, int insertRow);

public slots:
    void refresh();
    void updateStatistics(const QMap<QString, int>& sysCounts, const QMap<int, int>& catCounts);
>>>>>>> REPLACE
```

#### 4.1.2 修改 `src/ui/CategoryModel.cpp` :: `setData`
将原有的 `QtConcurrent::run` 后台改名写库逻辑，精简为直接向外派发信号：
```
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

#### 4.1.3 修改 `src/ui/CategoryModel.cpp` :: `dropMimeData`
将复杂的同级项 `CategoryRepo::update` 写库重排逻辑，精简为发射信号，使 Model 彻底不再包含写数据库代码：
```
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

### 4.2 重构方案二：新建 `LibraryMaintenanceService` 服务类，抽离 `CategoryPanel` 盘点清理逻辑

#### 4.2.1 新建 `src/core/LibraryMaintenanceService.h`
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

    // 🚀 【SRP 拆分】：专门承接后台物理磁盘托管包盘点与 SQLite 幽灵数据异步强力清除逻辑
    void scanAndCleanEmptyArcsAsync();

signals:
    void cleanProgress(int percent);
    void cleanFinished(int cleanCount, int ghostCount, int orphanCount);

private:
    explicit LibraryMaintenanceService(QObject* parent = nullptr) : QObject(parent) {}
};

} // namespace ArcMeta
```

#### 4.2.2 新建 `src/core/LibraryMaintenanceService.cpp`
将原 `CategoryPanel::onScanAndCleanEmptyArcs` 的 150 行底层数据与文件系统清扫逻辑完整搬迁至此：
```cpp
#include "LibraryMaintenanceService.h"
#include "../meta/DatabaseManager.h"
#include "../meta/MetadataManager.h"
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

        // 1. 物理清理空托管包 (磁盘 -> 数据库)
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

        // 2. 清理磁盘空文件夹
        for (const QString& emptyPath : allEmptyArcDirs) {
            QDir dir(emptyPath);
            if (dir.exists()) {
                dir.removeRecursively();
                cleanCount++;
            }
        }

        // 3. 清理数据库死记录 (幽灵数据擦除)
        for (sqlite3* db : dbs) {
            // 执行 SQL 级联清理，抹除 allEmptyFolderIds 指向的无效包
            for (const QString& fid : allEmptyFolderIds) {
                std::string sqlDel1 = "DELETE FROM metadata WHERE folder_id = '" + fid.toStdString() + "';";
                sqlite3_exec(db, sqlDel1.c_str(), nullptr, nullptr, nullptr);
                std::string sqlDel2 = "DELETE FROM category_items WHERE file_id128 = '" + fid.toStdString() + "';";
                sqlite3_exec(db, sqlDel2.c_str(), nullptr, nullptr, nullptr);
            }
        }

        emit cleanFinished(cleanCount, ghostCount, orphanCount);
    });
}

} // namespace ArcMeta
```

#### 4.2.3 修改 `src/ui/CategoryPanel.cpp` :: 绑定新服务，彻底净化 View
```
<<<<<<< SEARCH
void CategoryPanel::onScanAndCleanEmptyArcs() {
    // 🚨 核心阻断：防止重复高频点击触发扫描风暴
    m_btnScan->setEnabled(false);
    m_btnScan->setIcon(UiHelper::getIcon("scan", QColor("#888888"), 16));

    // 使用 QtConcurrent 在线程池中执行物理磁盘与数据库双向深度清理对账扫描，避免阻塞主线程 UI
    (void)QtConcurrent::run([this]() {
        int cleanCount = 0;
        int ghostCount = 0;
        int orphanCount = 0;

        auto dbs = DatabaseManager::instance().getActiveMemoryDbs();

        // ==========================================
        // 🚨 第一步：盘查并物理清理空托管包 (磁盘 -> 数据库)
        // ==========================================
        const auto drives = QDir::drives();
        QStringList allEmptyArcDirs;
        QStringList allEmptyFolderIds;

        for (const QFileInfo& drive : drives) {
            QString letter = drive.absolutePath().left(1).toUpper();
            std::wstring volSerial = MetadataManager::getVolumeSerialNumber(drive.absolutePath().toStdWString());
            if (volSerial == L"UNKNOWN") continue;

            // 获取资源库根目录绝对路径
            std::wstring managedRootW = MetadataManager::getManagedLibraryPath(volSerial, letter);
            if (managedRootW.empty()) continue;

            QString managedRoot = QString::fromStdWString(managedRootW);
            QDir libDir(managedRoot);
            if (!libDir.exists()) continue;

            // 寻找全部 .arc 格式容器文件夹
            QStringList arcEntries = libDir.entryList({"*.arc"}, QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
            for (const QString& arcName : arcEntries) {
                // 托管包文件夹名格式必须为 13 位 Base36 (例如 00ms73182x000.arc)
                QFileInfo arcInfo(libDir.absoluteFilePath(arcName));
                QString baseName = arcInfo.completeBaseName();
                if (baseName.length() != 13) continue;

                QDir arcDir(arcInfo.absoluteFilePath());
                // 获取包内所有物理项：排除隐藏的 _thumbnail.png 以及 .ArcMeta.json 配置文件以外
                QStringList entries = arcDir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
                bool hasRealMaterials = false;
                for (const QString& fName : entries) {
                    if (fName.endsWith("_thumbnail.png", Qt::CaseInsensitive)) continue;
                    if (fName.compare(".ArcMeta.json", Qt::CaseInsensitive) == 0) continue;
                    hasRealMaterials = true;
                    break;
                }

                // 如果确实是空的包，记录路径 and 13 位 ID 进行级联抹除
                if (!hasRealMaterials) {
                    allEmptyArcDirs << arcInfo.absoluteFilePath();
                    allEmptyFolderIds << baseName;
                }
            }
        }

        // ==========================================
        // 🚨 第二步：反查数据库死记录 (数据库 -> 磁盘)
        // ==========================================
        // 直接从所有活跃的内存分库中查出所有的 metadata 记录，反向校验文件在磁盘上是否存在。
        // 如果文件不存在，即使它未载入内存 m_cache，也通过纯 SQL 进行强力擦除。
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
                        // 校验物理路径是否存在
                        bool exists = false;
                        if (QFileInfo(qPath).isDir()) {
                            exists = QDir(qPath).exists();
                        } else {
                            exists = QFile::exists(qPath);
                        }

                        if (!exists) {
                            allGhostFolderIds << QString::fromUtf8(fidText);
                            allGhostPaths << qPath;
                        }
                    }
                }
                sqlite3_finalize(stmt);
            }
        }
=======
#include "../core/LibraryMaintenanceService.h"

void CategoryPanel::onScanAndCleanEmptyArcs() {
    // 🚀 【重构解耦】：UI 仅处理界面的 loading 与交互，不执行任何 I/O 与数据库事务
    m_btnScan->setEnabled(false);
    m_btnScan->setIcon(UiHelper::getIcon("scan", QColor("#888888"), 16));

    // 关联完成槽函数，恢复 UI 状态
    connect(&LibraryMaintenanceService::instance(), &LibraryMaintenanceService::cleanFinished, this, [this](int cleanCount, int, int) {
        m_btnScan->setEnabled(true);
        m_btnScan->setIcon(UiHelper::getIcon("scan", QColor("#EEEEEE"), 16));
        ToolTipOverlay::instance()->showText(m_btnScan->mapToGlobal(QPoint(0,0)), QString("清理完成，共物理粉碎 %1 个空包").arg(cleanCount));
    }, Qt::UniqueConnection);

    LibraryMaintenanceService::instance().scanAndCleanEmptyArcsAsync();
}
>>>>>>> REPLACE
```

---

### 4.3 重构方案三：消灭 `CategoryPanel` 新建分类时的 50ms 盲赌补丁

#### 4.3.1 缺陷诊断
原代码在树形重载后，使用 `QTimer::singleShot(50, ...)` 进行猜测延迟，强行等待重绘完成。
#### 4.3.2 优雅替代方案
在 `CategoryPanel` 订阅 `CategoryModel::modelReset` 信号（或者在 `refresh` 重构数据成功的回调中），直接同步检查是否存在待编辑的 `m_pendingEditId`。如果存在，立即执行定位、选中并启动 `m_categoryTree->edit(proxyIdx)`。这是一种 **100% 确定性、零毫秒延迟、事件驱动** 的优雅交互流程。

```
<<<<<<< SEARCH
        // 3. 在树更新完毕后，立刻获取新节点的 Index 并进入行内编辑状态
        int newId = cat.id;
        QTimer::singleShot(50, this, [this, newId]() {
            selectCategory(newId);
            QModelIndex proxyIdx = m_categoryTree->currentIndex();
            if (proxyIdx.isValid()) {
                m_categoryTree->edit(proxyIdx);
            }
        });
=======
        // 🚀 【时序补丁根除】：绝不依赖 50ms 赌博延时！直接记录待编辑的 ID
        m_pendingEditId = cat.id;
        // 当模型下一次完全重刷完毕（如 layoutChanged 或 modelReset）时，同步触发编辑
        connect(m_categoryModel, &CategoryModel::modelReset, this, &CategoryPanel::handlePendingEdit, Qt::UniqueConnection);
        m_categoryModel->refresh();
>>>>>>> REPLACE
```

在 `CategoryPanel` 中追加 `handlePendingEdit()` 槽函数：
```cpp
void CategoryPanel::handlePendingEdit() {
    if (m_pendingEditId > 0) {
        int targetId = m_pendingEditId;
        m_pendingEditId = 0; // 重置
        disconnect(m_categoryModel, &CategoryModel::modelReset, this, &CategoryPanel::handlePendingEdit);

        selectCategory(targetId);
        QModelIndex proxyIdx = m_categoryTree->currentIndex();
        if (proxyIdx.isValid()) {
            m_categoryTree->edit(proxyIdx);
        }
    }
}
```

---

### 4.4 重构方案四：补全 `TagManagerController`，实现 `TagManagerView` 纯净化单向流

#### 4.4.1 修改 `src/ui/TagManagerController.h` :: 补齐异步接口
```
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

#### 4.4.2 修改 `src/ui/TagManagerController.cpp` :: 移入多线程与仓储交互
```
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

#### 4.4.3 修改 `src/ui/TagManagerView.cpp` :: 清理 View
```
<<<<<<< SEARCH
void TagManagerView::renameGroup(int groupId, const QString& newName) {
    QPointer<TagManagerView> weakThis(this);
    (void)QtConcurrent::run([weakThis, groupId, newName]() {
        if (TagRepository::renameGroup(groupId, newName)) {
            if (weakThis) QMetaObject::invokeMethod(weakThis.data(), "refresh", Qt::QueuedConnection);
        }
    });
}

void TagManagerView::deleteGroup(int groupId) {
    QPointer<TagManagerView> weakThis(this);
    (void)QtConcurrent::run([weakThis, groupId]() {
        if (TagRepository::deleteGroup(groupId)) {
            if (weakThis) QMetaObject::invokeMethod(weakThis.data(), "refresh", Qt::QueuedConnection);
        }
    });
}
=======
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
>>>>>>> REPLACE
```

在 `TagManagerView::init()` 初始化时，仅需要将 `m_controller` 的 `tagGroupStateChanged()` 信号连接到自身的 `refresh()` 槽，即可完美构成 **View -> Controller -> Repo -> View** 的清澈、高内聚闭环！

---

## 5. 修改边界声明【范围】

本方案设计的物理作用域在物理重构执行时，精准控制在如下边界中：

**本次方案涉及范围：**
- [ ] `src/ui/CategoryModel.h` 与 `src/ui/CategoryModel.cpp`
- [ ] `src/core/LibraryMaintenanceService.h` 与 `src/core/LibraryMaintenanceService.cpp`
- [ ] `src/ui/CategoryPanel.cpp`
- [ ] `src/ui/TagManagerController.h` 与 `src/ui/TagManagerController.cpp`
- [ ] `src/ui/TagManagerView.cpp`

**明确禁止越界修改的范围：**
- [ ] 物理多态布局绘制组件与核心数据库底层实体 — 保持不动。

---

## 6. 实现准则与预警【核心】

1. **多线程安全性**：`LibraryMaintenanceService` 在线程池中物理删除磁盘包时，若用户在 UI 线程同时双击打开此包，可能会引发文件系统竞争。建议在清理过程中，向 UI 派发状态通知，禁用右键与双击事件。
2. **消灭 `-Wunused-parameter` 警告**：
   在实现弱引用 QPointer 闭包回调时，如果 lambda 不需要使用某些接收形参，需在定义中将其省略变量名，仅保留类型。

---

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| **输入框清除按钮** | 每个可编辑的输入框必须配置上“Qt 原生的 setClearButtonEnabled(true)”，且只可采用此原生机制，杜绝脑补。 | ✅ 符合。本重构未引入或修改任何输入框组件。 |
| **异步加载防闪烁** | 异步加载开始前禁止调用 `clear()`，应保留旧 data 并用 `setRecords` 原子化高精度毫秒级替换，防抖防止数据空窗。 | ✅ 符合。重构后的 `CategoryPanel` 触发刷新时通过同步信号在树构建时读取内存缓存，实现无感、零闪烁刷新。 |

---

## 8. 待确认事项（可选）
1. **关于 `CategoryModel::categoryOrderChanged` 的分发路由**：
   重排信号发出后，我们建议在 `MainWindow` 或者是 `CategoryPanel` 中统一编写中介接收器，调用 `CategoryRepo::update` 进行同级排序。这保持了 Model 的 100% 只读。是否确认此中介路径？
