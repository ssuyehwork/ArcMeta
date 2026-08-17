#include <QFileInfo>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QtConcurrent>
#include <QThreadPool>
#include <QDir>
#include <QDirIterator>
#include <QDebug>
#include <QTimer>
#include <QDateTime>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QImageReader>
#include <QSvgRenderer>
#ifdef Q_OS_WIN
#include <objbase.h>
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "MetadataManager.h"
#include "MetadataDefs.h"
#include "DatabaseManager.h"
#include "PhysicalDataExtractor.h"
#include "IngestionProgressEngine.h"
#include "../core/AppConfig.h"
#include "../meta/CategoryRepo.h"
#include "../ui/MediaColorExtractor.h"
#include "StatisticsService.h"
#include "../core/VolumeOnlineManager.h"
#include "../ui/UiHelper.h"
#include "MediaExtractorPipeline.h"
#include "../util/ShellHelper.h"
#include "sqlite3.h"
#include "AmMetaJson.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

#include <windows.h>
#include <objbase.h>
#include <fileapi.h>
#include <winbase.h>
#include <handleapi.h>
#include <winnt.h>
#include <sddl.h>


#include <cstdio>
#include <cwchar>
#include <sstream>
#include <iomanip>
#include <mutex>
#include <shared_mutex>
#include <algorithm>

namespace ArcMeta {

// 🚨 全系统唯一权威 23 字段查询 SQL
static const char* kSqlSelectAllMeta = 
    "SELECT folder_id, path, is_folder, rating, color, tags, note, url, "
    "ctime, mtime, atime, file_size, palettes, is_trash, original_path, "
    "width, height, ingestion_status, auto_color, base_name, ext, added_at, sha256 "
    "FROM metadata";

// 🚨 全系统唯一权威 23 字段插入/更新 SQL
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

// --- Helper Functions ---

// 统一资产判定静态函数，物理上不管是文件夹还是文件，只要以 .arc 结尾在内存语义中均为原子资产
static bool isManagedAsset(bool isFolder, const std::wstring& path) {
    return !isFolder || (path.size() >= 4 && path.compare(path.size() - 4, 4, L".arc") == 0);
}

// 🚨 内存数据库模式唯一ID体系重构：路径级 Base36 ID 静态提取解析器
static std::string extractBase36Id(const std::wstring& path) {
    // 查找 ".arc" 容器扩展名在路径中的位置
    size_t pos = path.find(L".arc");
    if (pos == std::wstring::npos) return "";

    // 向上查找紧邻 ".arc" 前方的路径分隔符以界定容器名称
    size_t lastSep = path.rfind(L'\\', pos);
    if (lastSep == std::wstring::npos) {
        lastSep = path.rfind(L'/', pos);
    }

    size_t start = (lastSep == std::wstring::npos) ? 0 : lastSep + 1;
    std::wstring folderName = path.substr(start, pos - start);

    // 托管资产容器文件夹名格式恒为 13 位 Base36 字符串 (如 00ms73182x000)
    if (folderName.length() == 13) {
        return std::string(folderName.begin(), folderName.end());
    }
    return "";
}

std::wstring MetadataManager::normalizePath(const std::wstring& path) {
    if (path.empty()) return L"";
    // 2026-06-xx 物理对账优化：Windows 环境下路径不区分大小写，
    // 统一转换为全小写以确保内存缓存 (std::unordered_map) 的 Key 匹配一致性，彻底消除“幽灵项”。
    QString qp = QDir::toNativeSeparators(QDir::cleanPath(QString::fromStdWString(path))).toLower();
    if (qp.length() == 2 && qp.endsWith(':')) qp += '\\';
    return qp.toStdWString();
}

std::string MetadataManager::generateFallbackFolderId(const std::wstring& vol, const std::wstring& frn) {
    if (vol.empty() || frn.empty()) return "";
    std::string result = "FRN:";
    result.append(QString::fromStdWString(vol).toUpper().toStdString());
    result.append(":");
    result.append(QString::fromStdWString(frn).toUpper().toStdString());
    return result;
}

std::string MetadataManager::generateDeterministicFolderId(const std::wstring& path) {
    if (path.empty()) return "";
    std::wstring nPath = MetadataManager::normalizePath(path);
    std::wstring vol = MetadataManager::getVolumeSerialNumber(nPath);
    
    std::wstring seedW(vol);
    seedW.append(L":");
    seedW.append(nPath);

    QByteArray seed = QString::fromStdWString(seedW).toUtf8();
    QByteArray hash = QCryptographicHash::hash(seed, QCryptographicHash::Sha256);
    
    std::string result = "PATHURL:";
    result.append(hash.left(16).toHex().toUpper().toStdString());
    return result;
}

std::wstring MetadataManager::generateDeterministicFrn(const std::wstring& path) {
    if (path.empty()) return L"VIRTUAL_EMPTY";
    QByteArray hash = QCryptographicHash::hash(QString::fromStdWString(path).toUtf8(), QCryptographicHash::Sha256);
    return QString(hash.left(8).toHex().toUpper()).toStdWString();
}

// --- MetadataManager Implementation ---

MetadataManager& MetadataManager::instance() {
    static MetadataManager inst;
    return inst;
}

MetadataManager::MetadataManager(QObject* parent) : QObject(parent) {
    // [RCU 内存快照初始化]：分配空快照，防空指针异常
    // m_shards auto initialized
    m_uiSignalTimer = new QTimer(this);
    m_uiSignalTimer->setInterval(200); // 200ms 时间窗口
    m_uiSignalTimer->setSingleShot(true);

    connect(m_uiSignalTimer, &QTimer::timeout, [this]() {
        std::vector<QString> paths;
        bool hasReloadAll = false;
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            for (const auto& p : m_pendingUiPaths) {
                if (p == "__RELOAD_ALL__") {
                    hasReloadAll = true;
                }
                paths.push_back(p);
            }
            m_pendingUiPaths.clear();
        }

        if (hasReloadAll || paths.size() > 50) {
            emit metaChanged("__RELOAD_ALL__");
        } else {
            for (const auto& p : paths) {
                emit metaChanged(p);
            }
        }
    });

    // 2026-06-xx 物理加固：监听程序退出信号
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, [this]() {
        // 2026-06-xx 物理切换：强制刷新 SQLite 到磁盘
        DatabaseManager::instance().shutdown();
        AppConfig::instance().setValue("System/LastCleanShutdown", true);
        AppConfig::instance().sync();
    });
}


void MetadataManager::initFromDatabase() {
    // 2026-06-xx 物理加固：防止重复初始化
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        if (m_loaded) return;
    }

    DatabaseManager::instance().init();
    
    std::unordered_map<std::wstring, RuntimeMeta> tempCache;
    std::unordered_map<std::string, std::wstring> tempFidToPath;
    std::unordered_map<std::wstring, std::vector<std::wstring>> tempParentToChildren;
    std::unordered_map<std::wstring, double> tempFolderProgressCache;

    auto loadFromDb = [&](sqlite3* db) {
        if (!db) return;
        sqlite3_stmt* stmt = nullptr;

        // 1. 读取主元数据表
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

                const char* hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 22));
                if (hash) rm.sha256 = hash;

                rm.isManaged = true;

                tempCache[path] = rm;
                if (!rm.folderId.empty()) tempFidToPath[rm.folderId] = path;

                // 若检测到历史污染数据，仅在内存中清洗，不进行数据库写回，保证开库过程纯只读

                // 维护树级索引...
                std::wstring parentPath = QDir::toNativeSeparators(QFileInfo(QString::fromStdWString(path)).absolutePath()).toStdWString();
                parentPath = normalizePath(parentPath);
                if (parentPath != path) {
                    tempParentToChildren[parentPath].push_back(path);
                }
            }
            sqlite3_finalize(stmt);
        }

        // 读取该库的 category_items 表，将关联关系一次性填入 RuntimeMeta 内存对象中
        const char* itemsSql = "SELECT folder_id, category_id FROM category_items WHERE category_id > 0";
        sqlite3_stmt* stmtItems = nullptr;
        if (sqlite3_prepare_v2(db, itemsSql, -1, &stmtItems, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmtItems) == SQLITE_ROW) {
                const char* fid = reinterpret_cast<const char*>(sqlite3_column_text(stmtItems, 0));
                int catId = sqlite3_column_int(stmtItems, 1);
                if (fid) {
                    std::string sFid(fid);
                    if (tempFidToPath.count(sFid)) {
                        auto& catVector = tempCache[tempFidToPath[sFid]].categoryIds;
                        if (std::find(catVector.begin(), catVector.end(), catId) == catVector.end()) {
                            catVector.push_back(catId);
                        }
                    }
                }
            }
            sqlite3_finalize(stmtItems);
        }

        // 2. Plan-124: 加载进度缓存 (正确的闭包内部位置)
        const char* statsSql = "SELECT key, value FROM system_stats WHERE key LIKE 'PROGRESS:%'";
        if (sqlite3_prepare_v2(db, statsSql, -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const char* key = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                double val = sqlite3_column_double(stmt, 1);
                if (key) {
                    std::string sKey(key);
                    if (sKey.find("PROGRESS:") == 0) {
                        std::wstring fPath = normalizePath(QString::fromUtf8(key + 9).toStdWString());
                        tempFolderProgressCache[fPath] = val;
                    }
                }
            }
            sqlite3_finalize(stmt);
        }
    }; // 🚨 正确：loadFromDb 闭包在这里统一结束！

    // 0. 加载全局库 (盘符置顶等全局元数据)
    loadFromDb(DatabaseManager::instance().getGlobalDb());

    // 1. 扫描所有已加载的数据库
    QString metaDir = QCoreApplication::applicationDirPath() + "/.arcmeta";
    QDir dir(metaDir);
    if (dir.exists()) {
        QStringList dbFiles = dir.entryList({"Arcmeta_*.db"}, QDir::Files | QDir::Hidden | QDir::System);

        QRegularExpression re("^Arcmeta_([0-9A-F]{8})(?:_([A-Z]))?\\.db$", QRegularExpression::CaseInsensitiveOption);
        std::set<std::wstring> loadedSerials;

        QMap<std::wstring, QString> serialToLetter;
        const auto drives = QDir::drives();
        for (const QFileInfo& d : drives) {
            std::wstring s = getVolumeSerialNumber(d.absolutePath().toStdWString());
            if (s != L"UNKNOWN") {
                serialToLetter[s] = d.absolutePath().at(0).toUpper();
            }
        }

        for (const QString& dbFile : dbFiles) {
            QRegularExpressionMatch match = re.match(dbFile);
            if (match.hasMatch()) {
                QString volSerialStr = match.captured(1).toUpper();
                std::wstring wSerial = volSerialStr.toStdWString();
                
                if (loadedSerials.find(wSerial) == loadedSerials.end()) {
                    QString currentLetter = serialToLetter.value(wSerial, "");
                    loadFromDb(DatabaseManager::instance().getDriveDb(wSerial, currentLetter));
                    loadedSerials.insert(wSerial);
                }
            }
        }
    }

    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_folderIdToPath = tempFidToPath;
        m_parentToChildren = tempParentToChildren;
        m_folderProgressCache = tempFolderProgressCache;

        for (auto& entry : m_parentToChildren) {
            std::sort(entry.second.begin(), entry.second.end());
            entry.second.erase(std::unique(entry.second.begin(), entry.second.end()), entry.second.end());
        }

        std::vector<std::wstring> dirtySvgPaths;
        for (const auto& pair : tempCache) {
            const RuntimeMeta& meta = pair.second;
            if (!meta.baseName.empty()) {
                if (meta.isFolder) {
                    auto& v = m_subFolderNameToFolderIds[meta.baseName];
                    if (std::find(v.begin(), v.end(), meta.folderId) == v.end()) v.push_back(meta.folderId);
                } else {
                    auto& v = m_assetNameToFolderIds[meta.baseName];
                    if (std::find(v.begin(), v.end(), meta.folderId) == v.end()) v.push_back(meta.folderId);
                    if (!meta.ext.empty()) {
                        auto& ve = m_extensionToFolderIds[meta.ext];
                        if (std::find(ve.begin(), ve.end(), meta.folderId) == ve.end()) ve.push_back(meta.folderId);
                    }
                }
            }

        }

        m_loaded = true;
        for (const auto& pair : tempCache) {
            size_t idx = getShardIndex(pair.first);
            std::unique_lock<std::shared_mutex> shardLock(m_shards[idx].mutex);
            m_shards[idx].items[pair.first] = pair.second;
        }
    }

    notifyUI(RefreshLevel::FullRebuild);
}

void MetadataManager::notifyUI(RefreshLevel level, const QString& path) {
    switch (level) {
        case RefreshLevel::CountsOnly:
            notifyCategoryCountChanged();
            break;
        case RefreshLevel::PathUpdate:
            if (!path.isEmpty()) {
                {
                    std::unique_lock<std::shared_mutex> lock(m_mutex);
                    m_pendingUiPaths.insert(path);
                }
                QMetaObject::invokeMethod(this, "triggerUiSignalTimer", Qt::QueuedConnection);
            }
            break;
        case RefreshLevel::FullRebuild:
            notifyFullUIRebuild();
            break;
        case RefreshLevel::CategoryOnly:
            if (m_isInternalOperating) return;
            {
                std::unique_lock<std::shared_mutex> lock(m_mutex);
                m_pendingUiPaths.insert("__RELOAD_CATEGORY_ONLY__");
            }
            QMetaObject::invokeMethod(this, "triggerUiSignalTimer", Qt::QueuedConnection);
            break;
    }
}

void MetadataManager::notifyCategoryCountChanged() {
    if (m_isInternalOperating) return; // 2026-xx-xx 按照 Plan-105：操作期间拦截冗余刷新信号
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_pendingUiPaths.insert("__RELOAD_COUNT__");
    }
    QMetaObject::invokeMethod(this, "triggerUiSignalTimer", Qt::QueuedConnection);
}

void MetadataManager::notifyFullUIRebuild() {
    if (m_isInternalOperating) return; // 2026-xx-xx 按照 Plan-105：操作期间拦截冗余刷新信号
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_pendingUiPaths.insert("__RELOAD_ALL__");
    }
    QMetaObject::invokeMethod(this, "triggerUiSignalTimer", Qt::QueuedConnection);
}

