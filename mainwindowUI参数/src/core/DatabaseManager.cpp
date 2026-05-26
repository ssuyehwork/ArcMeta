#include "DatabaseManager.h"
#include <QDebug>
#include <QSqlRecord>
#include <QSqlError>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QDir>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QFileInfo>
#include <QStandardPaths>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QtConcurrent>
#include <QThreadPool>
#include <QMessageBox>
#include <utility>
#include <algorithm>
#include "../ui/StringUtils.h"
#include "../ui/FramelessDialog.h"

DatabaseManager& DatabaseManager::instance() {
    static DatabaseManager inst;
    return inst;
}

QStringList DatabaseManager::s_tagClipboard;
QMutex DatabaseManager::s_tagClipboardMutex;

void DatabaseManager::setTagClipboard(const QStringList& tags) {
    QMutexLocker locker(&s_tagClipboardMutex);
    s_tagClipboard = tags;
}

QStringList DatabaseManager::getTagClipboard() {
    QMutexLocker locker(&s_tagClipboardMutex);
    return s_tagClipboard;
}

DatabaseManager::DatabaseManager(QObject* parent) : QObject(parent) {
    // 2026-03-xx 按照用户要求：支持 RapidManager 独立配置域
    QSettings settings;
#ifdef RAPID_MANAGER_TARGET
    settings.beginGroup("RapidManager");
#else
    settings.beginGroup("QuickWindow");
#endif
    m_autoCategorizeEnabled = settings.value("autoCategorizeClipboard", false).toBool();
    m_extensionTargetCategoryId = settings.value("extensionTargetCategoryId", -1).toInt();
    m_lockedCategoriesHidden = settings.value("lockedCategoriesHidden", false).toBool();
    settings.endGroup();

    m_autoSaveTimer = new QTimer(this);
    m_autoSaveTimer->setInterval(7000); // 7秒增量同步间隔
    connect(m_autoSaveTimer, &QTimer::timeout, this, &DatabaseManager::handleAutoSave);
}

void DatabaseManager::setAutoCategorizeEnabled(bool enabled) {
    if (m_autoCategorizeEnabled != enabled) {
        m_autoCategorizeEnabled = enabled;
        QSettings settings;
#ifdef RAPID_MANAGER_TARGET
        settings.beginGroup("RapidManager");
#else
        settings.beginGroup("QuickWindow");
#endif
        settings.setValue("autoCategorizeClipboard", enabled);
        settings.endGroup();
        emit autoCategorizeEnabledChanged(enabled);
    }
}

void DatabaseManager::setActiveCategoryId(int id) {
    if (m_activeCategoryId != id) {
        m_activeCategoryId = id;
        emit activeCategoryIdChanged(id);
    }
}

void DatabaseManager::setExtensionTargetCategoryId(int id) {
    if (m_extensionTargetCategoryId != id) {
        m_extensionTargetCategoryId = id;
        QSettings settings;
#ifdef RAPID_MANAGER_TARGET
        settings.beginGroup("RapidManager");
#else
        settings.beginGroup("QuickWindow");
#endif
        settings.setValue("extensionTargetCategoryId", id);
        settings.endGroup();
        emit extensionTargetCategoryIdChanged(id);
    }
}

QString DatabaseManager::getCategoryNameById(int id) {
    if (id <= 0) return "";
    QMutexLocker locker(&m_mutex);
    QSqlQuery query(m_db);
    query.prepare("SELECT name FROM categories WHERE id = :id");
    query.bindValue(":id", id);
    if (query.exec() && query.next()) {
        return query.value(0).toString();
    }
    return "";
}

QVariantMap DatabaseManager::getRootCategory(int catId) {
    if (catId <= 0) return QVariantMap();
    QMutexLocker locker(&m_mutex);
    
    int currentId = catId;
    QVariantMap result;
    
    // 递归向上查找父分类，直到顶级
    while (true) {
        QSqlQuery query(m_db);
        query.prepare("SELECT id, name, parent_id FROM categories WHERE id = :id");
        query.bindValue(":id", currentId);
        
        if (query.exec() && query.next()) {
            result["id"] = query.value("id");
            result["name"] = query.value("name");
            int parentId = query.value("parent_id").isNull() ? -1 : query.value("parent_id").toInt();
            
            if (parentId <= 0) {
                // 已经到达最顶层
                break;
            } else {
                currentId = parentId;
            }
        } else {
            break;
        }
    }
    
    return result;
}

DatabaseManager::~DatabaseManager() {
    if (m_autoSaveTimer) {
        m_autoSaveTimer->stop();
    }
    if (m_db.isOpen()) {
        m_db.close();
    }
}

void DatabaseManager::logStartup(const QString& msg) {
    // 2026-03-xx 按照用户要求：彻底移除 startup_debug.log 文件写入逻辑，仅保留控制台调试输出
    qDebug() << "[DB-STARTUP]" << msg;
}

bool DatabaseManager::init(const QString& dbPath) {
    QMutexLocker locker(&m_mutex);
    m_isInitialized = false;
    m_lastError.clear();
    
    logStartup("--- 独立模式初始化开始 ---");

    if (!QSqlDatabase::isDriverAvailable("QSQLITE")) {
        m_lastError = "缺失 QSQLITE 驱动。";
        return false;
    }
    
    // 2026-03-xx 极致剥离：直接连接到目标 .db 文件，不再使用壳/核解密架构
    m_dbPath = dbPath; 
    
    if (m_db.isOpen()) m_db.close();
    
    QString connectionName = "RapidManager_Main_Conn";
    if (QSqlDatabase::contains(connectionName)) {
        m_db = QSqlDatabase::database(connectionName);
    } else {
        m_db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
    }
    
    m_db.setDatabaseName(m_dbPath);

    // [CRITICAL] 检查是否误用了旧版加密壳文件
    if (QFile::exists(m_dbPath)) {
        QFile file(m_dbPath);
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray header = file.read(16);
            if (header.startsWith("RAPIDNOTESHELL")) {
                m_lastError = "检测到旧版加密数据库文件。独立版不再支持壳加密架构，请先使用旧版程序导出数据，或删除此文件以重新开始。";
                logStartup("[ERR] " + m_lastError);
                return false;
            }
            file.close();
        }
    }

    logStartup("正在建立 SQL 直连: " + m_dbPath);
    if (!m_db.open()) {
        // [CRITICAL] 捕获具体 SQL 错误原因（如：权限不足、文件被占用）
        m_lastError = QString("无法打开数据库文件。\n原因: %1 (错误码: %2)").arg(m_db.lastError().text(), m_db.lastError().nativeErrorCode());
        logStartup("[ERR] SQL 打开失败: " + m_lastError);
        return false;
    }
    logStartup("SQL 连接成功。");

    // 记录 SQLite 版本以便排障
    QSqlQuery versionQuery(m_db);
    if (versionQuery.exec("SELECT sqlite_version()")) {
        if (versionQuery.next()) logStartup("SQLite 内核版本: " + versionQuery.value(0).toString());
    }

    // [OPTIMIZATION] 尝试开启 WAL 模式，但不作为致命错误
    QSqlQuery walQuery(m_db);
    logStartup("尝试开启 WAL 模式...");
    if (!walQuery.exec("PRAGMA journal_mode = WAL;")) {
        logStartup("[WRN] 无法开启 WAL 模式 (可能由于网络驱动器限制): " + walQuery.lastError().text());
    } else {
        walQuery.exec("PRAGMA synchronous = NORMAL;");
    }

    // 完整性预检 (同样不作为致命错误，除非建表失败)
    logStartup("执行完整性预检...");
    if (!walQuery.exec("PRAGMA integrity_check;")) {
        logStartup("[WRN] 完整性预检返回异常: " + walQuery.lastError().text());
    }

    logStartup("执行建表及升级检查...");
    if (!createTables()) {
        // [CRITICAL] m_lastError 已经在 createTables 内部被赋予了更详细的 SQL 错误描述
        logStartup("[ERR] " + m_lastError);
        return false;
    }

    m_isInitialized = true;
    logStartup("--- 初始化全部成功 ---");

    // 2026-03-xx 按照用户要求：彻底移除后台自动同步与增量包逻辑，回归纯净数据库模式

    m_autoSaveTimer->start();
    return true;
}

void DatabaseManager::closeAndPack() {
    QMutexLocker locker(&m_mutex);
    if (!m_isInitialized) return;
    m_isInitialized = false;
    
    QString connName = m_db.connectionName();
    if (m_db.isOpen()) {
        QSqlQuery cp(m_db);
        cp.exec("PRAGMA wal_checkpoint(FULL);");
        m_db.close();
    }
    m_db = QSqlDatabase(); 
    if (!connName.isEmpty()) {
        QSqlDatabase::removeDatabase(connName);
    }
    qDebug() << "[DB] 独立版数据库已安全关闭。";
}

bool DatabaseManager::saveKernelToShell(const QString& source) {
    // 2026-03-xx 独立版直连物理文件，无需合壳逻辑
    Q_UNUSED(source);
    return true;
}

bool DatabaseManager::tryRecoverFromBackup() {
    return false;
}

void DatabaseManager::markDirty() {
    m_isDirty = true;
    m_lastActivityTime = QDateTime::currentDateTime();
}

void DatabaseManager::handleAutoSave() {
    // 2026-03-xx 按照用户最高要求：彻底移除后台增量同步逻辑，改为简单的定期全量刷盘
    QMutexLocker locker(&m_mutex);
    if (!m_isDirty) return;

    qint64 idleSecs = m_lastActivityTime.secsTo(QDateTime::currentDateTime());
    if (idleSecs < 10) return; // 闲置 10 秒即全量刷盘

    m_isDirty = false;
    locker.unlock();
    
    QThreadPool::globalInstance()->start([this]() {
        saveKernelToShell("IdleAutoSave");
    });
}

void DatabaseManager::backupIncremental() {}

void DatabaseManager::backupDatabaseLatest() {}
void DatabaseManager::backupDatabase() {}

