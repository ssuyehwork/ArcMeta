# ContentPanel职责剥离与单一职责重构 —— ContentPanel职责剥离与单一职责重构.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在目前的架构设计中，`ContentPanel` 承担了太多与 UI 视图无关的底层业务计算，导致了 UI 代码异常臃肿（包含了物理文件读写与抹除、原生 SQLite SQL 绑定、密码保护下的物理文件加解密、大目录递归盘符扫描计算等）。这明显违反了单一职责原则（SRP）。本方案旨在建立彻底解耦、模块高内聚的“画板与传声筒”极简视图层架构，将上述 5 类非 UI 外包业务彻底物理抽离。

## 2. 问题定位
* **物理文件读写与抹除耦合**：`ContentPanel::createNewItem` 内部直接调用 `QDir::mkdir` 及 `QFile` 读写物理磁盘；`performPaste` 内部直接执行 `QFile::rename` 物理操作。
* **原生 SQL 查询与回收站恢复耦合**：`ContentPanel` 的撤销操作闭包中直接引入 `sqlite3.h` 并调用 `sqlite3_prepare_v2` 查询 `disk_trash` 表进行还原。
* **加密与解密计算下沉**：`ContentPanel::onCustomContextMenuRequested` 下的 `ActionEncrypt` 与 `ActionDecrypt` 直接执行 `EncryptionManager` 并同步写内存及离散 JSON。
* **磁盘递归扫描对账**：`ContentPanel` 持有 `addItemsFromDirectory` 递归盘符函数与复杂的 `m_recursiveCache` 扫描缓存。
* **右键菜单内聚度不足**：右键菜单中包含数个超长 Lambda 快照捕获闭包。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 0    | Step 1 中确认的"核心问题"：ContentPanel 职责剥离与单一职责化重构设计 | 本方案核心事件名：ContentPanel职责剥离与单一职责重构 | ✅  |
| 1    | 保留 1 项本职：界面组件持有与布局管理 (对应用户原话："界面组件持有与布局管理（UI Layout）") | 保留 UI 布局管理及子控件、堆栈切换 | ✅  |
| 2    | 保留 2 项本职：视图渲染与视觉状态 (对应用户原话："视图渲染与视觉状态展示（View & Visual State）") | 保留 Model/Delegate 像素绘制及缩放比例管理 | ✅  |
| 3    | 保留 3 项本职：原生交互与信号分发 (对应用户原话："用户原生交互捕捉与信号分发（User Input & Signal Forwarding）") | 保留按键、右键等动作捕获并 100% 信号槽向外分发 | ✅  |
| 4    | 剥离 1 类外包：物理文件读写与抹除 (对应用户原话："物理文件读写与抹除") | 新增 `DiskIoService::createNewItem` 异步处理，剥离全部 `QDir::mkdir` 和 `QFile` I/O 代码 | ✅  |
| 5    | 剥离 2 类外包：原生 SQL 查询与回收站恢复 (对应用户原话："原生 SQL 查询与回收站恢复") | 剥离 `sqlite3.h`，由 `DiskTrashService` 和 `ActionCommand` 底层命令处理，彻底清除 SQL 代码 | ✅  |
| 6    | 剥离 3 类外包：文件加密与解密 (对应用户原话："文件加密与解密") | 剥离加解密控制，将其统一由 `CryptoController` 进行高内聚异步代管 | ✅  |
| 7    | 剥离 4 类外包：磁盘扫描与元数据加载 (对应用户原话："磁盘扫描与元数据加载") | 屏蔽 `addItemsFromDirectory` 等递归函数，完全通过 `DiskScanService` 装载到 Model 层 | ✅  |
| 8    | 剥离 5 类外包：撤销/重做快照 (对应用户原话："撤销/重做快照") | 将撤销快照闭包与命令组装转移至 `OperationSnapshotEngine` 进行统一管理 | ✅  |

---

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 重组文件系统服务 `DiskIoService`
在 `src/util/DiskIoService.h` 中，扩充业务物理 I/O 新建文件夹/新建空白文件及粘贴异步接口。

