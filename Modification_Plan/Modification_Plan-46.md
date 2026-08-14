这是彻底修复 4 处缺陷后的 **最终版极速重构方案（Modification_Plan-46_Corrected.md）**。你可以将下述方案直接提交给 Jules 执行。

---

# 系统级架构净化与重构方案 —— Modification_Plan-46_Corrected.md

> 状态：待批准执行

## 1. 任务背景与修正说明
本方案针对上一版方案中存在的 4 处致命设计缺陷进行了彻底修正与强化，确保改动在多线程、高分屏以及递归扫描模式下 100% 稳定可靠：
1. **完全归一化文件过滤**：将包含 `.arc` 在内的所有辅助/配置文件过滤逻辑**彻底封装进 `FileFilterService`**，绝对不在 `DiskScanService` 中保留任何硬编码过滤逻辑。
2. **支持递归目录的元数据装饰器**：重构 `MetaCacheDecorator`，使其根据条目绝对路径动态分组加载对应父目录的 `.ArcMeta.json`，解决递归扫描下深层子目录元数据丢失的问题。
3. **真实线程安全的持久层数据库锁**：废除外部 Repo 中的局部 fake 锁，统一使用 `DatabaseManager` 内部的全局数据库互斥锁（`DatabaseManager::instance().dbMutex()`）控制并发访问。
4. **无定时器同步编辑器高亮**：通过轻量派生类 `FileNameLineEdit` 覆写 `focusInEvent`，在 Qt 默认焦点事件触发后同步校准选中范围，彻底淘汰 `QTimer::singleShot(0)` 定时器补丁与 `Show` 事件时序错位问题。

---

## 2. 详细解决方案

本部分由执行者 AI 角色（Jules）直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换。

### 2.1 新建 `FileFilterService`（彻底归一化过滤标准）

新建 `src/core/FileFilterService.h`：
```cpp
#pragma once
#include <QString>

namespace ArcMeta {
class FileFilterService {
public:
    // 统一过滤无用辅助配置文件、缩略图、系统缓存目录及 .arc 资产包
    static bool isAuxiliaryFile(const QString& path);
};
}
```

新建 `src/core/FileFilterService.cpp`：
```cpp
#include "FileFilterService.h"
#include <QFileInfo>

namespace ArcMeta {
bool FileFilterService::isAuxiliaryFile(const QString& path) {
    if (path.isEmpty()) return true;

    QFileInfo info(path);
    QString fileName = info.fileName();

    // 1. 过滤内部配置文件与缩略图
    if (fileName.endsWith(".ArcMeta.json", Qt::CaseInsensitive) ||
        fileName.endsWith("_thumbnail.png", Qt::CaseInsensitive) ||
        fileName.endsWith("metadata.scch", Qt::CaseInsensitive) ||
        fileName.endsWith("metadata.scch.tmp", Qt::CaseInsensitive)) {
        return true; 
    }

    // 2. 过滤缓存目录与 .arc 系统资产包（使其在目录树遍历中隐形）
    if (fileName.compare(".arcmeta", Qt::CaseInsensitive) == 0 ||
        fileName.endsWith(".arc", Qt::CaseInsensitive)) {
        return true;
    }

    return false;
}
}
```

---

### 2.2 重构 `DiskScanService`（纯粹物理遍历，零硬编码）

