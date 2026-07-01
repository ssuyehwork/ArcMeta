#include "AutoImportManager.h"
#include "../mft/MftReader.h"
#include "../meta/MetadataManager.h"
#include "../meta/DatabaseManager.h"
#include "AppConfig.h"
#include <QDebug>
#include <QCoreApplication>
#include <QDir>
#include <QMetaObject>

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
    // 2026-07-xx 按照 Plan-120：补全对 entryUpdated 的监听，以覆盖文件移动至 Library 的场景
    connect(&MftReader::instance(), &MftReader::entryUpdated, this, &AutoImportManager::onEntryUpdated, Qt::QueuedConnection);
    m_isListening = true;
}

void AutoImportManager::stopListening() {
    if (!m_isListening) return;
    disconnect(&MftReader::instance(), &MftReader::entryAdded, this, &AutoImportManager::onEntryAdded);
    disconnect(&MftReader::instance(), &MftReader::entryUpdated, this, &AutoImportManager::onEntryUpdated);
    m_isListening = false;
}

void AutoImportManager::onEntryAdded(uint64_t key) {
    int idx = MftReader::instance().getIndexByKey(key);
    if (idx < 0) return;

    QString qPath = MftReader::instance().getFullPath(idx);
    std::wstring fullPath = qPath.toStdWString();
    std::wstring managedFolder;
    
    bool isManaged = checkAndGetManagedPath(fullPath, managedFolder);
     
    if (isManaged) {
        // 2026-08-xx 物理同步：如果是文件夹，启动递归入库同步
        if (idx >= 0 && MftReader::instance().isDirectory(idx)) {
            handleRecursiveIngestion(fullPath);
        }

        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_pendingPaths.push_back(fullPath);
        
        QMetaObject::invokeMethod(m_debounceTimer, "start", Qt::QueuedConnection);
    }
}

void AutoImportManager::onEntryUpdated(uint64_t key) {
    // 2026-07-xx 按照 Plan-120：逻辑与 onEntryAdded 一致，处理跨目录移动
    int idx = MftReader::instance().getIndexByKey(key);
    if (idx < 0) return;

    QString qPath = MftReader::instance().getFullPath(idx);
    std::wstring fullPath = qPath.toStdWString();
    uint64_t frn = MftReader::instance().getFrn(idx);

    // 2026-08-xx 物理同步：处理物理重命名驱动逻辑更新 (Outer -> Inner)
    if (MftReader::instance().isDirectory(idx)) {
        int catId = CategoryRepo::findByFrn(frn);
        if (catId > 0) {
            QString newName = QFileInfo(qPath).fileName();
            Category cat = CategoryRepo::getById(catId);
            if (cat.id > 0) {
                // 根目录强制保护逻辑
                if (cat.parentId == 0 && QString::fromStdWString(cat.name).startsWith("ArcMeta.Library_")) {
                    QString expectedName = "ArcMeta.Library_" + qPath.left(1).toUpper();
                    if (newName != expectedName) {
                        qDebug() << "[AutoImport] 检测到根目录违规重命名，强制恢复:" << newName << "->" << expectedName;
                        QString parentDir = QFileInfo(qPath).absolutePath();
                        QString oldPath = QDir::toNativeSeparators(QDir(parentDir).absoluteFilePath(expectedName));
                        QFile::rename(qPath, oldPath);
                        return; // 物理恢复后，等待下一轮 USN 信号
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
        // 如果是新出现的文件夹（由于移动），也需要递归处理
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
    // 库内文件夹不记录（没有意义，本来就在库里）
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
    // 如果传入的是路径而非序列号，则提取序列号
    if (volSerial.find(L":") != std::wstring::npos || volSerial.find(L"\\") != std::wstring::npos) {
        volSerial = MetadataManager::getVolumeSerialNumber(pathOrVolSerial);
    }
    if (volSerial.empty() || volSerial == L"UNKNOWN") return L"";

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

    // 2026-07-xx 按照 Plan-118：约定优于配置的默认兜底
    // 若配置不存在，使用默认命名规则 ArcMeta.Library_[盘符]，
    // 但必须验证该文件夹物理存在，避免对不存在的路径做前缀匹配。
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
            // 2026-07-xx 按照 Plan-116：通过 USN 链路触发的入库，标记为已授权
            MetadataManager::instance().registerItem(path, true);
        }
    }

    MetadataManager::instance().notifyFullUIRebuild();
    qDebug() << "[AutoImport] 自动入库完成，处理项数:" << pathsToProcess.size();
}

void AutoImportManager::handleRecursiveIngestion(const std::wstring& rootPath) {
    QDir dir(QString::fromStdWString(rootPath));
    if (!dir.exists()) return;

    // 1. 获取 rootPath 对应的分类 ID，如果缺失则补齐
    int rootCatId = 0;
    std::string rootFid;
    std::wstring rootFrnStr;
    if (MetadataManager::fetchWinApiMetadataDirect(rootPath, rootFid, &rootFrnStr)) {
        try {
            uint64_t frn = std::stoull(rootFrnStr, nullptr, 16);
            rootCatId = CategoryRepo::findByFrn(frn);
            if (rootCatId == 0) {
                // 尝试通过父级路径补齐
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
                cat.parentId = parentCatId;
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

    // 2. 递归同步子项目
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
}

} // namespace ArcMeta