bool DatabaseManager::createTables() {
    QSqlQuery query(m_db);
    
    // [SAFETY] 封装执行逻辑，自动捕获错误
    auto execQuery = [&](const QString& sql, const QString& context) {
        if (!query.exec(sql)) {
            m_lastError = QString("[%1失败] %2").arg(context, query.lastError().text());
            return false;
        }
        return true;
    };

    QString createNotesTable = R"(
        CREATE TABLE IF NOT EXISTS notes (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            title TEXT,
            content TEXT,
            tags TEXT,
            color TEXT DEFAULT '#2d2d2d',
            category_id INTEGER,
            item_type TEXT DEFAULT 'text',
            data_blob BLOB,
            content_hash TEXT,
            rating INTEGER DEFAULT 0,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            updated_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            is_pinned INTEGER DEFAULT 0,
            is_locked INTEGER DEFAULT 0,
            is_favorite INTEGER DEFAULT 0,
            is_deleted INTEGER DEFAULT 0,
            source_app TEXT,
            source_title TEXT,
            last_accessed_at DATETIME,
            sort_order INTEGER DEFAULT 0,
            remark TEXT DEFAULT ''
        )
    )";
    if (!execQuery(createNotesTable, "创建notes表")) return false;

    QString createCategoriesTable = R"(
        CREATE TABLE IF NOT EXISTS categories (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            parent_id INTEGER,
            color TEXT DEFAULT '#808080',
            sort_order INTEGER DEFAULT 0,
            preset_tags TEXT,
            password TEXT,
            password_hint TEXT,
            is_deleted INTEGER DEFAULT 0,
            is_pinned INTEGER DEFAULT 0,
            updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
        )
    )";
    if (!execQuery(createCategoriesTable, "创建categories表")) return false;

    // [FIX] 彻底强化迁移：严禁跳过。必须确保 categories 拥有 updated_at 字段，否则回收站统合查询必挂。
    {
        auto addCol = [&](const QString& table, const QString& col, const QString& def) {
            QStringList existingCols;
            QSqlQuery check(m_db);
            if (check.exec(QString("PRAGMA table_info(%1)").arg(table))) {
                while (check.next()) existingCols << check.value(1).toString().toLower();
            }
            if (!existingCols.contains(col.toLower())) {
                QSqlQuery alter(m_db);
                if (!alter.exec(QString("ALTER TABLE %1 ADD COLUMN %2 %3").arg(table, col, def))) {
                    m_lastError = QString("[列迁移失败:%1.%2] %3").arg(table, col, alter.lastError().text());
                    return false;
                }
            }
            return true;
        };
        if (!addCol("categories", "updated_at", "DATETIME DEFAULT CURRENT_TIMESTAMP")) return false;
        if (!addCol("categories", "is_deleted", "INTEGER DEFAULT 0")) return false;
        if (!addCol("categories", "is_pinned", "INTEGER DEFAULT 0")) return false;
        if (!addCol("categories", "sort_order", "INTEGER DEFAULT 0")) return false;
        if (!addCol("categories", "color", "TEXT DEFAULT '#808080'")) return false;
        if (!addCol("categories", "parent_id", "INTEGER")) return false;
        if (!addCol("categories", "preset_tags", "TEXT")) return false;
        if (!addCol("categories", "password", "TEXT")) return false;
        if (!addCol("categories", "password_hint", "TEXT")) return false;
    }
    if (!execQuery("CREATE TABLE IF NOT EXISTS tags (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT UNIQUE NOT NULL)", "创建tags表")) return false;
    if (!execQuery("CREATE TABLE IF NOT EXISTS note_tags (note_id INTEGER, tag_id INTEGER, PRIMARY KEY (note_id, tag_id))", "创建note_tags表")) return false;
    if (!execQuery("CREATE INDEX IF NOT EXISTS idx_notes_content_hash ON notes(content_hash)", "创建索引")) return false;

    // [CRITICAL] FTS5 模块预检：检测当前 SQLite 驱动是否加载了 FTS5
    m_hasFts5 = false;
    if (query.exec("SELECT name FROM pragma_module_list() WHERE name='fts5'")) {
        if (query.next()) m_hasFts5 = true;
    } else {
        // pragma_module_list 不存在时，尝试通过创建测试表判定
        if (query.exec("CREATE VIRTUAL TABLE fts5_test_check USING fts5(c)")) {
            m_hasFts5 = true;
            query.exec("DROP TABLE fts5_test_check");
        }
    }

    if (m_hasFts5) {
        qDebug() << "[DB] 环境支持 FTS5，启用高效全文检索";
        bool hasTagsColumn = false;
        if (query.exec("PRAGMA table_info(notes_fts)")) {
            while (query.next()) {
                if (query.value(1).toString() == "tags") {
                    hasTagsColumn = true;
                    break;
                }
            }
        }
        
        if (!hasTagsColumn) {
            query.exec("DROP TABLE IF EXISTS notes_fts");
        }

        QString createFtsTable = R"(
            CREATE VIRTUAL TABLE IF NOT EXISTS notes_fts USING fts5(
                title, content, tags, content='notes', content_rowid='id'
            )
        )";
        // 即使支持模块，建表也可能因其他原因失败，予以捕获但不作为阻断致命错误
        if (!query.exec(createFtsTable)) {
            qWarning() << "[DB] FTS5 建表失败，降级运行:" << query.lastError().text();
            m_hasFts5 = false;
        } else if (!hasTagsColumn) {
            query.exec("INSERT INTO notes_fts(rowid, title, content, tags) SELECT id, title, content, tags FROM notes WHERE is_deleted = 0");
        }
    } else {
        qWarning() << "[DB] 环境不支持 FTS5，将自动降级为 LIKE 模式";
    }

    // 试用期与使用次数表
    if (!execQuery("CREATE TABLE IF NOT EXISTS system_config (key TEXT PRIMARY KEY, value TEXT)", "创建系统配置表")) return false;
    
    // 2026-03-xx 按照用户要求：彻底移除试用信息初始化逻辑

    // [CRITICAL] 待办事项表：扩展支持联动、循环和子任务。
    QString createTodosTable = R"(
        CREATE TABLE IF NOT EXISTS todos (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            title TEXT NOT NULL,
            content TEXT,
            start_time DATETIME,
            end_time DATETIME,
            status INTEGER DEFAULT 0, -- 0:待办, 1:已完成, 2:已逾期
            reminder_time DATETIME,
            priority INTEGER DEFAULT 0,
            color TEXT,
            note_id INTEGER DEFAULT -1,
            repeat_mode INTEGER DEFAULT 0, -- 0:None, 1:Daily, 2:Weekly, 3:Monthly
            parent_id INTEGER DEFAULT -1,
            progress INTEGER DEFAULT 0,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
        )
    )";
    if (!execQuery(createTodosTable, "创建待办事项表")) return false;

    // 增量升级逻辑
    {
        QStringList newCols = {"note_id", "repeat_mode", "parent_id", "progress"};
        for (const auto& col : newCols) {
             QSqlQuery check(m_db);
             if (check.exec(QString("PRAGMA table_info(todos)"))) {
                 bool exists = false;
                 while(check.next()) if(check.value(1).toString().toLower() == col) exists = true;
                 if(!exists) {
                     if (!query.exec(QString("ALTER TABLE todos ADD COLUMN %1 INTEGER DEFAULT 0").arg(col))) {
                         m_lastError = QString("[待办表列迁移失败:%1] %2").arg(col, query.lastError().text());
                         return false;
                     }
                 }
             }
        }
    }

    // [MODIFIED] 强化版迁移：确保 notes 表字段完整
    {
        auto addCol = [&](const QString& table, const QString& col, const QString& def) {
            QStringList existingCols;
            QSqlQuery check(m_db);
            if (check.exec(QString("PRAGMA table_info(%1)").arg(table))) {
                while (check.next()) existingCols << check.value(1).toString().toLower();
            }
            if (!existingCols.contains(col.toLower())) {
                qDebug() << "[DB] 迁移检测：正在补齐" << table << "表的缺失字段 ->" << col;
                QSqlQuery alter(m_db);
                if (!alter.exec(QString("ALTER TABLE %1 ADD COLUMN %2 %3").arg(table, col, def))) {
                    m_lastError = QString("[笔记表列迁移失败:%1] %2").arg(col, alter.lastError().text());
                    return false;
                }
            }
            return true;
        };
        if (!addCol("notes", "sort_order", "INTEGER DEFAULT 0")) return false;
        if (!addCol("notes", "is_deleted", "INTEGER DEFAULT 0")) return false;
        if (!addCol("notes", "updated_at", "DATETIME DEFAULT CURRENT_TIMESTAMP")) return false;
        if (!addCol("notes", "last_accessed_at", "DATETIME")) return false;
        if (!addCol("notes", "is_pinned", "INTEGER DEFAULT 0")) return false;
        if (!addCol("notes", "is_locked", "INTEGER DEFAULT 0")) return false;
        if (!addCol("notes", "is_favorite", "INTEGER DEFAULT 0")) return false;
        if (!addCol("notes", "source_app", "TEXT")) return false;
        if (!addCol("notes", "source_title", "TEXT")) return false;
        if (!addCol("notes", "rating", "INTEGER DEFAULT 0")) return false;
        if (!addCol("notes", "content_hash", "TEXT")) return false;
        if (!addCol("notes", "item_type", "TEXT DEFAULT 'text'")) return false;
        if (!addCol("notes", "category_id", "INTEGER")) return false;
        if (!addCol("notes", "color", "TEXT DEFAULT '#2d2d2d'")) return false;
        if (!addCol("notes", "data_blob", "BLOB")) return false;
        if (!addCol("notes", "tags", "TEXT")) return false;
        if (!addCol("notes", "title", "TEXT")) return false;
        if (!addCol("notes", "content", "TEXT")) return false;
        if (!addCol("notes", "created_at", "DATETIME DEFAULT CURRENT_TIMESTAMP")) return false;
        if (!addCol("notes", "remark", "TEXT DEFAULT ''")) return false;
    }

    return true;
}

void DatabaseManager::addNoteAsync(const QString& title, const QString& content, const QStringList& tags,
                                  const QString& color, int categoryId,
                                  const QString& itemType, const QByteArray& dataBlob,
                                  const QString& sourceApp, const QString& sourceTitle,
                                  const QString& remark) {
    // [OPTIMIZATION] 数据库防洪保护：
    // 如果二进制数据块较大（超过 1MB）且当前线程池积压任务过多，则转为同步执行以利用天然的 UI 阻塞进行限流，防止 OOM。
    bool isHeavy = dataBlob.size() > 1024 * 1024;
    if (isHeavy && QThreadPool::globalInstance()->activeThreadCount() > 4) {
        addNote(title, content, tags, color, categoryId, itemType, dataBlob, sourceApp, sourceTitle, remark);
    } else {
        QThreadPool::globalInstance()->start([=]() {
            instance().addNote(title, content, tags, color, categoryId, itemType, dataBlob, sourceApp, sourceTitle, remark);
        });
    }
}

int DatabaseManager::addNote(const QString& title, const QString& content, const QStringList& tags,
                            const QString& color, int categoryId,
                            const QString& itemType, const QByteArray& dataBlob,
                            const QString& sourceApp, const QString& sourceTitle,
                            const QString& remark) {
    QVariantMap newNoteMap;
    bool success = false;
    QString currentTime = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    QByteArray hashData = dataBlob.isEmpty() ? content.toUtf8() : dataBlob;
    QString contentHash = QCryptographicHash::hash(hashData, QCryptographicHash::Sha256).toHex();
    {   
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) { qDebug() << "[DB] 错误: 数据库未打开"; return 0; }

        QString finalColor = color.isEmpty() ? "#2d2d2d" : color;
        QStringList finalTags = tags;

        // 查重：如果内容已存在，则更新标题、标签及分类
        QSqlQuery checkQuery(m_db);
        checkQuery.prepare("SELECT id, category_id, tags FROM notes WHERE content_hash = :hash AND is_deleted = 0 LIMIT 1");
        checkQuery.bindValue(":hash", contentHash);
        if (checkQuery.exec() && checkQuery.next()) {
            int existingId = checkQuery.value(0).toInt();
            QVariant oldCatVal = checkQuery.value(1);
            
            // 获取已有笔记的详细信息，用于智能判定是否需要更新标题等
            QVariantMap existingNote = getNoteById(existingId);
            QString existingTagsStr = existingNote.value("tags").toString();
            QStringList existingTags = existingTagsStr.split(",", Qt::SkipEmptyParts);
            for(QString& t : existingTags) t = t.trimmed();

            // 智能合并标签
            for (const QString& t : std::as_const(finalTags)) {
                if (!existingTags.contains(t.trimmed())) existingTags << t.trimmed();
            }

            // 漂移保护逻辑：如果笔记已有明确分类，则优先保留原分类，防止在自动归档时发生分类位移
            int finalCatToUse = categoryId;
            if (!oldCatVal.isNull() && oldCatVal.toInt() > 0) {
                finalCatToUse = oldCatVal.toInt(); 
            }

            // 获取新分类/旧分类的颜色
            QString finalColor = color;
            
            if (finalCatToUse != -1) {
                QSqlQuery catQuery(m_db);
                catQuery.prepare("SELECT color, preset_tags FROM categories WHERE id = :id");
                catQuery.bindValue(":id", finalCatToUse);
                if (catQuery.exec() && catQuery.next()) {
                    if (color.isEmpty()) finalColor = catQuery.value(0).toString();
                    QString preset = catQuery.value(1).toString();
                    if (!preset.isEmpty()) {
                        QStringList pTags = preset.split(",", Qt::SkipEmptyParts);
                        for (const QString& t : pTags) {
                            if (!existingTags.contains(t.trimmed())) existingTags << t.trimmed();
                        }
                    }
                }
            }

            QSqlQuery updateQuery(m_db);
            // 重复内容时，更新标签、时间及来源
            QString sql = "UPDATE notes SET tags = :tags, updated_at = :now, source_app = :app, source_title = :stitle, category_id = :cat_id";
            if (!finalColor.isEmpty()) sql += ", color = :color";
            
            // [CRITICAL] 智能标题保护逻辑：禁止恢复“旧版全量覆盖标题”的傻逼行为。
            // 必须确保：仅当原标题是自动生成的通用标题，且新标题更有意义时才覆盖；否则必须保持笔记原始标题不变。
            // [OPTIMIZED] 扩展通用标题判定，支持网址形式标题的自动覆盖替换
            QString existingTitle = existingNote.value("title").toString().trimmed();
            bool isExistingGeneric = existingTitle.isEmpty() || existingTitle == "无标题灵感" || 
                                     existingTitle.startsWith("[截图]") || 
                                     existingTitle.startsWith("[截图]") ||
                                     existingTitle.startsWith("Copied ") ||
                                     existingTitle.startsWith("http://") ||
                                     existingTitle.startsWith("https://") ||
                                     existingTitle.startsWith("www.");
                                     
            bool isNewMeaningful = !title.isEmpty() && !title.startsWith("[拖入") && !title.startsWith("[图片");
            
            if (isExistingGeneric && isNewMeaningful && existingTitle != title) {
                sql += ", title = :title";
            }
            
            sql += " WHERE id = :id";

            updateQuery.prepare(sql);
            updateQuery.bindValue(":tags", existingTags.join(", "));
            updateQuery.bindValue(":now", currentTime);
            updateQuery.bindValue(":app", sourceApp);
            updateQuery.bindValue(":stitle", sourceTitle);
            updateQuery.bindValue(":cat_id", finalCatToUse == -1 ? QVariant(QMetaType::fromType<int>()) : finalCatToUse);
            if (!finalColor.isEmpty()) updateQuery.bindValue(":color", finalColor);
            if (sql.contains(":title")) updateQuery.bindValue(":title", title);
            updateQuery.bindValue(":id", existingId);
            
            if (updateQuery.exec()) success = true;
            if (success) { 
                qDebug() << "[DB] 命中重复记录，已更新 ID:" << existingId;
                locker.unlock(); 
                emit noteUpdated(); 
                return existingId; 
            }
        }
        if (categoryId != -1) {
            QSqlQuery catQuery(m_db);
            catQuery.prepare("SELECT color, preset_tags FROM categories WHERE id = :id");
            catQuery.bindValue(":id", categoryId);
            if (catQuery.exec() && catQuery.next()) {
                if (color.isEmpty()) finalColor = catQuery.value(0).toString();
                QString preset = catQuery.value(1).toString();
                if (!preset.isEmpty()) {
                    QStringList pTags = preset.split(",", Qt::SkipEmptyParts);
                    for (const QString& t : pTags) {
                        QString trimmed = t.trimmed();
                        if (!finalTags.contains(trimmed)) finalTags << trimmed;
                    }
                }
            }
        }
        QSqlQuery query(m_db);
        query.prepare("INSERT INTO notes (title, content, tags, color, category_id, item_type, data_blob, content_hash, created_at, updated_at, source_app, source_title, remark) VALUES (:title, :content, :tags, :color, :category_id, :item_type, :data_blob, :hash, :created_at, :updated_at, :source_app, :source_title, :remark)");
        query.bindValue(":title", title);
        query.bindValue(":content", content);
        
        QStringList cleanedFinalTags;
        for (const QString& t : finalTags) {
            QString tr = t.trimmed();
            if (!tr.isEmpty() && !cleanedFinalTags.contains(tr)) cleanedFinalTags << tr;
        }
        query.bindValue(":tags", cleanedFinalTags.join(", "));
        
        query.bindValue(":color", finalColor);
        query.bindValue(":category_id", categoryId == -1 ? QVariant(QMetaType::fromType<int>()) : categoryId);
        query.bindValue(":item_type", itemType);
        query.bindValue(":data_blob", dataBlob);
        query.bindValue(":hash", contentHash);
        query.bindValue(":created_at", currentTime);
        query.bindValue(":updated_at", currentTime);
        query.bindValue(":source_app", sourceApp);
        query.bindValue(":source_title", sourceTitle);
        query.bindValue(":remark", remark);
        if (query.exec()) {
            success = true;
            markDirty();
            qDebug() << "[DB] 新纪录插入成功";
            QVariant lastId = query.lastInsertId();
            QSqlQuery fetch(m_db);
            fetch.prepare("SELECT * FROM notes WHERE id = :id");
            fetch.bindValue(":id", lastId);
            if (fetch.exec() && fetch.next()) {
                QSqlRecord rec = fetch.record();
                for (int i = 0; i < rec.count(); ++i) newNoteMap[rec.fieldName(i).toLower()] = fetch.value(i);
            }
        }
    }
    if (success && !newNoteMap.isEmpty()) {
        int newId = newNoteMap["id"].toInt();
        syncFts(newId, title, content, newNoteMap["tags"].toString());
        
        // [STABILITY] 跨线程信号同步加固：
        // 如果当前不在主线程执行（由 addNoteAsync 触发），则强制通过 QueuedConnection 发送信号，防止 UI 竞态崩溃
        if (QThread::currentThread() != qApp->thread()) {
            QMetaObject::invokeMethod(this, [this, newNoteMap](){ emit noteAdded(newNoteMap); }, Qt::QueuedConnection);
        } else {
            emit noteAdded(newNoteMap);
        }
        return newId;
    }
    return 0;
}