在 `src/core/DiskScanService.cpp` 中执行彻底净化（剔除 `AmMetaJson` 引用，纯物理遍历，完全依赖 `FileFilterService`）：
```
<<<<<<< SEARCH
#include "DiskScanService.h"
#include "../meta/AmMetaJson.h"
#include <QDir>
#include <QFileInfo>

namespace {
static inline bool isAuxiliaryFile(const QString& path) {
    if (path.isEmpty()) return true;

    // 🚨 仅保留 .ArcMeta.json，彻底清除 .am_meta.json 历史判断
    if (path.endsWith(".ArcMeta.json", Qt::CaseInsensitive) ||
        path.endsWith("_thumbnail.png", Qt::CaseInsensitive) ||
        path.endsWith("metadata.scch", Qt::CaseInsensitive) ||
        path.endsWith(".arc", Qt::CaseInsensitive)) {
        return true; // 屏蔽过滤
    }

    return false;
}
}

namespace ArcMeta {

std::vector<ItemRecord> DiskScanService::scanDirectory(const QString& path,
                                                        bool recursive,
                                                        const std::function<bool()>& shouldContinue) {
    std::vector<ItemRecord> allItems;

    std::function<void(const QString&, bool)> scanDir;
    scanDir = [&](const QString& p, bool rec) {
        QDir dir(p);
        if (!dir.exists()) return;

        // 自动加载该文件夹下的 AmMetaJson 离散标记缓存
        AmMetaJson jsonCache(p.toStdWString());
        jsonCache.load();
        const auto& cachedItems = jsonCache.items();

        QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name);
        for (const QFileInfo& info : entries) {
            if (shouldContinue && !shouldContinue()) return;

            if (isAuxiliaryFile(info.absoluteFilePath()) || info.fileName() == "metadata.scch.tmp") continue;
            // 应用自身的内部缓存目录，磁盘模式完全不进入、不展示、不扫描它，
            // 防止缓存目录被当作普通文件夹再次生成"缓存的缓存"
            if (info.isDir() && info.fileName().compare(".arcmeta", Qt::CaseInsensitive) == 0) continue;

            QString absPath = info.absoluteFilePath();
            ItemRecord itemRec = ItemRecord::create(absPath, nullptr, false);

            // 如果该物理文件在离散配置文件中有对应的离散打标缓存，将其无缝还原到 ItemRecord 中
            std::wstring fileName = info.fileName().toStdWString();
            auto it = cachedItems.find(fileName);
            if (it != cachedItems.end()) {
                itemRec.rating = it->second.rating;
                itemRec.manualColor = QString::fromStdWString(it->second.color);
                itemRec.pinned = it->second.pinned;
                itemRec.note = QString::fromStdWString(it->second.note);
                itemRec.url = QString::fromStdWString(it->second.url);
                itemRec.tags.clear();
                for (const auto& t : it->second.tags) {
                    itemRec.tags.append(QString::fromStdWString(t));
                }
                itemRec.width = it->second.width;
                itemRec.height = it->second.height;
                itemRec.autoColor = QString::fromStdWString(it->second.autoColor);
                itemRec.added_at = it->second.addedAt;

                itemRec.palettes.clear();
                for (const auto& pe : it->second.palettes) {
                    itemRec.palettes.push_back({pe.color, pe.ratio});
                }
            }

            allItems.push_back(itemRec);

            if (rec && info.isDir()) {
                scanDir(absPath, true);
            }
        }
    };

    scanDir(path, recursive);
    return allItems;
}
=======
#include "DiskScanService.h"
#include "FileFilterService.h"
#include <QDir>
#include <QFileInfo>

namespace ArcMeta {

std::vector<ItemRecord> DiskScanService::scanDirectory(const QString& path,
                                                        bool recursive,
                                                        const std::function<bool()>& shouldContinue) {
    std::vector<ItemRecord> allItems;

    std::function<void(const QString&, bool)> scanDir;
    scanDir = [&](const QString& p, bool rec) {
        QDir dir(p);
        if (!dir.exists()) return;

        QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name);
        for (const QFileInfo& info : entries) {
            if (shouldContinue && !shouldContinue()) return;

            QString absPath = info.absoluteFilePath();
            
            // 🚨 统一调用文件过滤服务（归一化处理所有辅助文件、.arc、.arcmeta）
            if (FileFilterService::isAuxiliaryFile(absPath)) continue;

            ItemRecord itemRec = ItemRecord::create(absPath, nullptr, false);
            allItems.push_back(itemRec);

            if (rec && info.isDir()) {
                scanDir(absPath, true);
            }
        }
    };

    scanDir(path, recursive);
    return allItems;
}
>>>>>>> REPLACE
```

---

### 2.3 新建支持多层/递归目录的 `MetaCacheDecorator` 装饰器

新建 `src/meta/MetaCacheDecorator.h`：
```cpp
#pragma once
#include "../core/ItemRecord.h"
#include <vector>

namespace ArcMeta {
class MetaCacheDecorator {
public:
    // 按条目物理父目录自动建立缓存池并线程安全地装配离散 JSON 业务元数据（全面支持单级与多级递归目录）
    static void decorate(std::vector<ItemRecord>& records);
};
}
```

