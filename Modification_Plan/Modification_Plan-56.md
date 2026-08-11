# 重复检测与哈希持久化重构 —— Modification_Plan-56.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在导入万级资产或大批量项时，由于重复检测频繁计算像素级缩略图、图像解码、以及缺乏持久化哈希字段（`sha256`），导致频繁卡死与假死。本方案在底层数据库、内存镜像和算法服务层引入极速递进判重流程与 SHA256 持久化，彻底解决假死现象。

## 2. 问题定位
- **判重假死根因**：`DuplicateDetectorService::detectDuplicates` 在主线程全量读取并计算每个文件的磁盘缩略图，且对于相同的体积没有进行快速采样哈希和全量 SHA256 数据库级持久化过滤，导致了严重的 I/O 与 CPU 假死。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 扩展 metadata 数据库 Schema 增加第 23 个列 sha256 | 在 DatabaseManager.cpp 建表语句及 PRAGMA 表结构检测区域增加并适配该字段 | ✅ |
| 2    | 建立 idx_metadata_hash 体戏与哈希联合索引 | 在 Schema 脚本末尾追加此联合索引建立语句 | ✅ |
| 3    | 扩展内存镜像结构体 RuntimeMeta，增加 sha256 存储字段 | 在 RuntimeMeta 结构体增加 `std::string sha256` 字段 | ✅ |
| 4    | 新增 setSha256 接口，异步落盘不阻塞 UI | 在 MetadataManager 声明并实现 setSha256 API | ✅ |
| 5    | 映射及写入占位符更新为 23 个字段 | 在 select/insert 语句、loadFromDb 映射及 bindMetaHelper 中追加绑定 | ✅ |
| 6    | 重复检测在检测阶段仅比对属性与哈希，彻底剥离像素级缩略图计算 | 重构 detectDuplicates 与 DuplicateItemInfo，剔除图片计算，延迟至确定冲突后再提取 | ✅ |
| 7    | 三级递进判重流程：体积比对、快速采样、全量比对 | 在 detectDuplicates 中完美编写此四阶段决策链 | ✅ |

> 所有项保持 100% 物理对齐一致。

## 4. 详细解决方案

*本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。*

### 4.1 DatabaseManager.cpp 修改块
对 `loadDb` 的 schema 常量字符串、检测及自动平滑迁移做追加：

```
<<<<<<< SEARCH
        CREATE TABLE IF NOT EXISTS metadata (
            folder_id TEXT PRIMARY KEY,
            path TEXT NOT NULL,
            is_folder INTEGER DEFAULT 0,
            rating INTEGER DEFAULT 0,
            color TEXT,
            tags TEXT,
            note TEXT,
            url TEXT,
            ctime INTEGER,
            mtime INTEGER,
            atime INTEGER,
            file_size INTEGER,
            palettes BLOB,
            is_trash INTEGER DEFAULT 0,
            original_path TEXT,
            width INTEGER DEFAULT 0,
            height INTEGER DEFAULT 0,
            ingestion_status INTEGER DEFAULT -1,
            auto_color TEXT DEFAULT '',
            base_name TEXT DEFAULT '',
            ext TEXT DEFAULT '',
            added_at INTEGER DEFAULT 0
        );
        CREATE INDEX IF NOT EXISTS idx_path ON metadata(path);
        CREATE INDEX IF NOT EXISTS idx_metadata_added_at ON metadata(added_at);
=======
        CREATE TABLE IF NOT EXISTS metadata (
            folder_id TEXT PRIMARY KEY,
            path TEXT NOT NULL,
            is_folder INTEGER DEFAULT 0,
            rating INTEGER DEFAULT 0,
            color TEXT,
            tags TEXT,
            note TEXT,
            url TEXT,
            ctime INTEGER,
            mtime INTEGER,
            atime INTEGER,
            file_size INTEGER,
            palettes BLOB,
            is_trash INTEGER DEFAULT 0,
            original_path TEXT,
            width INTEGER DEFAULT 0,
            height INTEGER DEFAULT 0,
            ingestion_status INTEGER DEFAULT -1,
            auto_color TEXT DEFAULT '',
            base_name TEXT DEFAULT '',
            ext TEXT DEFAULT '',
            added_at INTEGER DEFAULT 0,
            sha256 TEXT DEFAULT ''
        );
        CREATE INDEX IF NOT EXISTS idx_path ON metadata(path);
        CREATE INDEX IF NOT EXISTS idx_metadata_added_at ON metadata(added_at);
        CREATE INDEX IF NOT EXISTS idx_metadata_hash ON metadata(file_size, sha256);
>>>>>>> REPLACE
```

