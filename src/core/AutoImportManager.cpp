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
#include <QMessageBox>
#include <QtConcurrent>
#include <QFuture>
#include <functional>
#include <cwchar>
#include <map>
#include <cstdint>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace ArcMeta {

static std::recursive_mutex s_dbAccessMutex;

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

    // [Plan-131 方案 B] 预热 FRN 缓存
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        m_managedFrnCache.clear();
        const auto drives = QDir::drives();
        for (const QFileInfo& d : drives) {
            std::wstring volSerial = MetadataManager::getVolumeSerialNumber(d.absolutePath().toStdWString());
            std::wstring managedPath = MetadataManager::getManagedLibraryPath(volSerial, d.absolutePath());
            if (!managedPath.empty()) {
                std::string fid; std::wstring frnStr;
                if (MetadataManager::fetchWinApiMetadataDirect(managedPath, fid, &frnStr)) {
                    try {
                        uint64_t frn = std::stoull(frnStr, nullptr, 16);
                        m_managedFrnCache.insert(frn & 0x0000FFFFFFFFFFFFull);
                        qDebug() << "[AutoImport] [Plan-131] 已缓存托管库根 FRN:" << QString::fromStdWString(frnStr) << "->" << QString::fromStdWString(managedPath);
                    } catch (...) {
                        qWarning() << "[AutoImport] 解析托管库根 FRN 失败:" << QString::fromStdWString(frnStr);
                    }
                }
            }
        }
    }

    connect(&MftReader::instance(), &MftReader::entryAdded, this, &AutoImportManager::onEntryAdded, Qt::QueuedConnection);
    connect(&MftReader::instance(), &MftReader::entryUpdated, this, &AutoImportManager::onEntryUpdated, Qt::QueuedConnection);
    connect(&MftReader::instance(), &MftReader::entryRemoved, this, &AutoImportManager::onEntryRemoved, Qt::QueuedConnection);
    m_isListening = true;
}

void AutoImportManager::stopListening() {
    if (!m_isListening) return;
    disconnect(&MftReader::instance(), &MftReader::entryAdded, this, &AutoImportManager::onEntryAdded);
    disconnect(&MftReader::instance(), &MftReader::entryUpdated, this, &AutoImportManager::onEntryUpdated);
    disconnect(&MftReader::instance(), &MftReader::entryRemoved, this, &AutoImportManager::onEntryRemoved);
    m_isListening = false;
}

void AutoImportManager::syncAllManagedLibraries() {
    const auto drives = QDir::drives();
    bool changed = false;
    for (const QFileInfo& d : drives) {
        QString drive = d.absolutePath();
        QString letter = drive.left(1).toUpper();
        if (!letter.endsWith(":")) letter += ":";
        
        QDir rootDir(drive);
        QStringList entries = rootDir.entryList({"ArcMeta.Library_*"}, QDir::Dirs | QDir::Hidden);
        
        QString targetName = "ArcMeta.Library_" + letter.left(1);
        for (const QString& entry : entries) {
            if (QString::compare(entry, targetName, Qt::CaseInsensitive) == 0) {
                QString managedPath = rootDir.absoluteFilePath(entry);
                qDebug() << "[AutoImport] 启动对账：发现物理托管库，执行同步 ->" << managedPath;

                // [Plan-129] 热点补齐：如果该盘符未开启 MFT 索引，立即点火，确保运行期间变动可感知
                if (!MftReader::instance().isDriveIndexed(letter)) {
                    qDebug() << "[AutoImport] 检测到托管库所在盘符未索引，热启动 USN 监控 ->" << letter;
                    MftReader::instance().buildIndex({letter});
                }

                (void)QtConcurrent::run([this, managedPath]() {
                    handleRecursiveIngestion(QDir::toNativeSeparators(managedPath).toStdWString());
                });
                changed = true;
            }
        }
    }
    if (changed) {
        MetadataManager::instance().notifyFullUIRebuild();
    }
}