// 🚨 SSOT 重构核心：单一权威资产入库登记管线
bool MetadataManager::registerAsset(const std::string& initialFolderId, const std::wstring& assetPath, int targetCatId) { 
    std::wstring nPath = normalizePath(assetPath); 
    sqlite3* db = DatabaseManager::instance().getDbForPath(nPath); 
    if (!db) return false; 
 
    std::string folderId = initialFolderId;
    long long nowMsecs = QDateTime::currentMSecsSinceEpoch(); 
 
    // 1. 拆分主文件名与后缀 
    std::wstring baseName, ext; 
    parsePathComponents(nPath, false, baseName, ext); 
 
    // 准备 RuntimeMeta 对象用于参数绑定与后续内存缓存更新
    RuntimeMeta rm; 
    rm.isFolder = false; // 强契约：资产恒为非目录 
    QFileInfo fi(QString::fromStdWString(nPath));
    rm.fileSize = fi.size(); 
    rm.ctime = fi.exists() ? fi.birthTime().toMSecsSinceEpoch() : nowMsecs; 
    rm.mtime = fi.exists() ? fi.lastModified().toMSecsSinceEpoch() : nowMsecs; 
    rm.atime = fi.exists() ? fi.lastRead().toMSecsSinceEpoch() : nowMsecs; 
    rm.added_at = nowMsecs; 
    rm.baseName = baseName; 
    rm.ext = ext; 
    rm.isManaged = true;

    // 🚀 【乐观生成 + UNIQUE 碰撞捕获】：0 次预检 SELECT，碰撞时仅捕获 SQLITE_CONSTRAINT 重试
    int attempts = 0;
    const int maxAttempts = 5;
    bool success = false;

    while (attempts++ < maxAttempts) {
        SqlTransaction trans(db); 
        rm.folderId = folderId;

        // 2. 写入数据库 metadata 表 (绝对绑定内部主文件路径，使用全局 kSqlInsertMeta 声明) 
        sqlite3_stmt* stmtMeta = nullptr; 
        int rc = SQLITE_ERROR;
        if (sqlite3_prepare_v2(db, kSqlInsertMeta, -1, &stmtMeta, nullptr) == SQLITE_OK) { 
            bindMetaHelper(stmtMeta, nPath, rm);
            rc = sqlite3_step(stmtMeta); 
            sqlite3_finalize(stmtMeta); 
        } 

        if (rc == SQLITE_DONE) {
            // 3. 若指定了有效用户分类，写入 category_items 表 
            if (targetCatId > 0) { 
                const char* sqlItems = "INSERT OR REPLACE INTO category_items (category_id, folder_id, path_hint, added_at) VALUES (?, ?, ?, ?)"; 
                sqlite3_stmt* stmtItems = nullptr; 
                if (sqlite3_prepare_v2(db, sqlItems, -1, &stmtItems, nullptr) == SQLITE_OK) { 
                    sqlite3_bind_int(stmtItems, 1, targetCatId); 
                    sqlite3_bind_text(stmtItems, 2, folderId.c_str(), -1, SQLITE_TRANSIENT); 
                    sqlite3_bind_text16(stmtItems, 3, nPath.c_str(), -1, SQLITE_TRANSIENT); 
                    sqlite3_bind_double(stmtItems, 4, static_cast<double>(nowMsecs)); 
                    sqlite3_step(stmtItems); 
                    sqlite3_finalize(stmtItems); 
                } 
            } 

            // 🚨 3.5：无论是否有自定义分类，入库资产都自动检测并绑定到物理托管根库分类
            CategoryRepo::bindToLibraryRootCategory(folderId, nPath);

            if (trans.commit()) {
                success = true;
                break;
            }
        }

        // 🚀 仅在触发 UNIQUE 约束碰撞（SQLITE_CONSTRAINT）时，重新生成 ID 并重试
        if (rc == SQLITE_CONSTRAINT || rc == SQLITE_CONSTRAINT_PRIMARYKEY || rc == 19) {
            folderId = ShellHelper::generateBase36Id().toStdString();
        } else {
            // 其他常规数据库错误直接退出
            break;
        }
    }

    if (!success) return false;
 
    // 4. 同步更新内存缓存 RuntimeMeta (SSOT 规则) 
    { 
        // 补齐分类 ID 到内存对象中，防止内存中分类列表为空
        if (targetCatId > 0) {
            rm.categoryIds.push_back(targetCatId);
        }
        std::unique_lock<std::shared_mutex> lock(m_mutex); 
        size_t idx = getShardIndex(nPath);
        {
            std::unique_lock<std::shared_mutex> shardLock(m_shards[idx].mutex);
            m_shards[idx].items[nPath] = rm;
        }
        m_folderIdToPath[folderId] = nPath; 
    } 
 
    // 实时通知统计服务增量更新 
    StatisticsService::instance().notifyAssetAdded(targetCatId, false); 
 
    // 6. 激活后台提取流水线解析分辨率与调色盘 
    ensureActivated(nPath); 
    updateIngestionStatus(nPath, 0); 
    registerItemsAsync({QString::fromStdWString(nPath)}, true); 
 
    notifyCategoryCountChanged();

    // 1. 同步刷新分类仓储内存快照
    CategoryRepo::refreshMemoryCache();

    // 2. 触发统计服务异步全量重算账本，推动侧边栏数字刷新
    StatisticsService::instance().requestFullRecountAsync();

    notifyUI(RefreshLevel::FullRebuild); 
    return true; 
}

// 🚨 SSOT 重构核心：跨盘托管库胶囊物理迁移（跨盘 1:1 重锚定）
std::string MetadataManager::migrateCapsuleToLibrary(const std::string& assetId, const QString& targetLibraryPath) { 
    std::wstring currentPath = getPathByFolderId(assetId); 
    if (currentPath.empty()) return ""; 
 
    QFileInfo fileInfo(QString::fromStdWString(currentPath)); 
    QDir containerDir = fileInfo.dir(); // 获取 00ms73182x000.arc 胶囊文件夹 
    QString containerName = containerDir.dirName(); 
 
    // 生成全新唯一ID，保证ID唯一性（即便数据完全一致，但ID不同。对应用户原话：“复制整套数据到了之后，只需要修改其ID即可，这样就能保证ID的唯一性”）
    std::string newAssetId = ShellHelper::generateBase36Id().toStdString();
    QString targetContainerDir = targetLibraryPath + "/" + QString::fromStdString(newAssetId) + ".arc"; 
 
    // 1. 物理复制整套胶囊包内部的文件
    if (!QDir().mkpath(targetContainerDir)) {
        qWarning() << "[CapsuleMigration] Failed to create dir:" << targetContainerDir;
        return "";
    }
    QDir srcDir(containerDir.absolutePath());
    QStringList files = srcDir.entryList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QString& file : files) {
        if (!QFile::copy(srcDir.absoluteFilePath(file), targetContainerDir + "/" + file)) {
            qWarning() << "[CapsuleMigration] Failed to copy:" << file << "to" << targetContainerDir;
            if (!QDir(targetContainerDir).removeRecursively()) {
                qCritical() << "[CapsuleMigration] Rollback failed! Corrupted folder leftover:" << targetContainerDir;
            }
            return "";
        }
    }
 
    // 2. 计算迁移/复制后的主资产新路径 
    QString newMainFilePath = targetContainerDir + "/" + fileInfo.fileName(); 
    std::wstring wNewPath = normalizePath(newMainFilePath.toStdWString()); 
 
    // 3. 获取旧的元数据并进行移植（不从旧库清除，保留整套原始数据与ID，以实现复制式归入）
    RuntimeMeta oldMeta = getMeta(currentPath); 
 
    // 4. 将旧的元数据移植并写入到新库，只修改其ID
    oldMeta.folderId = newAssetId; 
    oldMeta.isManaged = true; 
    { 
        std::unique_lock<std::shared_mutex> lock(m_mutex); 
        size_t idx = getShardIndex(wNewPath);
        {
            std::unique_lock<std::shared_mutex> shardLock(m_shards[idx].mutex);
            m_shards[idx].items[wNewPath] = oldMeta;
        }
        m_folderIdToPath[newAssetId] = wNewPath; 
    } 
 
    // 5. 异步落盘到新库并通知 UI 刷新 
    persistAsync(wNewPath, true, true); 
 
    return newAssetId; 
}

void MetadataManager::registerItem(const std::wstring& path, bool authorized) {
    (void)authorized;
    std::wstring nPath = normalizePath(path);

    // [Plan-131 方案 C + Plan-53 降级自愈安全防护] 物理指纹与高级特征双重准入机制
    std::string pFid;
    long long pSize = 0, pMtime = 0;
    if (fetchWinApiMetadataDirect(nPath, pFid, nullptr, &pSize, nullptr, nullptr, &pMtime, nullptr)) {
        size_t idx = getShardIndex(nPath);
        {
            std::shared_lock<std::shared_mutex> shardLock(m_shards[idx].mutex);
            auto it = m_shards[idx].items.find(nPath);
            if (it != m_shards[idx].items.end()) {
                // 只有当文件指纹一致、曾经被置为1，且色彩和尺寸物理属性都确切存在、非残缺时，才允许返回跳过！
                // 这杜绝了历史解析失败时留下空元数据、又因状态为 1 无法再次扫描提取的致命 Bug 
                bool metadataValid = true;
                QFileInfo info(QString::fromStdWString(nPath));
                if (info.isFile() && MediaColorExtractor::isGraphicsFile(info.suffix().toLower())) {
                    if (it->second.width <= 0 || it->second.height <= 0 || it->second.autoColor.empty()) {
                        metadataValid = false;
                    }
                }
                if (it->second.ingestionStatus == 1 && it->second.fileSize == pSize && it->second.mtime == pMtime && metadataValid) {
                    return; // 物理指纹及高级多媒体特征完备且未发生改变，安全返回
                }
            }
        }
    }

    // 1. 激活项目 (获取 FID/FRN 等物理属性)
    // 注意：ensureActivated 内部对已存在项会跳过，故此处需确保若指纹变化能更新缓存
    {
        size_t idx = getShardIndex(nPath);
        std::unique_lock<std::shared_mutex> shardLock(m_shards[idx].mutex);
        if (m_shards[idx].items.count(nPath)) {
            m_shards[idx].items[nPath].fileSize = pSize;
            m_shards[idx].items[nPath].mtime = pMtime;
        }
    }
    ensureActivated(nPath);

    // 2. 登记项目（待处理状态 0）
    updateIngestionStatus(nPath, 0);

    // 3. 投递至后台抽取流水线
    MediaExtractorPipeline::instance().enqueue(nPath);
}

void MetadataManager::markAsRegistered(const std::wstring& path) { 
    std::wstring nPath = normalizePath(path); 
     
    (void)QtConcurrent::run([this, nPath]() { 
        std::wstring volSerial = getVolumeSerialNumber(nPath); 
        QString letter = (nPath.length() >= 2 && nPath[1] == L':') ? QString::fromWCharArray(&nPath[0], 1) : ""; 
        sqlite3* db = DatabaseManager::instance().getDriveDb(volSerial, letter); 
        if (!db) return; 
 
        // 🚨 一键自动清退历史上误写入的 is_folder = 1 的 .arc 外壳垃圾记录 
        { 
            SqlTransaction cleanTrans(db); 
            sqlite3_stmt* cleanStmt; 
            if (sqlite3_prepare_v2(db, "DELETE FROM metadata WHERE is_folder = 1 AND path LIKE '%.arc'", -1, &cleanStmt, nullptr) == SQLITE_OK) { 
                sqlite3_step(cleanStmt); 
                sqlite3_finalize(cleanStmt); 
            } 
            cleanTrans.commit(); 
        } 
 
        std::vector<std::wstring> pathsToRegister; 
 
        QFileInfo info(QString::fromStdWString(nPath)); 
        if (info.isDir()) { 
            std::function<void(const QDir&)> recursiveScan = [&](const QDir& targetDir) {
                QFileInfoList entries = targetDir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
                for (const QFileInfo& entry : entries) {
                    QString fn = entry.fileName();
                    
                    // 🚨 穿透 .arc 胶囊，直接提取内部的主资产文件，绝不将 .arc 目录入库
                    if (entry.isDir() && fn.endsWith(".arc", Qt::CaseInsensitive)) {
                        QDir arcDir(entry.absoluteFilePath());
                        QFileInfoList innerFiles = arcDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
                        for (const QFileInfo& inner : innerFiles) {
                            QString innerFn = inner.fileName();
                            if (!innerFn.endsWith("_thumbnail.png", Qt::CaseInsensitive)) {
                                pathsToRegister.push_back(normalizePath(inner.absoluteFilePath().toStdWString()));
                            }
                        }
                    }
                    else if (entry.isDir()) {
                        // 递归深入普通子目录
                        recursiveScan(QDir(entry.absoluteFilePath()));
                    }
                    else if (entry.isFile() && !fn.endsWith("_thumbnail.png", Qt::CaseInsensitive)) {
                        pathsToRegister.push_back(normalizePath(entry.absoluteFilePath().toStdWString()));
                    }
                }
            };
            recursiveScan(QDir(info.absoluteFilePath()));
        } else { 
            pathsToRegister.push_back(nPath); 
        } 
 
        if (pathsToRegister.empty()) return; 
 
        std::vector<std::wstring> actualEnqueues; 
        SqlTransaction trans(db); 
        for (const auto& p : pathsToRegister) { 
            ensureActivated(p); 
            RuntimeMeta meta = getMeta(p);
            QString qp = QString::fromStdWString(p);
            QFileInfo fi(qp);

            // 🚨 缩略图缺失检测器 (Thumbnail Missing Detector)
            bool missingThumbnail = false;
            if (MediaColorExtractor::isGraphicsFile(fi.suffix().toLower())) {
                QImage cached = CapsuleMediaExtractor::getCapsuleThumbnailReadOnly(qp);
                if (cached.isNull()) {
                    missingThumbnail = true;
                }
            }

            // 🚨 增量准入准则：只有在已解析完成 (ingestionStatus == 1)、物理修改时间与大小未变，且缩略图未缺失时，才跳过
            if (!missingThumbnail && meta.ingestionStatus == 1 && meta.mtime == fi.lastModified().toMSecsSinceEpoch() && meta.fileSize == fi.size()) {
                continue;
            }
            updateIngestionStatus(p, 0); 
            actualEnqueues.push_back(p); 
        } 
         
        if (trans.commit() && !actualEnqueues.empty()) { 
            MediaExtractorPipeline::instance().enqueueBatch(actualEnqueues); 
        } 
    }); 
} 