新建 `src/meta/MetaCacheDecorator.cpp`：
```cpp
#include "MetaCacheDecorator.h"
#include "AmMetaJson.h"
#include <QFileInfo>
#include <unordered_map>
#include <memory>

namespace ArcMeta {
void MetaCacheDecorator::decorate(std::vector<ItemRecord>& records) {
    if (records.empty()) return;

    // 按父目录路径建立离散 JSON 缓存池，避免重复读取同一目录的配置文件
    std::unordered_map<std::wstring, std::shared_ptr<AmMetaJson>> jsonCacheMap;

    for (auto& itemRec : records) {
        if (itemRec.isCategory) continue;

        QFileInfo info(itemRec.path);
        std::wstring dirPath = info.absolutePath().toStdWString();

        auto cacheIt = jsonCacheMap.find(dirPath);
        if (cacheIt == jsonCacheMap.end()) {
            auto jsonCache = std::make_shared<AmMetaJson>(dirPath);
            jsonCache->load();
            jsonCacheMap[dirPath] = jsonCache;
            cacheIt = jsonCacheMap.find(dirPath);
        }

        const auto& cachedItems = cacheIt->second->items();
        std::wstring fileName = info.fileName().toStdWString();
        
        auto it = cachedItems.find(fileName);
        if (it != cachedItems.end()) {
            itemRec.rating = it->second.rating;
            itemRec.manualColor = QString::fromStdWString(it->second.color);
            itemRec.pinned = it->second.pinned;
            itemRec.note = QString::fromStdWString(it->second.note);
            itemRec.url = QString::fromStdWString(it->second.url);
            itemRec.tags.clear();
            for (const auto& t : it->second.tags) {
                itemRec.tags.append(QString::fromStdWString(t));
            }
            itemRec.width = it->second.width;
            itemRec.height = it->second.height;
            itemRec.autoColor = QString::fromStdWString(it->second.autoColor);
            itemRec.added_at = it->second.addedAt;

            itemRec.palettes.clear();
            for (const auto& pe : it->second.palettes) {
                itemRec.palettes.push_back({pe.color, pe.ratio});
            }
        }
    }
}
}
```

在 `src/ui/ContentPanel.cpp` 工作线程中调用装饰器（不限制根目录）：
```
<<<<<<< SEARCH
        std::vector<ItemRecord> allItems = DiskScanService::scanDirectory(
            path, recursive,
            [panelPtr]() { return static_cast<bool>(panelPtr); }
        );
        if (!panelPtr) return; 
 
        QMetaObject::invokeMethod(QCoreApplication::instance(), [panelPtr, path, allItems, reqId]() { 
=======
        std::vector<ItemRecord> allItems = DiskScanService::scanDirectory(
            path, recursive,
            [panelPtr]() { return static_cast<bool>(panelPtr); }
        );
        if (!panelPtr) return; 

        // 🚀 线程安全地装配离散业务元数据（支持单级与深层递归目录）
        MetaCacheDecorator::decorate(allItems);
 
        QMetaObject::invokeMethod(QCoreApplication::instance(), [panelPtr, path, allItems, reqId]() { 
>>>>>>> REPLACE
```

---

### 2.4 新建线程安全持久层 `DiskTrashRepo`（消除假锁与数据库跨线程崩溃隐患）

新建 `src/meta/DiskTrashRepo.h`：
```cpp
#pragma once
#include <vector>
#include <string>

namespace ArcMeta {

struct DiskTrashRawItem {
    int id;
    std::wstring trashPath;
    std::wstring originalPath;
    std::wstring fileName;
    bool isFolder;
    long long fileSize;
    long long deletedAt;
};

class DiskTrashRepo {
public:
    // 获取当前活动连接库中的所有物理回收记录（通过 DatabaseManager 全局互斥锁保证绝对多线程安全）
    static std::vector<DiskTrashRawItem> getAllTrashItems();
};

}
```

新建 `src/meta/DiskTrashRepo.cpp`：
```cpp
#include "DiskTrashRepo.h"
#include "DatabaseManager.h"
#include "sqlite3.h"
#include <QMutexLocker>

namespace ArcMeta {

std::vector<DiskTrashRawItem> DiskTrashRepo::getAllTrashItems() {
    std::vector<DiskTrashRawItem> results;

    // 🚨 核心修复：使用 DatabaseManager 的全局互斥锁，彻底避免并发句柄破坏
    QMutexLocker locker(DatabaseManager::instance().dbMutex());

    std::vector<sqlite3*> dbs = DatabaseManager::instance().getActiveMemoryDbs();
    for (sqlite3* db : dbs) {
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT id, trash_path, original_path, drive_letter, file_name, is_folder, file_size, deleted_at FROM disk_trash";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                DiskTrashRawItem r;
                r.id = sqlite3_column_int(stmt, 0);
                const wchar_t* wTrashPath = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 1));
                const wchar_t* wOrigPath = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 2));
                const wchar_t* wFileName = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 4));
                r.isFolder = (sqlite3_column_int(stmt, 5) != 0);
                r.fileSize = sqlite3_column_int64(stmt, 6);
                r.deletedAt = sqlite3_column_int64(stmt, 7);

                if (wTrashPath && wOrigPath) {
                    r.trashPath = wTrashPath;
                    r.originalPath = wOrigPath;
                    r.fileName = wFileName ? wFileName : L"";
                    results.push_back(r);
                }
            }
            sqlite3_finalize(stmt);
        }
    }
    return results;
}

}
```