void AutoImportManager::onEntryAdded(uint64_t key) {
    qDebug() << "[AutoImport] 接收到 entryAdded 信号 Key:" << QString::number(key, 16);
    (void)QtConcurrent::run([this, key]() {
        // 2026-08-xx 按照 Plan-126：USN 高效过滤 (FRN 链判定)
        if (!isUnderManagedLibrary(key)) {
            // qDebug() << "[AutoImport] entryAdded 被过滤 (不在托管库下) Key:" << QString::number(key, 16);
            return;
        }

        qDebug() << "[AutoImport] 确认变动发生在托管库内! Key:" << QString::number(key, 16);

        std::lock_guard<std::recursive_mutex> dbLock(s_dbAccessMutex);
        int idx = MftReader::instance().getIndexByKey(key);
        if (idx < 0) return;

        QString qPath = MftReader::instance().getFullPath(idx);

        // 调试：用户需要弹窗感知变动
        QMetaObject::invokeMethod(qApp, [qPath]() {
            QMessageBox::information(nullptr, "USN 变动感知", "感知到托管库内【新增】项目：\n" + qPath);
        }, Qt::QueuedConnection);

        std::wstring fullPath = qPath.toStdWString();
        uint64_t frn = MftReader::instance().getFrn(idx);

        if (MftReader::instance().isDirectory(idx)) {
            // 1:1 镜像同步：创建逻辑分类
            int existingCat = CategoryRepo::findByFrn(frn);
            if (existingCat == 0) {
                QString fileName = QFileInfo(qPath).fileName();
                std::wstring parentPath = QFileInfo(qPath).absolutePath().toStdWString();
                std::string parentFid;
                std::wstring pFrnStr;
                int parentCatId = 0;
                if (MetadataManager::fetchWinApiMetadataDirect(parentPath, parentFid, &pFrnStr)) {
                    try {
                        uint64_t pFrn = std::stoull(pFrnStr, nullptr, 16);
                        parentCatId = CategoryRepo::findByFrn(pFrn);
                    } catch (...) {}
                }

                Category cat;
                cat.parentId = parentCatId;
                cat.name = fileName.toStdWString();
                cat.physicalFrn = frn;
                cat.physicalPath = fullPath;
                cat.color = CategoryRepo::getDefaultColor();
                CategoryRepo::add(cat);

                // 核心修复：新文件夹移入必须触发递归同步，处理其内部子项
                handleRecursiveIngestion(fullPath);
                MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::FullRebuild);
            }
        } else {
            // 职责收拢：USN 驱动入库
            {
                std::lock_guard<std::mutex> lock(m_queueMutex);
                m_pendingPaths.push_back(fullPath);
            }
            QMetaObject::invokeMethod(m_debounceTimer, "start", Qt::QueuedConnection);
        }
    });
}

void AutoImportManager::onEntryUpdated(uint64_t key) {
    qDebug() << "[AutoImport] 接收到 entryUpdated 信号 Key:" << QString::number(key, 16);
    (void)QtConcurrent::run([this, key]() {
        // 2026-08-xx 按照 Plan-128：操作溯源判定
        bool isInternal = MetadataManager::instance().isInternalOperating();
        bool isUnderLibrary = isUnderManagedLibrary(key);

        if (isUnderLibrary) {
            int idx = MftReader::instance().getIndexByKey(key);
            if (idx >= 0) {
                QString qPath = MftReader::instance().getFullPath(idx);
                // 调试：用户需要弹窗感知变动
                QMetaObject::invokeMethod(qApp, [qPath]() {
                    QMessageBox::information(nullptr, "USN 变动感知", "感知到托管库内【更新/移动】项目：\n" + qPath);
                }, Qt::QueuedConnection);
            }
        }

        if (!isUnderLibrary) {
            // [信号审计]：项移出了托管库
            int idx = MftReader::instance().getIndexByKey(key);
            if (idx >= 0) {
                uint64_t frn = MftReader::instance().getFrn(idx);
                size_t dIdx = static_cast<size_t>(key >> 48);
                auto drives = MftReader::instance().getDriveList();
                
                if (dIdx < drives.size()) {
                    std::wstring volSerial = MetadataManager::getVolumeSerialNumber(drives[dIdx]);
                    wchar_t frnBuf[17]; swprintf(frnBuf, 17, L"%016llX", frn);
                    std::string fid = MetadataManager::generateFallbackFid(volSerial, frnBuf);
                    std::wstring oldPath = MetadataManager::instance().getPathByFid(fid);

                    // 物理红线：必须确保该项此前在托管库内（即存在元数据记录）才执行后续逻辑
                    if (!oldPath.empty() && MetadataManager::isInsideManagedLibrary(oldPath)) {
                        if (!isInternal) {
                            // 第三方移动出库：标记失效
                            if (MftReader::instance().isDirectory(idx)) {
                                MetadataManager::instance().setInvalidRecursive(oldPath, true);
                            } else {
                                MetadataManager::instance().setInvalid(oldPath, true);
                            }
                        } else {
                            // 2026-08-xx 按照 Plan-128：内部操作移出托管库 -> 执行硬删除
                            qDebug() << "[AutoImport] 内部操作移出托管库：执行硬删除 ->" << QString::fromStdWString(oldPath);
                            MetadataManager::instance().removeMetadataSync(oldPath);
                        }
                    }
                }
            }
            return;
        }

        std::lock_guard<std::recursive_mutex> dbLock(s_dbAccessMutex);
        int idx = MftReader::instance().getIndexByKey(key);
        if (idx < 0) return;

        QString qPath = MftReader::instance().getFullPath(idx);
        std::wstring fullPath = qPath.toStdWString();
        uint64_t frn = MftReader::instance().getFrn(idx);

        if (MftReader::instance().isDirectory(idx)) {
            // 1:1 镜像同步：重命名或位移
            int catId = CategoryRepo::findByFrn(frn);
            if (catId > 0) {
                Category cat = CategoryRepo::getById(catId);
                QString newName = QFileInfo(qPath).fileName();
                
                // 物理父目录 FRN 校验
                std::wstring parentPath = QFileInfo(qPath).absolutePath().toStdWString();
                std::string pfid; std::wstring pfrnStr;
                int newParentId = 0;
                if (MetadataManager::fetchWinApiMetadataDirect(parentPath, pfid, &pfrnStr)) {
                    newParentId = CategoryRepo::findByFrn(std::stoull(pfrnStr, nullptr, 16));
                }

                if (QString::fromStdWString(cat.name) != newName || cat.parentId != newParentId) {
                    qDebug() << "[Mirror] 物理同步逻辑分类 ->" << newName << "Parent:" << newParentId;
                    cat.name = newName.toStdWString();
                    cat.parentId = newParentId;
                    cat.physicalPath = fullPath;
                    CategoryRepo::update(cat);
                    MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::FullRebuild);
                }
            } else {
                // [Plan-130] 补全实时入库分流：处理首次从库外移入的文件夹
                qDebug() << "[AutoImport] 检测到新文件夹移入库内，触发递归入库 ->" << qPath;
                handleRecursiveIngestion(fullPath);
            }
        } else {
            // 文件更新：重新注册元数据
            {
                std::lock_guard<std::mutex> lock(m_queueMutex);
                m_pendingPaths.push_back(fullPath);
            }
            QMetaObject::invokeMethod(m_debounceTimer, "start", Qt::QueuedConnection);
        }
    });
}