void MetadataManager::markAsIngested(const std::wstring& path) {
    updateIngestionStatus(path, 1);
}

void MetadataManager::updateIngestionStatus(const std::wstring& path, int newStatus) {
    std::wstring nPath = normalizePath(path);
    bool changed = false;
    {
        size_t idx = getShardIndex(nPath);
        std::unique_lock<std::shared_mutex> shardLock(m_shards[idx].mutex);
        auto it = m_shards[idx].items.find(nPath);
        if (it != m_shards[idx].items.end()) {
            if (it->second.ingestionStatus != newStatus) {
                it->second.ingestionStatus = newStatus;
                changed = true;
            }
        }
    }

    if (changed) {
        persistAsync(nPath, false, true);

        // 异步更新父目录进度，避免阻塞
        std::wstring parentPath = QDir::toNativeSeparators(QFileInfo(QString::fromStdWString(nPath)).absolutePath()).toStdWString();
        if (!parentPath.empty() && isInsideManagedLibrary(parentPath)) {
            QThreadPool::globalInstance()->start([this, parentPath]() {
                calculateAndPersistProgress(parentPath);
            });
        }
    }
}

void MetadataManager::calculateAndPersistProgress(const std::wstring& folderPath) {
    std::wstring nFolder = normalizePath(folderPath);
    
    // 1. 获取库归属数据库
    std::wstring volSerial = getVolumeSerialNumber(nFolder);
    QString letter = (nFolder.length() >= 2 && nFolder[1] == L':') ? QString::fromWCharArray(&nFolder[0], 1) : "";
    sqlite3* db = DatabaseManager::instance().getDriveDb(volSerial, letter);
    if (!db) {
        return;
    }

    // 互斥锁定该物理分库递归句柄，解决高并发下在同一个 sqlite3 连接中冲突导致的死锁，确保重入安全
    auto dbLock = DatabaseManager::instance().getDriveMutex(volSerial);
    std::lock_guard<std::recursive_mutex> lockConn(*dbLock);

    // 2. 统计状态（严禁物理读盘，仅使用数据库标记）
    // 进度 = (该目录下状态为 1 的项目数) / (该目录下状态为 0 和 1 的项目总数)
    int count0 = 0;
    int count1 = 0;

    sqlite3_stmt* stmt;
    const char* sql = "SELECT ingestion_status, COUNT(*) FROM metadata WHERE path LIKE ? GROUP BY ingestion_status";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        std::wstring pattern = nFolder;
        if (pattern.back() != L'\\' && pattern.back() != L'/') pattern += L'\\';
        pattern += L"%";

        sqlite3_bind_text16(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int status = sqlite3_column_int(stmt, 0);
            int count = sqlite3_column_int(stmt, 1);
            if (status == 0) count0 = count;
            else if (status == 1) count1 = count;
        }
        sqlite3_finalize(stmt);
    }

    double progress = 0.0;
    if (count0 + count1 > 0) {
        progress = (double)count1 / (count0 + count1);
    }

    // 3. 持久化进度到 system_stats 表
    const char* upsertSql = "INSERT OR REPLACE INTO system_stats (key, value) VALUES (?, ?)";
    if (sqlite3_prepare_v2(db, upsertSql, -1, &stmt, nullptr) == SQLITE_OK) {
        std::string key = "PROGRESS:" + QString::fromStdWString(nFolder).toUtf8().toStdString();
        sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 2, progress);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
        }
        sqlite3_finalize(stmt);
    }

    // Plan-124: 更新内存缓存
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_folderProgressCache[nFolder] = progress;
    }

    // 通知 UI 更新
    notifyUI(RefreshLevel::PathUpdate, QString::fromStdWString(nFolder));
}

double MetadataManager::getProgressFromDb(const std::wstring& folderPath) {
    std::wstring nFolder = normalizePath(folderPath);
    
    // Plan-124: 优先从内存缓存获取
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        auto it = m_folderProgressCache.find(nFolder);
        if (it != m_folderProgressCache.end()) return it->second;
    }

    std::wstring volSerial = getVolumeSerialNumber(nFolder);
    QString letter = (nFolder.length() >= 2 && nFolder[1] == L':') ? QString::fromWCharArray(&nFolder[0], 1) : "";
    sqlite3* db = DatabaseManager::instance().getDriveDb(volSerial, letter);
    if (!db) return -1.0;

    double progress = -1.0;
    sqlite3_stmt* stmt;
    const char* sql = "SELECT value FROM system_stats WHERE key = ?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        std::string key = "PROGRESS:" + QString::fromStdWString(nFolder).toUtf8().toStdString();
        sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            progress = sqlite3_column_double(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    // 回填缓存
    if (progress >= 0) {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_folderProgressCache[nFolder] = progress;
    }

    return progress;
}

bool MetadataManager::hasChildrenInCache(const std::wstring& folderPath) {
    std::wstring nFolder = normalizePath(folderPath);
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    auto it = m_parentToChildren.find(nFolder);
    return it != m_parentToChildren.end() && !it->second.empty();
}

std::vector<std::pair<std::wstring, RuntimeMeta>> MetadataManager::getChildrenFromCache(const std::wstring& folderPath) {
    std::wstring nFolder = normalizePath(folderPath);
    std::vector<std::pair<std::wstring, RuntimeMeta>> results;

    std::shared_lock<std::shared_mutex> lock(m_mutex);
    auto it = m_parentToChildren.find(nFolder);
    if (it != m_parentToChildren.end()) {
        results.reserve(it->second.size());
        for (const auto& childPath : it->second) {
            size_t idx = getShardIndex(childPath);
            std::shared_lock<std::shared_mutex> shardLock(m_shards[idx].mutex);
            auto itMeta = m_shards[idx].items.find(childPath);
            if (itMeta != m_shards[idx].items.end()) {
                results.push_back({childPath, itMeta->second});
            }
        }
    }
    return results;
}

void MetadataManager::registerItemsAsync(const QStringList& paths, bool authorized) {
    if (paths.isEmpty()) return;
    (void)authorized;
    
    (void)QtConcurrent::run([this, paths]() {
        std::vector<std::wstring> stdPaths;
        for (const auto& qp : paths) {
            std::wstring nPath = normalizePath(qp.toStdWString());
            ensureActivated(nPath);
            updateIngestionStatus(nPath, 0);
            stdPaths.push_back(nPath);
        }
        MediaExtractorPipeline::instance().enqueueBatch(stdPaths);
    });
}

RuntimeMeta MetadataManager::getMeta(const std::wstring& path) {
    std::wstring nPath = normalizePath(path);
    size_t idx = getShardIndex(nPath);
    std::shared_lock<std::shared_mutex> lock(m_shards[idx].mutex);
    auto it = m_shards[idx].items.find(nPath);
    if (it != m_shards[idx].items.end()) {
        return it->second;
    }
    return RuntimeMeta();
}

std::wstring MetadataManager::getPathByFolderId(const std::string& fid) {
    if (fid.empty()) return L"";
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    auto it = m_folderIdToPath.find(fid);
    return (it != m_folderIdToPath.end()) ? it->second : L"";
}

void MetadataManager::ensureActivated(const std::wstring& nPath) {
    // 1. 读锁检查 (快速路径)
    {
        size_t idx = getShardIndex(nPath);
        std::shared_lock<std::shared_mutex> shardLock(m_shards[idx].mutex);
        if (m_shards[idx].items.find(nPath) != m_shards[idx].items.end()) return;
    }

    // 2. 锁外同步获取物理属性 (耗时 I/O 操作)
    // 2026-07-xx 按照 Plan-88：杜绝在 unique_lock 期间执行 Win32 API 访问
    RuntimeMeta rm;
    std::wstring frn;
    std::wstring type;
    
    // 自愈与健壮性改造：若 Win32 原生 API 失败（如 Linux Sandbox、共享访问冲突等），提供 QFileInfo 完美兜底，确保激活成功 
    bool success = fetchWinApiMetadataDirect(nPath, rm.folderId, &frn, &rm.fileSize, &type, &rm.ctime, &rm.mtime, &rm.atime);
    if (!success) {
        QFileInfo qinfo(QString::fromStdWString(nPath));
        if (qinfo.exists()) {
            rm.fileSize = qinfo.size();
            rm.isFolder = qinfo.isDir();
            rm.ctime = qinfo.birthTime().toMSecsSinceEpoch();
            rm.mtime = qinfo.lastModified().toMSecsSinceEpoch();
            rm.atime = qinfo.lastRead().toMSecsSinceEpoch();
            rm.folderId = generateDeterministicFolderId(nPath);
            success = true;
        }
    } else {
        rm.isFolder = (type == L"folder");
    }

    if (success) {
        // 3. 写锁写入缓存
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        size_t idx = getShardIndex(nPath);
        {
            std::unique_lock<std::shared_mutex> shardLock(m_shards[idx].mutex);
            if (m_shards[idx].items.count(nPath)) return; // 二次检查防止竞态
        }

        // 🚨 内存数据库模式唯一ID体系重构：激活写入内存缓存前，将主键统一覆盖为 13 位 Base36 ID
        std::string base36 = extractBase36Id(nPath);
        if (!base36.empty()) {
            rm.folderId = base36;
        }

        // 共享元数据逻辑 (FID 关联)
        if (!rm.folderId.empty() && m_folderIdToPath.count(rm.folderId)) {
            std::wstring existPath = m_folderIdToPath[rm.folderId];
            size_t existIdx = getShardIndex(existPath);
            std::shared_lock<std::shared_mutex> shardLock(m_shards[existIdx].mutex);
            auto existingIt = m_shards[existIdx].items.find(existPath);
            if (existingIt != m_shards[existIdx].items.end()) {
                const RuntimeMeta& existing = existingIt->second;
                rm.rating    = existing.rating;
                rm.manualColor = existing.manualColor;
                rm.autoColor = existing.autoColor;
                rm.tags      = existing.tags;
                rm.note      = existing.note;
                rm.url       = existing.url;
                rm.width     = existing.width;
                rm.height    = existing.height;
                rm.palettes  = existing.palettes;
                rm.isManaged = existing.isManaged;
            }
        }

        {
            std::unique_lock<std::shared_mutex> shardLock(m_shards[idx].mutex);
            m_shards[idx].items[nPath] = rm;
        }
        if (!rm.folderId.empty()) {
            m_folderIdToPath[rm.folderId] = nPath;

            // Plan-124: 维护树级索引
            std::wstring parentPath = QDir::toNativeSeparators(QFileInfo(QString::fromStdWString(nPath)).absolutePath()).toStdWString();
            parentPath = normalizePath(parentPath);
            if (parentPath != nPath) {
                auto& children = m_parentToChildren[parentPath];
                if (std::find(children.begin(), children.end(), nPath) == children.end()) {
                    children.push_back(nPath);
                }
            }

            // 索引同步逻辑
            std::wstring name, ext;
            parsePathComponents(nPath, rm.isFolder, name, ext);
            if (!name.empty()) {
                if (rm.isFolder) {
                    auto& v = m_subFolderNameToFolderIds[name];
                    if (std::find(v.begin(), v.end(), rm.folderId) == v.end()) v.push_back(rm.folderId);
                } else {
                    auto& v = m_assetNameToFolderIds[name];
                    if (std::find(v.begin(), v.end(), rm.folderId) == v.end()) v.push_back(rm.folderId);
                    if (!ext.empty()) {
                        auto& ve = m_extensionToFolderIds[ext];
                        if (std::find(ve.begin(), ve.end(), rm.folderId) == ve.end()) ve.push_back(rm.folderId);
                    }
                }
            }
        }
    }
}


void MetadataManager::setRating(const std::wstring& path, int rating, bool notify) {
    std::wstring nPath = normalizePath(path);
    ensureActivated(nPath);
    size_t idx = getShardIndex(nPath);
    {
        std::unique_lock<std::shared_mutex> lock(m_shards[idx].mutex);
        m_shards[idx].items[nPath].rating = rating;
    }
    if (notify) {
        notifyUI(RefreshLevel::PathUpdate, QString::fromStdWString(nPath));
    }
    if (isInsideManagedLibrary(nPath)) {
        DatabaseManager::instance().enqueueSyncTask([this, nPath]() {
            persistAsync(nPath);
        });
    }
}

void MetadataManager::setSha256(const std::wstring& path, const std::string& sha256, bool notify) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    ensureActivated(nPath);
    size_t idx = getShardIndex(nPath);
    {
        std::unique_lock<std::shared_mutex> shardLock(m_shards[idx].mutex);
        m_shards[idx].items[nPath].sha256 = sha256;
    }
    if (notify) notifyUI(RefreshLevel::PathUpdate, QString::fromStdWString(nPath));
    if (isInsideManagedLibrary(nPath)) {
        DatabaseManager::instance().enqueueSyncTask([this, nPath]() {
            persistAsync(nPath);
        });
    }
}

void MetadataManager::updateExtractedMediaFeaturesBatch(const std::vector<ExtractedFeatureItem>& items) {
    if (items.empty()) return;

    std::unordered_map<sqlite3*, std::vector<ExtractedFeatureItem>> dbGroupMap;
    for (const auto& item : items) {
        std::wstring nPath = normalizePath(item.path);
        {
            size_t idx = getShardIndex(nPath);
            std::unique_lock<std::shared_mutex> shardLock(m_shards[idx].mutex);
            if (m_shards[idx].items.count(nPath)) {
                RuntimeMeta& meta = m_shards[idx].items[nPath];
                meta.width = item.width;
                meta.height = item.height;
                if (item.mtime > 0) meta.mtime = item.mtime;
                if (item.fileSize > 0) meta.fileSize = item.fileSize;
                meta.autoColor = item.autoColor;
                meta.ingestionStatus = item.ingestionStatus;
                meta.palettes.clear();
                for (const auto& p : item.palettes) {
                    meta.palettes.emplace_back(p.first, p.second);
                }
            }
        }

        sqlite3* db = DatabaseManager::instance().getDbForPath(nPath);
        if (db) {
            dbGroupMap[db].push_back(item);
        }
    }

    for (auto& pair : dbGroupMap) {
        sqlite3* db = pair.first;
        auto itemList = pair.second;
        DatabaseManager::instance().enqueueSyncTask([db, itemList]() {
            SqlTransaction trans(db);
            const char* sql = "UPDATE metadata SET width = ?, height = ?, auto_color = ?, palettes = ?, ingestion_status = ?, mtime = ?, file_size = ? WHERE path = ?";
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                for (const auto& it : itemList) {
                    sqlite3_bind_int(stmt, 1, it.width);
                    sqlite3_bind_int(stmt, 2, it.height);
                    
                    QString qColor = QString::fromStdWString(it.autoColor);
                    sqlite3_bind_text16(stmt, 3, qColor.utf16(), -1, SQLITE_TRANSIENT);
                    
                    QJsonArray arr;
                    for (const auto& pe : it.palettes) {
                        QJsonObject obj; obj["color"] = pe.first.name(); obj["ratio"] = (double)pe.second; arr.append(obj);
                    }
                    QByteArray ba = QJsonDocument(arr).toJson(QJsonDocument::Compact);
                    sqlite3_bind_blob(stmt, 4, ba.constData(), ba.size(), SQLITE_TRANSIENT);
                    sqlite3_bind_int(stmt, 5, it.ingestionStatus);
                    sqlite3_bind_int64(stmt, 6, it.mtime);
                    sqlite3_bind_int64(stmt, 7, it.fileSize);

                    QString qPath = QString::fromStdWString(it.path);
                    sqlite3_bind_text16(stmt, 8, qPath.utf16(), -1, SQLITE_TRANSIENT);

                    sqlite3_step(stmt);
                    sqlite3_reset(stmt);
                }
                sqlite3_finalize(stmt);
            }
            trans.commit();
        });
    }
}