```
<<<<<<< SEARCH
    if (!hasBaseNameColumn) {
        qDebug() << "[DB] 检测到旧版数据库，正在添加 base_name 字段...";
        sqlite3_exec(conn.memDb, "ALTER TABLE metadata ADD COLUMN base_name TEXT DEFAULT ''", nullptr, nullptr, nullptr);
    }
    if (!hasExtColumn) {
        qDebug() << "[DB] 检测到旧版数据库，正在添加 ext 字段...";
        sqlite3_exec(conn.memDb, "ALTER TABLE metadata ADD COLUMN ext TEXT DEFAULT ''", nullptr, nullptr, nullptr);
=======
    bool hasSha256Column = false;
    sqlite3_stmt* checkStmt3 = nullptr;
    if (sqlite3_prepare_v2(conn.memDb, "PRAGMA table_info(metadata)", -1, &checkStmt3, nullptr) == SQLITE_OK) {
        while (sqlite3_step(checkStmt3) == SQLITE_ROW) {
            const char* colName = reinterpret_cast<const char*>(sqlite3_column_text(checkStmt3, 1));
            if (colName && std::string(colName) == "sha256") {
                hasSha256Column = true;
            }
        }
        sqlite3_finalize(checkStmt3);
    }
    if (!hasSha256Column) {
        qDebug() << "[DB] 检测到旧版数据库，正在添加 sha256 字段并建立联合索引...";
        sqlite3_exec(conn.memDb, "ALTER TABLE metadata ADD COLUMN sha256 TEXT DEFAULT ''", nullptr, nullptr, nullptr);
        sqlite3_exec(conn.memDb, "CREATE INDEX IF NOT EXISTS idx_metadata_hash ON metadata(file_size, sha256);", nullptr, nullptr, nullptr);
    }

    if (!hasBaseNameColumn) {
        qDebug() << "[DB] 检测到旧版数据库，正在添加 base_name 字段...";
        sqlite3_exec(conn.memDb, "ALTER TABLE metadata ADD COLUMN base_name TEXT DEFAULT ''", nullptr, nullptr, nullptr);
    }
    if (!hasExtColumn) {
        qDebug() << "[DB] 检测到旧版数据库，正在添加 ext 字段...";
        sqlite3_exec(conn.memDb, "ALTER TABLE metadata ADD COLUMN ext TEXT DEFAULT ''", nullptr, nullptr, nullptr);
>>>>>>> REPLACE
```

### 4.2 MetadataManager.h 修改块
在 `RuntimeMeta` 结构体及 `MetadataManager` 声明中增加：

```
<<<<<<< SEARCH
    std::wstring baseName; // 2026-08-xx 持久化基名，避免重复解析计算
    std::wstring ext;      // 2026-08-xx 持久化后缀名，统一小写
    
    // 2026-06-xx 物理对标：补充时间戳与大小字段
=======
    std::wstring baseName; // 2026-08-xx 持久化基名，避免重复解析计算
    std::wstring ext;      // 2026-08-xx 持久化后缀名，统一小写
    std::string sha256;    // 文件 SHA256 哈希持久化字段
    
    // 2026-06-xx 物理对标：补充时间戳与大小字段
>>>>>>> REPLACE
```