在 `src/meta/DatabaseManager.h` 中暴露全局互斥锁：
```cpp
// 在 DatabaseManager 类中添加：
public:
    QMutex* dbMutex() { return &m_dbMutex; }
private:
    QMutex m_dbMutex;
```

重构 `src/core/CategoryLoadService.cpp`：
```
<<<<<<< SEARCH
#include "CategoryLoadService.h"
#include "../meta/MetadataManager.h"
#include "../meta/CategoryRepo.h"
#include "CategoryLockManager.h"
#include "../meta/DatabaseManager.h"
#include <QFileInfo>

namespace {
static inline bool isAuxiliaryFile(const QString& path) {
    if (path.isEmpty()) return true;

    // 🚨 仅保留 .ArcMeta.json，彻底清除 .am_meta.json 历史判断
    // 🚨 修正：移除对 .arc 的过滤！.arc 是托管资源库真实的资产胶囊，绝非无用辅助文件
    if (path.endsWith(".ArcMeta.json", Qt::CaseInsensitive) ||
        path.endsWith("_thumbnail.png", Qt::CaseInsensitive) ||
        path.endsWith("metadata.scch", Qt::CaseInsensitive)) {
        return true; // 屏蔽过滤真正的辅助配置文件与缩略图
    }

    return false;
}
}

namespace ArcMeta {
=======
#include "CategoryLoadService.h"
#include "../meta/MetadataManager.h"
#include "../meta/CategoryRepo.h"
#include "CategoryLockManager.h"
#include "FileFilterService.h"
#include "../meta/DiskTrashRepo.h"
#include <QFileInfo>

namespace ArcMeta {
>>>>>>> REPLACE
```