void MetadataManager::updateExtractedMediaFeatures( 
    const std::wstring& path,  
    int width,  
    int height,  
    const std::wstring& autoColor,  
    const QVector<QPair<QColor, float>>& palettes,  
    int ingestionStatus)  
{ 
    std::wstring nPath = normalizePath(path); 
    RuntimeMeta metaCopy; 
    { 
        size_t idx = getShardIndex(nPath);
        std::unique_lock<std::shared_mutex> shardLock(m_shards[idx].mutex);
        if (!m_shards[idx].items.count(nPath)) return;

        RuntimeMeta& meta = m_shards[idx].items[nPath]; 
        meta.width = width; 
        meta.height = height; 
        meta.autoColor = autoColor; 
        meta.ingestionStatus = ingestionStatus; 
         
        meta.palettes.clear(); 
        for (const auto& p : palettes) { 
            meta.palettes.emplace_back(p.first, p.second); 
        } 
        metaCopy = meta; 
    } 

    // 仅发起 1 次数据库事务 
    DatabaseManager::instance().enqueueSyncTask([this, nPath, metaCopy]() { 
        sqlite3* db = DatabaseManager::instance().getDbForPath(nPath); 
        if (!db) return; 

        SqlTransaction trans(db); 
        const char* sql = "UPDATE metadata SET width = ?, height = ?, auto_color = ?, palettes = ?, ingestion_status = ? WHERE folder_id = ?"; 
        sqlite3_stmt* stmt = nullptr; 
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) { 
            sqlite3_bind_int(stmt, 1, metaCopy.width); 
            sqlite3_bind_int(stmt, 2, metaCopy.height); 
            sqlite3_bind_text16(stmt, 3, metaCopy.autoColor.c_str(), -1, SQLITE_TRANSIENT); 
             
            // 序列化调色盘 JSON 
            QJsonArray arr; 
            for (const auto& pe : metaCopy.palettes) { 
                QJsonObject obj; obj["color"] = pe.color.name(); obj["ratio"] = (double)pe.ratio; arr.append(obj); 
            } 
            QByteArray ba = QJsonDocument(arr).toJson(QJsonDocument::Compact); 
            sqlite3_bind_blob(stmt, 4, ba.constData(), ba.size(), SQLITE_TRANSIENT); 
            sqlite3_bind_int(stmt, 5, metaCopy.ingestionStatus); 
            sqlite3_bind_text(stmt, 6, metaCopy.folderId.c_str(), -1, SQLITE_TRANSIENT); 

            sqlite3_step(stmt); 
            sqlite3_finalize(stmt); 
        } 
        trans.commit(); 
    }); 

    notifyUI(RefreshLevel::PathUpdate, QString::fromStdWString(nPath)); 
}

void MetadataManager::setAddedAt(const std::wstring& path, long long addedAt, bool notify) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    ensureActivated(nPath);
    size_t idx = getShardIndex(nPath);
    {
        std::unique_lock<std::shared_mutex> shardLock(m_shards[idx].mutex);
        m_shards[idx].items[nPath].added_at = addedAt;
    }
    if (notify) notifyUI(RefreshLevel::PathUpdate, QString::fromStdWString(nPath));
    if (isInsideManagedLibrary(nPath)) {
        DatabaseManager::instance().enqueueSyncTask([this, nPath]() {
            persistAsync(nPath);
        });
    }
}

void MetadataManager::addCategoryToItemMemory(const std::wstring& path, int categoryId) {
    std::wstring nPath = normalizePath(path);
    ensureActivated(nPath);
    size_t idx = getShardIndex(nPath);
    std::unique_lock<std::shared_mutex> shardLock(m_shards[idx].mutex);
    auto& cats = m_shards[idx].items[nPath].categoryIds;
    if (std::find(cats.begin(), cats.end(), categoryId) == cats.end()) {
        cats.push_back(categoryId);
    }
}

void MetadataManager::removeCategoryFromItemMemory(const std::wstring& path, int categoryId) {
    std::wstring nPath = normalizePath(path);
    size_t idx = getShardIndex(nPath);
    std::unique_lock<std::shared_mutex> shardLock(m_shards[idx].mutex);
    auto it = m_shards[idx].items.find(nPath);
    if (it != m_shards[idx].items.end()) {
        auto& cats = it->second.categoryIds;
        cats.erase(std::remove(cats.begin(), cats.end(), categoryId), cats.end());
    }
}

void MetadataManager::clearCategoriesFromItemMemory(const std::wstring& path) {
    std::wstring nPath = normalizePath(path);
    size_t idx = getShardIndex(nPath);
    std::unique_lock<std::shared_mutex> shardLock(m_shards[idx].mutex);
    auto it = m_shards[idx].items.find(nPath);
    if (it != m_shards[idx].items.end()) {
        it->second.categoryIds.clear();
    }
}

void MetadataManager::renameTag(const QString& oldName, const QString& newName) {
    if (oldName == newName) return;
    
    std::vector<std::wstring> affectedPaths;
    for (size_t i = 0; i < NUM_SHARDS; ++i) {
        std::unique_lock<std::shared_mutex> lock(m_shards[i].mutex);
        for (auto& pair : m_shards[i].items) {
            if (pair.second.tags.contains(oldName)) {
                pair.second.tags.removeAll(oldName);
                if (!newName.isEmpty() && !pair.second.tags.contains(newName)) {
                    pair.second.tags.append(newName);
                }
                affectedPaths.push_back(pair.first);
            }
        }
    }
    
    persistBatchAsync(affectedPaths);
    notifyFullUIRebuild();
}

void MetadataManager::removeTag(const QString& tagName) {
    std::vector<std::wstring> affectedPaths;
    for (size_t i = 0; i < NUM_SHARDS; ++i) {
        std::unique_lock<std::shared_mutex> lock(m_shards[i].mutex);
        for (auto& pair : m_shards[i].items) {
            if (pair.second.tags.contains(tagName)) {
                pair.second.tags.removeAll(tagName);
                affectedPaths.push_back(pair.first);
            }
        }
    }
    
    persistBatchAsync(affectedPaths);
    notifyFullUIRebuild();
}

void MetadataManager::setColor(const std::wstring& path, const std::wstring& color, bool notify) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    ensureActivated(nPath);
    
    // 从源头上进行颜色 Hex 归一化处理
    std::wstring normColor = UiHelper::normalizeColorHex(QString::fromStdWString(color)).toStdWString();
    
    bool changed = false;
    bool isFolder = false;
    { 
        std::unique_lock<std::shared_mutex> lock(m_mutex); 
        size_t idx = getShardIndex(nPath);
        {
            std::unique_lock<std::shared_mutex> shardLock(m_shards[idx].mutex);
            auto it = m_shards[idx].items.find(nPath);
            if (it != m_shards[idx].items.end()) {
                isFolder = it->second.isFolder;
                if (it->second.manualColor != normColor) {
                    it->second.manualColor = normColor;
                    changed = true;
                }
            } else {
                m_shards[idx].items[nPath].manualColor = normColor;
                changed = true;
            }
        }
    }
    
    if (notify) notifyUI(RefreshLevel::PathUpdate, QString::fromStdWString(nPath));

    if (isInsideManagedLibrary(nPath)) {
        if (changed) {
            DatabaseManager::instance().enqueueSyncTask([this, nPath]() {
                persistAsync(nPath);
            });
            
            // 自下而上：如果改变的是文件夹，同步更新映射分类颜色
            if (isFolder) {
                if (CategoryRepo::updateCategoryColorByPath(nPath, normColor)) {
                    if (notify) notifyUI(RefreshLevel::CategoryOnly);
                }
            }
        }
    }
}

void MetadataManager::setPinned(const std::wstring& path, bool pinned, bool notify) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    ensureActivated(nPath);
    size_t idx = getShardIndex(nPath);
    {
        std::unique_lock<std::shared_mutex> shardLock(m_shards[idx].mutex);
        m_shards[idx].items[nPath].pinned = pinned;
    }
    if (notify) notifyUI(RefreshLevel::PathUpdate, QString::fromStdWString(nPath));

    if (isInsideManagedLibrary(nPath)) {
        DatabaseManager::instance().enqueueSyncTask([this, nPath]() {
            persistAsync(nPath);
        });
    }
}

void MetadataManager::setTags(const std::wstring& path, const QStringList& tags, bool notify) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    ensureActivated(nPath);

    bool oldEmpty = false;
    QStringList oldTags;
    bool isFolder = false;

    {
        size_t idx = getShardIndex(nPath);
        {
            std::unique_lock<std::shared_mutex> shardLock(m_shards[idx].mutex);
            auto it = m_shards[idx].items.find(nPath);
            if (it != m_shards[idx].items.end()) {
                oldEmpty = it->second.tags.isEmpty();
                oldTags = it->second.tags;
                isFolder = it->second.isFolder;
            }
        }
    }

    {
        size_t idx = getShardIndex(nPath);
        std::unique_lock<std::shared_mutex> shardLock(m_shards[idx].mutex);
        m_shards[idx].items[nPath].tags = tags;
    }

    if (notify) notifyUI(RefreshLevel::PathUpdate, QString::fromStdWString(nPath));

    if (isInsideManagedLibrary(nPath)) {
        DatabaseManager::instance().enqueueSyncTask([this, nPath]() {
            persistAsync(nPath);
        });
    }
}

void MetadataManager::setNote(const std::wstring& path, const std::wstring& note, bool notify) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    ensureActivated(nPath);
    size_t idx = getShardIndex(nPath);
    {
        std::unique_lock<std::shared_mutex> shardLock(m_shards[idx].mutex);
        m_shards[idx].items[nPath].note = note;
    }
    if (notify) notifyUI(RefreshLevel::PathUpdate, QString::fromStdWString(nPath));

    if (isInsideManagedLibrary(nPath)) {
        DatabaseManager::instance().enqueueSyncTask([this, nPath]() {
            persistAsync(nPath);
        });
    }
}

void MetadataManager::setURL(const std::wstring& path, const std::wstring& url, bool notify) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    ensureActivated(nPath);
    size_t idx = getShardIndex(nPath);
    {
        std::unique_lock<std::shared_mutex> shardLock(m_shards[idx].mutex);
        m_shards[idx].items[nPath].url = url;
    }
    if (notify) notifyUI(RefreshLevel::PathUpdate, QString::fromStdWString(nPath));

    if (isInsideManagedLibrary(nPath)) {
        DatabaseManager::instance().enqueueSyncTask([this, nPath]() {
            persistAsync(nPath);
        });
    }
}

void MetadataManager::setEncrypted(const std::wstring& path, bool encrypted, bool notify) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    ensureActivated(nPath);
    size_t idx = getShardIndex(nPath);
    {
        std::unique_lock<std::shared_mutex> shardLock(m_shards[idx].mutex);
        m_shards[idx].items[nPath].encrypted = encrypted;
    }
    if (notify) notifyUI(RefreshLevel::PathUpdate, QString::fromStdWString(nPath));
    if (isInsideManagedLibrary(nPath)) {
        DatabaseManager::instance().enqueueSyncTask([this, nPath]() {
            persistAsync(nPath);
        });
    }
}

void MetadataManager::setManaged(const std::wstring& path, bool managed, bool notify) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    ensureActivated(nPath);
    size_t idx = getShardIndex(nPath);
    {
        std::unique_lock<std::shared_mutex> shardLock(m_shards[idx].mutex);
        m_shards[idx].items[nPath].isManaged = managed;
    }
    if (notify) notifyUI(RefreshLevel::PathUpdate, QString::fromStdWString(nPath));
    // 2026-07-xx 逻辑校准：isManaged 是由数据库持久化驱动的标记。
    // 如果显式设为 true，则发起一次持久化以确保入库；如果是设为 false（罕见），无需特殊持久化。
    if (managed) {
        DatabaseManager::instance().enqueueSyncTask([this, nPath]() {
            persistAsync(nPath);
        });
    }
}

void MetadataManager::setPalettes(const std::wstring& path, const QVector<QPair<QColor, float>>& palettes, bool notify) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    ensureActivated(nPath);
    std::vector<PaletteEntry> entries;
    for (int i = 0; i < palettes.size(); ++i) { entries.push_back(PaletteEntry(palettes[i].first, palettes[i].second)); }
    size_t idx = getShardIndex(nPath);
    {
        std::unique_lock<std::shared_mutex> shardLock(m_shards[idx].mutex);
        m_shards[idx].items[nPath].palettes = entries;
    }
    if (notify) notifyUI(RefreshLevel::PathUpdate, QString::fromStdWString(nPath));
    if (isInsideManagedLibrary(nPath)) {
        DatabaseManager::instance().enqueueSyncTask([this, nPath]() {
            persistAsync(nPath);
        });
    }
}