bool DatabaseManager::updateNote(int id, const QString& title, const QString& content, const QStringList& tags, const QString& color, int categoryId,
                               const QString& itemType, const QByteArray& dataBlob,
                               const QString& sourceApp, const QString& sourceTitle,
                               const QString& remark) {
    bool success = false;
    QString currentTime = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    
    // 重新计算内容哈希
    QByteArray hashData = dataBlob.isEmpty() ? content.toUtf8() : dataBlob;
    QString contentHash = QCryptographicHash::hash(hashData, QCryptographicHash::Sha256).toHex();

    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        QSqlQuery query(m_db);
        
        // [CRITICAL] 锁定：更新笔记属性时必须全量同步所有元数据。严禁遗漏 hash 和 item_type。
        QString sql = "UPDATE notes SET title=:title, content=:content, tags=:tags, updated_at=:updated_at, "
                      "category_id=:category_id, color=:color, last_accessed_at=:now, "
                      "content_hash=:hash, item_type=:type, data_blob=:blob, "
                      "source_app=:app, source_title=:stitle, remark=:remark";
        sql += " WHERE id=:id";

        query.prepare(sql);
        query.bindValue(":title", title);
        query.bindValue(":content", content);
        query.bindValue(":now", currentTime);
        query.bindValue(":hash", contentHash);
        query.bindValue(":type", itemType);
        query.bindValue(":blob", dataBlob);
        query.bindValue(":app", sourceApp);
        query.bindValue(":stitle", sourceTitle);
        query.bindValue(":remark", remark);
        
        QStringList trimmedTags;
        for (const QString& t : tags) {
            QString tr = t.trimmed();
            if (!tr.isEmpty() && !trimmedTags.contains(tr)) trimmedTags << tr;
        }
        query.bindValue(":tags", trimmedTags.join(", "));
        
        query.bindValue(":updated_at", currentTime);
        query.bindValue(":category_id", categoryId == -1 ? QVariant() : categoryId);
        
        QString finalColor = color;
        if (finalColor.isEmpty()) {
            if (categoryId != -1) {
                QSqlQuery catQuery(m_db);
                catQuery.prepare("SELECT color FROM categories WHERE id = :id");
                catQuery.bindValue(":id", categoryId);
                if (catQuery.exec() && catQuery.next()) finalColor = catQuery.value(0).toString();
                else finalColor = "#0A362F";
            } else {
                finalColor = "#0A362F";
            }
        }
        query.bindValue(":color", finalColor);
        query.bindValue(":id", id);
        success = query.exec();
        if (success) markDirty();
    }
    if (success) { 
        QStringList trimmedTags;
        for (const QString& t : tags) {
            QString tr = t.trimmed();
            if (!tr.isEmpty() && !trimmedTags.contains(tr)) trimmedTags << tr;
        }
        syncFts(id, title, content, trimmedTags.join(", ")); 
        
        // [STABILITY] 跨线程信号同步加固
        if (QThread::currentThread() != qApp->thread()) {
            QMetaObject::invokeMethod(this, [this](){ emit noteUpdated(); }, Qt::QueuedConnection);
        } else {
            emit noteUpdated();
        }
    }
    return success;
}

bool DatabaseManager::reorderCategories(int parentId, bool ascending) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;
    QSqlQuery query(m_db);
    if (parentId <= 0) query.prepare("SELECT id, name FROM categories WHERE parent_id IS NULL OR parent_id <= 0");
    else { query.prepare("SELECT id, name FROM categories WHERE parent_id = :pid"); query.bindValue(":pid", parentId); }
    if (!query.exec()) return false;
    struct CatInfo { int id; QString name; };
    QList<CatInfo> list;
    while (query.next()) list.append({query.value(0).toInt(), query.value(1).toString()});
    if (list.isEmpty()) return true;
    std::sort(list.begin(), list.end(), [ascending](const CatInfo& a, const CatInfo& b) {
        if (ascending) return a.name.localeAwareCompare(b.name) < 0;
        return a.name.localeAwareCompare(b.name) > 0;
    });
    m_db.transaction();
    QSqlQuery update(m_db);
    for (int i = 0; i < list.size(); ++i) {
        update.prepare("UPDATE categories SET sort_order = :val WHERE id = :id");
        update.bindValue(":val", i);
        update.bindValue(":id", list[i].id);
        update.exec();
    }
    bool ok = m_db.commit();
    if (ok) { markDirty(); emit categoriesChanged(); }
    return ok;
}

bool DatabaseManager::updateCategoryOrder(int parentId, const QList<int>& categoryIds) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;
    if (!m_db.transaction()) return false;
    QSqlQuery query(m_db);
    query.prepare("UPDATE categories SET parent_id = :pid, sort_order = :order WHERE id = :id");
    for (int i = 0; i < categoryIds.size(); ++i) {
        query.bindValue(":pid", parentId <= 0 ? QVariant() : parentId);
        query.bindValue(":order", i);
        query.bindValue(":id", categoryIds[i]);
        if (!query.exec()) { m_db.rollback(); return false; }
    }
    bool ok = m_db.commit();
    if (ok) { markDirty(); emit categoriesChanged(); }
    return ok;
}

bool DatabaseManager::reorderAllCategories(bool ascending) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;
    QSqlQuery query(m_db);
    query.exec("SELECT DISTINCT parent_id FROM categories");
    QList<int> parents;
    bool hasRoot = false;
    while (query.next()) {
        if (query.value(0).isNull() || query.value(0).toInt() <= 0) hasRoot = true;
        else parents.append(query.value(0).toInt());
    }
    bool success = true;
    if (hasRoot) success &= reorderCategories(-1, ascending);
    for (int pid : parents) success &= reorderCategories(pid, ascending);
    return success;
}

bool DatabaseManager::setCategoryPassword(int id, const QString& password, const QString& hint) {
    bool success = false;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        QString hashedPassword = QString(QCryptographicHash::hash(password.toUtf8(), QCryptographicHash::Sha256).toHex());
        QSqlQuery query(m_db);
        query.prepare("UPDATE categories SET password=:password, password_hint=:hint WHERE id=:id");
        query.bindValue(":password", hashedPassword);
        query.bindValue(":hint", hint);
        query.bindValue(":id", id);
        success = query.exec();
        if (success) markDirty();
    }
    if (success) emit categoriesChanged();
    return success;
}

bool DatabaseManager::removeCategoryPassword(int id) {
    bool success = false;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        QSqlQuery query(m_db);
        query.prepare("UPDATE categories SET password=NULL, password_hint=NULL WHERE id=:id");
        query.bindValue(":id", id);
        success = query.exec();
        if (success) { markDirty(); m_unlockedCategories.remove(id); }
    }
    if (success) emit categoriesChanged();
    return success;
}

bool DatabaseManager::verifyCategoryPassword(int id, const QString& password) {
    bool correct = false;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        QString hashedPassword = QString(QCryptographicHash::hash(password.toUtf8(), QCryptographicHash::Sha256).toHex());
        QSqlQuery query(m_db);
        query.prepare("SELECT password FROM categories WHERE id=:id");
        query.bindValue(":id", id);
        if (query.exec() && query.next()) {
            if (query.value(0).toString() == hashedPassword) correct = true;
        }
    }
    if (correct) unlockCategory(id);
    return correct;
}

bool DatabaseManager::isCategoryLocked(int id) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;
    if (m_unlockedCategories.contains(id)) return false;
    QSqlQuery query(m_db);
    query.prepare("SELECT password FROM categories WHERE id=:id");
    query.bindValue(":id", id);
    if (query.exec() && query.next()) return !query.value(0).toString().isEmpty();
    return false;
}

void DatabaseManager::lockCategory(int id) { { QMutexLocker locker(&m_mutex); m_unlockedCategories.remove(id); } emit categoriesChanged(); }
void DatabaseManager::lockAllCategories() { { QMutexLocker locker(&m_mutex); m_unlockedCategories.clear(); } emit categoriesChanged(); }
void DatabaseManager::toggleLockedCategoriesVisibility() {
    qDebug() << "[TRACE-DB] toggleLockedCategoriesVisibility 被调用。";
    // 2026-03-xx 按照用户要求：无论解锁/锁住状态，切换显示时立即全部重锁
    {
        QMutexLocker locker(&m_mutex);
        m_unlockedCategories.clear();
        m_lockedCategoriesHidden = !m_lockedCategoriesHidden;
        
        QSettings settings;
#ifdef RAPID_MANAGER_TARGET
        settings.beginGroup("RapidManager");
#else
        settings.beginGroup("QuickWindow");
#endif
        settings.setValue("lockedCategoriesHidden", m_lockedCategoriesHidden);
        settings.endGroup();
    }
    emit categoriesChanged();
}
void DatabaseManager::unlockCategory(int id) { { QMutexLocker locker(&m_mutex); m_unlockedCategories.insert(id); } emit categoriesChanged(); }

bool DatabaseManager::restoreAllFromTrash() {
    bool success = false;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        m_db.transaction();
        
        QSqlQuery query(m_db);
        // 恢复所有分类
        query.exec("UPDATE categories SET is_deleted = 0 WHERE is_deleted = 1");
        // 恢复所有笔记，并恢复默认颜色（如果原分类已不存在，这部分逻辑在获取颜色时会处理）
        success = query.exec("UPDATE notes SET is_deleted = 0, updated_at = datetime('now','localtime') WHERE is_deleted = 1");
        
        success = m_db.commit();
    }
    if (success) { markDirty(); emit noteUpdated(); emit categoriesChanged(); }
    return success;
}

bool DatabaseManager::updateNoteState(int id, const QString& column, const QVariant& value) {
    bool success = false;
    QString title, content, tags;
    bool needsFts = false;
    QString currentTime = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        // [CRITICAL] 必须包含 item_type 以支持从图片识别提取的文字类型标记
        // 2026-03-xx 按照用户要求：彻底移除 "is_locked" (单条笔记锁定) 的更新支持
        QStringList allowedColumns = {"is_pinned", "is_favorite", "is_deleted", "tags", "rating", "category_id", "color", "content", "title", "item_type", "remark"};
        if (!allowedColumns.contains(column)) return false;
        QSqlQuery query(m_db);
        if (column == "is_favorite") {
            bool fav = value.toBool();
            // 2026-03-13 按照用户要求：收藏颜色统一为 #F2B705
            QString color = fav ? "#F2B705" : ""; 
            if (!fav) {
                QSqlQuery catQuery(m_db);
                catQuery.prepare("SELECT c.color FROM categories c JOIN notes n ON n.category_id = c.id WHERE n.id = :id");
                catQuery.bindValue(":id", id);
                if (catQuery.exec() && catQuery.next()) color = catQuery.value(0).toString();
                else color = "#0A362F"; 
            }
            // [CRITICAL] 锁定：修改属性必须同步更新 last_accessed_at。严禁移除。
            query.prepare("UPDATE notes SET is_favorite = :val, color = :color, updated_at = :now, last_accessed_at = :now WHERE id = :id");
            query.bindValue(":color", color);
        } else if (column == "is_deleted") {
            bool del = value.toBool();
            QString color = del ? "#2d2d2d" : "#0A362F";
            // [CRITICAL] 锁定：删除状态变更必须同步更新 last_accessed_at。严禁移除。
            // [MODIFIED] 不再强制清除 category_id，以支持原位恢复
            query.prepare("UPDATE notes SET is_deleted = :val, color = :color, updated_at = :now, last_accessed_at = :now WHERE id = :id");
            query.bindValue(":color", color);
        } else if (column == "category_id") {
            int catId = value.isNull() ? -1 : value.toInt();
            QString color = "#0A362F"; 
            if (catId != -1) {
                QSqlQuery catQuery(m_db);
                catQuery.prepare("SELECT color FROM categories WHERE id = :id");
                catQuery.bindValue(":id", catId);
                if (catQuery.exec() && catQuery.next()) color = catQuery.value(0).toString();
            }
            // [CRITICAL] 锁定：移动分类必须同步更新 last_accessed_at。严禁移除。
            query.prepare("UPDATE notes SET category_id = :val, color = :color, is_deleted = 0, updated_at = :now, last_accessed_at = :now WHERE id = :id");
            query.bindValue(":color", color);
        } else {
            // [CRITICAL] 锁定：通用状态修改必须同步更新 last_accessed_at。严禁移除。
            query.prepare(QString("UPDATE notes SET %1 = :val, updated_at = :now, last_accessed_at = :now WHERE id = :id").arg(column));
        }
        query.bindValue(":val", value);
        query.bindValue(":now", currentTime);
        query.bindValue(":id", id);
        success = query.exec();
        if (success) markDirty();
        if (success && (column == "content" || column == "title" || column == "tags")) {
            needsFts = true;
            QSqlQuery fetch(m_db);
            fetch.prepare("SELECT title, content, tags FROM notes WHERE id = ?");
            fetch.addBindValue(id);
            if (fetch.exec() && fetch.next()) { 
                title = fetch.value(0).toString(); 
                content = fetch.value(1).toString(); 
                tags = fetch.value(2).toString();
            }
        }
    } 
    if (success) { if (needsFts) syncFts(id, title, content, tags); emit noteUpdated(); }
    return success;
}

