#include "AutoImportManager.h"
#include "../mft/MftReader.h"
#include "../meta/MetadataManager.h"
#include "../meta/DatabaseManager.h"
#include "../meta/CategoryRepo.h"
#include "AppConfig.h"
#include <QDebug>
#include <QCoreApplication>
#include <QDir>
#include <QMetaObject>
#include <QFileInfo>
#include <QFile>
#include <QTimer>
#include <functional>
#include <cwchar>
#include <map>
#include <cstdint>

#ifdef Q_OS_WIN
#include <windows.h>
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
    connect(&MftReader::instance(), &MftReader::entryUpdated, this, &AutoImportManager::onEntryUpdated, Qt::QueuedConnection);
    m_isListening = true;
}

void AutoImportManager::stopListening() {
    if (!m_isListening) return;
    disconnect(&MftReader::instance(), &MftReader::entryAdded, this, &AutoImportManager::onEntryAdded);
    disconnect(&MftReader::instance(), &MftReader::entryUpdated, this, &AutoImportManager::onEntryUpdated);
    m_isListening = false;
}

void AutoImportManager::syncAllManagedLibraries() {
    const auto drives = QDir::drives();
    bool changed = false;
    for (const QFileInfo& d : drives) {
        QString drive = d.absolutePath();
        QString letter = drive.left(1).toUpper();
        
        QDir rootDir(drive);
        QStringList entries = rootDir.entryList({"ArcMeta.Library_*"}, QDir::Dirs | QDir::Hidden);
        
        QString targetName = "ArcMeta.Library_" + letter;
        for (const QString& entry : entries) {
            if (QString::compare(entry, targetName, Qt::CaseInsensitive) == 0) {
                QString managedPath = rootDir.absoluteFilePath(entry);
                qDebug() << "[AutoImport] 启动对账：发现物理托管库，执行同步 ->" << managedPath;
                handleRecursiveIngestion(QDir::toNativeSeparators(managedPath).toStdWString());
                changed = true;
            }
        }
    }
    if (changed) {
        MetadataManager::instance().notifyFullUIRebuild();
    }
}

void AutoImportManager::onEntryAdded(uint64_t key) {
    int idx = MftReader::instance().getIndexByKey(key);
    if (idx < 0) return;

    QString qPath = MftReader::instance().getFullPath(idx);
    std::wstring fullPath = qPath.toStdWString();
    std::wstring managedFolder;
    
    bool isManaged = checkAndGetManagedPath(fullPath, managedFolder);
     
    if (isManaged) {
        if (MftReader::instance().isDirectory(idx)) {
            handleRecursiveIngestion(fullPath);
        }

        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_pendingPaths.push_back(fullPath);
        
        QMetaObject::invokeMethod(m_debounceTimer, "start", Qt::QueuedConnection);
    }
}

void AutoImportManager::onEntryUpdated(uint64_t key) {
    int idx = MftReader::instance().getIndexByKey(key);
    if (idx < 0) return;

    QString qPath = MftReader::instance().getFullPath(idx);
    std::wstring fullPath = qPath.toStdWString();
    uint64_t frn = MftReader::instance().getFrn(idx);

    if (MftReader::instance().isDirectory(idx)) {
        int catId = CategoryRepo::findByFrn(frn);
        if (catId > 0) {
            QString newName = QFileInfo(qPath).fileName();
            Category cat = CategoryRepo::getById(catId);
            if (cat.id > 0) {
                if (cat.parentId == 0 && QString::fromStdWString(cat.name).startsWith("ArcMeta.Library_", Qt::CaseInsensitive)) {
                    QString expectedName = "ArcMeta.Library_" + qPath.left(1).toUpper();
                    if (newName != expectedName) {
                        qDebug() << "[AutoImport] 检测到根目录违规重命名，强制恢复:" << newName << "->" << expectedName;
                        QString parentDir = QFileInfo(qPath).absolutePath();
                        QString oldPath = QDir::toNativeSeparators(QDir(parentDir).absoluteFilePath(expectedName));
                        QFile::rename(qPath, oldPath);
                        return; 
                    }
                }

                if (QString::fromStdWString(cat.name) != newName) {
                    qDebug() << "[AutoImport] 同步物理重命名到逻辑分类:" << newName;
                    cat.name = newName.toStdWString();
                    cat.physicalPath = fullPath;
                    CategoryRepo::update(cat);
                    MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::FullRebuild);
                }
            }
        }
    }

    std::wstring managedFolder;
    bool isManaged = checkAndGetManagedPath(fullPath, managedFolder);
    if (isManaged) {
        if (MftReader::instance().isDirectory(idx)) {
            handleRecursiveIngestion(fullPath);
        }

        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_pendingPaths.push_back(fullPath);

        QMetaObject::invokeMethod(m_debounceTimer, "start", Qt::QueuedConnection);
    }
}