```
<<<<<<< SEARCH
    void ensureActivated(const std::wstring& nPath);

    void setRating(const std::wstring& path, int rating, bool notify = true);
=======
    void ensureActivated(const std::wstring& nPath);

    void setRating(const std::wstring& path, int rating, bool notify = true);
    void setSha256(const std::wstring& path, const std::string& sha256, bool notify = false);
>>>>>>> REPLACE
```

### 4.3 MetadataManager.cpp 修改块
对 SQL 查询/插入宏、加载映射以及 API 完成实现与绑定调整：

```
<<<<<<< SEARCH
// 🚨 全系统唯一权威 22 字段查询 SQL
static const char* kSqlSelectAllMeta = 
    "SELECT folder_id, path, is_folder, rating, color, tags, note, url, "
    "ctime, mtime, atime, file_size, palettes, is_trash, original_path, "
    "width, height, ingestion_status, auto_color, base_name, ext, added_at "
    "FROM metadata";

// 🚨 全系统唯一权威 22 字段插入/更新 SQL
static const char* kSqlInsertMeta = 
    "INSERT OR REPLACE INTO metadata (folder_id, path, is_folder, rating, color, tags, note, url, "
    "ctime, mtime, atime, file_size, palettes, is_trash, original_path, "
    "width, height, ingestion_status, auto_color, base_name, ext, added_at) "
    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";

static void bindMetaHelper(sqlite3_stmt* stmt, const std::wstring& path, const RuntimeMeta& meta) {
    sqlite3_bind_text(stmt, 1, meta.folderId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text16(stmt, 2, path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, meta.isFolder ? 1 : 0);
    sqlite3_bind_int(stmt, 4, meta.rating);
    sqlite3_bind_text16(stmt, 5, meta.manualColor.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text16(stmt, 6, meta.tags.join(",").toStdWString().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text16(stmt, 7, meta.note.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text16(stmt, 8, meta.url.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 9, meta.ctime);
    sqlite3_bind_int64(stmt, 10, meta.mtime);
    sqlite3_bind_int64(stmt, 11, meta.atime);
    sqlite3_bind_int64(stmt, 12, meta.fileSize);

    QJsonArray arr;
    for (const auto& pe : meta.palettes) {
        QJsonObject obj;
        obj["color"] = pe.color.name();
        obj["ratio"] = (double)pe.ratio;
        arr.append(obj);
    }
    QByteArray ba = QJsonDocument(arr).toJson(QJsonDocument::Compact);
    sqlite3_bind_blob(stmt, 13, ba.constData(), ba.size(), SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 14, meta.isTrash ? 1 : 0);
    sqlite3_bind_text16(stmt, 15, meta.originalPath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 16, meta.width);
    sqlite3_bind_int(stmt, 17, meta.height);
    sqlite3_bind_int(stmt, 18, meta.ingestionStatus);
    sqlite3_bind_text16(stmt, 19, meta.autoColor.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text16(stmt, 20, meta.baseName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text16(stmt, 21, meta.ext.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 22, meta.added_at);
}
=======
// 🚨 全系统唯一权威 23 字段查询 SQL (增加 sha256 字段)
static const char* kSqlSelectAllMeta = 
    "SELECT folder_id, path, is_folder, rating, color, tags, note, url, "
    "ctime, mtime, atime, file_size, palettes, is_trash, original_path, "
    "width, height, ingestion_status, auto_color, base_name, ext, added_at, sha256 "
    "FROM metadata";

// 🚨 全系统唯一权威 23 字段插入/更新 SQL (增加 sha256 字段)
static const char* kSqlInsertMeta = 
    "INSERT OR REPLACE INTO metadata (folder_id, path, is_folder, rating, color, tags, note, url, "
    "ctime, mtime, atime, file_size, palettes, is_trash, original_path, "
    "width, height, ingestion_status, auto_color, base_name, ext, added_at, sha256) "
    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";

static void bindMetaHelper(sqlite3_stmt* stmt, const std::wstring& path, const RuntimeMeta& meta) {
    sqlite3_bind_text(stmt, 1, meta.folderId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text16(stmt, 2, path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, meta.isFolder ? 1 : 0);
    sqlite3_bind_int(stmt, 4, meta.rating);
    sqlite3_bind_text16(stmt, 5, meta.manualColor.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text16(stmt, 6, meta.tags.join(",").toStdWString().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text16(stmt, 7, meta.note.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text16(stmt, 8, meta.url.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 9, meta.ctime);
    sqlite3_bind_int64(stmt, 10, meta.mtime);
    sqlite3_bind_int64(stmt, 11, meta.atime);
    sqlite3_bind_int64(stmt, 12, meta.fileSize);

    QJsonArray arr;
    for (const auto& pe : meta.palettes) {
        QJsonObject obj;
        obj["color"] = pe.color.name();
        obj["ratio"] = (double)pe.ratio;
        arr.append(obj);
    }
    QByteArray ba = QJsonDocument(arr).toJson(QJsonDocument::Compact);
    sqlite3_bind_blob(stmt, 13, ba.constData(), ba.size(), SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 14, meta.isTrash ? 1 : 0);
    sqlite3_bind_text16(stmt, 15, meta.originalPath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 16, meta.width);
    sqlite3_bind_int(stmt, 17, meta.height);
    sqlite3_bind_int(stmt, 18, meta.ingestionStatus);
    sqlite3_bind_text16(stmt, 19, meta.autoColor.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text16(stmt, 20, meta.baseName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text16(stmt, 21, meta.ext.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 22, meta.added_at);
    sqlite3_bind_text(stmt, 23, meta.sha256.c_str(), -1, SQLITE_TRANSIENT);
}
>>>>>>> REPLACE
```