bool DatabaseManager::updateNoteStateBatch(const QList<int>& ids, const QString& column, const QVariant& value) {
    if (ids.isEmpty()) return true;
    bool success = false;
    QString currentTime = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        // [CRITICAL] 保持与 updateNoteState 相同的允许列白名单，确保功能不丢失
        // 2026-03-xx 按照用户要求：彻底移除 "is_locked" (单条笔记锁定) 的批量更新支持
        QStringList allowedColumns = {"is_pinned", "is_favorite", "is_deleted", "tags", "rating", "category_id", "color", "content", "title", "item_type"};
        if (!allowedColumns.contains(column)) return false;
        m_db.transaction();
        QSqlQuery query(m_db);
        if (column == "category_id") {
            int catId = value.isNull() ? -1 : value.toInt();
            QString color = "#0A362F";
            if (catId != -1) {
                QSqlQuery catQuery(m_db);
                catQuery.prepare("SELECT color FROM categories WHERE id = :id");
                catQuery.bindValue(":id", catId);
                if (catQuery.exec() && catQuery.next()) color = catQuery.value(0).toString();
            }
            // [CRITICAL] 锁定：批量移动分类必须同步更新 last_accessed_at。严禁移除。
            query.prepare("UPDATE notes SET category_id = :val, color = :color, is_deleted = 0, updated_at = :now, last_accessed_at = :now WHERE id = :id");
            for (int id : ids) {
                query.bindValue(":val", value);
                query.bindValue(":color", color);
                query.bindValue(":now", currentTime);
                query.bindValue(":id", id);
                query.exec();
            }
        } else if (column == "is_favorite") {
            bool fav = value.toBool();
            if (fav) {
                // [CRITICAL] 锁定：批量收藏同步更新 last_accessed_at。
                // 2026-03-13 按照用户要求：收藏颜色统一为 #F2B705
                query.prepare("UPDATE notes SET is_favorite = 1, color = '#F2B705', updated_at = :now, last_accessed_at = :now WHERE id = :id");
            } else {
                // [CRITICAL] 锁定：批量取消收藏同步更新 last_accessed_at。
                query.prepare("UPDATE notes SET is_favorite = 0, color = COALESCE((SELECT color FROM categories WHERE id = notes.category_id), '#0A362F'), updated_at = :now, last_accessed_at = :now WHERE id = :id");
            }
            for (int id : ids) {
                query.bindValue(":now", currentTime);
                query.bindValue(":id", id);
                query.exec();
            }
        } else if (column == "is_deleted") {
            bool del = value.toBool();
            if (del) {
                // [CRITICAL] 锁定：批量删除同步更新 last_accessed_at。不再清除 category_id 以支持原位恢复。
                query.prepare("UPDATE notes SET is_deleted = 1, color = '#2d2d2d', is_pinned = 0, is_favorite = 0, updated_at = :now, last_accessed_at = :now WHERE id = :id");
            } else {
                // [CRITICAL] 锁定：批量恢复同步更新 last_accessed_at。
                query.prepare("UPDATE notes SET is_deleted = 0, color = '#0A362F', updated_at = :now, last_accessed_at = :now WHERE id = :id");
            }
            for (int id : ids) {
                query.bindValue(":now", currentTime);
                query.bindValue(":id", id);
                query.exec();
            }
        } else {
            // [CRITICAL] 锁定：批量修改通用属性同步更新 last_accessed_at。
            QString sql = QString("UPDATE notes SET %1 = :val, updated_at = :now, last_accessed_at = :now WHERE id = :id").arg(column);
            query.prepare(sql);
            for (int id : ids) {
                query.bindValue(":val", value);
                query.bindValue(":now", currentTime);
                query.bindValue(":id", id);
                query.exec();
            }
        }
        success = m_db.commit();
    }
    if (success) {
        markDirty();
        for (int id : ids) syncFtsById(id);
        emit noteUpdated();
    }
    return success;
}

bool DatabaseManager::recordAccess(int id) {
    bool success = false;
    QString currentTime = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        QSqlQuery query(m_db);
        query.prepare("UPDATE notes SET last_accessed_at = :now WHERE id = :id");
        query.bindValue(":now", currentTime);
        query.bindValue(":id", id);
        success = query.exec();
    }
    return success;
}

bool DatabaseManager::toggleNoteState(int id, const QString& column) {
    QVariant currentVal;
    {
        QMutexLocker locker(&m_mutex);
        QSqlQuery query(m_db);
        query.prepare(QString("SELECT %1 FROM notes WHERE id = :id").arg(column));
        query.bindValue(":id", id);
        if (query.exec() && query.next()) currentVal = query.value(0);
    }
    if (currentVal.isValid()) return updateNoteState(id, column, !currentVal.toBool());
    return false;
}

bool DatabaseManager::moveNotesToCategory(const QList<int>& noteIds, int catId) {
    if (noteIds.isEmpty()) return true;
    bool success = false;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        m_db.transaction();
        QString catColor = "#0A362F"; 
        QString presetTags;
        if (catId != -1) {
            QSqlQuery catQuery(m_db);
            catQuery.prepare("SELECT color, preset_tags FROM categories WHERE id = :id");
            catQuery.bindValue(":id", catId);
            if (catQuery.exec() && catQuery.next()) { catColor = catQuery.value(0).toString(); presetTags = catQuery.value(1).toString(); }
        }
        QSqlQuery query(m_db);
        // [CRITICAL] 移动分类同步更新 last_accessed_at
        query.prepare("UPDATE notes SET category_id = :cat_id, color = :color, is_deleted = 0, updated_at = :now, last_accessed_at = :now WHERE id = :id");
        QString now = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
        for (int id : noteIds) {
            query.bindValue(":cat_id", catId == -1 ? QVariant() : catId);
            query.bindValue(":color", catColor);
            query.bindValue(":now", now);
            query.bindValue(":id", id);
            query.exec();
            if (!presetTags.isEmpty()) {
                QSqlQuery fetchTags(m_db);
                fetchTags.prepare("SELECT tags FROM notes WHERE id = :id");
                fetchTags.bindValue(":id", id);
                if (fetchTags.exec() && fetchTags.next()) {
                    QString existing = fetchTags.value(0).toString();
                    QStringList tagList = existing.split(",", Qt::SkipEmptyParts);
                    QStringList newTags = presetTags.split(",", Qt::SkipEmptyParts);
                    bool changed = false;
                    for (const QString& t : newTags) { if (!tagList.contains(t.trimmed())) { tagList.append(t.trimmed()); changed = true; } }
                    if (changed) { QSqlQuery updateTags(m_db); updateTags.prepare("UPDATE notes SET tags = :tags WHERE id = :id"); updateTags.bindValue(":tags", tagList.join(", ")); updateTags.bindValue(":id", id); updateTags.exec(); }
                }
            }
        }
        success = m_db.commit();
    }
    if (success) {
        markDirty();
        for (int id : noteIds) syncFtsById(id);
        emit noteUpdated();
    }
    return success;
}

bool DatabaseManager::deleteNotesBatch(const QList<int>& ids) {
    if (ids.isEmpty()) return true;
    bool success = false;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        m_db.transaction();
        QSqlQuery query(m_db);
        query.prepare("DELETE FROM notes WHERE id=:id");
        for (int id : ids) { query.bindValue(":id", id); if (query.exec()) removeFts(id); }
        success = m_db.commit();
    }
    if (success) {
        markDirty();
        emit noteUpdated();
    }
    return success;
}

bool DatabaseManager::softDeleteNotes(const QList<int>& ids) {
    if (ids.isEmpty()) return true;
    bool success = false;
    QString currentTime = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        m_db.transaction();
        QSqlQuery query(m_db);
        query.prepare("UPDATE notes SET is_deleted = 1, color = '#2d2d2d', is_pinned = 0, is_favorite = 0, updated_at = :now, last_accessed_at = :now WHERE id = :id");
        for (int id : ids) { query.bindValue(":now", currentTime); query.bindValue(":id", id); query.exec(); }
        success = m_db.commit();
    }
    if (success) {
        markDirty();
        emit noteUpdated();
    }
    return success;
}



// [CRITICAL] 核心搜索逻辑：采用 FTS5 全文检索（降级至 LIKE）。
QList<QVariantMap> DatabaseManager::searchNotes(const QString& keyword, const QString& filterType, const QVariant& filterValue, int page, int pageSize, const QVariantMap& criteria) {
    QMutexLocker locker(&m_mutex);
    QList<QVariantMap> results;
    if (!m_db.isOpen()) {
        qCritical() << "[DB] searchNotes 失败：数据库未打开";
        return results;
    }

    // [NEW] 处理回收站特殊视图：包含已删除的分类
    if (filterType == "trash" && keyword.isEmpty()) {
        // [OLD_VERSION_RECOVERY] 100% 还原旧版 17 字段 SQL 结构，杜绝字段缺失报错
        // 2026-03-xx 按照用户要求：移除笔记级 is_locked 逻辑，查询时强制设为 0
        QString sql = "SELECT id, title, content, tags, color, category_id, item_type, data_blob, created_at, updated_at, is_pinned, 0 AS is_locked, is_favorite, is_deleted, source_app, source_title, last_accessed_at, remark "
                      "FROM notes WHERE is_deleted = 1 "
                      "UNION ALL "
                      "SELECT id, name AS title, '(已删除的分类包)' AS content, '' AS tags, color, parent_id AS category_id, 'deleted_category' AS item_type, NULL AS data_blob, NULL AS created_at, NULL AS updated_at, 0 AS is_pinned, 0 AS is_locked, 0 AS is_favorite, 1 AS is_deleted, '' AS source_app, '' AS source_title, NULL AS last_accessed_at, '' AS remark "
                      "FROM categories WHERE is_deleted = 1 "
                      "ORDER BY is_pinned DESC, updated_at DESC";
        
        QSqlQuery query(m_db);
        if (query.exec(sql)) {
            while (query.next()) {
                QVariantMap map;
                QSqlRecord rec = query.record();
                for (int i = 0; i < rec.count(); ++i) map[rec.fieldName(i)] = query.value(i);
                results.append(map);
            }
        } else {
            qCritical() << "[DB] searchNotes(trash) failed:" << query.lastError().text() << "SQL:" << sql;
        }
        return results;
    }

    QString baseSql = "SELECT notes.* FROM notes ";
    bool useFts = !keyword.isEmpty() && m_hasFts5;

    if (useFts) {
        // [OPTIMIZED] 使用 FTS5 进行全文搜索，显著提升大数据量下的检索速度与相关性排序
        baseSql = "SELECT notes.* FROM notes JOIN notes_fts ON notes.id = notes_fts.rowid ";
    }

    QString whereClause;
    QVariantList params;
    applyCommonFilters(whereClause, params, filterType, filterValue, criteria);
    
    if (!keyword.isEmpty()) {
        if (useFts) {
            whereClause += "AND notes_fts MATCH ? ";
            params << sanitizeFtsKeyword(keyword);
        } else {
            // [FALLBACK] 降级至传统 LIKE 搜索 (兼容不支持FTS5的环境)
            whereClause += "AND (notes.title LIKE ? OR notes.content LIKE ? OR notes.tags LIKE ?) ";
            QString likeKey = "%" + keyword + "%";
            params << likeKey << likeKey << likeKey;
        }
    }
    
    QString finalSql = baseSql + whereClause + "ORDER BY ";
    if (!keyword.isEmpty()) { 
        if (useFts) {
            // FTS 模式下优先使用 rank (相关性)
            finalSql += "notes_fts.rank, is_pinned DESC, sort_order ASC, updated_at DESC"; 
        } else {
            finalSql += "is_pinned DESC, sort_order ASC, updated_at DESC";
        }
    } else {
        if (filterType == "recently_visited") {
            finalSql += "is_pinned DESC, last_accessed_at DESC";
        } else {
            // [CRITICAL] 锁定：除了最近访问和回收站外，其余视图均严格遵循 置顶 > 排序值 > 更新时间 的排序准则。
            finalSql += "is_pinned DESC, sort_order ASC, updated_at DESC";
        }
    }
    
    if (page > 0) finalSql += QString(" LIMIT %1 OFFSET %2").arg(pageSize).arg((page - 1) * pageSize);
    
    QSqlQuery query(m_db);
    query.prepare(finalSql);
    for (int i = 0; i < params.size(); ++i) query.bindValue(i, params[i]);
    
    if (query.exec()) { 
        while (query.next()) { 
            QVariantMap map; 
            QSqlRecord rec = query.record(); 
            for (int i = 0; i < rec.count(); ++i) map[rec.fieldName(i)] = query.value(i); 
            results.append(map); 
        } 
    }
    else qCritical() << "searchNotes failed:" << query.lastError().text();
    return results;
}

// [CRITICAL] 核心计数逻辑：必须与 searchNotes 的过滤条件保持 1:1 同步。
int DatabaseManager::getNotesCount(const QString& keyword, const QString& filterType, const QVariant& filterValue, const QVariantMap& criteria) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return 0;

    if (filterType == "trash") {
        // [OLD_VERSION_RECOVERY] 回归旧版简单计数逻辑
        int trashNotes = 0;
        QSqlQuery nQuery(m_db);
        nQuery.prepare("SELECT COUNT(*) FROM notes WHERE is_deleted = 1");
        if (nQuery.exec() && nQuery.next()) trashNotes = nQuery.value(0).toInt();

        int trashCats = 0;
        QSqlQuery cQuery(m_db);
        cQuery.prepare("SELECT COUNT(*) FROM categories WHERE is_deleted = 1");
        if (cQuery.exec() && cQuery.next()) trashCats = cQuery.value(0).toInt();

        return trashNotes + trashCats;
    }

    QString baseSql = "SELECT COUNT(*) FROM notes ";
    bool useFts = !keyword.isEmpty() && m_hasFts5;

    if (useFts) {
        baseSql = "SELECT COUNT(*) FROM notes JOIN notes_fts ON notes.id = notes_fts.rowid ";
    }

    QString whereClause;
    QVariantList params;
    applyCommonFilters(whereClause, params, filterType, filterValue, criteria);
    
    if (!keyword.isEmpty()) {
        if (useFts) {
            whereClause += "AND notes_fts MATCH ? ";
            params << sanitizeFtsKeyword(keyword);
        } else {
            whereClause += "AND (notes.title LIKE ? OR notes.content LIKE ? OR notes.tags LIKE ?) ";
            QString likeKey = "%" + keyword + "%";
            params << likeKey << likeKey << likeKey;
        }
    }
    
    QSqlQuery query(m_db);
    query.prepare(baseSql + whereClause);
    for (int i = 0; i < params.size(); ++i) query.bindValue(i, params[i]);
    if (query.exec()) { if (query.next()) return query.value(0).toInt(); }
    else qCritical() << "getNotesCount failed:" << query.lastError().text();
    return 0;
}

QStringList DatabaseManager::getAllTags() {
    QMutexLocker locker(&m_mutex);
    QStringList allTags;
    if (!m_db.isOpen()) return allTags;
    QSqlQuery query(m_db);
    if (query.exec("SELECT tags FROM notes WHERE tags != '' AND is_deleted = 0")) {
        while (query.next()) {
            QString tagsStr = query.value(0).toString();
            QStringList parts = tagsStr.split(QRegularExpression("[,，]"), Qt::SkipEmptyParts);
            for (const QString& part : parts) {
                QString trimmed = part.trimmed();
                if (!trimmed.isEmpty() && !allTags.contains(trimmed)) allTags.append(trimmed);
            }
        }
    }
    allTags.sort();
    return allTags;
}

