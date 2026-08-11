# ContentPanel 单一职责重构 —— contentpanel-srp-refactor.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在当前架构中，`ContentPanel` 承担了部分过重的物理 I/O 操作、原生数据库查询、文件加解密处理及状态快照拼装。为了彻底解决界面卡顿和死锁隐患，使 UI 渲染层专注于视觉绘制，需要将 `ContentPanel` 彻底重构为一个只负责界面布局、视觉渲染与原生交互捕捉并分发的纯粹“传声筒和画板”。

## 2. 问题定位
- **模块/行号**：`src/ui/ContentPanel.h` / `src/ui/ContentPanel.cpp`
- **问题分析**：
  1. `createNewItem` 与 `performPaste` 内部直接调用物理文件操作（`QDir::mkdir`、`QFile` 操作）以及部分本地同步逻辑，导致物理文件读写与抹除职责混杂在 UI 类中（对应用户原话：“1. 物理文件读写与抹除 (创建文件夹、粉碎文件)”）。
  2. 回收站还原等逻辑中含有底层直接针对 SQLite 回收站记录的操作，视图代码直接引入了数据库对账与恢复行为（对应用户原话：“2. 原生 SQL 查询与回收站恢复”）。
  3. `ActionEncrypt` 与 `ActionDecrypt` 中含有同步对文件加解密及加密属性落盘的逻辑，导致加解密与 UI 线程产生潜在同步卡顿风险（对应用户原话：“3. 文件加密与解密 (输入密码、后台加密)”）。
  4. 存在部分历史冗余的递归文件扫描与缓存管理，需要统一为后台服务分发（对应用户原话：“4. 磁盘扫描与元数据加载”）。
  5. 右键菜单及写操作的撤销重做快照拼装混杂，需要移入统一快照引擎（对应用户原话：“5. 撤销/重做快照”）。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 0    | Step 1 中确认的"核心问题"：ContentPanel 单一职责重构 | 本方案核心事件名：ContentPanel 单一职责重构 | ✅ 一致 |
| 1    | 界面组件持有与布局管理（UI Layout）（对应用户原话：“界面组件持有与布局管理（UI Layout）”） | `ContentPanel` 保留管理容器内子控件、响应窗口拉伸等 UI 布局本职 | ✅ 一致 |
| 2    | 视图渲染与视觉状态展示（View & Visual State）（对应用户原话：“视图渲染与视觉状态展示（View & Visual State）”） | `ContentPanel` 保留交给 Model/Delegate 绘制、维护网格缩放比例等视觉本职 | ✅ 一致 |
| 3    | 用户原生交互捕捉与信号分发（User Input & Signal Forwarding）（对应用户原话：“用户原生交互捕捉与信号分发（User Input & Signal Forwarding）”） | `ContentPanel` 仅监听原生键鼠动作并将其 100% 抽象分发为信号，绝不亲自执行任何实际业务 | ✅ 一致 |
| 4    | 物理文件读写与抹除 (创建文件夹、粉碎文件)（对应用户原话：“1. 物理文件读写与抹除 (创建文件夹、粉碎文件)”） | 所有彻底物理删除、新文件夹创建及文件粘贴异步外包至 `DiskIoService` 的异步线程池（`QtConcurrent::run`） | ✅ 一致 |
| 5    | 原生 SQL 查询与回收站恢复（对应用户原话：“2. 原生 SQL 查询与回收站恢复”） | 彻底移除 `ContentPanel` 里的 `sqlite3.h` 头文件及任何原生 SQL 语句绑定，回收站还原业务逻辑移交至 `DiskTrashService` 的异步 API | ✅ 一致 |
| 6    | 文件加密与解密 (输入密码、后台加密)（对应用户原话：“3. 文件加密与解密 (输入密码、后台加密)”） | 加密与解密操作异步线程外包，由专门的控制类 `CryptoController` 进行高内聚代管 | ✅ 一致 |
| 7    | 磁盘扫描与元数据加载（对应用户原话：“4. 磁盘扫描与元数据加载”） | 剥离 `addItemsFromDirectory` 递归，由专门的扫描对账服务和 Model 模型层进行双轨隔离加载 | ✅ 一致 |
| 8    | 撤销/重做快照（对应用户原话：“5. 撤销/重做快照”） | 状态快照与 Undo 命令生成完全剥离，统一交由 `OperationSnapshotEngine` 高内聚解耦 | ✅ 一致 |

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 扩展底层 `DiskIoService`
在 `src/util/DiskIoService.h` 中，补充异步粘贴/移动、创建新文件夹或文档的后台代管接口：

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
     * @brief 异步执行物理创建新条目操作，支持 QPointer 弱引用保护生命周期
     */
    template<typename T>
    static void asyncCreateNewItem(
        const QString& type,
        const QString& currentPath,
        QPointer<T> context,
        std::function<void(const QString&)> completionCallback)
    {
        (void)QtConcurrent::run([type, currentPath, context, completionCallback]() {
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

            QString fullPath = QDir(currentPath).filePath(finalName + ext);
            int counter = 1;
            while (QFileInfo::exists(fullPath)) {
                fullPath = QDir(currentPath).filePath(finalName + QString(" (%1)").arg(counter++) + ext);
            }

            bool success = false;
            if (type == "folder") {
                success = QDir(currentPath).mkdir(QFileInfo(fullPath).fileName());
            } else {
                QFile file(fullPath);
                if (file.open(QIODevice::WriteOnly)) {
                    file.write("");
                    file.close();
                    success = true;
                }
            }

            if (success && completionCallback) {
                QMetaObject::invokeMethod(QCoreApplication::instance(), [context, completionCallback, fullPath]() {
                    if (context) {
                        completionCallback(fullPath);
                    }
                });
            }
        });
    }

    /**
     * @brief 异步执行物理粘贴操作
     */
    template<typename T>
    static void asyncPasteItems(
        const QStringList& paths,
        const QString& destDir,
        bool cutMode,
        QPointer<T> context,
        std::function<void(bool)> completionCallback)
    {
        (void)QtConcurrent::run([paths, destDir, cutMode, context, completionCallback]() {
            bool allOk = true;
            for (const QString& src : paths) {
                QFileInfo fi(src);
                QString dest = QDir(destDir).filePath(fi.fileName());
                if (QFileInfo::exists(dest)) {
                    dest = QDir(destDir).filePath(fi.baseName() + "_副本." + fi.completeSuffix());
                }

                bool ok = false;
                if (cutMode) {
                    ok = QFile::rename(src, dest);
                } else {
                    ok = QFile::copy(src, dest);
                }
                if (!ok) allOk = false;
            }

            if (completionCallback) {
                QMetaObject::invokeMethod(QCoreApplication::instance(), [context, completionCallback, allOk]() {
                    if (context) {
                        completionCallback(allOk);
                    }
                });
            }
        });
    }

    /**
     * @brief 异步执行物理删除或安全抹除操作，支持 QPointer 弱引用保护生命周期
>>>>>>> REPLACE
```

### 4.2 新增 `CryptoController` 加解密控制器
在 `src/core/` 目录下，新建 `CryptoController.h` 负责无感加解密：

```cpp
#pragma once
#include <QStringList>
#include <QPointer>
#include <QtConcurrent>
#include <QCoreApplication>
#include <QFile>
#include <string>
#include <functional>
#include "../crypto/EncryptionManager.h"
#include "../meta/MetadataManager.h"

namespace ArcMeta {

/**
 * @brief CryptoController: 异步管理文件物理加解密的解耦控制器
 */
class CryptoController {
public:
    static CryptoController& instance() {
        static CryptoController inst;
        return inst;
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