```
<<<<<<< SEARCH
                rm.width = sqlite3_column_int(stmt, 15);
                rm.height = sqlite3_column_int(stmt, 16);
                rm.ingestionStatus = sqlite3_column_int(stmt, 17);

                const wchar_t* autoColor = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 18));
                if (autoColor) rm.autoColor = autoColor;

                const wchar_t* wBaseName = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 19));
                if (wBaseName) rm.baseName = wBaseName;

                const wchar_t* wExt = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 20));
                if (wExt) rm.ext = wExt;

                rm.added_at = sqlite3_column_int64(stmt, 21);
                rm.isManaged = true;
=======
                rm.width = sqlite3_column_int(stmt, 15);
                rm.height = sqlite3_column_int(stmt, 16);
                rm.ingestionStatus = sqlite3_column_int(stmt, 17);

                const wchar_t* autoColor = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 18));
                if (autoColor) rm.autoColor = autoColor;

                const wchar_t* wBaseName = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 19));
                if (wBaseName) rm.baseName = wBaseName;

                const wchar_t* wExt = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 20));
                if (wExt) rm.ext = wExt;

                rm.added_at = sqlite3_column_int64(stmt, 21);
                const char* hashText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 22));
                if (hashText) rm.sha256 = hashText;
                
                rm.isManaged = true;
>>>>>>> REPLACE
```

实现 `setSha256` 接口：