void AutoImportManager::onEntryRemoved(uint64_t key) {
    (void)QtConcurrent::run([this, key]() {
        // 2026-08-xx 按照 Plan-128：操作溯源判定
        bool isInternal = MetadataManager::instance().isInternalOperating();

        // 由于 MftReader 已经删除了索引，无法通过 MftReader 获取路径，需直接操作数据库。
        // key 的低 48 位即为 FRN
        uint64_t frn = key & 0x0000FFFFFFFFFFFFull;
        
        std::lock_guard<std::recursive_mutex> dbLock(s_dbAccessMutex);
        
        // 1. 检查是否为镜像分类
        int catId = CategoryRepo::findByFrn(frn);
        if (catId > 0) {
            if (isInternal) {
                qDebug() << "[Mirror] 内部删除：同步移除镜像分类 ID:" << catId;
                CategoryRepo::remove(catId);
            } else {
                qDebug() << "[Mirror] 第三方删除：递归标记分类下项目失效";
                // 获取分类对应的物理路径并执行递归失效
                Category cat = CategoryRepo::getById(catId);
                if (!cat.physicalPath.empty()) {
                    MetadataManager::instance().setInvalidRecursive(cat.physicalPath, true);
                }
                // 注意：第三方删除时，分类本身也应被逻辑隐藏或标记
                CategoryRepo::remove(catId); 
            }
            MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::FullRebuild);
        }

        // 2. 文件项审计
        // 2026-08-xx 按照 Plan-128：物理删除审计。由于 MFT 记录已消失，通过所有在线卷的 FID 缓存找回路径。
        auto drives = MftReader::instance().getDriveList();
        for (const auto& volPath : drives) {
            std::wstring volSerial = MetadataManager::getVolumeSerialNumber(volPath);
            
            wchar_t frnBuf[17];
            swprintf(frnBuf, 17, L"%016llX", frn);
            std::string fid = MetadataManager::generateFallbackFid(volSerial, frnBuf);

            std::wstring path = MetadataManager::instance().getPathByFid(fid);
            if (!path.empty()) {
                // 物理红线：仅针对原先位于托管库内的项执行审计分流
                if (MetadataManager::isInsideManagedLibrary(path)) {
                    if (isInternal) {
                        qDebug() << "[AutoImport] 内部操作删除：执行硬删除 ->" << QString::fromStdWString(path);
                        MetadataManager::instance().removeMetadataSync(path);
                    } else {
                        qDebug() << "[AutoImport] 第三方外部删除：标记失效 ->" << QString::fromStdWString(path);
                        // 调试：用户需要弹窗感知变动
                        QString qPath = QString::fromStdWString(path);
                        QMetaObject::invokeMethod(qApp, [qPath]() {
                            QMessageBox::warning(nullptr, "USN 变动感知", "感知到托管库内项目【被删除】：\n" + qPath);
                        }, Qt::QueuedConnection);
                        MetadataManager::instance().setInvalid(path, true);
                    }
                }
            }
        }
        MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::FullRebuild);
    });
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

    // 2026-08-xx 异步化改造：将耗时的 registerItem 循环移入后台线程
    (void)QtConcurrent::run([this, pathsToProcess]() {
        std::lock_guard<std::recursive_mutex> dbLock(s_dbAccessMutex);
        MetadataManager::instance().setInternalOperating(true);

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

            // 此处可能涉及数据库加载/重命名，需在锁保护下执行
            DatabaseManager::instance().getMemoryDb(vol, letter);

            for (const auto& path : pair.second) {
                // registerItem 内部包含图像元数据提取，是 CPU 密集型操作
                MetadataManager::instance().registerItem(path, true);
            }
        }

        MetadataManager::instance().setInternalOperating(false);
        MetadataManager::instance().notifyFullUIRebuild();
    });
}