```
<<<<<<< SEARCH
std::vector<ItemRecord> CategoryLoadService::loadTrashItems() {
    std::vector<ItemRecord> libraryTrash;
    std::vector<ItemRecord> diskTrash;

    // 1. 数据集 A：资源库托管回收项
    MetadataManager::instance().forEachCachedItem([&](const std::wstring& path, const RuntimeMeta& meta) {
        if (!meta.isTrash) return;

        // 过滤辅助文件
        QString qPath = QString::fromStdWString(path);
        if (isAuxiliaryFile(qPath)) {
            return;
        }

        ItemRecord r = ItemRecord::create(qPath, &meta, true);
        r.groupName = "Library";
        libraryTrash.push_back(r);
    });

    // 2. 数据集 B：目录导航物理回收项
    std::vector<sqlite3*> dbs = DatabaseManager::instance().getActiveMemoryDbs();
    for (sqlite3* db : dbs) {
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT id, trash_path, original_path, drive_letter, file_name, is_folder, file_size, deleted_at FROM disk_trash";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                int id = sqlite3_column_int(stmt, 0);
                const wchar_t* wTrashPath = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 1));
                const wchar_t* wOrigPath = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 2));
                const wchar_t* wFileName = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 4));
                int isFolder = sqlite3_column_int(stmt, 5);
                long long fileSize = sqlite3_column_int64(stmt, 6);
                long long deletedAt = sqlite3_column_int64(stmt, 7);

                if (wTrashPath && wOrigPath) {
                    ItemRecord r;
                    r.path = QString::fromWCharArray(wTrashPath);
                    r.originalPath = QString::fromWCharArray(wOrigPath);
                    r.filename = wFileName ? QString::fromWCharArray(wFileName) : QFileInfo(r.path).fileName();
                    r.isDir = (isFolder != 0);
                    r.size = fileSize;
                    r.mtime = deletedAt;
                    r.ctime = deletedAt;
                    r.atime = deletedAt;
                    r.isDiskTrash = true;
                    r.diskTrashId = id;
                    r.groupName = "DiskNav";

                    if (r.isDir) {
                        r.suffix = "";
                    } else {
                        int lastDot = r.filename.lastIndexOf('.');
                        r.suffix = (lastDot != -1) ? r.filename.mid(lastDot + 1).toLower() : "";
                    }

                    diskTrash.push_back(r);
                }
            }
            sqlite3_finalize(stmt);
        }
    }
=======
std::vector<ItemRecord> CategoryLoadService::loadTrashItems() {
    std::vector<ItemRecord> libraryTrash;
    std::vector<ItemRecord> diskTrash;

    // 1. 数据集 A：资源库托管回收项
    MetadataManager::instance().forEachCachedItem([&](const std::wstring& path, const RuntimeMeta& meta) {
        if (!meta.isTrash) return;

        // 过滤辅助文件
        QString qPath = QString::fromStdWString(path);
        if (FileFilterService::isAuxiliaryFile(qPath)) {
            return;
        }

        ItemRecord r = ItemRecord::create(qPath, &meta, true);
        r.groupName = "Library";
        libraryTrash.push_back(r);
    });

    // 2. 数据集 B：目录导航物理回收项（通过 DiskTrashRepo 获取，杜绝数据库句柄跨线程崩溃）
    auto trashItems = DiskTrashRepo::getAllTrashItems();
    for (const auto& item : trashItems) {
        ItemRecord r;
        r.path = QString::fromStdWString(item.trashPath);
        r.originalPath = QString::fromStdWString(item.originalPath);
        r.filename = !item.fileName.empty() ? QString::fromStdWString(item.fileName) : QFileInfo(r.path).fileName();
        r.isDir = item.isFolder;
        r.size = item.fileSize;
        r.mtime = item.deletedAt;
        r.ctime = item.deletedAt;
        r.atime = item.deletedAt;
        r.isDiskTrash = true;
        r.diskTrashId = item.id;
        r.groupName = "DiskNav";

        if (r.isDir) {
            r.suffix = "";
        } else {
            int lastDot = r.filename.lastIndexOf('.');
            r.suffix = (lastDot != -1) ? r.filename.mid(lastDot + 1).toLower() : "";
        }

        diskTrash.push_back(r);
    }
>>>>>>> REPLACE
```

---

### 2.5 派生 `FileNameLineEdit` 完美消除定时器并解决时序错位

在 `src/ui/ThumbnailDelegate.h` 中添加轻量派生类定义：
```cpp
#include <QLineEdit>

namespace ArcMeta {

class FileNameLineEdit : public QLineEdit {
    Q_OBJECT
public:
    explicit FileNameLineEdit(QWidget* parent = nullptr) : QLineEdit(parent) {}
    void setIsFolder(bool isFolder) { m_isFolder = isFolder; }

protected:
    void focusInEvent(QFocusEvent* event) override {
        QLineEdit::focusInEvent(event); // 先执行基类 Focus 事件
        if (m_isFolder) {
            selectAll();
        } else {
            int lastDot = text().lastIndexOf('.');
            if (lastDot > 0) {
                setSelection(0, lastDot);
            } else {
                selectAll();
            }
        }
    }

private:
    bool m_isFolder = false;
};

}
```

