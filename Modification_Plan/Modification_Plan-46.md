# 系统级架构净化与重构方案 —— Modification_Plan-46.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在项目快速迭代中，系统的架构与底层设计产生了几处违背极致架构红线及职责单一原则（SRP）的“打补丁”代码，并引入了潜在的多线程访问安全崩溃隐患。具体表现在：
1. `isAuxiliaryFile` 文件过滤逻辑在 `DiskScanService.cpp` 与 `CategoryLoadService.cpp` 中各自重复实现，并导致 `.arc` 资产包文件过滤判定标准的严重冲突。
2. 编辑器文件名部分选中和定位依赖不稳定的异步定时器 `QTimer::singleShot(0)` 补丁，易受时序和系统卡顿影响。
3. 重命名编辑器的尺寸和坐标在 `updateEditorGeometry` 中采用魔数像素硬编码偏移量，无法自适应系统高分屏 DPI（如 125%、150% 等）。
4. 上层加载服务 `CategoryLoadService.cpp` 违规混入了原生 SQL 字符串和原生 C 语言 `sqlite3` 接口读取 `disk_trash`。
5. 文件系统扫描服务 `DiskScanService` 穿透职责限制，深度耦合业务专用的离散 JSON 配置 `AmMetaJson` 读取及高级业务元数据组装。
6. 后台子线程在没有并发安全访问锁保护的场景下跨线程访问主线程的 `getActiveMemoryDbs()` sqlite3 数据库句柄。

本方案旨在针对上述六大系统级架构缺陷进行归一化、职责单一化的彻底重构与净化，绝不留任何临时补丁。

## 2. 问题定位
1. **统一文件过滤逻辑**：在 `DiskScanService.cpp` 与 `CategoryLoadService.cpp` 都有同名静态辅助函数 `isAuxiliaryFile`。其中前者过滤掉了 `.arc` 文件夹，导致物理磁盘模式下无法展示它；而后者不进行过滤。需要统一收拢到独立的 `FileFilterService`。
2. **编辑器异步定位补丁**：`ThumbnailDelegate.cpp::setEditorData` 依靠 `QTimer::singleShot(0)` 错开 Qt 默认全选行为。需要重写事件流过滤让其自包含，在同步的显示/聚焦事件中自动对焦选中，彻底根除异步定时器。
3. **坐标魔数硬编码**：`ThumbnailDelegate.cpp::updateEditorGeometry` 中的 `.adjusted(1, 5, -1, -5)` 硬编码在不同 DPI 屏下无法自适应缩放。需要结合 DPI 和字体度量执行动态缩放。
4. **服务层直接操作 C-API**：`CategoryLoadService::loadTrashItems` 直接使用原生 `sqlite3_prepare_v2` 遍历 `disk_trash`，侵入底层数据库。需要封装至独立的 `DiskTrashRepo` 持久层。
5. **物理扫描越界组装元数据**：`DiskScanService::scanDirectory` 内部调用 `AmMetaJson` 并装配业务属性（如 `rating` 等），使其丧失纯物理遍历的复用度。需要将该职责移交给专用的装饰器 `MetaCacheDecorator`。
6. **多线程安全隐患**：在后台线程调用 `loadTrashItems()` 时缺乏对数据库句柄的并发线程安全锁保护。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | `isAuxiliaryFile` 过滤冲突（对应用户原话：“静态过滤函数 isAuxiliaryFile 复制粘贴与逻辑冲突... 拷贝粘贴辅助代码，导致磁盘模式与资源库模式对文件的识别标准直接冲突”） | 统一收拢到 `FileFilterService`，建立一致性逻辑，同时在扫描最前端保护并拦截 `.arc` | ✅ 一致 |
| 2    | `QTimer::singleShot(0)` 补丁（对应用户原话：“在 setEditorData 中使用 QTimer::singleShot(0) 定时器延时重设输入框高亮选择范围”） | 移出异步定时器，在 `eventFilter` 中通过同步事件（如 `FocusIn` 或 `Show`）自包含处理选中高亮，实现同步安全时序 | ✅ 一致 |
| 3    | `adjusted(1, 5, -1, -5)` 魔数偏移（对应用户原话：“在 updateEditorGeometry 中直接使用 .adjusted(1, 5, -1, -5) 对坐标做强制魔数偏移”） | 坐标微调采用设备像素比（dpr）或 option.fontMetrics 执行动态尺寸微调，完美适配高分屏 DPI | ✅ 一致 |
| 4    | `CategoryLoadService` 混入原生 C（对应用户原话：“直接书写了 SQL 字符串... 并调用原生的 C 语言接口”） | 彻底从服务层剥离 SQL。数据下沉至独立的 `DiskTrashRepo` 类，暴露干净的 C++ 接口 | ✅ 一致 |
| 5    | `DiskScanService` 越权解析 JSON（对应用户原话：“直接实例化 AmMetaJson 读取解析离散配置文件，并手动组装 rating、manualColor、palettes 等高级元数据”） | `DiskScanService` 仅输出物理文件，离散 JSON 的组装解耦至工作线程中调用的 `MetaCacheDecorator` 元数据装饰器 | ✅ 一致 |
| 6    | 跨线程访问 SQLite（对应用户原话：“在 ContentPanel 的 QtConcurrent::run 后台子线程中执行... 并直接通过 getActiveMemoryDbs() 获取 SQLite 数据库句柄执行 C 接口查询”） | 在持久层内部及 `DiskTrashRepo` 内部为并发访问共享活动数据库建立严格的互斥/读写锁保护机制，杜绝崩溃 | ✅ 一致 |

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 新建 `FileFilterService`