void MetadataManager::setItemVisualMetadata(const std::wstring& path, const std::wstring& color, const QVector<QPair<QColor, float>>& palettes, bool notify) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    ensureActivated(nPath);
    std::vector<PaletteEntry> entries;
    for (int i = 0; i < palettes.size(); ++i) { entries.push_back(PaletteEntry(palettes[i].first, palettes[i].second)); }
    
    bool isFolder = false;
    {
        size_t idx = getShardIndex(nPath);
        std::unique_lock<std::shared_mutex> shardLock(m_shards[idx].mutex);
        RuntimeMeta& meta = m_shards[idx].items[nPath];
        meta.autoColor = color;
        meta.palettes = entries;
        isFolder = meta.isFolder;
    }
    
    // 【同步逻辑】如果是文件夹主色提取，则同步更新 categories 分类定义表中的颜色
    if (isFolder) {
        CategoryRepo::updateCategoryColorByPath(nPath, color);
    }
    
    if (notify) notifyUI(RefreshLevel::PathUpdate, QString::fromStdWString(nPath));
    if (isInsideManagedLibrary(nPath)) {
        DatabaseManager::instance().enqueueSyncTask([this, nPath]() {
            persistAsync(nPath);
        });
    }
}

void MetadataManager::setItemDimensions(const std::wstring& path, int width, int height) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    ensureActivated(nPath);
    {
        size_t idx = getShardIndex(nPath);
        std::unique_lock<std::shared_mutex> shardLock(m_shards[idx].mutex);
        RuntimeMeta& meta = m_shards[idx].items[nPath];
        meta.width = width;
        meta.height = height;
    }
    if (isInsideManagedLibrary(nPath)) {
        DatabaseManager::instance().enqueueSyncTask([this, nPath]() {
            persistAsync(nPath, false);
        });
    }
}

QVector<QColor> MetadataManager::getPalettes(const std::wstring& path) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    size_t idx = getShardIndex(nPath);
    std::shared_lock<std::shared_mutex> shardLock(m_shards[idx].mutex);
    auto it = m_shards[idx].items.find(nPath);
    if (it != m_shards[idx].items.end() && !it->second.palettes.empty()) {
        QVector<QColor> colors;
        for (const auto& entry : it->second.palettes) colors << entry.color;
        return colors;
    }
    return {};
}