void AutoImportManager::recordRecentVisitedFolder(const std::wstring& path) {
    if (path.empty()) return;
    std::wstring managedFolder;
    if (instance().checkAndGetManagedPath(path, managedFolder)) return;

    std::wstring volSerial = MetadataManager::getVolumeSerialNumber(path);
    if (volSerial.empty()) return;

    QString key = QString("RecentVisited/Volume_%1").arg(QString::fromStdWString(volSerial));
    QStringList list = AppConfig::instance().getValue(key, QStringList()).toStringList();

    QString qPath = QString::fromStdWString(MetadataManager::normalizePath(path));
    list.removeAll(qPath);
    list.prepend(qPath);
    while (list.size() > 14) list.removeLast();

    AppConfig::instance().setValue(key, list);
}

QStringList AutoImportManager::getRecentVisitedFolders(const std::wstring& volSerial) {
    if (volSerial.empty()) return QStringList();
    QString key = QString("RecentVisited/Volume_%1").arg(QString::fromStdWString(volSerial));
    return AppConfig::instance().getValue(key, QStringList()).toStringList();
}

bool AutoImportManager::checkAndGetManagedPath(const std::wstring& path, std::wstring& outManagedFolder) {
    std::wstring managedAbs = getManagedLibraryPath(path);
    if (managedAbs.empty()) return false;

    if (path.size() >= managedAbs.size() && _wcsnicmp(path.c_str(), managedAbs.c_str(), managedAbs.size()) == 0) {
        outManagedFolder = managedAbs;
        return true;
    }
    return false;
}

std::wstring AutoImportManager::getManagedLibraryPath(const std::wstring& pathOrVolSerial) {
    if (pathOrVolSerial.empty()) return L"";

    std::wstring volSerial = pathOrVolSerial;
    if (volSerial.find(L":") != std::wstring::npos || volSerial.find(L"\\") != std::wstring::npos) {
        volSerial = MetadataManager::getVolumeSerialNumber(pathOrVolSerial);
    }
    if (volSerial.empty() || volSerial == L"UNKNOWN") return L"";

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

    if (relPath.isEmpty()) {
        relPath = "ArcMeta.Library_" + drive.left(1).toUpper();
        bool exists = QDir(drive + relPath).exists(); 
        if (!exists) return L"";
    }

    std::wstring result = MetadataManager::normalizePath((drive.toStdWString() + relPath.toStdWString()));
    return result;
}

void AutoImportManager::processImportQueue() {
    std::vector<std::wstring> pathsToProcess;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        pathsToProcess = std::move(m_pendingPaths);
        m_pendingPaths.clear();
    }

    if (pathsToProcess.empty()) return;

    std::map<std::wstring, std::vector<std::wstring>> pathsByVol;
    for (const auto& p : pathsToProcess) {
        pathsByVol[MetadataManager::getVolumeSerialNumber(p)].push_back(p);
    }

    for (auto& pair : pathsByVol) {
        const std::wstring& vol = pair.first;
        if (vol.empty()) continue;

        QString letter = "";
        if (!pair.second.empty()) {
            const std::wstring& firstPath = pair.second.front();
            if (firstPath.length() >= 2 && firstPath[1] == L':') {
                letter = QString::fromWCharArray(&firstPath[0], 1);
            }
        }

        DatabaseManager::instance().getMemoryDb(vol, letter);

        for (const auto& path : pair.second) {
            MetadataManager::instance().registerItem(path, true);
        }
    }

    MetadataManager::instance().notifyFullUIRebuild();
}