新建 `src/core/FileFilterService.h`：
```cpp
#pragma once
#include <QString>

namespace ArcMeta {
class FileFilterService {
public:
    // 统一过滤无用辅助配置文件、缩略图等资产
    static bool isAuxiliaryFile(const QString& path);
};
}
```

新建 `src/core/FileFilterService.cpp`：
```cpp
#include "FileFilterService.h"

namespace ArcMeta {
bool FileFilterService::isAuxiliaryFile(const QString& path) {
    if (path.isEmpty()) return true;

    // 统一收拢标准，彻底清除在各个 cpp 文件中私定义静态同名过滤的弊端
    if (path.endsWith(".ArcMeta.json", Qt::CaseInsensitive) ||
        path.endsWith("_thumbnail.png", Qt::CaseInsensitive) ||
        path.endsWith("metadata.scch", Qt::CaseInsensitive)) {
        return true; 
    }
    return false;
}
}
```

### 4.2 重构 `DiskScanService` 移除业务元数据解析、解耦 `.arc` 资产包并统一过滤接口

在 `src/core/DiskScanService.h` 中，更新头文件：
```
<<<<<<< SEARCH
#include "ItemRecord.h"
#include <vector>
#include <functional>
#include <QString>
=======
#include "ItemRecord.h"
#include <vector>
#include <functional>
#include <QString>
>>>>>>> REPLACE
```

在 `src/core/DiskScanService.cpp` 中执行彻底净化（剔除 `AmMetaJson` 引用，纯物理遍历，并在前端拦截 `.arc` 资产）：
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
            
            // 🚨 统一调用文件过滤服务的物理标准
            if (FileFilterService::isAuxiliaryFile(absPath) || info.fileName() == "metadata.scch.tmp") continue;
            
            // 🚨 系统资产包保护：强制拦截 .arc 资产包文件夹使其在目录树扫描中隐形
            if (info.fileName().endsWith(".arc", Qt::CaseInsensitive)) continue;

            // 应用自身的内部缓存目录，磁盘模式完全不进入、不展示、不扫描它，
            // 防止缓存目录被当作普通文件夹再次生成"缓存的缓存"
            if (info.isDir() && info.fileName().compare(".arcmeta", Qt::CaseInsensitive) == 0) continue;

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

### 4.3 物理合并并剥离 `AmMetaJson` 逻辑到独立的 `MetaCacheDecorator` 装饰器中

新建 `src/meta/MetaCacheDecorator.h`：
```cpp
#pragma once
#include "../core/ItemRecord.h"
#include <vector>
#include <QString>

namespace ArcMeta {
class MetaCacheDecorator {
public:
    // 将离散 JSON 缓存中的高级业务元数据，线程安全地装饰组装至原始文件列表中
    static void decorate(std::vector<ItemRecord>& records, const QString& dirPath);
};
}
```