void MetadataManager::renameBatchAsync(
    const std::vector<std::pair<std::wstring, std::wstring>>& rawPathPairs,
    std::function<void(int successCount)> onCompleted) 
{
    if (rawPathPairs.empty()) {
        if (onCompleted) onCompleted(0);
        return;
    }

    // 1. 全局开启信号锁，挂起并发 UI 刷新
    setInternalOperating(true);

    // 2. 将全量改名合并为【唯一 1 个后台异步线程】
    (void)QtConcurrent::run([this, rawPathPairs, onCompleted]() {
        int successCount = 0;

        // A. 路径归一化预处理（解决 Windows 斜杠/盘符大小写造成的幽灵数据）
        std::vector<std::pair<std::wstring, std::wstring>> normalizedPairs;
        normalizedPairs.reserve(rawPathPairs.size());
        for (const auto& pair : rawPathPairs) {
            std::wstring nOld = normalizePath(pair.first);
            std::wstring nNew = normalizePath(pair.second);
            if (!nOld.empty() && !nNew.empty() && nOld != nNew) {
                normalizedPairs.emplace_back(nOld, nNew);
            }
        }

        if (normalizedPairs.empty()) {
            setInternalOperating(false);
            if (onCompleted) {
                QMetaObject::invokeMethod(qApp, [onCompleted]() { onCompleted(0); }, Qt::QueuedConnection);
            }
            return;
        }

        // B. 内存分片节点批量替换
        std::vector<std::pair<std::wstring, std::wstring>> ioTasks;
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            for (const auto& pair : normalizedPairs) {
                const std::wstring& curOld = pair.first;
                const std::wstring& curNew = pair.second;

                size_t oldIdx = getShardIndex(curOld);
                RuntimeMeta meta;
                bool found = false;
                {
                    std::unique_lock<std::shared_mutex> shardLock(m_shards[oldIdx].mutex);
                    auto it = m_shards[oldIdx].items.find(curOld);
                    if (it != m_shards[oldIdx].items.end()) {
                        meta = it->second;
                        m_shards[oldIdx].items.erase(it);
                        found = true;
                    }
                }

                if (!found) continue;

                std::string fid = meta.folderId;
                bool isFolder = meta.isFolder;

                std::wstring oldName, oldExt;
                parsePathComponents(curOld, isFolder, oldName, oldExt);
                if (!oldName.empty()) {
                    if (isFolder) {
                        auto& v = m_subFolderNameToFolderIds[oldName];
                        v.erase(std::remove(v.begin(), v.end(), fid), v.end());
                        if (v.empty()) m_subFolderNameToFolderIds.erase(oldName);
                    } else {
                        auto& v = m_assetNameToFolderIds[oldName];
                        v.erase(std::remove(v.begin(), v.end(), fid), v.end());
                        if (v.empty()) m_assetNameToFolderIds.erase(oldName);
                        if (!oldExt.empty()) {
                            auto& ve = m_extensionToFolderIds[oldExt];
                            ve.erase(std::remove(ve.begin(), ve.end(), fid), ve.end());
                            if (ve.empty()) m_extensionToFolderIds.erase(oldExt);
                        }
                    }
                }

                std::wstring newName, newExt;
                parsePathComponents(curNew, isFolder, newName, newExt);
                meta.baseName = newName;
                meta.ext = newExt;

                size_t newIdx = getShardIndex(curNew);
                {
                    std::unique_lock<std::shared_mutex> shardLock(m_shards[newIdx].mutex);
                    m_shards[newIdx].items[curNew] = meta;
                }
                if (!fid.empty()) m_folderIdToPath[fid] = curNew;

                if (!newName.empty()) {
                    if (isFolder) {
                        auto& v = m_subFolderNameToFolderIds[newName];
                        if (std::find(v.begin(), v.end(), fid) == v.end()) v.push_back(fid);
                    } else {
                        auto& v = m_assetNameToFolderIds[newName];
                        if (std::find(v.begin(), v.end(), fid) == v.end()) v.push_back(fid);
                        if (!newExt.empty()) {
                            auto& ve = m_extensionToFolderIds[newExt];
                            if (std::find(ve.begin(), ve.end(), fid) == ve.end()) ve.push_back(fid);
                        }
                    }
                }

                ioTasks.push_back(pair);
                successCount++;
            }
        }

        // B2. 在锁外执行磁盘元数据文件的重命名与迁移
        for (const auto& pair : ioTasks) {
            QFileInfo oldFileInfo(QString::fromStdWString(pair.first));
            QFileInfo newFileInfo(QString::fromStdWString(pair.second));
            if (oldFileInfo.isDir()) {
                AmMetaJson::migrateFolderCache(oldFileInfo.absoluteFilePath(), newFileInfo.absoluteFilePath());
            } else {
                AmMetaJson::renameItem(oldFileInfo.absolutePath(), oldFileInfo.fileName(), newFileInfo.fileName());
            }
        }

        // C. SQLite 数据库分库批量提交大事务（彻底消除 SQLITE_BUSY 报错）
        struct DbBatchRenameTask {
            std::string fid;
            std::wstring newPath;
            std::wstring newName;
            std::wstring newExt;
        };
        std::map<sqlite3*, std::vector<DbBatchRenameTask>> groupedTasks;
        for (const auto& pair : normalizedPairs) {
            const std::wstring& curNew = pair.second;
            std::string fid;
            std::wstring newName, newExt;
            {
                size_t idx = getShardIndex(curNew);
                std::shared_lock<std::shared_mutex> shardLock(m_shards[idx].mutex);
                auto it = m_shards[idx].items.find(curNew);
                if (it != m_shards[idx].items.end()) {
                    fid = it->second.folderId;
                    newName = it->second.baseName;
                    newExt = it->second.ext;
                }
            }
            if (fid.empty()) continue;

            std::wstring volSerial = getVolumeSerialNumber(curNew);
            QString letter = (curNew.length() >= 2 && curNew[1] == L':') ? QString::fromWCharArray(&curNew[0], 1) : "";
            sqlite3* db = DatabaseManager::instance().getDriveDb(volSerial, letter);
            if (db) {
                groupedTasks[db].push_back({fid, curNew, newName, newExt});
            }
        }

        const char* updSql = "UPDATE metadata SET path = ?, base_name = ?, ext = ? WHERE folder_id = ?";
        for (auto& entry : groupedTasks) {
            sqlite3* targetDb = entry.first;
            const auto& tasks = entry.second;

            SqlTransaction trans(targetDb); // 【仅开启 1 次事务】
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(targetDb, updSql, -1, &stmt, nullptr) == SQLITE_OK) {
                for (const auto& task : tasks) {
                    sqlite3_bind_text16(stmt, 1, task.newPath.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text16(stmt, 2, task.newName.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text16(stmt, 3, task.newExt.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(stmt, 4, task.fid.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_step(stmt);
                    sqlite3_reset(stmt);
                }
                sqlite3_finalize(stmt);
            }
            trans.commit(); // 【仅提交 1 次事务】
        }

        // D. 同步更新 CategoryRepo 物理路径
        for (const auto& pair : normalizedPairs) {
            CategoryRepo::renamePhysicalCategoryPath(pair.first, pair.second);
        }

        // E. 清理操作标志位，安全回到 UI 主线程通知
        setInternalOperating(false);

        QMetaObject::invokeMethod(qApp, [this, successCount, onCompleted]() {
            notifyFullUIRebuild(); // 【仅发射 1 次全量 UI 刷新信号】
            if (onCompleted) onCompleted(successCount);
        }, Qt::QueuedConnection);
    });
}


void MetadataManager::renameItem(const std::wstring& oldPath, const std::wstring& newPath) {
    std::wstring nOld = normalizePath(oldPath);
    std::wstring nNew = normalizePath(newPath);
    if (nOld == nNew) return;

    // 2026-08-xx 按照性能优化要求：将级联更名逻辑移至后台线程，杜绝大目录重命名阻塞主线程 (Plan-128)
    (void)QtConcurrent::run([this, nOld, nNew]() {
        std::vector<std::pair<std::wstring, std::wstring>> itemsToRename;
        std::vector<std::pair<std::wstring, std::wstring>> ioTasks;
        
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            // 1. 深度收集所有子孙路径
            forEachCachedItem([&](const std::wstring& p, const RuntimeMeta&) {
                if (p == nOld) {
                    itemsToRename.push_back({p, nNew});
                } else if (p.find(nOld + L"\\") == 0 || p.find(nOld + L"/") == 0) {
                    std::wstring relative = p.substr(nOld.length());
                    itemsToRename.push_back({p, nNew + relative});
                }
            });

            if (itemsToRename.empty()) return;

            // 2. 优化：先一次性切断根级树索引关系，防止循环内 O(K^2) 的 std::remove 开销
            std::wstring rootOldParent = normalizePath(QDir::toNativeSeparators(QFileInfo(QString::fromStdWString(nOld)).absolutePath()).toStdWString());
            if (m_parentToChildren.count(rootOldParent)) {
                auto& children = m_parentToChildren[rootOldParent];
                children.erase(std::remove(children.begin(), children.end(), nOld), children.end());
                if (children.empty()) m_parentToChildren.erase(rootOldParent);
            }

            for (const auto& pair : itemsToRename) {
                const std::wstring& curOld = pair.first;
                const std::wstring& curNew = pair.second;

                size_t oldIdx = getShardIndex(curOld);
                RuntimeMeta meta;
                bool found = false;
                {
                    std::unique_lock<std::shared_mutex> shardLock(m_shards[oldIdx].mutex);
                    auto it = m_shards[oldIdx].items.find(curOld);
                    if (it != m_shards[oldIdx].items.end()) {
                        meta = it->second;
                        m_shards[oldIdx].items.erase(it);
                        found = true;
                    }
                }

                if (!found) continue;

                std::string fid = meta.folderId;
                bool isFolder = meta.isFolder;

                // [倒排索引维护]
                std::wstring oldName, oldExt;
                parsePathComponents(curOld, isFolder, oldName, oldExt);
                if (!oldName.empty()) {
                    if (isFolder) {
                        auto& v = m_subFolderNameToFolderIds[oldName];
                        v.erase(std::remove(v.begin(), v.end(), fid), v.end());
                        if (v.empty()) m_subFolderNameToFolderIds.erase(oldName);
                    } else {
                        auto& v = m_assetNameToFolderIds[oldName];
                        v.erase(std::remove(v.begin(), v.end(), fid), v.end());
                        if (v.empty()) m_assetNameToFolderIds.erase(oldName);
                        if (!oldExt.empty()) {
                            auto& ve = m_extensionToFolderIds[oldExt];
                            ve.erase(std::remove(ve.begin(), ve.end(), fid), ve.end());
                            if (ve.empty()) m_extensionToFolderIds.erase(oldExt);
                        }
                    }
                }

                // [树级索引维护] - 内部项仅移除
                if (curOld != nOld) {
                    m_parentToChildren.erase(curOld); 
                }

                // 3. 缓存迁移
                std::wstring newName, newExt;
                parsePathComponents(curNew, isFolder, newName, newExt);
                meta.baseName = newName;
                meta.ext = newExt;

                size_t newIdx = getShardIndex(curNew);
                {
                    std::unique_lock<std::shared_mutex> shardLock(m_shards[newIdx].mutex);
                    m_shards[newIdx].items[curNew] = meta;
                }
                if (!fid.empty()) m_folderIdToPath[fid] = curNew;

                // [倒排索引重建]
                if (!newName.empty()) {
                    if (isFolder) {
                        auto& v = m_subFolderNameToFolderIds[newName];
                        if (std::find(v.begin(), v.end(), fid) == v.end()) v.push_back(fid);
                    } else {
                        auto& v = m_assetNameToFolderIds[newName];
                        if (std::find(v.begin(), v.end(), fid) == v.end()) v.push_back(fid);
                        if (!newExt.empty()) {
                            auto& ve = m_extensionToFolderIds[newExt];
                            if (std::find(ve.begin(), ve.end(), fid) == ve.end()) ve.push_back(fid);
                        }
                    }
                }

                std::wstring curNewParent = normalizePath(QDir::toNativeSeparators(QFileInfo(QString::fromStdWString(curNew)).absolutePath()).toStdWString());
                if (curNewParent != curNew) {
                    auto& children = m_parentToChildren[curNewParent];
                    if (std::find(children.begin(), children.end(), curNew) == children.end()) {
                        children.push_back(curNew);
                    }
                }

                // [进度缓存迁移]
                if (isFolder && m_folderProgressCache.count(curOld)) {
                    double prog = m_folderProgressCache[curOld];
                    m_folderProgressCache.erase(curOld);
                    m_folderProgressCache[curNew] = prog;
                }

                ioTasks.push_back(pair);
            }
        }

        // 在全局锁外安全执行磁盘 JSON 重命名与迁移
        for (const auto& pair : ioTasks) {
            QFileInfo oldFileInfo(QString::fromStdWString(pair.first));
            QFileInfo newFileInfo(QString::fromStdWString(pair.second));
            if (oldFileInfo.isDir()) {
                AmMetaJson::migrateFolderCache(oldFileInfo.absoluteFilePath(), newFileInfo.absoluteFilePath());
            } else {
                AmMetaJson::renameItem(oldFileInfo.absolutePath(), oldFileInfo.fileName(), newFileInfo.fileName());
            }
        }

        // 4. 物理数据库批量同步 (Plan-128: 引入事务保护)
        // 极致优化：预取根路径的卷信息，避免在循环中重复执行耗时的 Win32 磁盘查询
        std::wstring volSerial = getVolumeSerialNumber(nNew);
        QString letter = (nNew.length() >= 2 && nNew[1] == L':') ? QString::fromWCharArray(&nNew[0], 1) : "";
        sqlite3* memDb = DatabaseManager::instance().getDriveDb(volSerial, letter);
        
        struct DbItemRenameTask {
            std::string fid;
            std::wstring newPath;
            std::wstring newName;
            std::wstring newExt;
        };
        std::map<sqlite3*, std::vector<DbItemRenameTask>> groupedSyncTasks;
        for (const auto& pair : itemsToRename) {
            const std::wstring& curNew = pair.second;
            std::string fid;
            std::wstring newName, newExt;
            {
                size_t idx = getShardIndex(curNew);
                std::shared_lock<std::shared_mutex> shardLock(m_shards[idx].mutex);
                auto it = m_shards[idx].items.find(curNew);
                if (it != m_shards[idx].items.end()) {
                    fid = it->second.folderId;
                    newName = it->second.baseName;
                    newExt = it->second.ext;
                }
            }
            if (fid.empty()) continue;

            if (memDb) {
                groupedSyncTasks[memDb].push_back({fid, curNew, newName, newExt});
            }
        }

        const char* updSql = "UPDATE metadata SET path = ?, base_name = ?, ext = ? WHERE folder_id = ?";
        for (auto& entry : groupedSyncTasks) {
            sqlite3* targetDb = entry.first;
            auto& tasks = entry.second;

            // [Plan-131 方案 A] 直连磁盘模式，无需重复异步分发
            SqlTransaction trans(targetDb);
            sqlite3_stmt* memStmt;
            if (sqlite3_prepare_v2(targetDb, updSql, -1, &memStmt, nullptr) == SQLITE_OK) {
                for (const auto& task : tasks) {
                    sqlite3_bind_text16(memStmt, 1, task.newPath.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text16(memStmt, 2, task.newName.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text16(memStmt, 3, task.newExt.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(memStmt, 4, task.fid.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_step(memStmt);
                    sqlite3_reset(memStmt);
                }
                sqlite3_finalize(memStmt);
            }
            trans.commit();
        }

        notifyFullUIRebuild();
    });
}

void MetadataManager::syncAfterMove(const std::wstring& oldPath, const std::wstring& newPath) {
    std::wstring nOld = normalizePath(oldPath);
    std::wstring nNew = normalizePath(newPath);
    if (nOld == nNew) return;

    bool wasManaged = isInsideManagedLibrary(nOld);
    bool isNowManaged = isInsideManagedLibrary(nNew);

    if (wasManaged && isNowManaged) {
        // 库内移动（含跨托管子文件夹）：仅路径变化，元数据整体保留
        renameItem(nOld, nNew);
    } else if (wasManaged && !isNowManaged) {
        // 移出资源库：等同于永久删除，彻底清除元数据
        removeMetadataSync(nOld);
        notifyFullUIRebuild();
    } else if (!wasManaged && isNowManaged) {
        // 移入资源库：走登记流水线，触发媒体特征提取
        markAsRegistered(nNew);
    }
    // 库外移到库外：与托管数据无关，不做任何处理
}

void MetadataManager::removeMetadataSync(const std::wstring& path) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    std::wstring volSerial = getVolumeSerialNumber(nPath);
    QString letter = (nPath.length() >= 2 && nPath[1] == L':') ? QString::fromWCharArray(&nPath[0], 1) : "";
    sqlite3* db = DatabaseManager::instance().getDriveDb(volSerial, letter);
    
    int totalDelta = 0;
    std::vector<std::string> fids;
    
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);

        // 1. 优化：先从父级索引中一次性移除根路径，避免循环内 O(K^2)
        std::wstring rootParent = normalizePath(QDir::toNativeSeparators(QFileInfo(QString::fromStdWString(nPath)).absolutePath()).toStdWString());
        if (m_parentToChildren.count(rootParent)) {
            auto& children = m_parentToChildren[rootParent];
            children.erase(std::remove(children.begin(), children.end(), nPath), children.end());
            if (children.empty()) m_parentToChildren.erase(rootParent);
        }

        for (size_t i = 0; i < NUM_SHARDS; ++i) {
            std::unique_lock<std::shared_mutex> shardLock(m_shards[i].mutex);
            for (auto it = m_shards[i].items.begin(); it != m_shards[i].items.end(); ) {
                if (it->first == nPath || it->first.find(nPath + L"\\") == 0 || it->first.find(nPath + L"/") == 0) {
                    std::wstring curPath = it->first;

                    if (isManagedAsset(it->second.isFolder, curPath)) {
                        totalDelta--;
                        QString driveLetter = VolumeOnlineManager::extractDriveLetter(QString::fromStdWString(curPath));
                        int libCatId = CategoryRepo::getLibraryCategoryIdByDrive(driveLetter);
                        StatisticsService::instance().purgeAsset(libCatId, it->second.categoryIds, !it->second.tags.isEmpty(), it->second.isTrash); 
                    }
                    if (!it->second.folderId.empty()) {
                        std::string fid = it->second.folderId;
                        bool isFolder = it->second.isFolder;
                        fids.push_back(fid);
                        m_folderIdToPath.erase(fid);

                        std::wstring name, ext;
                        parsePathComponents(curPath, isFolder, name, ext);
                        if (!name.empty()) {
                            if (isFolder) {
                                auto& v = m_subFolderNameToFolderIds[name];
                                v.erase(std::remove(v.begin(), v.end(), fid), v.end());
                                if (v.empty()) m_subFolderNameToFolderIds.erase(name);
                            } else {
                                auto& v = m_assetNameToFolderIds[name];
                                v.erase(std::remove(v.begin(), v.end(), fid), v.end());
                                if (v.empty()) m_assetNameToFolderIds.erase(name);
                                if (!ext.empty()) {
                                    auto& ve = m_extensionToFolderIds[ext];
                                    ve.erase(std::remove(ve.begin(), ve.end(), fid), ve.end());
                                    if (ve.empty()) m_extensionToFolderIds.erase(ext);
                                }
                            }
                        }

                        m_parentToChildren.erase(curPath);
                        m_folderProgressCache.erase(curPath);
                    }
                    it = m_shards[i].items.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }

    // 2026-06-xx 物理级根除：基于 File ID (FRN) 批量清理
    if (db && !fids.empty()) {
        const char* sql = "DELETE FROM metadata WHERE folder_id = ?";
        // [Plan-131 方案 A] 直连模式，取消冗余异步任务
        SqlTransaction trans(db);
        sqlite3_stmt* memStmt;
        if (sqlite3_prepare_v2(db, sql, -1, &memStmt, nullptr) == SQLITE_OK) {
            for (const auto& fid : fids) {
                sqlite3_bind_text(memStmt, 1, fid.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(memStmt);
                sqlite3_reset(memStmt);
            }
            sqlite3_finalize(memStmt);
        }
        trans.commit();
    }

    // 2026-06-xx 物理级根除：基于 File ID (FRN) 批量清理所有分类关联，彻底杜绝“幽灵关联”
    if (!fids.empty()) {
        CategoryRepo::removeAllCategoriesBatch(fids);
    }

    // [Plan-5] 移除 1:1 自动建立的整个镜像分类树节点（标准化比对加固）
    auto allCats = CategoryRepo::getAll();
    bool anyCatRemoved = false;
    for (const auto& cat : allCats) {
        if (!cat.physicalPath.empty()) {
            std::wstring normCatPath = normalizePath(cat.physicalPath);
            if (normCatPath == nPath || normCatPath.find(nPath + L"\\") == 0 || normCatPath.find(nPath + L"/") == 0) {
                CategoryRepo::remove(cat.id);
                anyCatRemoved = true;
            }
        }
    }
    if (anyCatRemoved) {
        notifyUI(RefreshLevel::FullRebuild);
    }
}

void MetadataManager::removeMetadataBatchSync(const QStringList& paths) {
    if (paths.isEmpty()) return;

    // 1. 按数据库分组以支持大事务
    std::map<sqlite3*, std::vector<std::string>> groupedFids;
    std::vector<std::string> allFids;
    int totalDelta = 0;

    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        for (const QString& qp : paths) {
            std::wstring nPath = normalizePath(qp.toStdWString());
            
            std::vector<std::wstring> toRemove;
            forEachCachedItem([&](const std::wstring& p, const RuntimeMeta&) {
                if (p == nPath || p.find(nPath + L"\\") == 0 || p.find(nPath + L"/") == 0) {
                    toRemove.push_back(p);
                }
            });

            for (const auto& p : toRemove) {
                size_t idx = getShardIndex(p);
                RuntimeMeta meta;
                bool found = false;
                {
                    std::unique_lock<std::shared_mutex> shardLock(m_shards[idx].mutex);
                    auto it = m_shards[idx].items.find(p);
                    if (it != m_shards[idx].items.end()) {
                        meta = it->second;
                        m_shards[idx].items.erase(it);
                        found = true;
                    }
                }
                if (!found) continue;

                if (isManagedAsset(meta.isFolder, p) && !meta.isTrash) {
                    totalDelta--;
                }

                std::string fid = meta.folderId;
                if (!fid.empty()) {
                    allFids.push_back(fid);
                    m_folderIdToPath.erase(fid);

                    std::wstring volSerial = getVolumeSerialNumber(p);
                    QString letter = (p.length() >= 2 && p[1] == L':') ? QString::fromWCharArray(&p[0], 1) : "";
                    sqlite3* db = DatabaseManager::instance().getDriveDb(volSerial, letter);
                    if (db) groupedFids[db].push_back(fid);

                    std::wstring name, ext;
                    parsePathComponents(p, meta.isFolder, name, ext);
                    if (!name.empty()) {
                        if (meta.isFolder) {
                            auto& v = m_subFolderNameToFolderIds[name];
                            v.erase(std::remove(v.begin(), v.end(), fid), v.end());
                            if (v.empty()) m_subFolderNameToFolderIds.erase(name);
                        } else {
                            auto& v = m_assetNameToFolderIds[name];
                            v.erase(std::remove(v.begin(), v.end(), fid), v.end());
                            if (v.empty()) m_assetNameToFolderIds.erase(name);
                            if (!ext.empty()) {
                                auto& ve = m_extensionToFolderIds[ext];
                                ve.erase(std::remove(ve.begin(), ve.end(), fid), ve.end());
                                if (ve.empty()) m_extensionToFolderIds.erase(ext);
                            }
                        }
                    }
                    m_parentToChildren.erase(p);
                    m_folderProgressCache.erase(p);
                }
            }
        }
    }

    // 2. 数据库执行
    const char* sql = "DELETE FROM metadata WHERE folder_id = ?";
    for (auto& entry : groupedFids) {
        sqlite3* db = entry.first;
        const auto& fids = entry.second;

        // [Plan-131 方案 A] 直连模式，废除冗余异步分发
        SqlTransaction trans(db);
        sqlite3_stmt* memStmt;
        if (sqlite3_prepare_v2(db, sql, -1, &memStmt, nullptr) == SQLITE_OK) {
            for (const auto& fid : fids) {
                sqlite3_bind_text(memStmt, 1, fid.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(memStmt);
                sqlite3_reset(memStmt);
            }
            sqlite3_finalize(memStmt);
        }

        // 🚨 2026-07-27 按照 Plan-107：极速级联清除 system_stats 中的 PROGRESS 进度记录
        for (const QString& qp : paths) {
            std::wstring nPath = normalizePath(qp.toStdWString());
            std::string progressKey = "PROGRESS:" + QString::fromStdWString(nPath).toUtf8().toStdString();
            sqlite3_stmt* statStmt;
            if (sqlite3_prepare_v2(db, "DELETE FROM system_stats WHERE key = ?", -1, &statStmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(statStmt, 1, progressKey.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(statStmt);
                sqlite3_finalize(statStmt);
            }
        }
        trans.commit();
    }

    // 🚨 同步清理进程中的进度条内存缓存
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        for (const QString& qp : paths) {
            std::wstring nPath = normalizePath(qp.toStdWString());
            m_folderProgressCache.erase(nPath);
        }
    }

    if (!allFids.empty()) CategoryRepo::removeAllCategoriesBatch(allFids);
    
    notifyFullUIRebuild();

    // 关键操作后即时异步落盘
    DatabaseManager::instance().enqueueSyncTask([]() {
        DatabaseManager::instance().flushAll();
    });
}

void MetadataManager::markAsTrash(const std::wstring& path, bool isTrash, const std::wstring& origPath) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    std::string fid;
    fetchWinApiMetadataDirect(nPath, fid);

    bool changed = false;
    bool isManaged = false;
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        if (!fid.empty() && m_folderIdToPath.count(fid)) {
            std::wstring oldPath = m_folderIdToPath[fid];
            if (oldPath != nPath) {
                size_t oldIdx = getShardIndex(oldPath);
                std::unique_lock<std::shared_mutex> shardLock(m_shards[oldIdx].mutex);
                auto itOld = m_shards[oldIdx].items.find(oldPath);
                if (itOld != m_shards[oldIdx].items.end()) {
                    std::wstring oldName, oldExt;
                    parsePathComponents(oldPath, itOld->second.isFolder, oldName, oldExt);
                    if (!oldName.empty()) {
                        if (itOld->second.isFolder) {
                            auto& v = m_subFolderNameToFolderIds[oldName];
                            v.erase(std::remove(v.begin(), v.end(), fid), v.end());
                            if (v.empty()) m_subFolderNameToFolderIds.erase(oldName);
                        } else {
                            auto& v = m_assetNameToFolderIds[oldName];
                            v.erase(std::remove(v.begin(), v.end(), fid), v.end());
                            if (v.empty()) m_assetNameToFolderIds.erase(oldName);
                            if (!oldExt.empty()) {
                                auto& ve = m_extensionToFolderIds[oldExt];
                                ve.erase(std::remove(ve.begin(), ve.end(), fid), ve.end());
                                if (ve.empty()) m_extensionToFolderIds.erase(oldExt);
                            }
                        }
                    }

                    std::wstring oldParent = QDir::toNativeSeparators(QFileInfo(QString::fromStdWString(oldPath)).absolutePath()).toStdWString();
                    oldParent = normalizePath(oldParent);
                    if (m_parentToChildren.count(oldParent)) {
                        auto& children = m_parentToChildren[oldParent];
                        children.erase(std::remove(children.begin(), children.end(), oldPath), children.end());
                        if (children.empty()) m_parentToChildren.erase(oldParent);
                    }

                    m_shards[oldIdx].items.erase(itOld);
                }
            }
        }
    }
    
    ensureActivated(nPath); 

    bool isFolder = false;
    bool oldEmpty = false;
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        size_t idx = getShardIndex(nPath);
        std::unique_lock<std::shared_mutex> shardLock(m_shards[idx].mutex);
        auto it = m_shards[idx].items.find(nPath);
        if (it != m_shards[idx].items.end()) {
            if (it->second.isTrash != isTrash) {
                it->second.isTrash = isTrash;
                if (isTrash && !origPath.empty()) it->second.originalPath = origPath;
                changed = true;
                isManaged = it->second.isManaged;
                isFolder = it->second.isFolder;
                oldEmpty = it->second.tags.isEmpty();
            }
        }
        if (!fid.empty()) m_folderIdToPath[fid] = nPath;
    }
    
    if (changed) {
        // 2026-06-xx 按照用户要求：移入回收站时，必须和其他分类彻底隔离
        if (isTrash && !fid.empty()) {
            // 将文件移入“回收站”桶位（ID -8），这会自动解除所有现有分类关联
            CategoryRepo::moveToTrashBatch({fid});
        }

        // 实时通知统计服务回收站状态变更 
        StatisticsService::instance().notifyAssetTrashChanged(isTrash, oldEmpty); 

        persistAsync(nPath);
        
        // 2026-06-xx 物理修复：状态变更后必须强制发射信号，驱动侧边栏重数一遍
        notifyUI(RefreshLevel::FullRebuild);
    }
}

void MetadataManager::setTrash(const std::wstring& path, bool isTrash) {
    std::wstring nPath = normalizePath(path);
    bool changed = false;
    bool isFolder = false;
    bool oldEmpty = false;
    std::string fid;
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        size_t idx = getShardIndex(nPath);
        {
            std::unique_lock<std::shared_mutex> shardLock(m_shards[idx].mutex);
            auto it = m_shards[idx].items.find(nPath);
            if (it != m_shards[idx].items.end()) {
                if (it->second.isTrash != isTrash) {
                    it->second.isTrash = isTrash;
                    if (!isTrash) {
                        it->second.originalPath = L""; // Clear on restore
                    }
                    changed = true;
                    isFolder = it->second.isFolder;
                    oldEmpty = it->second.tags.isEmpty();
                    fid = it->second.folderId;
                }
            }
        }
    }
    persistAsync(nPath);
}

void MetadataManager::deletePermanently(const std::wstring& path) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    
    // 🛡️ 优先通过路径中的 13 位 Base36 ID 反查内存缓存 Key，防止路径解包不一致导致的匹配失败
    std::string base36Id = extractBase36Id(nPath);
    if (!base36Id.empty()) {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        auto it = m_folderIdToPath.find(base36Id);
        if (it != m_folderIdToPath.end()) {
            nPath = it->second; // 强行对齐为数据库与缓存中存储的标准路径
        }
    }

    // 执行彻底根除 (removeMetadataSync 会级联擦除 SQLite metadata 与 category_items)
    removeMetadataSync(nPath);

    // 广播 UI 全量刷新信号
    notifyUI(RefreshLevel::FullRebuild);
}

std::wstring MetadataManager::getVolumeSerialNumber(const std::wstring& path) {
    if (path.length() < 2 || path[1] != L':') return L"UNKNOWN";
    wchar_t root[4] = { static_cast<wchar_t>(towupper(path[0])), L':', L'\\', L'\0' };
    DWORD serial = 0;
    if (GetVolumeInformationW(root, nullptr, 0, &serial, nullptr, nullptr, nullptr, 0)) {
        wchar_t buf[16]; swprintf(buf, 16, L"%08X", serial); return buf;
    }
    return L"UNKNOWN";
}

std::wstring MetadataManager::getManagedLibraryPath(const std::wstring& volSerial, const QString& driveLetter) {
    if (volSerial.empty() || volSerial == L"UNKNOWN") return L"";

    QString cleanLetter = driveLetter;
    if (cleanLetter.endsWith("/") || cleanLetter.endsWith("\\")) {
        cleanLetter = cleanLetter.left(1);
    }
    QString driveRoot(cleanLetter);
    driveRoot.append(":");

    QString key = QString("ManagedFolder/Volume_%1").arg(QString::fromStdWString(volSerial));
    QString relPath = ::ArcMeta::AppConfig::instance().getValue(key, QVariant("")).toString();

    // 2026-07-xx 按照 Plan-118：约定优于配置的默认兜底逻辑
    if (relPath.isEmpty()) {
        QString defaultRel("ArcMeta.Library_");
        defaultRel.append(cleanLetter.at(0).toUpper());

        QString fullPath(driveRoot);
        fullPath.append("/");
        fullPath.append(defaultRel);
        
        if (QFileInfo::exists(QDir::toNativeSeparators(fullPath))) {
            relPath = defaultRel;
        }
    }

    if (relPath.isEmpty()) return L"";

    QString finalPath(driveRoot);
    finalPath.append("/");
    finalPath.append(relPath);

    return normalizePath(finalPath.toStdWString());
}

bool MetadataManager::isInsideManagedLibrary(const std::wstring& path) {
    if (path.empty()) return false;
    
    std::wstring normW = normalizePath(path);
    QString qPath = QString::fromStdWString(normW).toLower();

    // 1. 检查默认资源库
    std::wstring volSerial = getVolumeSerialNumber(path);
    QString letter = (path.length() >= 2 && path[1] == L':') ? QString::fromWCharArray(&path[0], 1) : "";
    std::wstring managedAbsW = getManagedLibraryPath(volSerial, letter);
    if (!managedAbsW.empty()) {
        QString managedAbs = QString::fromStdWString(managedAbsW).toLower();
        if (qPath.startsWith(managedAbs)) {
            if (qPath.length() == managedAbs.length() ||
                qPath[managedAbs.length()] == '\\' || qPath[managedAbs.length()] == '/') {
                return true;
            }
        }
    }

    return false;
}

bool MetadataManager::fetchWinApiMetadataDirect(const std::wstring& path, std::string& outId128, std::wstring* outFrn, long long* outSize, std::wstring* outType, long long* outCtime, long long* outMtime, long long* outAtime) {
    HANDLE hFile = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    std::wstring vol = getVolumeSerialNumber(path);
    if (hFile == INVALID_HANDLE_VALUE) {
        if (outFrn) *outFrn = MetadataManager::generateDeterministicFrn(path);
        outId128 = MetadataManager::generateDeterministicFolderId(path);
        return false;
    }
    BY_HANDLE_FILE_INFORMATION basicInfo;
    if (GetFileInformationByHandle(hFile, &basicInfo)) {
        wchar_t frnBuf[17];
        unsigned long long fullFrn = (static_cast<unsigned long long>(basicInfo.nFileIndexHigh) << 32) | basicInfo.nFileIndexLow;
        swprintf(frnBuf, 17, L"%016llX", fullFrn);
        if (outFrn) *outFrn = frnBuf;
        outId128 = MetadataManager::generateFallbackFolderId(vol, frnBuf);
        if (outSize) *outSize = (static_cast<long long>(basicInfo.nFileSizeHigh) << 32) | basicInfo.nFileSizeLow;
        if (outType) *outType = (basicInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? L"folder" : L"file";
        auto toMS = [](const FILETIME& ft) {
            ULARGE_INTEGER ull; ull.LowPart = ft.dwLowDateTime; ull.HighPart = ft.dwHighDateTime;
            return static_cast<long long>((ull.QuadPart - 116444736000000000ULL) / 10000ULL);
        };
        if (outCtime) *outCtime = toMS(basicInfo.ftCreationTime);
        if (outMtime) *outMtime = toMS(basicInfo.ftLastWriteTime);
        if (outAtime) *outAtime = toMS(basicInfo.ftLastAccessTime);
        CloseHandle(hFile);
        return true;
    }
    CloseHandle(hFile);
    return false;
}

void MetadataManager::syncPhysicalMetadata(const std::wstring& path, bool notify) { persistAsync(path, notify); }

void MetadataManager::activateItem(const std::wstring& path) {
    instance().registerItem(path);
}


void MetadataManager::registerArcmetaFrn(const std::wstring&) {
}

std::string MetadataManager::getFolderIdSync(const std::wstring& path) {
    // 1. 如果处于受控资源库中，直接提取 13 位 Base36 ID，终结系统级 FRN 物理依赖
    std::string base36 = extractBase36Id(path);
    if (!base36.empty()) {
        return base36;
    }

    // 2. 磁盘模式（非托管路径）不使用 Base36 ID，自愈退避至原本的系统级物理 FRN 
    std::string fid;
    if (!fetchWinApiMetadataDirect(path, fid, nullptr)) fid = MetadataManager::generateDeterministicFolderId(path);
    return fid;
}

void MetadataManager::persistBatchAsync(const std::vector<std::wstring>& paths, bool authorized) {
    WriteGuard guard;
    if (paths.empty()) return;

    // 1. 按数据库对路径进行分组，以支持大事务写入
    struct BatchTask {
        sqlite3* memDb;
        std::vector<std::wstring> groupPaths;
    };
    std::map<sqlite3*, std::vector<std::wstring>> groups;

    for (const auto& p : paths) {
        sqlite3* db = nullptr;
        if (p.length() == 3 && p[1] == L':' && (p[2] == L'\\' || p[2] == L'/')) {
            db = DatabaseManager::instance().getGlobalDb();
        } else {
            std::wstring volSerial = getVolumeSerialNumber(p);
            QString letter = (p.length() >= 2 && p[1] == L':') ? QString::fromWCharArray(&p[0], 1) : "";
            db = DatabaseManager::instance().getDriveDb(volSerial, letter);
        }
        if (db) groups[db].push_back(p);
    }

    for (auto& entry : groups) {
        sqlite3* memDb = entry.first;
        const auto& groupPaths = entry.second;

        // 2. 内存库批量提交 (使用 SqlTransaction 确保原子性与速度)
        SqlTransaction trans(memDb);
        std::vector<std::pair<std::wstring, RuntimeMeta>> recordsToSync;

        for (const auto& p : groupPaths) {
            RuntimeMeta rMeta = getMeta(p);
            if (rMeta.folderId.empty()) continue;

            // 准入检查
            bool isNew = true;
            sqlite3_stmt* checkStmt;
            if (sqlite3_prepare_v2(memDb, "SELECT 1 FROM metadata WHERE folder_id = ?", -1, &checkStmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(checkStmt, 1, rMeta.folderId.c_str(), -1, SQLITE_TRANSIENT);
                if (sqlite3_step(checkStmt) == SQLITE_ROW) isNew = false;
                sqlite3_finalize(checkStmt);
            }

            if (isNew && !authorized) {
                if (!isInsideManagedLibrary(p)) continue;
            }

            // 重新解析出最新基名与后缀塞入
            parsePathComponents(p, rMeta.isFolder, rMeta.baseName, rMeta.ext);

            sqlite3_stmt* memStmt;
            if (sqlite3_prepare_v2(memDb, kSqlInsertMeta, -1, &memStmt, nullptr) == SQLITE_OK) {
                bindMetaHelper(memStmt, p, rMeta);

                if (sqlite3_step(memStmt) == SQLITE_DONE) {
                    rMeta.isManaged = true;
                    {
                        size_t idx = getShardIndex(p);
                        std::unique_lock<std::shared_mutex> shardLock(m_shards[idx].mutex);
                        m_shards[idx].items[p] = rMeta;
                    }
                    recordsToSync.push_back({p, rMeta});
                }
                sqlite3_finalize(memStmt);
            }
        }
        trans.commit();
    }

    // 关键操作后即时异步落盘
    DatabaseManager::instance().enqueueSyncTask([]() {
        DatabaseManager::instance().flushAll();
    });
}

void MetadataManager::persistAsync(const std::wstring& path, bool notify, bool authorized) {
    WriteGuard guard;
    std::wstring nPath = MetadataManager::normalizePath(path);
    
    RuntimeMeta rMeta = getMeta(nPath);
    // 写入前现算一次持久化基名与后缀
    parsePathComponents(nPath, rMeta.isFolder, rMeta.baseName, rMeta.ext);
    
    sqlite3* memDb = nullptr;
    std::wstring volSerial;
    
    if (nPath.length() == 3 && nPath[1] == L':' && (nPath[2] == L'\\' || nPath[2] == L'/')) {
        memDb = DatabaseManager::instance().getGlobalDb();
    } else {
        volSerial = getVolumeSerialNumber(nPath);
        QString letter = (nPath.length() >= 2 && nPath[1] == L':') ? QString::fromWCharArray(&nPath[0], 1) : "";
        memDb = DatabaseManager::instance().getDriveDb(volSerial, letter);
    }
    if (!memDb) {
        return;
    }

    // 获取驱动盘递归互斥锁并上锁，解决并发写入和备份竞争造成的 SQLITE_BUSY / SQLITE_LOCKED 冲突，且确保重入安全
    std::shared_ptr<std::recursive_mutex> dbLock;
    if (!volSerial.empty()) {
        dbLock = DatabaseManager::instance().getDriveMutex(volSerial);
    }
    std::unique_lock<std::recursive_mutex> lockConn;
    if (dbLock) {
        lockConn = std::unique_lock<std::recursive_mutex>(*dbLock);
    }

    // 1. 内存库操作 (Memory Commit)
    bool isNew = true;
    {
        sqlite3_stmt* checkStmt;
        if (sqlite3_prepare_v2(memDb, "SELECT 1 FROM metadata WHERE folder_id = ?", -1, &checkStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(checkStmt, 1, rMeta.folderId.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(checkStmt) == SQLITE_ROW) isNew = false;
            sqlite3_finalize(checkStmt);
        }
    }

    if (isNew && !authorized) {
        if (!isInsideManagedLibrary(nPath)) return;
        authorized = true;
    }

    sqlite3_stmt* memStmt;
    if (sqlite3_prepare_v2(memDb, kSqlInsertMeta, -1, &memStmt, nullptr) == SQLITE_OK) {
        bindMetaHelper(memStmt, nPath, rMeta);
        if (sqlite3_step(memStmt) == SQLITE_DONE) {
            {
                rMeta.isManaged = true;
                size_t idx = getShardIndex(nPath);
                std::unique_lock<std::shared_mutex> shardLock(m_shards[idx].mutex);
                m_shards[idx].items[nPath] = rMeta;
            }
        }
        sqlite3_finalize(memStmt);
    }
        
    if (notify) notifyUI(RefreshLevel::PathUpdate, QString::fromStdWString(nPath));
}


void MetadataManager::parsePathComponents(const std::wstring& normalizedPath, bool isFolder, std::wstring& outName, std::wstring& outExt) {
    size_t lastSlash = normalizedPath.find_last_of(L"\\/");
    std::wstring fullName = (lastSlash == std::wstring::npos) ? normalizedPath : normalizedPath.substr(lastSlash + 1);

    if (isFolder) {
        outName = fullName;
        outExt = L"";
    } else {
        outName = fullName;
        size_t lastDot = fullName.find_last_of(L'.');
        if (lastDot != std::wstring::npos && lastDot > 0) {
            outExt = fullName.substr(lastDot + 1);
            // 统一转换为小写
            std::transform(outExt.begin(), outExt.end(), outExt.begin(), ::towlower);
        } else {
            outExt = L"";
        }
    }
}

std::wstring MetadataManager::getVolumeFromFolderId(const std::string& fid) {
    if (fid.empty()) return L"UNKNOWN";
    if (fid.find("FRN:") == 0) {
        size_t secondColon = fid.find(':', 4);
        if (secondColon != std::string::npos) {
            std::string vol = fid.substr(4, secondColon - 4);
            return QString::fromStdString(vol).toStdWString();
        }
    }
    return L"UNKNOWN";
}

void MetadataManager::unloadVolumeNameCache(const std::wstring& volSerial) {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    std::string prefix = "FRN:";
    prefix.append(QString::fromStdWString(volSerial).toUpper().toStdString());
    prefix.append(":");

    auto cleanupMap = [&](std::unordered_map<std::wstring, std::vector<std::string>>& map) {
        for (auto it = map.begin(); it != map.end(); ) {
            auto& fids = it->second;
            fids.erase(std::remove_if(fids.begin(), fids.end(), [&](const std::string& fid) {
                return fid.find(prefix) == 0;
            }), fids.end());

            if (fids.empty()) {
                it = map.erase(it);
            } else {
                ++it;
            }
        }
    };

    cleanupMap(m_assetNameToFolderIds);
    cleanupMap(m_subFolderNameToFolderIds);
    cleanupMap(m_extensionToFolderIds);
}

void MetadataManager::loadVolumeNameCache(const std::wstring& volSerial) {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    std::string prefix = "FRN:";
    prefix.append(QString::fromStdWString(volSerial).toUpper().toStdString());
    prefix.append(":");

    forEachCachedItem([&](const std::wstring& path, const RuntimeMeta& meta) {
        if (meta.folderId.find(prefix) == 0) {
            std::wstring name, ext;
            parsePathComponents(path, meta.isFolder, name, ext);
            if (!name.empty()) {
                if (meta.isFolder) {
                    auto& v = m_subFolderNameToFolderIds[name];
                    if (std::find(v.begin(), v.end(), meta.folderId) == v.end()) v.push_back(meta.folderId);
                } else {
                    auto& v = m_assetNameToFolderIds[name];
                    if (std::find(v.begin(), v.end(), meta.folderId) == v.end()) v.push_back(meta.folderId);
                    if (!ext.empty()) {
                        auto& ve = m_extensionToFolderIds[ext];
                        if (std::find(ve.begin(), ve.end(), meta.folderId) == ve.end()) ve.push_back(meta.folderId);
                    }
                }
            }
        }
    });
}

std::vector<std::string> MetadataManager::getFolderIdsByName(const std::wstring& filename) {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    std::wstring lowerName = filename;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::towlower);
    auto it = m_assetNameToFolderIds.find(lowerName);
    return (it != m_assetNameToFolderIds.end()) ? it->second : std::vector<std::string>();
}

std::vector<std::string> MetadataManager::getSubFolderIdsByName(const std::wstring& foldername) {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    std::wstring lowerName = foldername;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::towlower);
    auto it = m_subFolderNameToFolderIds.find(lowerName);
    return (it != m_subFolderNameToFolderIds.end()) ? it->second : std::vector<std::string>();
}

std::vector<std::string> MetadataManager::getFolderIdsByExtension(const std::wstring& extension) {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    std::wstring lowerExt = extension;
    std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(), ::towlower);
    auto it = m_extensionToFolderIds.find(lowerExt);
    return (it != m_extensionToFolderIds.end()) ? it->second : std::vector<std::string>();
}

bool MetadataManager::hasPendingSync() const { return false; }
QStringList MetadataManager::getPendingSyncDirs() { return {}; }
void MetadataManager::removeFidsFromLog(const QStringList&) {}
void MetadataManager::addToSyncLog(const std::wstring&) {}

QStringList MetadataManager::searchInCache(const QString& keyword, const QString& scopeSource, int categoryId, const QString& parentPath) {
    // [Plan-26] 彻底废除 O(N) 全量内存线性遍历，全面拥抱 FTS5 trigram 模糊检索引擎 + 内存 O(1) 快速反查
    QStringList results; if (keyword.isEmpty()) return results;
    
    // 2026-07-xx 按照方案计划：实现范围感知搜索
    std::unordered_set<std::string> scopeFids;
    bool hasScope = false;

    if (scopeSource == "category" && categoryId != 0) {
        // 1. 分类范围搜索：获取该分类及其子分类下的所有 FID
        // 2026-07-xx 按照 Plan-81：支持递归搜索
        std::vector<int> targetIds = { categoryId };
        if (categoryId > 0) {
            targetIds = CategoryRepo::getSubtreeIds(categoryId);
        }
        auto items = CategoryRepo::getItemsInCategories(targetIds);
        for (const auto& item : items) scopeFids.insert(item.folderId);
        hasScope = true;
    }

    // 2026-07-xx 物理对账：规范化父路径前缀用于导航范围搜索
    std::wstring wParentPath = (scopeSource == "nav" && !parentPath.isEmpty()) ? normalizePath(parentPath.toStdWString()) : L"";
    if (!wParentPath.empty()) {
        bool endsWithSlash = false;
        if (wParentPath.back() == L'\\' || wParentPath.back() == L'/') endsWithSlash = true;
        if (!endsWithSlash) {
            wParentPath += L'\\';
        }
    }

    // 2. 区分检索词长度获取匹配路径，避开 O(N) 扫描
    std::vector<std::wstring> matchedPaths;
    auto dbs = DatabaseManager::instance().getActiveMemoryDbs();

    if (keyword.length() >= 3) {
        // [Plan-26] FTS5 trigram 快速 Match 路径：通过倒排索引实现 O(log N) 模糊检索分流，彻底释解读写锁
        QString cleanKeyword = keyword;
        cleanKeyword.replace("\"", "");
        QString ftsQuery = "\"" + cleanKeyword + "\"";
        std::string utf8Query = ftsQuery.toUtf8().toStdString();

        const char* sql = "SELECT path FROM metadata WHERE rowid IN (SELECT rowid FROM metadata_fts WHERE metadata_fts MATCH ?)";
        for (sqlite3* db : dbs) {
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, utf8Query.c_str(), -1, SQLITE_TRANSIENT);
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    const wchar_t* wpath = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 0));
                    if (wpath) {
                        matchedPaths.push_back(normalizePath(wpath));
                    }
                }
                sqlite3_finalize(stmt);
            }
        }
    } else {
        // [Plan-26] 退化路径：LIKE 模糊匹配降级路径 (使用高性能 UTF-8 绑定以避免 SQLite 内部编码转换开销)
        QString likeQueryStr = "%" + keyword + "%";
        std::string utf8LikeQuery = likeQueryStr.toUtf8().toStdString();

        const char* sql = "SELECT path FROM metadata WHERE path LIKE ? OR note LIKE ? OR tags LIKE ?";
        for (sqlite3* db : dbs) {
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, utf8LikeQuery.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, utf8LikeQuery.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 3, utf8LikeQuery.c_str(), -1, SQLITE_TRANSIENT);
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    const char* utf8Path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                    if (utf8Path) {
                        matchedPaths.push_back(normalizePath(QString::fromUtf8(utf8Path).toStdWString()));
                    }
                }
                sqlite3_finalize(stmt);
            }
        }
    }

    // 去重
    std::sort(matchedPaths.begin(), matchedPaths.end());
    matchedPaths.erase(std::unique(matchedPaths.begin(), matchedPaths.end()), matchedPaths.end());

    // 3. 关联内存缓存并执行 Scope 过滤
    for (const auto& path : matchedPaths) {
        size_t idx = getShardIndex(path);
        std::shared_lock<std::shared_mutex> shardLock(m_shards[idx].mutex);
        auto it = m_shards[idx].items.find(path);
        if (it != m_shards[idx].items.end()) {
            const RuntimeMeta& meta = it->second;

            // Scope check
            if (hasScope) {
                if (scopeFids.find(meta.folderId) == scopeFids.end()) continue;
            } else if (!wParentPath.empty()) {
                if (path.find(wParentPath) != 0) continue;
            }

            results << QString::fromStdWString(path);
        }
    }

    return results;
}