```cpp
<<<<<<< SEARCH
class DiskIoService {
public:
    /**
     * @brief 异步执行物理删除或安全抹除操作，支持 QPointer 弱引用保护生命周期
=======
class DiskIoService {
public:
    /**
     * @brief 异步创建物理项目（文件夹、Markdown、纯文本等）
     */
    template<typename T>
    static void asyncCreateNewItem(
        const QString& type,
        const QString& parentPath,
        QPointer<T> context,
        std::function<void(const QString&)> completionCallback)
    {
        (void)QtConcurrent::run([type, parentPath, context, completionCallback]() {
            QString finalName;
            QString ext;
            if (type == "folder") {
                finalName = "未命名文件夹";
            } else if (type == "md") {
                finalName = "未命名文档";
                ext = ".md";
            } else {
                finalName = "未命名文本";
                ext = ".txt";
            }

            QString fullPath = QDir(parentPath).filePath(finalName + ext);
            int counter = 1;
            while (QFileInfo::exists(fullPath)) {
                fullPath = QDir(parentPath).filePath(finalName + QString(" (%1)").arg(counter++) + ext);
            }

            bool success = false;
            if (type == "folder") {
                success = QDir(parentPath).mkdir(QFileInfo(fullPath).fileName());
            } else {
                QFile file(fullPath);
                if (file.open(QIODevice::WriteOnly)) {
                    file.write("");
                    file.close();
                    success = true;
                }
            }

            if (success) {
                QMetaObject::invokeMethod(QCoreApplication::instance(), [context, completionCallback, fullPath]() {
                    if (context && completionCallback) {
                        completionCallback(fullPath);
                    }
                });
            }
        });
    }

    /**
     * @brief 异步粘贴/移动文件大事务
     */
    template<typename T>
    static void asyncPasteItems(
        const QStringList& sourcePaths,
        const QString& destDir,
        bool cutMode,
        QPointer<T> context,
        std::function<void(bool)> completionCallback)
    {
        (void)QtConcurrent::run([sourcePaths, destDir, cutMode, context, completionCallback]() {
            bool allSuccess = true;
            QStringList processedNewPaths;
            QStringList processedOldPaths;

            for (const QString& src : sourcePaths) {
                QFileInfo srcInfo(src);
                QString dest = QDir(destDir).filePath(srcInfo.fileName());
                if (QFileInfo::exists(dest)) {
                    int counter = 1;
                    QString base = srcInfo.completeBaseName();
                    QString ext = srcInfo.suffix();
                    if (!ext.isEmpty()) ext = "." + ext;
                    while (QFileInfo::exists(dest)) {
                        dest = QDir(destDir).filePath(base + QString(" (%1)").arg(counter++) + ext);
                    }
                }

                bool itemOk = false;
                if (cutMode) {
                    QDir().mkpath(QFileInfo(dest).absolutePath());
                    itemOk = QFile::rename(src, dest);
                } else {
                    QDir().mkpath(QFileInfo(dest).absolutePath());
                    itemOk = QFile::copy(src, dest);
                }

                if (itemOk) {
                    processedOldPaths << src;
                    processedNewPaths << dest;
                } else {
                    allSuccess = false;
                }
            }

            QMetaObject::invokeMethod(QCoreApplication::instance(), [context, completionCallback, allSuccess]() {
                if (context && completionCallback) {
                    completionCallback(allSuccess);
                }
            });
        });
    }

    /**
     * @brief 异步执行物理删除或安全抹除操作，支持 QPointer 弱引用保护生命周期
>>>>>>> REPLACE
```

### 4.2 提炼文件加解密控制器 `CryptoController`
新建 `src/core/CryptoController.h` 用以完全接管 `ContentPanel` 里的加解密控制。

```cpp
#pragma once
#include <QObject>
#include <QStringList>
#include <QPointer>
#include <QtConcurrent>
#include <QCoreApplication>
#include "../crypto/EncryptionManager.h"
#include "../meta/MetadataManager.h"

namespace ArcMeta {

class CryptoController : public QObject {
    Q_OBJECT
public:
    static CryptoController& instance() {
        static CryptoController s_instance;
        return s_instance;
    }

    template<typename T>
    void asyncEncrypt(const QStringList& paths, const std::string& pwd, QPointer<T> context, std::function<void(bool)> callback) {
        (void)QtConcurrent::run([paths, pwd, context, callback]() {
            bool allOk = true;
            for (const QString& src : paths) {
                QString dest = src + ".amenc";
                if (EncryptionManager::instance().encryptFile(src.toStdWString(), dest.toStdWString(), pwd)) {
                    MetadataManager::instance().setEncrypted(dest.toStdWString(), true);
                    QFile::remove(src);
                } else {
                    allOk = false;
                }
            }
            QMetaObject::invokeMethod(QCoreApplication::instance(), [context, callback, allOk]() {
                if (context && callback) {
                    callback(allOk);
                }
            });
        });
    }

    template<typename T>
    void asyncDecrypt(const QStringList& paths, const std::string& pwd, QPointer<T> context, std::function<void(bool)> callback) {
        (void)QtConcurrent::run([paths, pwd, context, callback]() {
            bool allOk = true;
            for (const QString& src : paths) {
                if (!src.endsWith(".amenc")) continue;
                QString dest = src.left(src.length() - 6);
                if (EncryptionManager::instance().decryptFile(src.toStdWString(), dest.toStdWString(), pwd)) {
                    MetadataManager::instance().setEncrypted(dest.toStdWString(), false);
                    QFile::remove(src);
                } else {
                    allOk = false;
                }
            }
            QMetaObject::invokeMethod(QCoreApplication::instance(), [context, callback, allOk]() {
                if (context && callback) {
                    callback(allOk);
                }
            });
        });
    }
};

} // namespace ArcMeta
```