bool AutoImportManager::isUnderManagedLibrary(uint64_t key) {
    // [Plan-131 方案 B]：基于 FRN 链的高效托管路径过滤 (内存级 O(log N) 比对)
    uint64_t currentFrn = key & 0x0000FFFFFFFFFFFFull;
    int driveIdx = static_cast<int>(key >> 48);

    // 向上溯源 FRN 链
    uint64_t frn = currentFrn;
    int depth = 0;
    while (frn != 0 && depth < 20) {
        depth++;
        {
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            if (m_managedFrnCache.count(frn)) {
                // qDebug() << "[AutoImport] FRN 匹配成功:" << QString::number(frn, 16) << "在缓存中";
                return true;
            }
        }
        
        // 2026-xx-xx 增强型动态捕获：在向上溯源过程中，如果发现任何父目录符合 ArcMeta.Library_ 规范
        // 且位于磁盘根目录 (Parent FRN = 5)，则自动将其识别为托管库根节点并缓存。
        uint64_t compositeKey = (static_cast<uint64_t>(driveIdx) << 48) | (frn & 0x0000FFFFFFFFFFFFull);
        int idx = MftReader::instance().getIndexByKey(compositeKey);

        // [核心修正]：不再仅依赖 MftReader 的 ParentFRN 接口，因为内存索引可能滞后或不完整
        uint64_t parentFrn = 0;
        QString name;

        if (idx >= 0) {
            name = MftReader::instance().getName(idx);
            parentFrn = MftReader::instance().getParentFrnByFrn(frn, driveIdx);
        } else {
            // [Plan-131] 物理兜底：如果内存索引还没更新，直接问 OS 要路径和父 FRN
            // 解决“移入空文件夹时感知不到”的问题：此时 MFT 内存索引可能还没来得及更新
            QString path = MftReader::instance().getPathByFrn(frn, driveIdx);
            if (!path.isEmpty()) {
                name = QFileInfo(path).fileName();
                std::string fid; std::wstring pfrnStr;
                if (MetadataManager::fetchWinApiMetadataDirect(QFileInfo(path).absolutePath().toStdWString(), fid, &pfrnStr)) {
                    try { parentFrn = std::stoull(pfrnStr, nullptr, 16); } catch(...) {}
                }
            }
        }

        if (!name.isEmpty()) {
            // 物理对标：NTFS 磁盘根目录的 Parent FRN 通常为 5，但在某些卷或虚拟盘上可能为 0
            if (parentFrn == 5 || parentFrn == 0 || parentFrn == 1407374883553285ull) {
                if (name.startsWith("ArcMeta.Library_", Qt::CaseInsensitive)) {
                    std::lock_guard<std::mutex> lock(m_cacheMutex);
                    m_managedFrnCache.insert(frn);
                    qDebug() << "[AutoImport] 成功探测到托管库根目录 ->" << name << "FRN:" << QString::number(frn, 16);
                    return true;
                }
            }
        }

        // 获取父级 FRN (通过 MftReader 内存索引)
        if (parentFrn == 0 || parentFrn == frn) break;
        frn = parentFrn;
    }

    return false;
}

void AutoImportManager::handleRecursiveIngestion(const std::wstring& rootPath) {
    QDir dir(QString::fromStdWString(rootPath));
    if (!dir.exists()) return;

    // 2026-08-xx 异步化改造：整机加锁保护数据库写入，并迁移信号抑制逻辑
    std::lock_guard<std::recursive_mutex> dbLock(s_dbAccessMutex);

    MetadataManager::instance().setInternalOperating(true);
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    
    {
        SqlTransaction trans(db);

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

        if (rootCatId > 0) {
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
        
        trans.commit();
    }

    MetadataManager::instance().setInternalOperating(false);
    MetadataManager::instance().notifyFullUIRebuild();
}

} // namespace ArcMeta