QMap<QString, int> MetadataManager::getAllTags() const {
    QMap<QString, int> tagCounts;
    forEachCachedItem([&](const std::wstring&, const RuntimeMeta& meta) {
        if (meta.isManaged && !meta.isTrash) {
            for (const QString& tag : meta.tags) {
                tagCounts[tag]++;
            }
        }
    });
    return tagCounts;
}

QList<QPair<QString, int>> MetadataManager::getTopTags(int limit) const {
    QMap<QString, int> counts = getAllTags();
    QList<QPair<QString, int>> list;
    for (auto it = counts.begin(); it != counts.end(); ++it) {
        list.append({it.key(), it.value()});
    }

    std::sort(list.begin(), list.end(), [](const QPair<QString, int>& a, const QPair<QString, int>& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
    });

    if (list.size() > limit) {
        return list.mid(0, limit);
    }
    return list;
}

void MetadataManager::recordAccess(const std::wstring& path) {
    std::wstring nPath = normalizePath(path);
    {
        std::lock_guard<std::mutex> lock(m_recentMutex);
        auto it = std::find(m_recentVisitedQueue.begin(), m_recentVisitedQueue.end(), nPath);
        if (it != m_recentVisitedQueue.end()) {
            m_recentVisitedQueue.erase(it);
        } else {
            m_recentVisitedSet.insert(nPath);
        }
        m_recentVisitedQueue.push_back(nPath);
    }
    
    double now = static_cast<double>(QDateTime::currentMSecsSinceEpoch());
    {
        size_t idx = getShardIndex(nPath);
        std::unique_lock<std::shared_mutex> shardLock(m_shards[idx].mutex);
        if (m_shards[idx].items.count(nPath)) {
            m_shards[idx].items[nPath].atime = static_cast<long long>(now);
        }
    }
    
    // 2. 纯内存更新访问时间，取消隐式数据库持久化写库任务
}