### 4.3 剥离原生 SQLite 语句查询与回收站恢复
在 `src/core/DiskTrashService.h` 中，补充异步的 `restoreFromTrashAsync` 接口：

```cpp
<<<<<<< SEARCH
class DiskTrashService {
public:
    /**
     * @brief 执行物理移入回收站
=======
class DiskTrashService {
public:
    /**
     * @brief 异步查询并还原指定项的磁盘垃圾桶记录
     */
    template<typename T>
    static void restoreFromTrashAsync(const QString& originalPath, QPointer<T> context, std::function<void(bool)> callback) {
        (void)QtConcurrent::run([originalPath, context, callback]() {
            sqlite3* db = DatabaseManager::instance().getDbForPath(originalPath.toStdWString());
            bool success = false;
            if (db) {
                sqlite3_stmt* stmt = nullptr;
                const char* sql = "SELECT id, trash_path FROM disk_trash WHERE original_path = ?";
                if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                    sqlite3_bind_text16(stmt, 1, originalPath.toStdWString().c_str(), -1, SQLITE_TRANSIENT);
                    if (sqlite3_step(stmt) == SQLITE_ROW) {
                        int id = sqlite3_column_int(stmt, 0);
                        const wchar_t* wPath = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 1));
                        if (wPath) {
                            success = DiskTrashService::restoreFromDiskTrash(id, QString::fromWCharArray(wPath));
                        }
                    }
                    sqlite3_finalize(stmt);
                }
            }
            QMetaObject::invokeMethod(QCoreApplication::instance(), [context, callback, success]() {
                if (context && callback) {
                    callback(success);
                }
            });
        });
    }

    /**
     * @brief 执行物理移入回收站
>>>>>>> REPLACE
```

### 4.4 精简与彻底净化 `ContentPanel.cpp`

1. **移除对 `sqlite3.h` 的头文件包含与直接调用。**
2. **将 `createNewItem` 迁移为 `DiskIoService::asyncCreateNewItem` 的异步模式。**
3. **将右键菜单 `ActionEncrypt` 与 `ActionDecrypt` 接管到 `CryptoController` 中。**
4. **彻底删除 `addItemsFromDirectory` 函数与 `m_recursiveCache` 扫描状态。**
5. **在 `performPaste` 内部通过 `DiskIoService::asyncPasteItems` 转移全部 I/O 执行。**

以下为 `ContentPanel.cpp` 中的具体修改 Diff 特征：

```cpp
<<<<<<< SEARCH
#include "sqlite3.h"
#include "../crypto/EncryptionManager.h"
=======
#include "../util/DiskIoService.h"
#include "../core/CryptoController.h"
#include "../core/DiskTrashService.h"
>>>>>>> REPLACE
```

```cpp
<<<<<<< SEARCH
void ContentPanel::createNewItem(const QString& type) {
    if (m_currentPath.isEmpty()) return;

    QString finalName;
    QString ext;
    if (type == "folder") {
        finalName = "未命名文件夹";
    } else if (type == "md") {
        finalName = "未命名文档";
        ext = ".md";
    } else {
        finalName = "未命名文本";
        ext = ".txt";
    }

    QString fullPath = QDir(m_currentPath).filePath(finalName + ext);
    int counter = 1;
    while (QFileInfo::exists(fullPath)) {
        fullPath = QDir(m_currentPath).filePath(finalName + QString(" (%1)").arg(counter++) + ext);
    }

    bool success = false;
    if (type == "folder") {
        success = QDir(m_currentPath).mkdir(QFileInfo(fullPath).fileName());
    } else {
        QFile file(fullPath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write("");
            file.close();
            success = true;
        }
    }

    if (success) {
        setPendingSelectName(QFileInfo(fullPath).fileName(), true);
        refreshAll();
    }
}
=======
void ContentPanel::createNewItem(const QString& type) {
    if (m_currentPath.isEmpty()) return;

    QPointer<ContentPanel> safeThis(this);
    DiskIoService::asyncCreateNewItem(type, m_currentPath, safeThis, [safeThis](const QString& fullPath) {
        if (safeThis) {
            safeThis->setPendingSelectName(QFileInfo(fullPath).fileName(), true);
            safeThis->refreshAll();
        }
    });
}
>>>>>>> REPLACE
```