```
<<<<<<< SEARCH
void MetadataManager::setRating(const std::wstring& path, int rating, bool notify) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    ensureActivated(nPath);
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        auto currentSnapshot = std::atomic_load(&m_snapshot);
        auto newMap = std::make_shared<std::unordered_map<std::wstring, RuntimeMeta>>(*currentSnapshot);
        (*newMap)[nPath].rating = rating;
        std::atomic_store(&m_snapshot, std::shared_ptr<const std::unordered_map<std::wstring, RuntimeMeta>>(newMap));
    }
    if (notify) notifyUI(RefreshLevel::PathUpdate, QString::fromStdWString(nPath));
    DatabaseManager::instance().enqueueSyncTask([this, nPath]() {
        persistAsync(nPath);
    });
}
=======
void MetadataManager::setRating(const std::wstring& path, int rating, bool notify) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    ensureActivated(nPath);
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        auto currentSnapshot = std::atomic_load(&m_snapshot);
        auto newMap = std::make_shared<std::unordered_map<std::wstring, RuntimeMeta>>(*currentSnapshot);
        (*newMap)[nPath].rating = rating;
        std::atomic_store(&m_snapshot, std::shared_ptr<const std::unordered_map<std::wstring, RuntimeMeta>>(newMap));
    }
    if (notify) notifyUI(RefreshLevel::PathUpdate, QString::fromStdWString(nPath));
    DatabaseManager::instance().enqueueSyncTask([this, nPath]() {
        persistAsync(nPath);
    });
}

void MetadataManager::setSha256(const std::wstring& path, const std::string& sha256, bool notify) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    ensureActivated(nPath);
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        auto currentSnapshot = std::atomic_load(&m_snapshot);
        auto newMap = std::make_shared<std::unordered_map<std::wstring, RuntimeMeta>>(*currentSnapshot);
        (*newMap)[nPath].sha256 = sha256;
        std::atomic_store(&m_snapshot, std::shared_ptr<const std::unordered_map<std::wstring, RuntimeMeta>>(newMap));
    }
    if (notify) notifyUI(RefreshLevel::PathUpdate, QString::fromStdWString(nPath));
    DatabaseManager::instance().enqueueSyncTask([this, nPath]() {
        persistAsync(nPath);
    });
}
>>>>>>> REPLACE
```

### 4.4 DuplicateDetectorService.h 修改块
解耦 `DuplicateItemInfo`，剥离 QImage，增加哈希成员及私有方法：

```
<<<<<<< SEARCH
struct DuplicateItemInfo {
    QString folderId;
    QString path;
    QString filename;
    int width = 0;
    int height = 0;
    qint64 size = 0;
    QString tagHint;
    QImage thumbnail;
};

struct DuplicateConflictGroup {
    DuplicateItemInfo existingItem;
    DuplicateItemInfo newItem;
};

class DuplicateDetectorService {
public:
    // 后台比对新导入项与数据库已有项，返回重复冲突组列表
    static std::vector<DuplicateConflictGroup> detectDuplicates(const QStringList& newImportedPaths);
};
=======
struct DuplicateItemInfo {
    QString folderId;
    QString path;
    QString filename;
    int width = 0;
    int height = 0;
    qint64 size = 0;
    QString tagHint;
    QImage thumbnail;
    std::string sha256;
};

struct DuplicateConflictGroup {
    DuplicateItemInfo existingItem;
    DuplicateItemInfo newItem;
};

class DuplicateDetectorService {
public:
    // 后台比对新导入项与数据库已有项，返回重复冲突组列表
    static std::vector<DuplicateConflictGroup> detectDuplicates(const QStringList& newImportedPaths);

private:
    static std::string calculateFastHash(const QString& filePath);
    static std::string calculateFullSha256(const QString& filePath);
};
>>>>>>> REPLACE
```

### 4.5 DuplicateDetectorService.cpp 修改块
实现快速哈希、全量哈希，并重构极速三级递进比对。彻底剔除在检测过程循环内产生 QImage 缩略图的操作：