double MetadataManager::getCachedAtime(const std::wstring& path) {
    size_t idx = getShardIndex(path);
    std::shared_lock<std::shared_mutex> shardLock(m_shards[idx].mutex);
    auto it = m_shards[idx].items.find(path);
    if (it != m_shards[idx].items.end()) {
        return static_cast<double>(it->second.atime);
    }
    return 0.0;
}

void MetadataManager::slideRecentWindow() {
    double expireThreshold = static_cast<double>(QDateTime::currentMSecsSinceEpoch()) - 86400000.0;

    // 🚨 核心修复：第一步，只在 m_recentMutex 保护下做"取快照"，不在这把锁里做任何可能耗时或
    // 需要嵌套加锁的操作（如 getCachedAtime 需要另一把 m_mutex）。
    // 这样即使这一步稍有延迟，也绝不会阻塞 UI 线程里 recordAccess() 抢 m_recentMutex 的时间超过微秒级。
    std::deque<std::wstring> snapshot;
    {
        std::lock_guard<std::mutex> lock(m_recentMutex);
        snapshot = m_recentVisitedQueue; // 拷贝一份，立即释放锁
    }

    // 第二步：在锁外计算哪些需要过期剔除（getCachedAtime 内部自己的 m_mutex 与本函数无嵌套关系）
    std::vector<std::wstring> toErase;
    for (const auto& path : snapshot) {
        double itemAtime = getCachedAtime(path);
        if (itemAtime < expireThreshold) {
            toErase.push_back(path);
        } else {
            break; // 队首往后都在窗口内，跳出
        }
    }

    if (toErase.empty()) return;

    // 第三步：只在真正要修改队列时，再次短暂加锁做剔除，持锁时间同样是微秒级
    std::lock_guard<std::mutex> lock(m_recentMutex);
    for (const auto& path : toErase) {
        if (!m_recentVisitedQueue.empty() && m_recentVisitedQueue.front() == path) {
            m_recentVisitedQueue.pop_front();
            m_recentVisitedSet.erase(path);
        }
    }
}

std::vector<LightMeta> MetadataManager::getLightweightCacheSnapshot() const {
    std::vector<LightMeta> result;
    forEachCachedItem([&](const std::wstring& path, const RuntimeMeta& meta) {
        result.push_back({
            path,
            meta.folderId,
            meta.isFolder,
            meta.isTrash,
            meta.tags.isEmpty(),
            static_cast<double>(meta.atime),
            meta.tags
        });
    });
    return result;
}

} // namespace ArcMeta