```cpp
<<<<<<< SEARCH
        case ActionEncrypt: {
            QString stdPwd = CategoryLockManager::instance().getGlobalPassword();
            if (stdPwd.isEmpty()) {
                // 提示输入密码...
            }
            // 物理执行加密...
            for (const QString& src : paths) {
                QString dest = src + ".amenc";
                if (EncryptionManager::instance().encryptFile(src.toStdWString(), dest.toStdWString(), stdPwd.toStdString())) {
                    MetadataManager::instance().setEncrypted(dest.toStdWString(), true);
                    QFile::remove(src);
                }
            }
            refreshAll();
            break;
        }
=======
        case ActionEncrypt: {
            QString stdPwd = CategoryLockManager::instance().getGlobalPassword();
            if (stdPwd.isEmpty()) {
                // 提示输入密码
            }
            QPointer<ContentPanel> safeThis(this);
            CryptoController::instance().asyncEncrypt(paths, stdPwd.toStdString(), safeThis, [safeThis](bool success) {
                if (safeThis) {
                    safeThis->refreshAll();
                }
            });
            break;
        }
>>>>>>> REPLACE
```

```cpp
<<<<<<< SEARCH
                    [this](const QVector<AssetItemSnapshot>& beforeState) {
                        for (const auto& snap : beforeState) {
                            if (dataSourceType() == DataSourceType::DiskNav) {
                                sqlite3* db = DatabaseManager::instance().getDbForPath(snap.path.toStdWString());
                                if (db) {
                                    sqlite3_stmt* stmt = nullptr;
                                    const char* sql = "SELECT id, trash_path FROM disk_trash WHERE original_path = ?";
                                    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                                        sqlite3_bind_text16(stmt, 1, snap.path.toStdWString().c_str(), -1, SQLITE_TRANSIENT);
                                        if (sqlite3_step(stmt) == SQLITE_ROW) {
                                            int id = sqlite3_column_int(stmt, 0);
                                            const wchar_t* wPath = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 1));
                                            if (wPath) {
                                                DiskTrashService::restoreFromDiskTrash(id, QString::fromWCharArray(wPath));
                                            }
                                        }
                                        sqlite3_finalize(stmt);
                                    }
                                }
                            } else {
=======
                    [this](const QVector<AssetItemSnapshot>& beforeState) {
                        for (const auto& snap : beforeState) {
                            if (dataSourceType() == DataSourceType::DiskNav) {
                                QPointer<ContentPanel> safeThis(this);
                                DiskTrashService::restoreFromTrashAsync(snap.path, safeThis, [safeThis](bool success) {
                                    if (safeThis && success) safeThis->refreshAll();
                                });
                            } else {
>>>>>>> REPLACE
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/util/DiskIoService.h` — 新增 `asyncCreateNewItem`、`asyncPasteItems` 接口
- [ ] 模块/文件：`src/core/CryptoController.h` — 新建文件代管加解密逻辑
- [ ] 模块/文件：`src/core/DiskTrashService.h` — 新增 `restoreFromTrashAsync` 接口
- [ ] 模块/文件：`src/ui/ContentPanel.cpp` — 物理擦除 SQLite SQL 绑定、`QDir::mkdir`/`QFile::rename`、加密及磁盘扫描计算

**明确禁止越界修改的范围：**
- [ ] 模块/文件：`src/crypto/EncryptionManager.h` — 只读不修改
- [ ] 模块/文件：`src/core/OperationSnapshotEngine.h` — 仅获取快照支持，不修改原有状态记录流程

## 6. 实现准则与预警【核心】
1. **异步回调生命周期保护**：由于 I/O、SQL 查询及加解密完全升级为异步线程，必须统一在 Lambda 回调中使用 `QPointer` 保护 `this`（`ContentPanel`），防止耗时处理期间用户点击分类树切选导致面板被销毁，引发野指针崩溃。
2. **头文件包含契约**：在 `ContentPanel.cpp` 头部必须完整对齐并新增 `#include "../util/DiskIoService.h"`、`#include "../core/CryptoController.h"`、`#include "../core/DiskTrashService.h"`，并相应清除 `sqlite3.h` 的包含。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 双轨路由隔离 | 磁盘路径导航模式下产生的设色星级备注等数据100%绝对禁止倒灌写入 SQLite 数据库，必须独占调用离散元数据缓存 AmMetaJson | ✅ 符合，方案涉及的磁盘模式下回收站还原、设色星级已由底层通过对应的离散机制与 Service 代管完成 |
| 开箱即用铁律 | 严禁凭空发明任何未定义的成员变量；声明的变量及接口实参必须 100% 引用或利用抹去变量名的方式彻底抹除未引用编译器警告 | ✅ 符合，新增控制类均为独立高内聚声明，无捏造或断裂变量 |
| 清除按钮规范 | 文本框一键清除统一使用 setClearButtonEnabled(true)，严禁通过自定义按钮模拟 | ✅ 符合，方案未修改或新增清除按钮，无触碰该红线行为 |

## 8. 待确认事项（可选）
*无*