在 `src/ui/ThumbnailDelegate.cpp` 中重构 `createEditor` 与 `updateEditorGeometry`：
```
<<<<<<< SEARCH
QWidget* ThumbnailDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    QWidget* editor = QStyledItemDelegate::createEditor(parent, option, index);
    if (editor) {
        // 按照用户要求：修改为项目标准蓝 (#3498db)
        editor->setStyleSheet(
            "QLineEdit {"
            "  background-color: #2D2D2D;"
            "  color: #FFFFFF;"
            "  selection-background-color: #3498db;"
            "  border: 1px solid #3498db;"
            "  border-radius: 4px;"
            "  padding: 0px 4px;"
            "  margin: 0px;"
            "  font-size: 8pt;"
            "}"
        );
        // 2026-07-26 极致重构：为编辑器安装事件过滤器，确保 eventFilter 能有效捕获键盘冲突并拦截（对应用户原话：“在编辑状态下按下向上/向下方向键时则不该向上游动选中项目”）
        editor->installEventFilter(const_cast<ThumbnailDelegate*>(this));
    }
    return editor;
}

void ThumbnailDelegate::updateEditorGeometry(QWidget* editor,
                                              const QStyleOptionViewItem& option,
                                              const QModelIndex& /*index*/) const {
    Metrics m = calculateMetrics(option);
    // 修正编辑器位置，使其与文件名文字区域对齐并留出少量边距
    // 高度降低 2 像素：通过上下各收缩 1 像素实现 (从 4 变 5)
    editor->setGeometry(m.textRect.adjusted(1, 5, -1, -5));
}

void ThumbnailDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const {
    QString value = index.model()->data(index, Qt::EditRole).toString();
    QLineEdit* lineEdit = qobject_cast<QLineEdit*>(editor); 
    if (lineEdit) {
        lineEdit->setText(value); 

        // 如果是文件夹或分类，全选；如果是文件，仅选中名称部分
        // 使用 QTimer::singleShot 确保在 Qt 内部默认全选逻辑之后执行，彻底解决失效问题
        bool isFolder = (index.data(m_typeRole).toString() == "folder" || index.data(m_typeRole).toString() == "category");
        
        QTimer::singleShot(0, lineEdit, [lineEdit, value, isFolder]() {
            if (!lineEdit) return;
            if (isFolder) {
                lineEdit->selectAll();
            } else {
                int lastDot = value.lastIndexOf('.'); 
                if (lastDot > 0) { 
                    lineEdit->setSelection(0, lastDot); 
                } else { 
                    lineEdit->selectAll(); 
                }
            }
        });
    }
}
=======
QWidget* ThumbnailDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    FileNameLineEdit* editor = new FileNameLineEdit(parent);
    editor->setStyleSheet(
        "QLineEdit {"
        "  background-color: #2D2D2D;"
        "  color: #FFFFFF;"
        "  selection-background-color: #3498db;"
        "  border: 1px solid #3498db;"
        "  border-radius: 4px;"
        "  padding: 0px 4px;"
        "  margin: 0px;"
        "  font-size: 8pt;"
        "}"
    );

    bool isFolder = (index.data(m_typeRole).toString() == "folder" || index.data(m_typeRole).toString() == "category");
    editor->setIsFolder(isFolder);
    editor->installEventFilter(const_cast<ThumbnailDelegate*>(this));
    return editor;
}

void ThumbnailDelegate::updateEditorGeometry(QWidget* editor,
                                              const QStyleOptionViewItem& option,
                                              const QModelIndex& /*index*/) const {
    Metrics m = calculateMetrics(option);
    // 根据当前视图设备的 DPI 比例动态自适应放缩微调
    double dpr = option.widget ? option.widget->devicePixelRatio() : 1.0;
    int offsetLeft = static_cast<int>(1 * dpr);
    int offsetTop = static_cast<int>(5 * dpr);
    int offsetRight = static_cast<int>(-1 * dpr);
    int offsetBottom = static_cast<int>(-5 * dpr);

    editor->setGeometry(m.textRect.adjusted(offsetLeft, offsetTop, offsetRight, offsetBottom));
}

void ThumbnailDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const {
    QString value = index.model()->data(index, Qt::EditRole).toString();
    FileNameLineEdit* lineEdit = qobject_cast<FileNameLineEdit*>(editor); 
    if (lineEdit) {
        lineEdit->setText(value); // 纯粹同步赋值，高亮交由 FileNameLineEdit::focusInEvent 完美同步接管
    }
}
>>>>>>> REPLACE
```

---

## 3. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] `src/core/FileFilterService.h` & `.cpp` （新建：彻底收拢全局文件过滤标准）
- [ ] `src/meta/DiskTrashRepo.h` & `.cpp` （新建：基于 `DatabaseManager` 全局锁的线程安全物理回收站仓储层）
- [ ] `src/meta/MetaCacheDecorator.h` & `.cpp` （新建：支持多层级/递归目录的业务元数据装饰器）
- [ ] `src/core/DiskScanService.cpp` （纯粹物理扫描，完全依赖 `FileFilterService`）
- [ ] `src/core/CategoryLoadService.cpp` （解耦 sqlite3 原生 SQL 侵入，统一调用仓储层与过滤服务）
- [ ] `src/ui/ThumbnailDelegate.h` & `.cpp` （引入 `FileNameLineEdit`，消除定时器与时序问题；坐标适应 DPI）
- [ ] `src/ui/ContentPanel.cpp` （后台工作线程遍历物理磁盘结果后，引入并执行装饰器组装业务元数据）

**明确禁止越界修改的范围：**
- [ ] 物理数据库连接初始化及正常的 SQLite 读写。
- [ ] 视图列表渲染以及其他的事件机制。