QList<QVariantMap> DatabaseManager::getRecentTagsWithCounts(int limit) {
    QMutexLocker locker(&m_mutex);
    QList<QVariantMap> results;
    if (!m_db.isOpen()) return results;
    struct TagData { QString name; int count = 0; QDateTime lastUsed; };
    QMap<QString, TagData> tagMap;
    QSqlQuery query(m_db);
    if (query.exec("SELECT tags, updated_at FROM notes WHERE tags != '' AND is_deleted = 0")) {
        while (query.next()) {
            QString tagsStr = query.value(0).toString();
            QDateTime updatedAt = query.value(1).toDateTime();
            QStringList parts = tagsStr.split(QRegularExpression("[,，]"), Qt::SkipEmptyParts);
            for (const QString& part : parts) {
                QString name = part.trimmed();
                if (name.isEmpty()) continue;
                if (!tagMap.contains(name)) tagMap[name] = {name, 1, updatedAt};
                else {
                    tagMap[name].count++;
                    if (updatedAt > tagMap[name].lastUsed) tagMap[name].lastUsed = updatedAt;
                }
            }
        }
    }
    QList<TagData> sortedList = tagMap.values();
    std::sort(sortedList.begin(), sortedList.end(), [](const TagData& a, const TagData& b) { if (a.lastUsed != b.lastUsed) return a.lastUsed > b.lastUsed; return a.count > b.count; });
    int actualLimit = qMin(limit, (int)sortedList.size());
    for (int i = 0; i < actualLimit; ++i) { QVariantMap m; m["name"] = sortedList[i].name; m["count"] = sortedList[i].count; results.append(m); }
    return results;
}

int DatabaseManager::addCategory(const QString& name, int parentId, const QString& color) {
    int lastId = -1;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return -1;
        int maxOrder = 0;
        QSqlQuery orderQuery(m_db);
        if (parentId == -1) orderQuery.exec("SELECT MAX(sort_order) FROM categories WHERE parent_id IS NULL OR parent_id = -1");
        else { orderQuery.prepare("SELECT MAX(sort_order) FROM categories WHERE parent_id = :pid"); orderQuery.bindValue(":pid", parentId); orderQuery.exec(); }
        if (orderQuery.next()) maxOrder = orderQuery.value(0).toInt();
        QString chosenColor = color;
        if (chosenColor.isEmpty()) { static const QStringList palette = { "#FF6B6B", "#4ECDC4", "#45B7D1", "#96CEB4", "#FFEEAD", "#D4A5A5", "#9B59B6", "#3498DB", "#E67E22", "#2ECC71", "#E74C3C", "#F1C40F", "#1ABC9C", "#34495E", "#95A5A6" }; chosenColor = palette.at(QRandomGenerator::global()->bounded(palette.size())); }
        QSqlQuery query(m_db);
        query.prepare("INSERT INTO categories (name, parent_id, color, sort_order) VALUES (:name, :parent_id, :color, :sort_order)");
        query.bindValue(":name", name);
        query.bindValue(":parent_id", parentId == -1 ? QVariant(QMetaType::fromType<int>()) : parentId);
        query.bindValue(":color", chosenColor);
        query.bindValue(":sort_order", maxOrder + 1);
        if (query.exec()) { lastId = query.lastInsertId().toInt(); markDirty(); }
    }
    if (lastId != -1) emit categoriesChanged();
    return lastId;
}

bool DatabaseManager::toggleCategoryPinned(int id) {
    bool success = false;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        QSqlQuery query(m_db);
        query.prepare("UPDATE categories SET is_pinned = NOT is_pinned WHERE id = :id");
        query.bindValue(":id", id);
        success = query.exec();
        if (success) markDirty();
    }
    if (success) emit categoriesChanged();
    return success;
}

bool DatabaseManager::renameCategory(int id, const QString& name) {
    bool success = false;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        QSqlQuery query(m_db);
        query.prepare("UPDATE categories SET name=:name WHERE id=:id");
        query.bindValue(":name", name);
        query.bindValue(":id", id);
        success = query.exec();
        if (success) markDirty();
    }
    if (success) emit categoriesChanged();
    return success;
}

bool DatabaseManager::setCategoryColor(int id, const QString& color) {
    bool success = false;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        m_db.transaction();
        QSqlQuery treeQuery(m_db);
        // [STABILITY] 增加递归深度限制（50层），防止循环引用导致的 SQL 执行器爆栈
        treeQuery.prepare(R"(
            WITH RECURSIVE category_tree(id, depth) AS (
                SELECT :id, 0 
                UNION ALL 
                SELECT c.id, ct.depth + 1 FROM categories c JOIN category_tree ct ON c.parent_id = ct.id 
                WHERE ct.depth < 50
            ) SELECT id FROM category_tree)");
        treeQuery.bindValue(":id", id);
        QList<int> allIds;
        if (treeQuery.exec()) { while (treeQuery.next()) allIds << treeQuery.value(0).toInt(); }
        if (!allIds.isEmpty()) {
            QString placeholders;
            for(int i=0; i<allIds.size(); ++i) placeholders += (i==0 ? "?" : ",?");
            QSqlQuery updateNotes(m_db);
            updateNotes.prepare(QString("UPDATE notes SET color = ? WHERE category_id IN (%1)").arg(placeholders));
            updateNotes.addBindValue(color);
            for(int cid : allIds) updateNotes.addBindValue(cid);
            updateNotes.exec();
            QSqlQuery updateCats(m_db);
            updateCats.prepare(QString("UPDATE categories SET color = ? WHERE id IN (%1)").arg(placeholders));
            updateCats.addBindValue(color);
            for(int cid : allIds) updateCats.addBindValue(cid);
            updateCats.exec();
        }
        success = m_db.commit();
    }
    if (success) markDirty();
    if (success) { emit categoriesChanged(); emit noteUpdated(); }
    return success;
}

bool DatabaseManager::hardDeleteCategories(const QList<int>& ids) {
    // 2026-03-xx 按照用户要求：分类物理删除，笔记软删除（移至回收站并重置 category_id）
    if (ids.isEmpty()) return true;
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    if (!m_db.transaction()) {
        qWarning() << "[DB] hardDeleteCategories 开启事务失败";
        return false;
    }

    QList<int> allIds;
    for (int startId : ids) {
        // [MODIFIED] 必须包含递归逻辑，确保所有子分类被物理清除，笔记被正确移出
        QSqlQuery treeQuery(m_db);
        treeQuery.prepare(R"(
            WITH RECURSIVE category_tree(id, depth) AS (
                SELECT :id, 0
                UNION ALL
                SELECT c.id, ct.depth + 1 FROM categories c JOIN category_tree ct ON c.parent_id = ct.id
                WHERE ct.depth < 50
            ) SELECT id FROM category_tree
        )");
        treeQuery.bindValue(":id", startId);
        if (treeQuery.exec()) {
            while (treeQuery.next()) {
                int cid = treeQuery.value(0).toInt();
                if (!allIds.contains(cid)) allIds << cid;
            }
        }
    }

    QStringList idStrings;
    for(int cid : allIds) idStrings << QString::number(cid);
    QString joinedIds = idStrings.join(",");

    // 1. 软删除关联笔记：标记 is_deleted=1，并将 category_id 设为 -1 (未分类)，防止孤儿记录
    QSqlQuery softDelNotes(m_db);
    QString softDelSql = QString(
        "UPDATE notes SET is_deleted = 1, category_id = -1, color = '#2d2d2d', "
        "is_pinned = 0, is_favorite = 0, updated_at = datetime('now','localtime') "
        "WHERE category_id IN (%1)"
    ).arg(joinedIds);

    if (!softDelNotes.exec(softDelSql)) {
        qWarning() << "[DB] 混合删除-笔记软处理失败:" << softDelNotes.lastError().text();
        m_db.rollback();
        return false;
    }

    // 2. 物理删除分类自身
    QSqlQuery query(m_db);
    bool ok = query.exec(QString("DELETE FROM categories WHERE id IN (%1)").arg(joinedIds));

    if (ok) {
        m_db.commit();
        qDebug() << "[DB] 成功执行混合删除：物理清除分类" << allIds.size() << "个，笔记移入回收站" << softDelNotes.numRowsAffected() << "条";
        markDirty();
        emit categoriesChanged();
        emit noteUpdated();
    } else {
        m_db.rollback();
        qWarning() << "[DB] 混合删除-分类物理清除失败:" << query.lastError().text();
    }
    return ok;
}

bool DatabaseManager::softDeleteCategories(const QList<int>& ids) {
    // 2026-03-xx 增加详尽日志，排查删除失效问题
    qDebug() << "[DB] softDeleteCategories 入口参数 ids:" << ids;
    if (ids.isEmpty()) return true;
    bool success = false;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        if (!m_db.transaction()) {
            qWarning() << "[DB] 无法开启事务:" << m_db.lastError().text();
            return false;
        }
        
        QSqlQuery query(m_db);
        for (int id : ids) {
            // 使用递归 CTE 找到所有子分类 ID
            QSqlQuery treeQuery(m_db);
            treeQuery.prepare(R"(
                WITH RECURSIVE category_tree(id, depth) AS (
                    SELECT :id, 0
                    UNION ALL
                    SELECT c.id, ct.depth + 1 FROM categories c JOIN category_tree ct ON c.parent_id = ct.id
                    WHERE ct.depth < 50
                ) SELECT id FROM category_tree
            )");
            treeQuery.bindValue(":id", id);
            QList<int> allIds;
            if (treeQuery.exec()) {
                while (treeQuery.next()) allIds << treeQuery.value(0).toInt();
            } else {
                qWarning() << "[DB] 递归查询失败:" << treeQuery.lastError().text();
            }

            qDebug() << "[DB] 分类 ID:" << id << "递归展开后的 IDs:" << allIds;

            if (!allIds.isEmpty()) {
                QStringList idStrings;
                for(int cid : allIds) idStrings << QString::number(cid);
                QString joinedIds = idStrings.join(",");

                // 1. 标记分类为已删除 (2026-03-xx 放弃 prepare 以解决 Parameter count mismatch)
                QSqlQuery delCat(m_db);
                if (!delCat.exec(QString("UPDATE categories SET is_deleted = 1, updated_at = datetime('now','localtime') WHERE id IN (%1)").arg(joinedIds))) {
                    qWarning() << "[DB] 更新 categories 状态失败:" << delCat.lastError().text();
                    m_db.rollback();
                    return false;
                } else {
                    qDebug() << "[DB] 成功标记" << delCat.numRowsAffected() << "个分类为已删除";
                }

                // 2. 标记所属笔记为已删除
                QSqlQuery delNotes(m_db);
                if (!delNotes.exec(QString("UPDATE notes SET is_deleted = 1, color = '#2d2d2d', is_pinned = 0, is_favorite = 0, updated_at = datetime('now','localtime'), last_accessed_at = datetime('now','localtime') WHERE category_id IN (%1)").arg(joinedIds))) {
                    qWarning() << "[DB] 更新 notes 状态失败:" << delNotes.lastError().text();
                    m_db.rollback();
                    return false;
                } else {
                    qDebug() << "[DB] 成功标记所属分类下的" << delNotes.numRowsAffected() << "条笔记为已删除";
                }
            }
        }
        success = m_db.commit();
        if (!success) {
            qWarning() << "[DB] 事务提交失败:" << m_db.lastError().text();
            m_db.rollback();
        } else {
            qDebug() << "[DB] softDeleteCategories 事务提交成功";
        }
    }
    if (success) {
        markDirty();
        emit categoriesChanged();
        emit noteUpdated();
    }
    return success;
}

bool DatabaseManager::restoreCategories(const QList<int>& ids) {
    if (ids.isEmpty()) return true;
    bool success = false;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        m_db.transaction();
        
        QSqlQuery query(m_db);
        for (int id : ids) {
            // 同样递归找到所有子项，确保整树恢复
            QSqlQuery treeQuery(m_db);
            treeQuery.prepare(R"(
                WITH RECURSIVE category_tree(id, depth) AS (
                    SELECT :id, 0
                    UNION ALL
                    SELECT c.id, ct.depth + 1 FROM categories c JOIN category_tree ct ON c.parent_id = ct.id
                    WHERE ct.depth < 50
                ) SELECT id FROM category_tree
            )");
            treeQuery.bindValue(":id", id);
            QList<int> allIds;
            if (treeQuery.exec()) {
                while (treeQuery.next()) allIds << treeQuery.value(0).toInt();
            }

            if (!allIds.isEmpty()) {
                QStringList placeholders;
                for(int i=0; i<allIds.size(); ++i) placeholders << "?";
                QString joined = placeholders.join(",");

                // 1. 恢复分类
                QSqlQuery resCat(m_db);
                resCat.prepare(QString("UPDATE categories SET is_deleted = 0 WHERE id IN (%1)").arg(joined));
                for(int cid : allIds) resCat.addBindValue(cid);
                resCat.exec();

                // 2. 恢复笔记。同步更新最后访问时间。
                QSqlQuery resNotes(m_db);
                resNotes.prepare(QString("UPDATE notes SET is_deleted = 0, updated_at = datetime('now','localtime'), last_accessed_at = datetime('now','localtime') WHERE category_id IN (%1)").arg(joined));
                for(int cid : allIds) resNotes.addBindValue(cid);
                resNotes.exec();
            }
        }
        success = m_db.commit();
    }
    if (success) {
        markDirty();
        emit categoriesChanged();
        emit noteUpdated();
    }
    return success;
}

bool DatabaseManager::moveNote(int id, DatabaseManager::MoveDirection direction, const QString& filterType, const QVariant& filterValue, const QVariantMap& criteria) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    // 1. 获取当前视图下的所有笔记 ID (按当前排序)
    QString baseSql = "SELECT id FROM notes ";
    QString whereClause;
    QVariantList params;
    applyCommonFilters(whereClause, params, filterType, filterValue, criteria);
    
    // 排除置顶干扰，只在同类（置顶或非置顶）内移动，或者简单处理：按当前最终显示顺序获取列表
    QString finalSql = baseSql + whereClause;
    if (filterType == "recently_visited") finalSql += " ORDER BY is_pinned DESC, last_accessed_at DESC";
    else finalSql += " ORDER BY is_pinned DESC, sort_order ASC, updated_at DESC";
    
    QSqlQuery query(m_db);
    query.prepare(finalSql);
    for (int i = 0; i < params.size(); ++i) query.bindValue(i, params[i]);
    
    QList<int> ids;
    if (query.exec()) {
        while (query.next()) ids << query.value(0).toInt();
    } else return false;

    int currentIndex = ids.indexOf(id);
    if (currentIndex == -1) return false;

    // 2. 调整位置
    switch (direction) {
        case Up: 
            if (currentIndex > 0) std::swap(ids[currentIndex], ids[currentIndex - 1]); 
            else return false; 
            break;
        case Down: 
            if (currentIndex < ids.size() - 1) std::swap(ids[currentIndex], ids[currentIndex + 1]); 
            else return false; 
            break;
        case Top: 
            if (currentIndex > 0) { ids.removeAt(currentIndex); ids.prepend(id); } 
            else return false; 
            break;
        case Bottom: 
            if (currentIndex < ids.size() - 1) { ids.removeAt(currentIndex); ids.append(id); } 
            else return false; 
            break;
    }

    // 3. 批量更新 sort_order
    m_db.transaction();
    QSqlQuery update(m_db);
    for (int i = 0; i < ids.size(); ++i) {
        update.prepare("UPDATE notes SET sort_order = :val WHERE id = :id");
        update.bindValue(":val", i);
        update.bindValue(":id", ids[i]);
        update.exec();
    }
    bool ok = m_db.commit();
    if (ok) { markDirty(); emit noteUpdated(); }
    return ok;
}

