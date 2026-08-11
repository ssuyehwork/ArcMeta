开始执行 **任务 1：底层数据库 SQL 字段错位修复与路径标签清洗**。

施工图纸如下，请交付执行者严格照此在 `MetadataManager.cpp` 中实施。

---

# 任务 1 施工图纸：`MetadataManager.cpp` 重构

### 步骤 1：定义全局统一 SQL 表达式常量（`MetadataManager.cpp` 顶部）

在 `MetadataManager.cpp` 文件顶部的 helper 区域（第 50 行左右），添加以下绝对统一的 22 字段 SQL 声明：

```cpp
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
```

---

### 步骤 2：统一参数绑定静态函数 `bindMetaHelper`

在 `MetadataManager.cpp` 顶部添加统一参数绑定闭包，避免在各个函数中重写拼写错位：

```cpp
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
```

---

### 步骤 3：重构 `MetadataManager::initFromScchMode`（引入 Data Sanitizer 清洗垃圾路径标签）

修改 `initFromScchMode` 函数中的 `loadFromDb` 逻辑：

1. 将 `const char* sql = "SELECT * FROM metadata";` **完全替换为** `kSqlSelectAllMeta`。
2. 修正反序列化下标，并**加入标签防线清洗**：

```cpp
auto loadFromDb = [&](sqlite3* db) {
    if (!db) return;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, kSqlSelectAllMeta, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            RuntimeMeta rm;
            const char* fid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (fid) rm.folderId = fid;

            const wchar_t* wpath = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 1));
            std::wstring path = normalizePath(wpath ? wpath : L"");

            rm.isFolder = sqlite3_column_int(stmt, 2) != 0;
            rm.rating = sqlite3_column_int(stmt, 3);
            
            const wchar_t* color = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 4));
            if (color) rm.manualColor = color;

            // 🚨 Column 5: 标签字段提取与物理路径数据清洗 (Data Sanitizer)
            const wchar_t* wtags = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 5));
            QString tagsStr = wtags ? QString::fromWCharArray(wtags) : "";
            QStringList rawTags = tagsStr.split(",", Qt::SkipEmptyParts);
            QStringList cleanTags;
            bool isDirtyData = false;

            for (const QString& t : rawTags) {
                QString trimmed = t.trimmed();
                // 物理清洗拦截：过滤包含盘符冒号(:)、路径斜杠(\ or /) 或 .arc 胶囊后缀的错误路径数据
                if (trimmed.contains(":\\") || trimmed.contains(":/") || trimmed.contains(".arc", Qt::CaseInsensitive)) {
                    isDirtyData = true;
                    continue; // 强行丢弃垃圾标签
                }
                cleanTags.append(trimmed);
            }
            rm.tags = cleanTags;

            // Column 6 ~ 21 严格按 kSqlSelectAllMeta 顺序列提取
            const wchar_t* note = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 6));
            if (note) rm.note = note;
            
            const wchar_t* url = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 7));
            if (url) rm.url = url;

            rm.ctime = sqlite3_column_int64(stmt, 8);
            rm.mtime = sqlite3_column_int64(stmt, 9);
            rm.atime = sqlite3_column_int64(stmt, 10);
            rm.fileSize = sqlite3_column_int64(stmt, 11);

            const void* paletteBlob = sqlite3_column_blob(stmt, 12);
            int paletteSize = sqlite3_column_bytes(stmt, 12);
            if (paletteBlob && paletteSize > 0) {
                QByteArray ba(reinterpret_cast<const char*>(paletteBlob), paletteSize);
                QJsonDocument doc = QJsonDocument::fromJson(ba);
                QJsonArray arr = doc.array();
                for (const auto& v : arr) {
                    QJsonObject obj = v.toObject();
                    PaletteEntry pe;
                    pe.color = QColor(obj["color"].toString());
                    pe.ratio = (float)obj["ratio"].toDouble();
                    rm.palettes.push_back(pe);
                }
            }

            rm.isTrash = sqlite3_column_int(stmt, 13) != 0;
            const wchar_t* wOrigPath = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 14));
            if (wOrigPath) rm.originalPath = wOrigPath;

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

            tempCache[path] = rm;
            if (!rm.folderId.empty()) tempFidToPath[rm.folderId] = path;

            // 若检测到历史污染数据，自动回写更正 SQLite 数据库
            if (isDirtyData) {
                sqlite3_stmt* fixStmt = nullptr;
                if (sqlite3_prepare_v2(db, "UPDATE metadata SET tags = ? WHERE folder_id = ?", -1, &fixStmt, nullptr) == SQLITE_OK) {
                    sqlite3_bind_text16(fixStmt, 1, cleanTags.join(",").toStdWString().c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(fixStmt, 2, fid, -1, SQLITE_TRANSIENT);
                    sqlite3_step(fixStmt);
                    sqlite3_finalize(fixStmt);
                }
            }

            // 维护树级索引...
            std::wstring parentPath = QDir::toNativeSeparators(QFileInfo(QString::fromStdWString(path)).absolutePath()).toStdWString();
            parentPath = normalizePath(parentPath);
            if (parentPath != path) {
                tempParentToChildren[parentPath].push_back(path);
            }
        }
        sqlite3_finalize(stmt);
    }
};
```

---

### 步骤 4：重构 `registerAsset` / `persistAsync` / `persistBatchAsync`

在 `MetadataManager.cpp` 中将上述三个函数的 `INSERT` 语句全部替换为 `kSqlInsertMeta`，并使用 `bindMetaHelper` 进行统一绑定：

1. **`registerAsset` 重构**：
   将内部手写的 20 参数 `INSERT` 语句替换为 `kSqlInsertMeta`，并在准备句柄后统一调用 `bindMetaHelper(stmtMeta, nPath, rm)`。

2. **`persistAsync` 与 `persistBatchAsync` 重构**：
   将 `sql` 变量替换为 `kSqlInsertMeta`，并在准备句柄后统一调用 `bindMetaHelper(memStmt, nPath, rMeta)`。

---

执行者完成上述 `MetadataManager.cpp` 的修改后，全系统的标签数据错位将被彻底解决。请在修改完成后告诉我，以便我们继续执行 **任务 2**。