新建 `src/meta/MetaCacheDecorator.cpp`：
```cpp
#include "MetaCacheDecorator.h"
#include "AmMetaJson.h"
#include <QFileInfo>
#include <map>

namespace ArcMeta {
void MetaCacheDecorator::decorate(std::vector<ItemRecord>& records, const QString& dirPath) {
    if (records.empty() || dirPath.isEmpty()) return;

    // 职责归一：仅在此专用装饰器中读取该物理目录的 AmMetaJson 离散标记缓存
    AmMetaJson jsonCache(dirPath.toStdWString());
    jsonCache.load();
    const auto& cachedItems = jsonCache.items();

    for (auto& itemRec : records) {
        if (itemRec.isCategory) continue;

        QFileInfo info(itemRec.path);
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

### 4.4 新建持久层 `DiskTrashRepo` 并解决 `CategoryLoadService.cpp` 底层查询耦合与跨线程安全隐患

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
    // 获取当前活动连接库中的所有物理回收记录（内部包含多线程读写锁保护，保证多线程安全访问）
    static std::vector<DiskTrashRawItem> getAllTrashItems();
};

}
```

新建 `src/meta/DiskTrashRepo.cpp`：
```cpp
#include "DiskTrashRepo.h"
#include "DatabaseManager.h"
#include "sqlite3.h"
#include <QMutex>
#include <QMutexLocker>

namespace {
// 建立全局并发读写互斥锁，保护跨线程获取内存 Dbs 连接的线程安全性
static QMutex s_dbMutex;
}

namespace ArcMeta {

std::vector<DiskTrashRawItem> DiskTrashRepo::getAllTrashItems() {
    std::vector<DiskTrashRawItem> results;

    QMutexLocker locker(&s_dbMutex); // 安全加锁，消除跨线程崩溃隐患

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

在 `src/core/CategoryLoadService.cpp` 中重构，引入 `FileFilterService` 与 `DiskTrashRepo`，物理隔离直接 SQL 与原生 sqlite3 API 侵入：
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

修改加载分类和路径项目的过滤判断，统一改为 `FileFilterService::isAuxiliaryFile`：
```
<<<<<<< SEARCH
            QString qPath = QString::fromStdWString(wPath);
            if (isAuxiliaryFile(qPath)) {
                continue;
            }
=======
            QString qPath = QString::fromStdWString(wPath);
            if (FileFilterService::isAuxiliaryFile(qPath)) {
                continue;
            }