bool DatabaseManager::moveNotesToRow(const QList<int>& idsToMove, int targetRow, const QString& filterType, const QVariant& filterValue, const QVariantMap& criteria) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    // 1. 获取当前视图下的完整 ID 列表 (按当前排序)
    QString baseSql = "SELECT id FROM notes ";
    QString whereClause;
    QVariantList params;
    applyCommonFilters(whereClause, params, filterType, filterValue, criteria);
    
    QString finalSql = baseSql + whereClause;
    if (filterType == "recently_visited") finalSql += " ORDER BY is_pinned DESC, last_accessed_at DESC";
    else if (filterType == "trash") finalSql += " ORDER BY updated_at DESC";
    else finalSql += " ORDER BY is_pinned DESC, sort_order ASC, updated_at DESC";
    
    QSqlQuery query(m_db);
    query.prepare(finalSql);
    for (int i = 0; i < params.size(); ++i) query.bindValue(i, params[i]);
    
    QList<int> fullList;
    if (query.exec()) {
        while (query.next()) fullList << query.value(0).toInt();
    } else return false;

    // 2. 执行逻辑上的移动
    // 先移除要移动的项
    for (int id : idsToMove) {
        fullList.removeAll(id);
    }
    
    // 在目标位置重新插入 (注意范围限制)
    int actualTarget = qBound(0, targetRow, fullList.size());
    for (int i = 0; i < idsToMove.size(); ++i) {
        fullList.insert(actualTarget + i, idsToMove[i]);
    }

    // 3. 批量更新 sort_order
    m_db.transaction();
    QSqlQuery update(m_db);
    for (int i = 0; i < fullList.size(); ++i) {
        update.prepare("UPDATE notes SET sort_order = :val WHERE id = :id");
        update.bindValue(":val", i);
        update.bindValue(":id", fullList[i]);
        update.exec();
    }
    bool ok = m_db.commit();
    if (ok) { markDirty(); emit noteUpdated(); }
    return ok;
}

bool DatabaseManager::reorderNotes(const QString& filterType, const QVariant& filterValue, bool ascending, const QVariantMap& criteria) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    QString baseSql = "SELECT id, title FROM notes ";
    QString whereClause;
    QVariantList params;
    applyCommonFilters(whereClause, params, filterType, filterValue, criteria);
    
    QSqlQuery query(m_db);
    query.prepare(baseSql + whereClause);
    for (int i = 0; i < params.size(); ++i) query.bindValue(i, params[i]);
    
    struct NoteSortInfo { int id; QString title; };
    QList<NoteSortInfo> list;
    if (query.exec()) {
        while (query.next()) list.append({query.value(0).toInt(), query.value(1).toString()});
    } else return false;

    if (list.isEmpty()) return true;

    std::sort(list.begin(), list.end(), [ascending](const NoteSortInfo& a, const NoteSortInfo& b) {
        if (ascending) return a.title.localeAwareCompare(b.title) < 0;
        return a.title.localeAwareCompare(b.title) > 0;
    });

    m_db.transaction();
    QSqlQuery update(m_db);
    for (int i = 0; i < list.size(); ++i) {
        update.prepare("UPDATE notes SET sort_order = :val WHERE id = :id");
        update.bindValue(":val", i);
        update.bindValue(":id", list[i].id);
        update.exec();
    }
    bool ok = m_db.commit();
    if (ok) { markDirty(); emit noteUpdated(); }
    return ok;
}

bool DatabaseManager::moveCategory(int id, DatabaseManager::MoveDirection direction) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;
    int parentId = -1;
    QSqlQuery parentQuery(m_db);
    parentQuery.prepare("SELECT parent_id FROM categories WHERE id = :id");
    parentQuery.bindValue(":id", id);
    if (parentQuery.exec() && parentQuery.next()) parentId = parentQuery.value(0).isNull() ? -1 : parentQuery.value(0).toInt();
    else return false;
    QSqlQuery siblingsQuery(m_db);
    if (parentId == -1) siblingsQuery.prepare("SELECT id FROM categories WHERE parent_id IS NULL OR parent_id = -1 ORDER BY sort_order ASC");
    else { siblingsQuery.prepare("SELECT id FROM categories WHERE parent_id = :pid ORDER BY sort_order ASC"); siblingsQuery.bindValue(":pid", parentId); }
    if (!siblingsQuery.exec()) return false;
    QList<int> siblings;
    while (siblingsQuery.next()) siblings << siblingsQuery.value(0).toInt();
    int currentIndex = siblings.indexOf(id);
    if (currentIndex == -1) return false;
    switch (direction) {
        case Up: if (currentIndex > 0) std::swap(siblings[currentIndex], siblings[currentIndex - 1]); else return false; break;
        case Down: if (currentIndex < siblings.size() - 1) std::swap(siblings[currentIndex], siblings[currentIndex + 1]); else return false; break;
        case Top: if (currentIndex > 0) { siblings.removeAt(currentIndex); siblings.prepend(id); } else return false; break;
        case Bottom: if (currentIndex < siblings.size() - 1) { siblings.removeAt(currentIndex); siblings.append(id); } else return false; break;
    }
    return updateCategoryOrder(parentId, siblings);
}

QList<QVariantMap> DatabaseManager::getAllCategories() {
    QMutexLocker locker(&m_mutex);
    QList<QVariantMap> results;
    if (!m_db.isOpen()) return results;
    QSqlQuery query(m_db);
    // [MODIFIED] 严格遵循：置顶 > 排序值 排序
    if (query.exec("SELECT * FROM categories WHERE is_deleted = 0 ORDER BY is_pinned DESC, sort_order ASC")) { 
        while (query.next()) { 
            QVariantMap map; 
            QSqlRecord rec = query.record(); 
            for (int i = 0; i < rec.count(); ++i) map[rec.fieldName(i)] = query.value(i); 
            results.append(map); 
        } 
    }
    return results;
}

QList<DatabaseManager::Todo> DatabaseManager::getAllTodos() {
    QMutexLocker locker(&m_mutex);
    QList<Todo> results;
    if (!m_db.isOpen()) return results;
    
    QSqlQuery query(m_db);
    // [USER_REQUEST] 获取所有任务，用于左侧栏全局视图
    query.exec("SELECT * FROM todos ORDER BY updated_at DESC");
    
    while (query.next()) {
        Todo t;
        t.id = query.value("id").toInt();
        t.title = query.value("title").toString();
        t.content = query.value("content").toString();
        t.startTime = QDateTime::fromString(query.value("start_time").toString(), "yyyy-MM-dd HH:mm:ss");
        t.endTime = QDateTime::fromString(query.value("end_time").toString(), "yyyy-MM-dd HH:mm:ss");
        t.status = query.value("status").toInt();
        t.reminderTime = QDateTime::fromString(query.value("reminder_time").toString(), "yyyy-MM-dd HH:mm:ss");
        t.priority = query.value("priority").toInt();
        t.color = query.value("color").toString();
        t.noteId = query.value("note_id").toInt();
        t.repeatMode = query.value("repeat_mode").toInt();
        t.parentId = query.value("parent_id").toInt();
        t.progress = query.value("progress").toInt();
        t.createdAt = QDateTime::fromString(query.value("created_at").toString(), "yyyy-MM-dd HH:mm:ss");
        t.updatedAt = QDateTime::fromString(query.value("updated_at").toString(), "yyyy-MM-dd HH:mm:ss");
        results.append(t);
    }
    return results;
}

QList<QVariantMap> DatabaseManager::getChildCategories(int parentId) {
    QMutexLocker locker(&m_mutex);
    QList<QVariantMap> results;
    if (!m_db.isOpen()) return results;
    QSqlQuery query(m_db);
    if (parentId <= 0) {
        query.prepare("SELECT * FROM categories WHERE (parent_id IS NULL OR parent_id <= 0) AND is_deleted = 0 ORDER BY is_pinned DESC, sort_order ASC");
    } else {
        query.prepare("SELECT * FROM categories WHERE parent_id = ? AND is_deleted = 0 ORDER BY is_pinned DESC, sort_order ASC");
        query.addBindValue(parentId);
    }
    if (query.exec()) {
        while (query.next()) {
            QVariantMap map;
            QSqlRecord rec = query.record();
            for (int i = 0; i < rec.count(); ++i) map[rec.fieldName(i).toLower()] = query.value(i);
            results.append(map);
        }
    }
    return results;
}

bool DatabaseManager::emptyTrash() {
    bool success = false;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        m_db.transaction();
        
        QSqlQuery query(m_db);
        // 1. 物理删除笔记
        query.exec("DELETE FROM notes WHERE is_deleted = 1");
        
        // 2. 物理删除分类
        query.exec("DELETE FROM categories WHERE is_deleted = 1");
        
        success = m_db.commit();
    }
    if (success) { markDirty(); emit noteUpdated(); }
    return success;
}

bool DatabaseManager::setCategoryPresetTags(int catId, const QString& tags) {
    bool ok = false;
    QList<int> affectedIds;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        m_db.transaction();
        QSqlQuery query(m_db);
        query.prepare("UPDATE categories SET preset_tags=:tags WHERE id=:id");
        query.bindValue(":tags", tags);
        query.bindValue(":id", catId);
        if (!query.exec()) { m_db.rollback(); return false; }
        if (!tags.isEmpty()) {
            QStringList newTagsList = tags.split(",", Qt::SkipEmptyParts);
            QSqlQuery fetchNotes(m_db);
            fetchNotes.prepare("SELECT id, tags FROM notes WHERE category_id = :catId AND is_deleted = 0");
            fetchNotes.bindValue(":catId", catId);
            if (fetchNotes.exec()) {
                while (fetchNotes.next()) {
                    int noteId = fetchNotes.value(0).toInt();
                    QString existingTagsStr = fetchNotes.value(1).toString();
                    QStringList existingTags = existingTagsStr.split(",", Qt::SkipEmptyParts);
                    bool changed = false;
                    for (const QString& t : newTagsList) { QString trimmed = t.trimmed(); if (!trimmed.isEmpty() && !existingTags.contains(trimmed)) { existingTags.append(trimmed); changed = true; } }
                    if (changed) { 
                        affectedIds << noteId;
                        QSqlQuery updateNote(m_db); 
                        updateNote.prepare("UPDATE notes SET tags = :tags WHERE id = :id"); 
                        updateNote.bindValue(":tags", existingTags.join(", ")); 
                        updateNote.bindValue(":id", noteId); 
                        updateNote.exec(); 
                    }
                }
            }
        }
        ok = m_db.commit();
    }
    if (ok) markDirty();
    if (ok) { 
        for (int id : affectedIds) syncFtsById(id);
        emit categoriesChanged(); 
        emit noteUpdated(); 
    }
    return ok;
}

QString DatabaseManager::getCategoryPresetTags(int catId) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return "";
    QSqlQuery query(m_db);
    query.prepare("SELECT preset_tags FROM categories WHERE id=:id");
    query.bindValue(":id", catId);
    if (query.exec() && query.next()) return query.value(0).toString();
    return "";
}

QVariantMap DatabaseManager::getNoteById(int id) {
    QMutexLocker locker(&m_mutex);
    QVariantMap map;
    if (!m_db.isOpen()) return map;
    QSqlQuery query(m_db);
    query.prepare("SELECT * FROM notes WHERE id = :id");
    query.bindValue(":id", id);
    if (query.exec() && query.next()) {
        QSqlRecord rec = query.record();
        for (int i = 0; i < rec.count(); ++i) {
            map[rec.fieldName(i).toLower()] = query.value(i);
        }
    }
    return map;
}

int DatabaseManager::getLastCreatedNoteId() {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return 0;
    QSqlQuery query(m_db);
    // [USER_REQUEST] 核心修复：直接通过 ID 倒序获取最后创建的一条非删除笔记，彻底杜绝排序干扰
    if (query.exec("SELECT id FROM notes WHERE is_deleted = 0 ORDER BY id DESC LIMIT 1")) {
        if (query.next()) return query.value(0).toInt();
    }
    return 0;
}

