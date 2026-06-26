#include "AutoImportManager.h"
#include "../mft/MftReader.h"
#include "../meta/MetadataManager.h"
#include "../meta/DatabaseManager.h"
#include "AppConfig.h"
#include <QDebug>
#include <QCoreApplication>
#include <QDir>
#include <QMetaObject>
#include <QtConcurrent>
#include <QDateTime>

#ifdef Q_OS_WIN
#include <windows.h>
#undef run
#endif

namespace ArcMeta {

AutoImportManager& AutoImportManager::instance() {
    static AutoImportManager inst;
    return inst;
}

AutoImportManager::AutoImportManager(QObject* parent) : QObject(parent) {
    m_debounceTimer = new QTimer(this);
    m_debounceTimer->setInterval(3000); 
    m_debounceTimer->setSingleShot(true);
    connect(m_debounceTimer, &QTimer::timeout, this, &AutoImportManager::processImportQueue);
}

AutoImportManager::~AutoImportManager() {
    stopListening();
}

void AutoImportManager::startListening() {
    if (m_isListening) return;
    connect(&MftReader::instance(), &MftReader::entryAdded, this, &AutoImportManager::onEntryAdded, Qt::QueuedConnection);
    connect(&MftReader::instance(), &MftReader::entryRemoved, this, &AutoImportManager::onEntryRemoved, Qt::QueuedConnection);
    m_isListening = true;
}

void AutoImportManager::stopListening() {
    if (!m_isListening) return;
    disconnect(&MftReader::instance(), &MftReader::entryAdded, this, &AutoImportManager::onEntryAdded);
    disconnect(&MftReader::instance(), &MftReader::entryRemoved, this, &AutoImportManager::onEntryRemoved);
    m_isListening = false;
}

void AutoImportManager::onEntryAdded(uint64_t key) {
    uint64_t frn = key & 0x0000FFFFFFFFFFFFull;
    int driveIdx = static_cast<int>(key >> 48);
    QString driveLetter;
    const auto drives = QDir::drives();
    if (driveIdx < drives.size()) driveLetter = drives[driveIdx].absolutePath().left(2).toUpper();
    else return;

    HANDLE hVol = CreateFileW((L"\\\\.\\" + driveLetter.toStdWString()).c_str(), 
                              GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 
                              NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (hVol != INVALID_HANDLE_VALUE) {
        QString relPath = MftReader::getPathByFrn(hVol, frn);
        CloseHandle(hVol);
        if (!relPath.isEmpty()) {
            QString fullPath = driveLetter + relPath;
            std::wstring managedFolder;
            if (checkAndGetManagedPath(fullPath.toStdWString(), managedFolder)) {
                sqlite3* db = DatabaseManager::instance().getGlobalDb();
                const char* sql = "REPLACE INTO pending_imports (frn, drive, path, status, timestamp) VALUES (?, ?, ?, 1, ?)";
                sqlite3_stmt* stmt;
                if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                    sqlite3_bind_int64(stmt, 1, frn);
                    sqlite3_bind_text(stmt, 2, driveLetter.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(stmt, 3, fullPath.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int64(stmt, 4, QDateTime::currentMSecsSinceEpoch());
                    sqlite3_step(stmt);
                    sqlite3_finalize(stmt);
                }
            }
        }
    }
}

void AutoImportManager::onEntryRemoved(uint64_t key) {
    uint64_t frn = key & 0x0000FFFFFFFFFFFFull;
    int driveIdx = static_cast<int>(key >> 48);
    QString driveLetter;
    const auto drives = QDir::drives();
    if (driveIdx < drives.size()) driveLetter = drives[driveIdx].absolutePath().left(2).toUpper();
    else return;

    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    const char* sql = "REPLACE INTO pending_imports (frn, drive, status, timestamp) VALUES (?, ?, -1, ?)";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, frn);
        sqlite3_bind_text(stmt, 2, driveLetter.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, QDateTime::currentMSecsSinceEpoch());
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

void AutoImportManager::startTask(const QString& drive) {
    (void)QtConcurrent::run([this, drive]() {
        sqlite3* db = DatabaseManager::instance().getGlobalDb();
        struct TaskItem { uint64_t frn; QString path; };
        QVector<TaskItem> toImport;
        const char* selectSql = "SELECT frn, path FROM pending_imports WHERE drive=? AND status=1";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, selectSql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, drive.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                toImport.append({(uint64_t)sqlite3_column_int64(stmt, 0), QString::fromUtf8((const char*)sqlite3_column_text(stmt, 1))});
            }
            sqlite3_finalize(stmt);
        }
        for (const auto& item : toImport) {
            MetadataManager::instance().registerItem(item.path.toStdWString());
            sqlite3_exec(db, QString("UPDATE pending_imports SET status=2 WHERE frn=%1").arg(item.frn).toUtf8().constData(), nullptr, nullptr, nullptr);
        }
        QVector<uint64_t> toRemove;
        const char* delSql = "SELECT frn FROM pending_imports WHERE drive=? AND status=-1";
        if (sqlite3_prepare_v2(db, delSql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, drive.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            while (sqlite3_step(stmt) == SQLITE_ROW) toRemove.append((uint64_t)sqlite3_column_int64(stmt, 0));
            sqlite3_finalize(stmt);
        }
        if (!toRemove.isEmpty()) {
            std::wstring volSerial = MetadataManager::getVolumeSerialNumber((drive + "\\").toStdWString());
            for (uint64_t frn : toRemove) {
                std::string fidPrefix = QString::fromStdWString(volSerial).toStdString() + "_" + std::to_string(frn);
                MetadataManager::instance().setInvalidByFidPrefix(fidPrefix, true);
                sqlite3_exec(db, QString("DELETE FROM pending_imports WHERE frn=%1").arg(frn).toUtf8().constData(), nullptr, nullptr, nullptr);
            }
        }
        MetadataManager::instance().notifyFullUIRebuild();
        emit taskFinished(drive);
    });
}

void AutoImportManager::pauseTask(const QString& drive) { Q_UNUSED(drive); }

bool AutoImportManager::checkAndGetManagedPath(const std::wstring& path, std::wstring& outManagedFolder) {
    std::wstring volSerial = MetadataManager::getVolumeSerialNumber(path);
    if (volSerial.empty()) return false;

    std::wstring managedAbs = getManagedFolderAbsolutePath(volSerial);
    if (managedAbs.empty()) return false;

    if (path.size() >= managedAbs.size() && _wcsnicmp(path.c_str(), managedAbs.c_str(), managedAbs.size()) == 0) {
        outManagedFolder = managedAbs;
        return true;
    }
    return false;
}

std::wstring AutoImportManager::getManagedFolderAbsolutePath(const std::wstring& volSerial) {
    // 根据序列号反查当前盘符 (Plan-68 4.1)
    QString drive;
    const auto drives = QDir::drives();
    for (const QFileInfo& d : drives) {
        if (MetadataManager::getVolumeSerialNumber(d.absolutePath().toStdWString()) == volSerial) {
            drive = d.absolutePath();
            break;
        }
    }
    if (drive.isEmpty()) return L"";

    QString key = QString("ManagedFolder/Volume_%1").arg(QString::fromStdWString(volSerial));
    QString relPath = AppConfig::instance().getValue(key, "").toString();
    if (relPath.isEmpty()) return L"";

    return MetadataManager::normalizePath((drive.toStdWString() + relPath.toStdWString()));
}

void AutoImportManager::processImportQueue() {
    std::vector<std::wstring> pathsToProcess;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        pathsToProcess = std::move(m_pendingPaths);
        m_pendingPaths.clear();
    }

    if (pathsToProcess.empty()) return;

    // 通道 3：按照磁盘聚合并执行静默挂载与入库
    std::map<std::wstring, std::vector<std::wstring>> pathsByVol;
    for (const auto& p : pathsToProcess) {
        pathsByVol[MetadataManager::getVolumeSerialNumber(p)].push_back(p);
    }

    for (auto& pair : pathsByVol) {
        const std::wstring& vol = pair.first;
        if (vol.empty()) continue;

        // 提取其中一个路径的盘符用于重命名纠偏
        QString letter = "";
        if (!pair.second.empty()) {
            const std::wstring& firstPath = pair.second.front();
            if (firstPath.length() >= 2 && firstPath[1] == L':') {
                letter = QString::fromWCharArray(&firstPath[0], 1);
            }
        }

        // 静默强制挂载数据库
        DatabaseManager::instance().getMemoryDb(vol, letter);

        for (const auto& path : pair.second) {
            MetadataManager::instance().registerItem(path);
        }
    }

    MetadataManager::instance().notifyFullUIRebuild();
    qDebug() << "[AutoImport] 自动入库完成，处理项数:" << pathsToProcess.size();
}

} // namespace ArcMeta