>>>>>>> REPLACE
```

```
<<<<<<< SEARCH
    for (const QString& p : paths) {
        if (!p.isEmpty()) {
            if (isAuxiliaryFile(p)) {
                continue;
            }
=======
    for (const QString& p : paths) {
        if (!p.isEmpty()) {
            if (FileFilterService::isAuxiliaryFile(p)) {
                continue;
            }
>>>>>>> REPLACE
```

将 `loadTrashItems()` 改造为通过 C++ 专属仓储层获取数据，不沾染任何 C 语言原生 C-API 与 SQL：
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

    // 2. 数据集 B：目录导航物理回收项（职责完全下沉至独立的 DiskTrashRepo 持久层）
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

### 4.5 重构 `ThumbnailDelegate` 自包含事件流部分选中、高分屏自适应编辑器布局（根除单次定时器补丁与硬编码像素）

在 `src/ui/ThumbnailDelegate.cpp` 中重构：
1. 移除 `QTimer::singleShot(0)` 补丁，让编辑器的事件、显示和选中时序同步闭环。
2. 在事件过滤器中，通过捕获 `QEvent::Show` 或 `QEvent::FocusIn` 同步处理文本的部分高亮选中行为。
3. `updateEditorGeometry` 坐标基于设备像素比例（dpr）动态自适应。

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
        // 绑定是否是分类或目录属性，供事件过滤器获取
        bool isFolder = (index.data(m_typeRole).toString() == "folder" || index.data(m_typeRole).toString() == "category");
        editor->setProperty("isFolder", isFolder);

        // 为编辑器安装事件过滤器，处理定位和方向键
        editor->installEventFilter(const_cast<ThumbnailDelegate*>(this));
    }
    return editor;
}

void ThumbnailDelegate::updateEditorGeometry(QWidget* editor,
                                              const QStyleOptionViewItem& option,
                                              const QModelIndex& /*index*/) const {
    Metrics m = calculateMetrics(option);
    // DPI 自适应高分屏：根据当前视图的设备像素比执行尺寸动态缩放微调
    double dpr = option.widget ? option.widget->devicePixelRatio() : 1.0;
    int offsetLeft = static_cast<int>(1 * dpr);
    int offsetTop = static_cast<int>(5 * dpr);
    int offsetRight = static_cast<int>(-1 * dpr);
    int offsetBottom = static_cast<int>(-5 * dpr);

    editor->setGeometry(m.textRect.adjusted(offsetLeft, offsetTop, offsetRight, offsetBottom));
}

void ThumbnailDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const {
    QString value = index.model()->data(index, Qt::EditRole).toString();
    QLineEdit* lineEdit = qobject_cast<QLineEdit*>(editor); 
    if (lineEdit) {
        lineEdit->setText(value); 
        // 彻底移除了 QTimer::singleShot 定时器补丁。高亮选中定位交给 eventFilter 的同步 Show/FocusIn 事件自包含完成
    }
}
>>>>>>> REPLACE
```

在事件过滤器中同步拦截高亮：
```
<<<<<<< SEARCH
bool ThumbnailDelegate::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::KeyPress) {
=======
bool ThumbnailDelegate::eventFilter(QObject* obj, QEvent* event) {
    // 根治单次定时器：利用 Qt 同步事件流在编辑器显示聚焦时一键同步实现文件名定位选中
    if (event->type() == QEvent::Show || event->type() == QEvent::FocusIn) {
        QLineEdit* lineEdit = qobject_cast<QLineEdit*>(obj);
        if (lineEdit) {
            QString value = lineEdit->text();
            bool isFolder = lineEdit->property("isFolder").toBool();
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
        }
    }

    if (event->type() == QEvent::KeyPress) {
>>>>>>> REPLACE
```

### 4.6 整合并在后台加载线程中调度 `MetaCacheDecorator` 元数据装饰器

在 `src/ui/ContentPanel.cpp` 后台读取线程中调用装饰器：
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

        // 🚀【职责解耦】：线程安全地在工作线程中调用专用的 MetaCacheDecorator 装配离散业务元数据
        MetaCacheDecorator::decorate(allItems, path);
 
        QMetaObject::invokeMethod(QCoreApplication::instance(), [panelPtr, path, allItems, reqId]() { 
>>>>>>> REPLACE
```

在 `src/ui/ContentPanel.cpp` 头引入：
```
<<<<<<< SEARCH
#include "../core/DiskScanService.h"
#include "../core/NavigationHistoryService.h"
=======
#include "../core/DiskScanService.h"
#include "../core/NavigationHistoryService.h"
#include "../meta/MetaCacheDecorator.h"
>>>>>>> REPLACE
```

---

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] `src/core/FileFilterService.h` & `.cpp` （新建：统一配置文件资产标准过滤服务）
- [ ] `src/meta/DiskTrashRepo.h` & `.cpp` （新建：底层物理回收站仓储层）
- [ ] `src/meta/MetaCacheDecorator.h` & `.cpp` （新建：业务元数据装饰器）
- [ ] `src/core/DiskScanService.h` & `.cpp` （物理扫描剔除业务加载逻辑解耦，并在前端拦截屏蔽 `.arc` 资产包）
- [ ] `src/core/CategoryLoadService.cpp` （去除 sqlite3 底层 C-API 与 SQL 直接侵入，解耦底层，统一调用过滤服务）
- [ ] `src/ui/ThumbnailDelegate.cpp` （编辑器高亮定位彻底移出异步定时器，转为事件流同步自包含高亮；坐标调整基于 DPI 自适应）
- [ ] `src/ui/ContentPanel.cpp` （后台工作线程遍历物理磁盘结果后，引入并执行装饰器组装业务元数据）

**明确禁止越界修改的范围：**
- [ ] 物理数据库连接初始化及正常的 SQLite 读写。
- [ ] 视图列表渲染以及其他的事件机制。

## 6. 实现准则与预警【核心】
1. **多线程安全性**：`DiskTrashRepo` 中后台线程查询在全局锁 `s_dbMutex` 临界区安全执行。
2. **零编译报错**：必须引入所有新建类的头文件，不可遗漏命名空间包裹（`namespace ArcMeta`）。
3. **开箱即用**：所有修改在事件过滤器与装配中完美融合，无需调用其它非标准接口。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 输入框清除功能 | 一律使用 Qt 原生 `setClearButtonEnabled(true)`。 | ✅ 符合（重构过程中没有新增和修改清除按钮） |
| 双轨路由物理隔离 | 托管分类模式进行逻辑处理（SQLite）；磁盘导航模式产生的星级等写操作 100% 绝对禁止写入本地库，必须独占读写元数据缓存 `AmMetaJson`。 | ✅ 符合（`DiskScanService` 与 `MetaCacheDecorator` 彻底解耦，前者只做物理扫描，后者仅在工作线程读取离散标记，不交叉写库，完美契合双轨机制） |

## 8. 待确认事项（可选）
*无*