QVariantMap DatabaseManager::getCounts() {
    QMutexLocker locker(&m_mutex);
    QVariantMap counts;
    if (!m_db.isOpen()) return counts;
    QSqlQuery query(m_db);
    auto getCount = [&](const QString& where, bool applySecurity = true) {
        QString sql = "SELECT COUNT(*) FROM notes WHERE " + where;
        QVariantList params;
        if (applySecurity) { QString securityClause; applySecurityFilter(securityClause, params, "all"); sql += " " + securityClause; }
        QSqlQuery q(m_db);
        q.prepare(sql);
        for(int i=0; i<params.size(); ++i) q.bindValue(i, params[i]);
        if (q.exec()) { if (q.next()) return q.value(0).toInt(); }
        return 0;
    };
    counts["all"] = getCount("is_deleted = 0");
    counts["today"] = getCount("is_deleted = 0 AND date(created_at) = date('now', 'localtime')");
    counts["yesterday"] = getCount("is_deleted = 0 AND date(created_at) = date('now', '-1 day', 'localtime')");
    counts["recently_visited"] = getCount("is_deleted = 0 AND date(last_accessed_at) = date('now', 'localtime')");
    // 2026-03-xx 按照用户要求修复傻逼逻辑：统一“未分类”判定口径，兼容 NULL 和 -1（分类物理删除后的残留）
    counts["uncategorized"] = getCount("is_deleted = 0 AND (category_id IS NULL OR category_id <= 0)");
    counts["untagged"] = getCount("is_deleted = 0 AND (tags IS NULL OR tags = '')");
    counts["bookmark"] = getCount("is_deleted = 0 AND is_favorite = 1");
    
    // [MODIFIED] 统一回收站统计口径：包含已删除笔记 + 已删除分类包
    int trashNotes = getCount("is_deleted = 1", false);
    int trashCats = 0;
    QSqlQuery catTrashQuery(m_db);
    if (catTrashQuery.exec("SELECT COUNT(*) FROM categories WHERE is_deleted = 1")) {
        if (catTrashQuery.next()) trashCats = catTrashQuery.value(0).toInt();
    }
    counts["trash"] = trashNotes + trashCats;

    // [CRITICAL] 锁定：核心分类统计逻辑。必须通过 parentMap 递归累加子分类计数到父分类，严禁改回简单的 GROUP BY 统计，以确保主分类显示的数字包含子项总和。
    QMap<int, int> directCounts;
    if (query.exec("SELECT category_id, COUNT(*) FROM notes WHERE is_deleted = 0 AND category_id IS NOT NULL GROUP BY category_id")) {
        while (query.next()) {
            directCounts[query.value(0).toInt()] = query.value(1).toInt();
        }
    }

    QMap<int, int> parentMap;
    QList<int> allCatIds;
    if (query.exec("SELECT id, parent_id FROM categories WHERE is_deleted = 0")) {
        while (query.next()) {
            int id = query.value(0).toInt();
            int parentId = query.value(1).isNull() ? -1 : query.value(1).toInt();
            parentMap[id] = parentId;
            allCatIds << id;
        }
    }

    QMap<int, int> recursiveCounts;
    for (int id : allCatIds) {
        int count = directCounts.value(id, 0);
        if (count == 0) continue;
        int currentId = id;
        while (currentId > 0) {
            recursiveCounts[currentId] += count;
            currentId = parentMap.value(currentId, -1);
        }
    }

    for (auto it = recursiveCounts.begin(); it != recursiveCounts.end(); ++it) {
        counts["cat_" + QString::number(it.key())] = it.value();
    }

    return counts;
}

QVariantMap DatabaseManager::getTrialStatus(bool validate) {
    // 2026-03-xx 按照用户最高要求：彻底废除授权校验逻辑，直接返回永久激活状态
    QVariantMap finalStatus;
    finalStatus["is_activated"] = true;
    finalStatus["fingerprint_mismatch"] = false;
    finalStatus["expired"] = false;
    finalStatus["usage_limit_reached"] = false;
    finalStatus["days_left"] = 99999;
    finalStatus["failed_attempts"] = 0;
    finalStatus["is_locked"] = false;
    return finalStatus;
}

void DatabaseManager::incrementUsageCount() {}

void DatabaseManager::beginBatch() {
    QMutexLocker locker(&m_mutex);
    m_isBatchMode = true;
    if (m_db.isOpen()) {
        m_db.transaction();
    }
}

void DatabaseManager::endBatch() {
    QMutexLocker locker(&m_mutex);
    if (m_db.isOpen()) {
        m_db.commit();
        qDebug() << "[DB] 批量模式结束：事务已毫秒级提交";
    }
    m_isBatchMode = false;

    // 2026-03-xx 按照用户要求：彻底移除授权文件同步逻辑
    markDirty();
}

void DatabaseManager::rollbackBatch() {
    QMutexLocker locker(&m_mutex);
    if (m_db.isOpen()) {
        m_db.rollback();
    }
    m_isBatchMode = false;
}

void DatabaseManager::resetUsageCount() {}

void DatabaseManager::resetActivation() {}

bool DatabaseManager::verifyActivationCode(const QString& code) { Q_UNUSED(code); return true; }

void DatabaseManager::resetFailedAttempts() {}

// 2026-03-xx 按照用户最高要求：物理清除所有试用文件读写与注册表追踪逻辑
void DatabaseManager::saveTrialToFile(const QVariantMap&) {}
QVariantMap DatabaseManager::loadTrialFromFile() { return QVariantMap(); }
bool DatabaseManager::validateGenuineHardware() { return true; }

// [CRITICAL] 核心统计逻辑：采用 FTS5 引擎（降级至 LIKE）。
QVariantMap DatabaseManager::getFilterStats(const QString& keyword, const QString& filterType, const QVariant& filterValue, const QVariantMap& criteria) {
    QMutexLocker locker(&m_mutex);
    QVariantMap stats;
    if (!m_db.isOpen()) return stats;

    QString baseSql = "FROM notes ";
    bool useFts = !keyword.isEmpty() && m_hasFts5;

    if (useFts) {
        baseSql = "FROM notes JOIN notes_fts ON notes.id = notes_fts.rowid ";
    }

    QString whereClause;
    QVariantList params;
    applyCommonFilters(whereClause, params, filterType, filterValue, criteria);
    
    if (!keyword.isEmpty()) {
        if (useFts) {
            whereClause += "AND notes_fts MATCH ? ";
            params << sanitizeFtsKeyword(keyword);
        } else {
            whereClause += "AND (notes.title LIKE ? OR notes.content LIKE ? OR notes.tags LIKE ?) ";
            QString likeKey = "%" + keyword + "%";
            params << likeKey << likeKey << likeKey;
        }
    }

    QSqlQuery query(m_db);
    QMap<int, int> stars;
    query.prepare("SELECT rating, COUNT(*) " + baseSql + whereClause + " GROUP BY rating");
    for (int i = 0; i < params.size(); ++i) query.bindValue(i, params[i]);
    if (query.exec()) { while (query.next()) stars[query.value(0).toInt()] = query.value(1).toInt(); }
    QVariantMap starsMap;
    for (auto it = stars.begin(); it != stars.end(); ++it) starsMap[QString::number(it.key())] = it.value();
    stats["stars"] = starsMap;

    QMap<QString, int> colors;
    query.prepare("SELECT color, COUNT(*) " + baseSql + whereClause + " GROUP BY color");
    for (int i = 0; i < params.size(); ++i) query.bindValue(i, params[i]);
    if (query.exec()) { while (query.next()) colors[query.value(0).toString()] = query.value(1).toInt(); }
    QVariantMap colorsMap;
    for (auto it = colors.begin(); it != colors.end(); ++it) colorsMap[it.key()] = it.value();
    stats["colors"] = colorsMap;

    QMap<QString, int> types;
    query.prepare("SELECT item_type, COUNT(*) " + baseSql + whereClause + " GROUP BY item_type");
    for (int i = 0; i < params.size(); ++i) query.bindValue(i, params[i]);
    if (query.exec()) { while (query.next()) types[query.value(0).toString()] = query.value(1).toInt(); }
    QVariantMap typesMap;
    for (auto it = types.begin(); it != types.end(); ++it) typesMap[it.key()] = it.value();
    stats["types"] = typesMap;

    QMap<QString, int> tags;
    query.prepare("SELECT tags " + baseSql + whereClause);
    for (int i = 0; i < params.size(); ++i) query.bindValue(i, params[i]);
    if (query.exec()) {
        while (query.next()) {
            QStringList parts = query.value(0).toString().split(QRegularExpression("[,，]"), Qt::SkipEmptyParts);
            for (const QString& t : parts) {
                QString trimmed = t.trimmed();
                if (!trimmed.isEmpty()) tags[trimmed]++;
            }
        }
    }
    QVariantMap tagsMap;
    for (auto it = tags.begin(); it != tags.end(); ++it) tagsMap[it.key()] = it.value();
    stats["tags"] = tagsMap;

    // 5. 创建日期统计
    QMap<QString, int> createDateCounts;
    query.prepare("SELECT date(created_at), COUNT(*) " + baseSql + whereClause + " GROUP BY date(created_at) ORDER BY date(created_at) DESC");
    for (int i = 0; i < params.size(); ++i) query.bindValue(i, params[i]);
    if (query.exec()) {
        while (query.next()) {
            createDateCounts[query.value(0).toString()] = query.value(1).toInt();
        }
    }
    QVariantMap createDateStats;
    for (auto it = createDateCounts.begin(); it != createDateCounts.end(); ++it) createDateStats[it.key()] = it.value();
    stats["date_create"] = createDateStats;

    // 6. 修改日期统计
    QMap<QString, int> updateDateCounts;
    query.prepare("SELECT date(updated_at), COUNT(*) " + baseSql + whereClause + " GROUP BY date(updated_at) ORDER BY date(updated_at) DESC");
    for (int i = 0; i < params.size(); ++i) query.bindValue(i, params[i]);
    if (query.exec()) {
        while (query.next()) {
            updateDateCounts[query.value(0).toString()] = query.value(1).toInt();
        }
    }
    QVariantMap updateDateStats;
    for (auto it = updateDateCounts.begin(); it != updateDateCounts.end(); ++it) updateDateStats[it.key()] = it.value();
    stats["date_update"] = updateDateStats;

    return stats;
}

int DatabaseManager::addTodo(const Todo& todo) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return -1;
    
    QSqlQuery query(m_db);
    query.prepare(R"(
        INSERT INTO todos (title, content, start_time, end_time, status, reminder_time, priority, color, 
                           note_id, repeat_mode, parent_id, progress, created_at, updated_at)
        VALUES (:title, :content, :start, :end, :status, :reminder, :priority, :color, 
                :note, :repeat, :parent, :prog, :created, :updated)
    )");
    
    QString now = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    query.bindValue(":title", todo.title);
    query.bindValue(":content", todo.content);
    query.bindValue(":start", todo.startTime.isValid() ? todo.startTime.toString("yyyy-MM-dd HH:mm:ss") : QVariant());
    query.bindValue(":end", todo.endTime.isValid() ? todo.endTime.toString("yyyy-MM-dd HH:mm:ss") : QVariant());
    query.bindValue(":status", todo.status);
    query.bindValue(":reminder", todo.reminderTime.isValid() ? todo.reminderTime.toString("yyyy-MM-dd HH:mm:ss") : QVariant());
    query.bindValue(":priority", todo.priority);
    query.bindValue(":color", todo.color);
    query.bindValue(":note", todo.noteId);
    query.bindValue(":repeat", todo.repeatMode);
    query.bindValue(":parent", todo.parentId);
    query.bindValue(":prog", todo.progress);
    query.bindValue(":created", now);
    query.bindValue(":updated", now);
    
    if (query.exec()) {
        int id = query.lastInsertId().toInt();
        markDirty();
        emit todoChanged();
        return id;
    }
    return -1;
}

bool DatabaseManager::updateTodo(const Todo& todo) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;
    
    QSqlQuery query(m_db);
    query.prepare(R"(
        UPDATE todos SET title=:title, content=:content, start_time=:start, end_time=:end, 
        status=:status, reminder_time=:reminder, priority=:priority, color=:color, 
        note_id=:note, repeat_mode=:repeat, parent_id=:parent, progress=:prog, updated_at=:updated
        WHERE id=:id
    )");
    
    QString now = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    query.bindValue(":title", todo.title);
    query.bindValue(":content", todo.content);
    query.bindValue(":start", todo.startTime.isValid() ? todo.startTime.toString("yyyy-MM-dd HH:mm:ss") : QVariant());
    query.bindValue(":end", todo.endTime.isValid() ? todo.endTime.toString("yyyy-MM-dd HH:mm:ss") : QVariant());
    query.bindValue(":status", todo.status);
    query.bindValue(":reminder", todo.reminderTime.isValid() ? todo.reminderTime.toString("yyyy-MM-dd HH:mm:ss") : QVariant());
    query.bindValue(":priority", todo.priority);
    query.bindValue(":color", todo.color);
    query.bindValue(":note", todo.noteId);
    query.bindValue(":repeat", todo.repeatMode);
    query.bindValue(":parent", todo.parentId);
    query.bindValue(":prog", todo.progress);
    query.bindValue(":updated", now);
    query.bindValue(":id", todo.id);
    
    bool ok = query.exec();
    if (ok) {
        markDirty();
        // [PROFESSIONAL] 循环任务自动生成逻辑
        if (todo.status == 1 && todo.repeatMode > 0) {
            Todo next = todo;
            next.id = -1; // 新纪录
            next.status = 0; // 初始状态
            next.progress = 0;
            
            if (todo.repeatMode == 1) { // 每天
                next.startTime = todo.startTime.addDays(1);
                next.endTime = todo.endTime.addDays(1);
                if (todo.reminderTime.isValid()) next.reminderTime = todo.reminderTime.addDays(1);
            } else if (todo.repeatMode == 2) { // 每周
                next.startTime = todo.startTime.addDays(7);
                next.endTime = todo.endTime.addDays(7);
                if (todo.reminderTime.isValid()) next.reminderTime = todo.reminderTime.addDays(7);
            } else if (todo.repeatMode == 3) { // 每月
                next.startTime = todo.startTime.addMonths(1);
                next.endTime = todo.endTime.addMonths(1);
                if (todo.reminderTime.isValid()) next.reminderTime = todo.reminderTime.addMonths(1);
            } else if (todo.repeatMode == 4) { // 每小时
                next.startTime = todo.startTime.addSecs(3600);
                next.endTime = todo.endTime.addSecs(3600);
                if (todo.reminderTime.isValid()) next.reminderTime = todo.reminderTime.addSecs(3600);
            } else if (todo.repeatMode == 5) { // 每分钟
                next.startTime = todo.startTime.addSecs(60);
                next.endTime = todo.endTime.addSecs(60);
                if (todo.reminderTime.isValid()) next.reminderTime = todo.reminderTime.addSecs(60);
            } else if (todo.repeatMode == 6) { // 每秒
                next.startTime = todo.startTime.addSecs(1);
                next.endTime = todo.endTime.addSecs(1);
                if (todo.reminderTime.isValid()) next.reminderTime = todo.reminderTime.addSecs(1);
            }
            
            // 递归调用 addTodo，但要注意锁
            locker.unlock();
            addTodo(next);
            locker.relock();
        }
        
        emit todoChanged();
    }
    return ok;
}

bool DatabaseManager::deleteTodo(int id) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;
    
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM todos WHERE id = ?");
    query.addBindValue(id);
    
    bool ok = query.exec();
    if (ok) {
        markDirty();
        emit todoChanged();
    }
    return ok;
}