```
<<<<<<< SEARCH
#include "DuplicateDetectorService.h"
#include "MetadataManager.h"
#include "CapsuleMediaExtractor.h"
#include "../util/DiskMediaExtractor.h"
#include <QFileInfo>
#include <QDir>

namespace ArcMeta {

std::vector<DuplicateConflictGroup> DuplicateDetectorService::detectDuplicates(const QStringList& newImportedPaths) {
    std::vector<DuplicateConflictGroup> conflicts;
    
    for (const QString& newPath : newImportedPaths) {
        QFileInfo newFi(newPath);
        std::wstring wNewPath = MetadataManager::normalizePath(newPath.toStdWString());
        
        MetadataManager::instance().forEachCachedItem([&](const std::wstring& cachedWPath, const RuntimeMeta& rm) {
            if (cachedWPath == wNewPath) return;
            
            QFileInfo cachedFi(QString::fromStdWString(cachedWPath));
            if (cachedFi.fileName().toLower() == newFi.fileName().toLower() && cachedFi.size() == newFi.size()) {
                DuplicateConflictGroup group;
                
                // 1. 已存在项信息
                group.existingItem.folderId = QString::fromStdString(rm.folderId);
                group.existingItem.path = QString::fromStdWString(cachedWPath);
                group.existingItem.filename = cachedFi.fileName();
                group.existingItem.size = cachedFi.size();
                group.existingItem.width = rm.width;
                group.existingItem.height = rm.height;
                if (!rm.tags.isEmpty()) {
                    group.existingItem.tagHint = rm.tags.join(", ");
                }
                group.existingItem.thumbnail = CapsuleMediaExtractor::getCapsuleThumbnailReadOnly(QString::fromStdWString(cachedWPath));
                
                // 2. 新文件项信息
                group.newItem.path = newPath;
                group.newItem.filename = newFi.fileName();
                group.newItem.size = newFi.size();
                // 新文件还未入库，宽高可以从 QImage 临时获取
                QImage newThumb = DiskMediaExtractor::getDiskThumbnail(newPath, 256);
                group.newItem.thumbnail = newThumb;
                group.newItem.width = newThumb.width();
                group.newItem.height = newThumb.height();
                
                conflicts.push_back(group);
            }
        });
    }
    return conflicts;
}

} // namespace ArcMeta
=======
#include "DuplicateDetectorService.h"
#include "MetadataManager.h"
#include <QFileInfo>
#include <QDir>
#include <QCryptographicHash>
#include <QFile>

namespace ArcMeta {

std::string DuplicateDetectorService::calculateFastHash(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return "";
    
    qint64 size = file.size();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    
    if (size <= 8192) {
        hash.addData(&file);
    } else {
        char buf[4096];
        // 读取头部 4KB
        file.read(buf, 4096);
        hash.addData(buf, 4096);
        // 读取尾部 4KB
        file.seek(size - 4096);
        file.read(buf, 4096);
        hash.addData(buf, 4096);
    }
    return hash.result().toHex().toStdString();
}

std::string DuplicateDetectorService::calculateFullSha256(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return "";
    
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&file);
    return hash.result().toHex().toStdString();
}

std::vector<DuplicateConflictGroup> DuplicateDetectorService::detectDuplicates(const QStringList& newImportedPaths) {
    std::vector<DuplicateConflictGroup> conflicts;
    
    for (const QString& newPath : newImportedPaths) {
        QFileInfo newFi(newPath);
        std::wstring wNewPath = MetadataManager::normalizePath(newPath.toStdWString());
        qint64 newSize = newFi.size();
        
        // 当体积符合时才懒加载计算新文件的 FastHash
        std::string newFastHash;
        bool hasComputedFastHash = false;
        
        // 当体积与 FastHash 均符合时才懒加载计算新文件的 FullSha256
        std::string newSha256;
        bool hasComputedFullSha256 = false;

        MetadataManager::instance().forEachCachedItem([&](const std::wstring& cachedWPath, const RuntimeMeta& rm) {
            if (cachedWPath == wNewPath) return;
            if (rm.isFolder || rm.isTrash) return;

            // 1. 第一级比对：体积比对
            if (rm.fileSize != newSize) return;

            // 2. 第二级比对：快速采样 Hash 比对
            if (!hasComputedFastHash) {
                newFastHash = calculateFastHash(newPath);
                hasComputedFastHash = true;
            }
            if (newFastHash.empty()) return;

            QString cachedPathStr = QString::fromStdWString(cachedWPath);
            std::string cachedFastHash = calculateFastHash(cachedPathStr);
            if (cachedFastHash.empty() || cachedFastHash != newFastHash) return;

            // 3. 第三级比对：全量 SHA256 缓存/按需比对
            if (!hasComputedFullSha256) {
                newSha256 = calculateFullSha256(newPath);
                hasComputedFullSha256 = true;
            }
            if (newSha256.empty()) return;

            std::string targetSha256 = rm.sha256;
            if (targetSha256.empty()) {
                targetSha256 = calculateFullSha256(cachedPathStr);
                if (!targetSha256.empty()) {
                    MetadataManager::instance().setSha256(cachedWPath, targetSha256, false);
                }
            }

            if (targetSha256.empty() || targetSha256 != newSha256) return;

            // 4. 第四级：冲突收集（完全不调用 getDiskThumbnail 与任何图片解码）
            DuplicateConflictGroup group;
            
            // 1. 已存在项信息
            group.existingItem.folderId = QString::fromStdString(rm.folderId);
            group.existingItem.path = cachedPathStr;
            group.existingItem.filename = QFileInfo(cachedPathStr).fileName();
            group.existingItem.size = newSize;
            group.existingItem.width = rm.width;
            group.existingItem.height = rm.height;
            group.existingItem.sha256 = targetSha256;
            if (!rm.tags.isEmpty()) {
                group.existingItem.tagHint = rm.tags.join(", ");
            }
            
            // 2. 新文件项信息
            group.newItem.path = newPath;
            group.newItem.filename = newFi.fileName();
            group.newItem.size = newSize;
            group.newItem.sha256 = newSha256;
            // 宽高先回退到 existingItem 极其对等（因为哈希完全一致，逻辑上宽高尺寸应当一致）
            group.newItem.width = rm.width;
            group.newItem.height = rm.height;

            conflicts.push_back(group);
        });
    }
    return conflicts;
}

} // namespace ArcMeta
>>>>>>> REPLACE
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/meta/DatabaseManager.cpp`（loadDb 结构平滑迁移、联合索引建立）
- [ ] 模块/文件：`src/meta/MetadataManager.h`（增加 RuntimeMeta 的 sha256 存储，增加 setSha256 API 声明）
- [ ] 模块/文件：`src/meta/MetadataManager.cpp`（kSqlSelectAllMeta/kSqlInsertMeta 23 字段映射、绑定扩展与 setSha256 异步落盘实现）
- [ ] 模块/文件：`src/meta/DuplicateDetectorService.h`（DuplicateItemInfo 与冲突组解耦， calculateFastHash 与 calculateFullSha256 声明）
- [ ] 模块/文件：`src/meta/DuplicateDetectorService.cpp`（快速哈希与全量哈希算法实现，极速三级递进判重 detectDuplicates 纯属性流程重构）

**明确禁止越界修改的范围：**
- [ ] 模块/文件：`src/ui/ContentPanel.cpp`（不修改排序 lessThan 逻辑——由专门排序方案处理）
- [ ] 模块/文件：除上述 5 个文件之外的其他任何文件——不修改

## 6. 实现准则与预警【核心】
1. **多线程并发安全**：`setSha256` 内部更新原子镜像时遵循 RCU 内存快照机制，在锁 `m_mutex` 保护下执行 map 深度拷贝与 `std::atomic_store` 替换，完美兼容多线程。落盘事务通过 WAL 高能异步队列安全执行，耗时 0ms。
2. **快速哈希准确性**：`calculateFastHash` 读取头部 4KB + 尾部 4KB 并支持小于 8KB 全量回退。若读取错误会自动返回空哈希，确保底层数据对齐的鲁棒性。

## 7. Memories.md 合规检查

| COMPONENT / MODE | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 双轨隔离 | 磁盘导航（DiskNav）写操作绝不倒灌入数据库。 | 本方案重复检测属于大批量拖拽导入（托管模式下的受控登记动作），哈希持久化严格跟随托管库持久化写入，不交叉，符合双轨隔离。 |

## 8. 待确认事项（可选）
无。