void AutoImportManager::handleRecursiveIngestion(const std::wstring& rootPath) {
    QDir dir(QString::fromStdWString(rootPath));
    if (!dir.exists()) return;

    // 2026-08-xx 性能优化：抑制信号，开启批量事务
    MetadataManager::instance().setInternalOperating(true);
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (db) sqlite3_exec(db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);

    int rootCatId = 0;
    std::string rootFid;
    std::wstring rootFrnStr;
    if (MetadataManager::fetchWinApiMetadataDirect(rootPath, rootFid, &rootFrnStr)) {
        try {
            uint64_t frn = std::stoull(rootFrnStr, nullptr, 16);
            rootCatId = CategoryRepo::findByFrn(frn);
            if (rootCatId == 0) {
                QFileInfo info(QString::fromStdWString(rootPath));
                std::wstring parentPath = info.absolutePath().toStdWString();
                std::string parentFid;
                std::wstring parentFrnStr;
                int parentCatId = 0;
                if (MetadataManager::fetchWinApiMetadataDirect(parentPath, parentFid, &parentFrnStr)) {
                    uint64_t pFrn = std::stoull(parentFrnStr, nullptr, 16);
                    parentCatId = CategoryRepo::findByFrn(pFrn);
                }

                Category cat;
                // 2026-08-xx 物理同步：ArcMeta.Library_* 强制作为顶级分类 (parentId = 0)
                if (info.fileName().startsWith("ArcMeta.Library_", Qt::CaseInsensitive)) {
                    cat.parentId = 0;
                } else {
                    cat.parentId = parentCatId;
                }
                cat.name = info.fileName().toStdWString();
                cat.physicalFrn = frn;
                cat.physicalPath = rootPath;
                cat.color = CategoryRepo::getDefaultColor();
                if (CategoryRepo::add(cat)) {
                    rootCatId = cat.id;
                }
            }
        } catch (...) {}
    }

    if (rootCatId <= 0) return;

    std::function<void(const QString&, int)> syncDir;
    syncDir = [&](const QString& currentPath, int parentCatId) {
        QDir currentDir(currentPath);
        QFileInfoList list = currentDir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden);

        for (const QFileInfo& fi : list) {
            std::wstring wPath = QDir::toNativeSeparators(fi.absoluteFilePath()).toStdWString();
            if (fi.isDir()) {
                int existingId = CategoryRepo::findCategoryId(parentCatId, fi.fileName().toStdWString());
                if (existingId == 0) {
                    std::string fid;
                    std::wstring frnStr;
                    if (MetadataManager::fetchWinApiMetadataDirect(wPath, fid, &frnStr)) {
                        try {
                            Category cat;
                            cat.parentId = parentCatId;
                            cat.name = fi.fileName().toStdWString();
                            cat.physicalFrn = std::stoull(frnStr, nullptr, 16);
                            cat.physicalPath = wPath;
                            cat.color = CategoryRepo::getDefaultColor();
                            if (CategoryRepo::add(cat)) {
                                existingId = cat.id;
                            }
                        } catch (...) {}
                    }
                }
                if (existingId > 0) {
                    syncDir(fi.absoluteFilePath(), existingId);
                }
            } else {
                MetadataManager::instance().registerItem(wPath, true);
                if (parentCatId > 0) {
                    std::string fid;
                    if (MetadataManager::fetchWinApiMetadataDirect(wPath, fid)) {
                        CategoryRepo::addItemToCategory(parentCatId, fid, wPath);
                    }
                }
            }
        }
    };

    syncDir(QString::fromStdWString(rootPath), rootCatId);

    // 2026-08-xx 性能优化：提交事务并恢复信号，最后执行一次全量 UI 重建
    if (db) sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);
    MetadataManager::instance().setInternalOperating(false);
    MetadataManager::instance().notifyFullUIRebuild();
}

} // namespace ArcMeta