QList<DatabaseManager::Todo> DatabaseManager::getTodosByDate(const QDate& date) {
    QMutexLocker locker(&m_mutex);
    QList<Todo> results;
    if (!m_db.isOpen()) return results;
    
    QSqlQuery query(m_db);
    // 匹配开始时间在指定日期的任务，或者没有开始时间但在指定日期创建的任务（可选推导）
    query.prepare("SELECT * FROM todos WHERE date(start_time) = :date OR (start_time IS NULL AND date(created_at) = :date) ORDER BY priority DESC, start_time ASC");
    query.bindValue(":date", date.toString("yyyy-MM-dd"));
    
    if (query.exec()) {
        while (query.next()) {
            Todo t;
            t.id = query.value("id").toInt();
            t.title = query.value("title").toString();
            t.content = query.value("content").toString();
            t.startTime = QDateTime::fromString(query.value("start_time").toString(), "yyyy-MM-dd HH:mm:ss");
            t.endTime = QDateTime::fromString(query.value("end_time").toString(), "yyyy-MM-dd HH:mm:ss");
            t.status = query.value("status").toInt();
            t.reminderTime = QDateTime::fromString(query.value("reminder_time").toString(), "yyyy-MM-dd HH:mm:ss");
            t.priority = query.value("priority").toInt();
            t.color = query.value("color").toString();
            t.noteId = query.value("note_id").toInt();
            t.repeatMode = query.value("repeat_mode").toInt();
            t.parentId = query.value("parent_id").toInt();
            t.progress = query.value("progress").toInt();
            t.createdAt = QDateTime::fromString(query.value("created_at").toString(), "yyyy-MM-dd HH:mm:ss");
            t.updatedAt = QDateTime::fromString(query.value("updated_at").toString(), "yyyy-MM-dd HH:mm:ss");
            results.append(t);
        }
    }
    return results;
}

QList<DatabaseManager::Todo> DatabaseManager::getAllPendingTodos() {
    QMutexLocker locker(&m_mutex);
    QList<Todo> results;
    if (!m_db.isOpen()) return results;
    
    QSqlQuery query(m_db);
    query.exec("SELECT * FROM todos WHERE status = 0 ORDER BY priority DESC, start_time ASC");
    
    while (query.next()) {
        Todo t;
        t.id = query.value("id").toInt();
        t.title = query.value("title").toString();
        t.content = query.value("content").toString();
        t.startTime = QDateTime::fromString(query.value("start_time").toString(), "yyyy-MM-dd HH:mm:ss");
        t.endTime = QDateTime::fromString(query.value("end_time").toString(), "yyyy-MM-dd HH:mm:ss");
        t.status = query.value("status").toInt();
        t.reminderTime = QDateTime::fromString(query.value("reminder_time").toString(), "yyyy-MM-dd HH:mm:ss");
        t.priority = query.value("priority").toInt();
        t.color = query.value("color").toString();
        t.createdAt = QDateTime::fromString(query.value("created_at").toString(), "yyyy-MM-dd HH:mm:ss");
        t.updatedAt = QDateTime::fromString(query.value("updated_at").toString(), "yyyy-MM-dd HH:mm:ss");
        results.append(t);
    }
    return results;
}

bool DatabaseManager::addTagsToNote(int noteId, const QStringList& tags) {
    QVariantMap note = getNoteById(noteId);
    if (note.isEmpty()) return false;
    
    QStringList existingStrList = note["tags"].toString().split(QRegularExpression("[,，]"), Qt::SkipEmptyParts);
    QStringList finalTags;
    // 确保原有标签也经过清理
    for (const QString& t : existingStrList) {
        QString trimmed = t.trimmed();
        if (!trimmed.isEmpty() && !finalTags.contains(trimmed)) finalTags << trimmed;
    }
    
    // 合并新标签
    for (const QString& t : tags) {
        QString trimmed = t.trimmed();
        if (!trimmed.isEmpty() && !finalTags.contains(trimmed)) finalTags << trimmed;
    }
    
    return updateNoteState(noteId, "tags", finalTags.join(", "));
}
bool DatabaseManager::renameTagGlobally(const QString& oldName, const QString& newName) {
    QString targetOld = oldName.trimmed();
    QString targetNew = newName.trimmed();
    if (targetOld.isEmpty() || targetOld == targetNew) return true;
    
    bool ok = false;
    QList<int> affectedIds;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        m_db.transaction();
        QSqlQuery query(m_db);
        // 使用更宽松的匹配，处理潜在的空格存储问题
        query.prepare("SELECT id, tags FROM notes WHERE tags LIKE ? AND is_deleted = 0");
        query.addBindValue("%" + targetOld + "%");
        
        if (query.exec()) {
            while (query.next()) {
                int noteId = query.value(0).toInt(); 
                QString tagsStr = query.value(1).toString();
                QStringList tagList = tagsStr.split(QRegularExpression("[,，]"), Qt::SkipEmptyParts);
                
                bool changed = false;
                QStringList newTagList;
                for (const QString& t : tagList) {
                    QString trimmedTag = t.trimmed();
                    if (trimmedTag == targetOld) {
                        if (!targetNew.isEmpty()) newTagList << targetNew;
                        changed = true;
                    } else if (!trimmedTag.isEmpty()) {
                        newTagList << trimmedTag;
                    }
                }
                
                if (changed) {
                    affectedIds << noteId;
                    newTagList.removeDuplicates();
                    QSqlQuery updateQuery(m_db);
                    updateQuery.prepare("UPDATE notes SET tags = ? WHERE id = ?");
                    updateQuery.addBindValue(newTagList.join(", "));
                    updateQuery.addBindValue(noteId);
                    updateQuery.exec();
                }
            }
        }
        ok = m_db.commit();
    }
    if (ok) {
        markDirty();
        for (int id : affectedIds) syncFtsById(id);
        emit noteUpdated();
    }
    return ok;
}

bool DatabaseManager::deleteTagGlobally(const QString& tagName) {
    QString target = tagName.trimmed();
    if (target.isEmpty()) return true;
    
    bool ok = false;
    QList<int> affectedIds;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen()) return false;
        m_db.transaction();
        QSqlQuery query(m_db);
        // 允许匹配带空格或不同分隔符的情况
        query.prepare("SELECT id, tags FROM notes WHERE tags LIKE ? AND is_deleted = 0");
        query.addBindValue("%" + target + "%");
        
        if (query.exec()) {
            while (query.next()) {
                int noteId = query.value(0).toInt(); 
                QString tagsStr = query.value(1).toString();
                QStringList tagList = tagsStr.split(QRegularExpression("[,，]"), Qt::SkipEmptyParts);
                
                bool changed = false;
                QStringList newTagList;
                for (const QString& t : tagList) {
                    QString trimmedTag = t.trimmed();
                    if (trimmedTag == target) {
                        changed = true;
                    } else if (!trimmedTag.isEmpty()) {
                        newTagList << trimmedTag;
                    }
                }
                
                if (changed) {
                    affectedIds << noteId;
                    newTagList.removeDuplicates();
                    QSqlQuery updateQuery(m_db);
                    updateQuery.prepare("UPDATE notes SET tags = ? WHERE id = ?");
                    updateQuery.addBindValue(newTagList.join(", "));
                    updateQuery.addBindValue(noteId);
                    updateQuery.exec();
                }
            }
        }
        ok = m_db.commit();
    }
    if (ok) {
        markDirty();
        for (int id : affectedIds) syncFtsById(id);
        emit noteUpdated();
    }
    return ok;
}

// [CRITICAL] 索引同步逻辑：必须确保 title, content, tags 三者同步进入 FTS 虚拟表。
void DatabaseManager::syncFts(int id, const QString& title, const QString& content, const QString& tags) {
    if (!m_hasFts5) return;
    QString plainTitle = title; QString plainContent = StringUtils::htmlToPlainText(content);
    QMutexLocker locker(&m_mutex);
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM notes_fts WHERE rowid = ?"); query.addBindValue(id); query.exec();
    query.prepare("INSERT INTO notes_fts(rowid, title, content, tags) VALUES (?, ?, ?, ?)"); query.addBindValue(id); query.addBindValue(plainTitle); query.addBindValue(plainContent); query.addBindValue(tags); query.exec();
}

void DatabaseManager::syncFtsById(int id) {
    QMutexLocker locker(&m_mutex);
    QSqlQuery query(m_db);
    query.prepare("SELECT title, content, tags FROM notes WHERE id = ?");
    query.addBindValue(id);
    if (query.exec() && query.next()) {
        QString title = query.value(0).toString();
        QString content = query.value(1).toString();
        QString tags = query.value(2).toString();
        locker.unlock(); // Release before calling syncFts which locks again
        syncFts(id, title, content, tags);
    }
}

void DatabaseManager::removeFts(int id) { QSqlQuery query(m_db); query.prepare("DELETE FROM notes_fts WHERE rowid = ?"); query.addBindValue(id); query.exec(); }

// [CRITICAL] 关键词清洗算法：禁止移除分词包装及通配符前缀匹配逻辑。此算法决定了全软件搜索的灵敏度，误改将导致多词匹配失效。
QString DatabaseManager::sanitizeFtsKeyword(const QString& keyword) {
    if (keyword.isEmpty()) return "";
    
    // 移除 FTS5 专用控制字符，防止注入或崩溃
    QString cleaned = keyword;
    cleaned.replace("\"", "\"\""); // 转义引号
    
    // 按空白字符拆分多个关键词
    QStringList terms = cleaned.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (terms.isEmpty()) return "";

    QString finalQuery;
    for (const QString& term : terms) {
        if (!finalQuery.isEmpty()) finalQuery += " ";
        // 为每个词包装引号并添加通配符，实现更符合直觉的“包含关键词”搜索
        finalQuery += "\"" + term + "\"*";
    }
    return finalQuery;
}

void DatabaseManager::applySecurityFilter(QString& whereClause, QVariantList& params, const QString& filterType) {
    if (filterType == "category" || filterType == "trash" || filterType == "uncategorized") return;
    QSqlQuery catQuery(m_db);
    catQuery.exec("SELECT id FROM categories WHERE password IS NOT NULL AND password != ''");
    QList<int> lockedIds;
    while (catQuery.next()) { int cid = catQuery.value(0).toInt(); if (!m_unlockedCategories.contains(cid)) lockedIds.append(cid); }
    if (!lockedIds.isEmpty()) {
        QStringList placeholders; for (int i = 0; i < lockedIds.size(); ++i) placeholders << "?";
        // 2026-03-xx 按照用户要求修复逻辑：在排除锁定分类时，必须确保“未分类”项目（NULL 或 <=0）始终可见，不被误杀
        whereClause += QString("AND (category_id IS NULL OR category_id <= 0 OR category_id NOT IN (%1)) ").arg(placeholders.join(", "));
        for (int id : lockedIds) params << id;
    }
}

// [CRITICAL] 通用过滤引擎：recently_visited 必须包含排除今日新建笔记的日期判定条件。此逻辑涉及业务分类的严谨性，禁止删除。
void DatabaseManager::applyCommonFilters(QString& whereClause, QVariantList& params, const QString& filterType, const QVariant& filterValue, const QVariantMap& criteria) {
    if (filterType == "trash") {
        whereClause = "WHERE is_deleted = 1 ";
    } else {
        whereClause = "WHERE is_deleted = 0 ";
        applySecurityFilter(whereClause, params, filterType);
        
        if (filterType == "category") { 
            // 2026-03-xx 按照用户要求修复傻逼逻辑：分类 ID 为 -1 时应视为“未分类”，统一查询口径
            if (filterValue.toInt() == -1) whereClause += "AND (category_id IS NULL OR category_id <= 0) "; 
            else { whereClause += "AND category_id = ? "; params << filterValue.toInt(); } 
        }
        else if (filterType == "uncategorized") {
            // 2026-03-xx 按照用户要求修复傻逼逻辑：统一“未分类”判定，防止物理删除分类后的笔记在恢复时变成“幽灵数据”
            whereClause += "AND (category_id IS NULL OR category_id <= 0) ";
        }
        else if (filterType == "today") whereClause += "AND date(created_at) = date('now', 'localtime') ";
        else if (filterType == "yesterday") whereClause += "AND date(created_at) = date('now', '-1 day', 'localtime') ";
        else if (filterType == "recently_visited") whereClause += "AND date(last_accessed_at) = date('now', 'localtime') ";
        else if (filterType == "bookmark") whereClause += "AND is_favorite = 1 ";
        else if (filterType == "untagged") whereClause += "AND (tags IS NULL OR tags = '') ";
    }
    
    if (filterType != "trash" && !criteria.isEmpty()) {
        if (criteria.contains("stars")) { 
            QStringList stars = criteria.value("stars").toStringList(); 
            if (!stars.isEmpty()) whereClause += QString("AND rating IN (%1) ").arg(stars.join(", ")); 
        }
        if (criteria.contains("types")) { 
            QStringList types = criteria.value("types").toStringList(); 
            if (!types.isEmpty()) { 
                QStringList placeholders; 
                for (const auto& t : types) { placeholders << "?"; params << t; } 
                whereClause += QString("AND item_type IN (%1) ").arg(placeholders.join(", ")); 
            } 
        }
        if (criteria.contains("colors")) { 
            QStringList colors = criteria.value("colors").toStringList(); 
            if (!colors.isEmpty()) { 
                QStringList placeholders; 
                for (const auto& c : colors) { placeholders << "?"; params << c; } 
                whereClause += QString("AND color IN (%1) ").arg(placeholders.join(", ")); 
            } 
        }
        if (criteria.contains("tags")) { 
            QStringList tags = criteria.value("tags").toStringList(); 
            if (!tags.isEmpty()) { 
                QStringList tagConds; 
                for (const auto& t : tags) { 
                    // [OPTIMIZED] 使用 REPLACE 消除存储中的空格干扰，确保无论存储格式是 ", " 还是 "," 都能精准匹配
                    tagConds << "(',' || REPLACE(tags, ' ', '') || ',') LIKE ?"; 
                    params << "%," + t.trimmed().replace(" ", "") + ",%"; 
                } 
                whereClause += QString("AND (%1) ").arg(tagConds.join(" OR ")); 
            } 
        }
        if (criteria.contains("date_create")) { 
            QStringList dates = criteria.value("date_create").toStringList(); 
            if (!dates.isEmpty()) { 
                QStringList dateConds; 
                for (const auto& d : dates) { 
                    dateConds << "date(created_at) = ?";
                    params << d;
                } 
                if (!dateConds.isEmpty()) whereClause += QString("AND (%1) ").arg(dateConds.join(" OR ")); 
            } 
        }
        if (criteria.contains("date_update")) { 
            QStringList dates = criteria.value("date_update").toStringList(); 
            if (!dates.isEmpty()) { 
                QStringList dateConds; 
                for (const auto& d : dates) { 
                    dateConds << "date(updated_at) = ?";
                    params << d;
                } 
                if (!dateConds.isEmpty()) whereClause += QString("AND (%1) ").arg(dateConds.join(" OR ")); 
            } 
        }
    }
}

QString DatabaseManager::getActiveFingerprint() {
    return "NONE";
}
