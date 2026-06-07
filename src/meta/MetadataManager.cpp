#include <QFileInfo>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QtConcurrent>
#include <QThreadPool>
#include <QDir>
#include <QDebug>
#include <QTimer>
#include <QCoreApplication>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "MetadataManager.h"
#include "MetadataDefs.h"
#include "AmMetaScch.h"
#include "AllFrnManager.h"
#include "TagRepo.h"
#include "DriverRepo.h"
#include "../mft/MftReader.h"
#include "../meta/CategoryRepo.h"
#include "../ui/UiHelper.h"

#include <windows.h>
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

namespace ArcMeta {

// --- Internal Helper Functions ---

static void migrateLegacyScch(const std::wstring& folderPath) {
    QString legacyPath = QString::fromStdWString(folderPath) + "/metadata.scch";
    if (!QFile::exists(legacyPath)) return;

    qDebug() << "[Metadata] 发现旧版元数据文件，正在迁移:" << legacyPath;

    // 读取旧格式
    ArcMeta::AmMetaScch legacy(folderPath, L"");
    if (!legacy.load()) return;

    // 拆分写入新格式
    // 1. 文件夹自身
    ArcMeta::AmMetaScch folderScch(folderPath, L"");
    folderScch.folder() = legacy.folder();
    folderScch.save();

    // 2. 每个文件项
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4996)
#endif
    const std::map<std::wstring, ItemMeta>& items = legacy.items();
#ifdef _MSC_VER
#pragma warning(pop)
#endif
    for (auto it = items.begin(); it != items.end(); ++it) {
        ArcMeta::AmMetaScch fileScch(folderPath, it->first);
        fileScch.setItem(it->second);
        fileScch.save();
    }

    // 3. 删除旧文件
    QFile::remove(legacyPath);
    qDebug() << "[Metadata] 旧版元数据迁移完成，已移除:" << legacyPath;
}

static std::wstring normalizePath(const std::wstring& path) {
    if (path.empty()) return L"";
    QString qp = QDir::toNativeSeparators(QDir::cleanPath(QString::fromStdWString(path)));
    if (qp.length() == 2 && qp.endsWith(':')) qp += '\\';
    if (qp.length() >= 2 && qp[1] == ':') qp[0] = qp[0].toUpper();
    return qp.toStdWString();
}

static std::string generateFallbackFid(const std::wstring& vol, const std::wstring& frn) {
    if (vol.empty() || frn.empty()) return "";
    return "FRN:" + QString::fromStdWString(vol).toUpper().toStdString() + ":" + QString::fromStdWString(frn).toUpper().toStdString();
}

static std::string generateDeterministicSha256Id(const std::wstring& path) {
    if (path.empty()) return "";
    std::wstring nPath = normalizePath(path);
    std::wstring vol = MetadataManager::getVolumeSerialNumber(nPath);
    QByteArray seed = QString::fromStdWString(vol + L":" + nPath).toUtf8();
    QByteArray hash = QCryptographicHash::hash(seed, QCryptographicHash::Sha256);
    return "PATHURL:" + hash.left(16).toHex().toUpper().toStdString();
}

static std::wstring generateDeterministicFrn(const std::wstring& path) {
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
    m_batchTimer = new QTimer(this);
    m_batchTimer->setInterval(1500);
    m_batchTimer->setSingleShot(true);
    connect(m_batchTimer, &QTimer::timeout, [this]() {
        std::vector<std::wstring> paths;
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            for (const auto& p : m_dirtyPaths) {
                paths.push_back(p);
            }
            m_dirtyPaths.clear();
        }
        
        // 2026-06-xx 性能优化：持久化任务切入后台线程池，杜绝主线程 I/O 挂起
        if (!paths.empty()) {
            (void)QtConcurrent::run([this, paths]() {
                for (const auto& p : paths) {
                    persistAsync(p);
                }
            });
        }
    });

    // 2026-06-xx 物理加固：监听程序退出信号，确保内存中的元数据变更落盘
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, [this]() {
        qDebug() << "[Metadata] 程序退出前强制保存所有脏数据...";
        std::vector<std::wstring> paths;
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            for (const auto& p : m_dirtyPaths) paths.push_back(p);
            m_dirtyPaths.clear();
        }
        for (const auto& p : paths) persistAsync(p);

        // 2026-06-xx 物理加固：同步保存 FRN 索引表
        AllFrnManager::saveImmediately();
    });
}


void MetadataManager::initFromScchMode() {
    // 2026-06-xx 物理加固：防止重复初始化导致的 IO 风暴
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        if (m_loaded) {
            qDebug() << "[PERF] SCCH 缓存已在内存中，跳过重复加载";
            return;
        }
    }

    qint64 startTime = QDateTime::currentMSecsSinceEpoch();

    // 1.5 旧格式迁移
    QMap<QString, QString> allFrns = AllFrnManager::getAllFrns();
    if (allFrns.isEmpty()) {
        qDebug() << "[Metadata] AllFrnManager 数据为空，请检查 All_FRN_metadata.scch 是否加载成功";
    }

    for (auto it = allFrns.begin(); it != allFrns.end(); ++it) {
        migrateLegacyScch(it.value().toStdWString());
    }

    qDebug() << "[PERF] 开始加载分布式 SCCH 缓存...";
    TagRepo::load();
    loadDriverMetadata();
    std::unordered_map<std::wstring, RuntimeMeta> tempCache;
    std::unordered_map<std::string, std::wstring> tempFidToPath;
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        tempCache = m_cache;
        for (auto it = m_cache.begin(); it != m_cache.end(); ++it) {
            if (!it->second.fileId128.empty()) tempFidToPath[it->second.fileId128] = it->first;
        }
    }

    QMap<QString, QString> frnsMap = AllFrnManager::getAllFrns();
    qDebug() << "[Metadata] 从 All_FRN_metadata.scch 读入索引项数:" << frnsMap.size();
    
    int folderCount = 0;
    int fileCount = 0;

    for (QMap<QString, QString>::const_iterator itMap = frnsMap.constBegin(); itMap != frnsMap.constEnd(); ++itMap) {
        QString frnStr = itMap.key();
        QString lastKnownPath = itMap.value();
        std::wstring resolvedPath = QDir::toNativeSeparators(lastKnownPath).toStdWString();

        qDebug() << "[Metadata] 正在处理 FRN 映射项 -> FRN:" << frnStr << "Path:" << lastKnownPath;

        if (!QDir(QString::fromStdWString(resolvedPath)).exists()) {
            bool ok = false;
            unsigned long long frnVal = frnStr.toULongLong(&ok, 16);
            if (ok) {
                for (size_t d = 0; d < 26; ++d) {
                    std::wstring p = MftReader::instance().getPathFast(static_cast<int>(d), frnVal);
                    if (!p.empty()) {
                        if (p.find(L"metadata.scch") != std::wstring::npos) {
                            resolvedPath = QDir::toNativeSeparators(QFileInfo(QString::fromStdWString(p)).absolutePath()).toStdWString();
                        } else {
                            resolvedPath = p;
                        }
                        AllFrnManager::registerFrn(frnStr.toStdWString(), resolvedPath);
                        break;
                    }
                }
            }
        }
        
        // 2026-06-xx 重构：不再只加载 metadata.scch，而是扫描 .arcmeta/ 目录
        // 1. 加载文件夹自身元数据
        ArcMeta::AmMetaScch folderLoader(resolvedPath, L"");
        if (folderLoader.load()) {
            std::wstring nResolvedPath = normalizePath(resolvedPath);
            long long fSize = 0, fCtime = 0, fMtime = 0, fAtime = 0;
            std::string fId128;
            fetchWinApiMetadataDirect(resolvedPath, fId128, nullptr, &fSize, nullptr, &fCtime, &fMtime, &fAtime);

            const FolderMeta& f = folderLoader.folder();
            if (!f.isDefault()) {
                RuntimeMeta fMeta;
                fMeta.rating = f.rating; fMeta.color = f.color;
                for (size_t i = 0; i < f.tags.size(); ++i) fMeta.tags << QString::fromStdWString(f.tags[i]);
                fMeta.pinned = f.pinned; fMeta.note = f.note; fMeta.url = f.url; fMeta.encrypted = f.encrypted;
                fMeta.isFolder = true;
                fMeta.fileId128 = fId128.empty() ? f.fileId128 : fId128;
                fMeta.fileSize = fSize; fMeta.ctime = fCtime; fMeta.mtime = fMtime; fMeta.atime = fAtime;
                fMeta.palettes = f.palettes;
                tempCache[nResolvedPath] = fMeta;
            }
        }

        // 2. 加载该目录下所有文件的元数据 (.arcmeta/*.scch)
        QDir arcmetaDir(QString::fromStdWString(resolvedPath) + "/.arcmeta");
        QStringList scchFiles = arcmetaDir.entryList({"*.scch"}, QDir::Files);
        if (folderLoader.load()) {
            folderCount++;
            qDebug() << "  [OK] 加载文件夹元数据:" << lastKnownPath;
        }

        for (const QString& scchFile : scchFiles) {
            if (scchFile == "__folder__.scch") continue;
            QString fileName = scchFile.left(scchFile.length() - 5); // 移除 .scch
            
            ArcMeta::AmMetaScch itemLoader(resolvedPath, fileName.toStdWString());
            if (itemLoader.load()) {
                const ItemMeta& item = itemLoader.item();
                if (item.hasUserOperations()) {
                    RuntimeMeta iMeta;
                    iMeta.rating = item.rating; iMeta.color = item.color;
                    for (size_t tIdx = 0; tIdx < item.tags.size(); ++tIdx) iMeta.tags << QString::fromStdWString(item.tags[tIdx]);
                    iMeta.pinned = item.pinned; iMeta.note = item.note; iMeta.url = item.url; iMeta.encrypted = item.encrypted;
                    iMeta.isFolder = (item.type == L"folder");
                    iMeta.fileId128 = item.fileId128;
                    
                    std::wstring itemPath = resolvedPath + L"\\" + fileName.toStdWString();
                    fetchWinApiMetadataDirect(itemPath, iMeta.fileId128, nullptr, &iMeta.fileSize, nullptr, &iMeta.ctime, &iMeta.mtime, &iMeta.atime);

                    iMeta.palettes = item.palettes;
                    std::wstring nItemPath = normalizePath(itemPath);
                    tempCache[nItemPath] = iMeta;
                    if (!iMeta.fileId128.empty()) tempFidToPath[iMeta.fileId128] = nItemPath;
                    fileCount++;
                }
            }
        }
    }
    qDebug() << "[Metadata] 分布式加载完成: 目录数=" << folderCount << " 文件数=" << fileCount;

    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_cache = tempCache;
        m_fidToPath = tempFidToPath;
        m_loaded = true;

        // 初始化增量计数器 (Part 4)
        // 2026-06-xx 物理修复：已在 unique_lock 保护下，确保 s_totalFileCount 初始化不包含文件夹
        int totalFiles = 0;
        for (auto it = m_cache.begin(); it != m_cache.end(); ++it) {
            if (!it->second.isFolder) totalFiles++;
        }
        CategoryRepo::setTotalFileCount(totalFiles);

    }

    // 2026-06-xx 逻辑加固：由于元数据加载可能导致分类计数的逻辑变动，强制执行一次全量重计
    // 物理注意：必须在锁外执行，否则会因为 CategoryRepo 回调 forEachCachedItem 导致互斥锁递归死锁
    CategoryRepo::fullRecount();

    qDebug() << "[PERF] 分布式 SCCH 缓存加载完成，项数:" << tempCache.size() << " 耗时:" << (QDateTime::currentMSecsSinceEpoch() - startTime) << "ms";
    emit metaChanged("__RELOAD_ALL__");
}

RuntimeMeta MetadataManager::getMeta(const std::wstring& path) {
    std::wstring nPath = normalizePath(path);
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        std::unordered_map<std::wstring, RuntimeMeta>::const_iterator it = m_cache.find(nPath);
        if (it != m_cache.end()) return it->second;
    }

    QFileInfo info(QString::fromStdWString(nPath));
    std::wstring parentDir;
    std::wstring fileName;

    if (info.isDir()) {
        parentDir = QDir::toNativeSeparators(info.absoluteFilePath()).toStdWString();
        fileName = L""; // 文件夹模式
    } else {
        parentDir = QDir::toNativeSeparators(info.absolutePath()).toStdWString();
        fileName = info.fileName().toStdWString();
    }

    // 优先加载文件级或文件夹级 .scch (fileName为空代表 __folder__.scch)
    ArcMeta::AmMetaScch loader(parentDir, fileName);
    if (loader.load()) {
        qDebug() << "[Metadata] 从磁盘加载命中:" << QString::fromStdWString(nPath);
        bool hasMeta = false;
        RuntimeMeta rm;
        if (info.isDir()) {
            const FolderMeta& folder = loader.folder();
            if (!folder.isDefault()) {
                rm.rating = folder.rating; rm.color = folder.color;
                rm.pinned = folder.pinned; rm.note = folder.note; rm.url = folder.url; rm.palettes = folder.palettes;
                rm.isFolder = true;
                rm.fileId128 = folder.fileId128;
                for (size_t i = 0; i < folder.tags.size(); ++i) rm.tags << QString::fromStdWString(folder.tags[i]);
                hasMeta = true;
            }
        } else {
            const ItemMeta& item = loader.item();
            if (item.hasUserOperations()) {
                rm.rating = item.rating; rm.color = item.color;
                rm.pinned = item.pinned; rm.encrypted = item.encrypted;
                rm.note = item.note; rm.url = item.url; rm.palettes = item.palettes;
                rm.isFolder = (item.type == L"folder");
                rm.fileId128 = item.fileId128;
                for (size_t i = 0; i < item.tags.size(); ++i) rm.tags << QString::fromStdWString(item.tags[i]);
                hasMeta = true;
            }
        }


        if (hasMeta) {
            fetchWinApiMetadataDirect(nPath, rm.fileId128, nullptr, &rm.fileSize, nullptr, &rm.ctime, &rm.mtime, &rm.atime);

            // 2026-06-xx 物理加固：补全 FRN 登记，确保下次启动能通过 All_FRN_metadata.scch 找回分布式元数据
            registerArcmetaFrn(parentDir);

            std::unique_lock<std::shared_mutex> lock(m_mutex);
            m_cache[nPath] = rm;
            if (!rm.fileId128.empty()) m_fidToPath[rm.fileId128] = nPath;
            return rm;
        }
    }
    return RuntimeMeta();
}

std::wstring MetadataManager::getPathByFid(const std::string& fid) {
    if (fid.empty()) return L"";
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    auto it = m_fidToPath.find(fid);
    return (it != m_fidToPath.end()) ? it->second : L"";
}

void MetadataManager::ensureActivated(const std::wstring& nPath) {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    if (m_cache.find(nPath) != m_cache.end()) return;

    qDebug() << "[Metadata] 正在激活路径元数据容器:" << QString::fromStdWString(nPath);
    // 激活操作
    RuntimeMeta rm;
    std::wstring frn;
    std::wstring type;
    if (fetchWinApiMetadataDirect(nPath, rm.fileId128, &frn, &rm.fileSize, &type, &rm.ctime, &rm.mtime, &rm.atime)) {
        rm.isFolder = (type == L"folder");
        m_cache[nPath] = rm;
        if (!rm.fileId128.empty()) m_fidToPath[rm.fileId128] = nPath;

        // 注册到 AllFrnManager (锚点为所属目录的 .arcmeta)
        QFileInfo info(QString::fromStdWString(nPath));
        std::wstring parentDir;
        if (rm.isFolder) {
            parentDir = QDir::toNativeSeparators(info.absoluteFilePath()).toStdWString();
        } else {
            parentDir = QDir::toNativeSeparators(info.absolutePath()).toStdWString();
        }

        registerArcmetaFrn(parentDir);

        if (!rm.isFolder) CategoryRepo::incrementTotalFileCount(1);
    }
}

void MetadataManager::setRating(const std::wstring& path, int rating, bool notify) {
    std::wstring nPath = normalizePath(path);
    qDebug() << "[Metadata] setRating ->" << QString::fromStdWString(nPath) << "Val:" << rating;
    ensureActivated(nPath);
    { 
        std::unique_lock<std::shared_mutex> lock(m_mutex); 
        m_cache[nPath].rating = rating; 
    }
    if (notify) emit metaChanged(QString::fromStdWString(nPath));
    debouncePersist(nPath);
}

void MetadataManager::setColor(const std::wstring& path, const std::wstring& color, bool notify) {
    std::wstring nPath = normalizePath(path);
    qDebug() << "[Metadata] setColor ->" << QString::fromStdWString(nPath) << "Val:" << QString::fromStdWString(color);
    ensureActivated(nPath);
    { 
        std::unique_lock<std::shared_mutex> lock(m_mutex); 
        m_cache[nPath].color = color; 
    }
    if (notify) emit metaChanged(QString::fromStdWString(nPath));
    debouncePersist(nPath);
}

void MetadataManager::setPinned(const std::wstring& path, bool pinned, bool notify) {
    std::wstring nPath = normalizePath(path);
    qDebug() << "[Metadata] setPinned ->" << QString::fromStdWString(nPath) << "Val:" << pinned;
    ensureActivated(nPath);
    { std::unique_lock<std::shared_mutex> lock(m_mutex); m_cache[nPath].pinned = pinned; }
    if (notify) emit metaChanged(QString::fromStdWString(nPath));
    debouncePersist(nPath);
}

void MetadataManager::setTags(const std::wstring& path, const QStringList& tags, bool notify) {
    std::wstring nPath = normalizePath(path);
    qDebug() << "[Metadata] setTags ->" << QString::fromStdWString(nPath) << "Val:" << tags;
    ensureActivated(nPath);

    // 1. 获取旧标签
    QStringList oldTags;
    std::string fid;
    bool isFolder = false;
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        auto it = m_cache.find(nPath);
        if (it != m_cache.end()) {
            oldTags = it->second.tags;
            fid = it->second.fileId128;
            isFolder = it->second.isFolder;
        }
    }

    // 2. 更新内存缓存
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_cache[nPath].tags = tags;
        if (fid.empty()) fid = m_cache[nPath].fileId128;
        if (!isFolder) isFolder = m_cache[nPath].isFolder;
    }

    // 3. 增量更新全局标签索引 (仅限文件)
    if (!fid.empty() && !isFolder) {
        // 解绑已移除的标签
        for (const QString& t : oldTags) {
            if (!tags.contains(t)) {
                TagRepo::unbindTag(fid, t.toStdWString());
            }
        }
        // 绑定新增的标签
        for (const QString& t : tags) {
            if (!oldTags.contains(t)) {
                TagRepo::bindTag(fid, t.toStdWString(), nPath);
            }
        }
    }

    if (notify) emit metaChanged(QString::fromStdWString(nPath));
    debouncePersist(nPath);
}

void MetadataManager::setNote(const std::wstring& path, const std::wstring& note, bool notify) {
    std::wstring nPath = normalizePath(path);
    qDebug() << "[Metadata] setNote ->" << QString::fromStdWString(nPath);
    ensureActivated(nPath);
    { std::unique_lock<std::shared_mutex> lock(m_mutex); m_cache[nPath].note = note; }
    if (notify) emit metaChanged(QString::fromStdWString(nPath));
    debouncePersist(nPath);
}

void MetadataManager::setURL(const std::wstring& path, const std::wstring& url, bool notify) {
    std::wstring nPath = normalizePath(path);
    qDebug() << "[Metadata] setURL ->" << QString::fromStdWString(nPath);
    ensureActivated(nPath);
    { std::unique_lock<std::shared_mutex> lock(m_mutex); m_cache[nPath].url = url; }
    if (notify) emit metaChanged(QString::fromStdWString(nPath));
    debouncePersist(nPath);
}

void MetadataManager::setEncrypted(const std::wstring& path, bool encrypted, bool notify) {
    std::wstring nPath = normalizePath(path);
    qDebug() << "[Metadata] setEncrypted ->" << QString::fromStdWString(nPath) << "Val:" << encrypted;
    ensureActivated(nPath);
    { std::unique_lock<std::shared_mutex> lock(m_mutex); m_cache[nPath].encrypted = encrypted; }
    if (notify) emit metaChanged(QString::fromStdWString(nPath));
    debouncePersist(nPath);
}

void MetadataManager::setPalettes(const std::wstring& path, const QVector<QPair<QColor, float>>& palettes, bool notify) {
    std::wstring nPath = normalizePath(path);
    qDebug() << "[Metadata] setPalettes ->" << QString::fromStdWString(nPath) << "Count:" << palettes.size();
    ensureActivated(nPath);
    std::vector<PaletteEntry> entries;
    for (int i = 0; i < palettes.size(); ++i) { entries.push_back(PaletteEntry(palettes[i].first, palettes[i].second)); }
    { std::unique_lock<std::shared_mutex> lock(m_mutex); m_cache[nPath].palettes = entries; }
    if (notify) emit metaChanged(QString::fromStdWString(nPath));
    debouncePersist(nPath);
}

void MetadataManager::setItemVisualMetadata(const std::wstring& path, const std::wstring& color, const QVector<QPair<QColor, float>>& palettes, bool notify) {
    std::wstring nPath = normalizePath(path);
    qDebug() << "[Metadata] setItemVisualMetadata ->" << QString::fromStdWString(nPath);
    ensureActivated(nPath);
    std::vector<PaletteEntry> entries;
    for (int i = 0; i < palettes.size(); ++i) { entries.push_back(PaletteEntry(palettes[i].first, palettes[i].second)); }
    
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        RuntimeMeta& meta = m_cache[nPath];
        meta.color = color;
        meta.palettes = entries;
    }
    
    if (notify) emit metaChanged(QString::fromStdWString(nPath));
    debouncePersist(nPath);
}

QVector<QColor> MetadataManager::getPalettes(const std::wstring& path) {
    std::wstring nPath = normalizePath(path);
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        auto it = m_cache.find(nPath);
        if (it != m_cache.end() && !it->second.palettes.empty()) {
            QVector<QColor> colors;
            for (const auto& entry : it->second.palettes) colors << entry.color;
            return colors;
        }
    }

    QFileInfo info(QString::fromStdWString(nPath));
    std::wstring parentDir = QDir::toNativeSeparators(info.absolutePath()).toStdWString();
    std::wstring fileName = info.fileName().toStdWString();

    ArcMeta::AmMetaScch loader(parentDir, info.isDir() ? L"" : fileName);
    if (loader.load()) {
        std::vector<PaletteEntry> entries;
        bool hasEntries = false;
        if (info.isDir()) {
            entries = loader.folder().palettes;
            hasEntries = !entries.empty();
        } else {
            entries = loader.item().palettes;
            hasEntries = !entries.empty();
        }

        
        if (!entries.empty()) {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            m_cache[nPath].palettes = entries;
        }

        QVector<QColor> colors;
        for (size_t i = 0; i < entries.size(); ++i) colors << entries[i].color;
        return colors;
    }
    return {};
}

void MetadataManager::debouncePersist(const std::wstring& nPath) {
    { std::unique_lock<std::shared_mutex> lock(m_mutex); m_dirtyPaths.insert(nPath); }
    QMetaObject::invokeMethod(m_batchTimer, "start", Qt::QueuedConnection);
}

void MetadataManager::renameItem(const std::wstring& oldPath, const std::wstring& newPath) {
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        std::unordered_map<std::wstring, RuntimeMeta>::iterator it = m_cache.find(oldPath);
        if (it != m_cache.end()) { 
            std::string fid = it->second.fileId128;
            m_cache[newPath] = it->second; 
            m_cache.erase(it); 
            if (!fid.empty()) m_fidToPath[fid] = newPath;
        }
    }
    emit metaChanged(QString::fromStdWString(newPath));
}

void MetadataManager::removeMetadataSync(const std::wstring& path) {
    std::wstring nPath = normalizePath(path);
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        for (std::unordered_map<std::wstring, RuntimeMeta>::iterator it = m_cache.begin(); it != m_cache.end(); ) {
            if (it->first == nPath || it->first.find(nPath + L"\\") == 0 || it->first.find(nPath + L"/") == 0) {
                if (!it->second.isFolder) CategoryRepo::incrementTotalFileCount(-1);
                if (!it->second.fileId128.empty()) m_fidToPath.erase(it->second.fileId128);
                it = m_cache.erase(it);
            }
            else ++it;
        }
    }
    QFileInfo info(QString::fromStdWString(path));
    if (info.isDir()) {
        // 删除新格式目录
        QDir arcmetaDir(info.absoluteFilePath() + "/.arcmeta");
        arcmetaDir.removeRecursively();
    } else {
        // 删除文件级 scch
        QString scchPath = info.absolutePath() + "/.arcmeta/" + info.fileName() + ".scch";
        QFile::remove(scchPath);
    }
}

std::wstring MetadataManager::getVolumeSerialNumber(const std::wstring& path) {
    if (path.length() < 2 || path[1] != L':') return L"UNKNOWN";
    wchar_t root[4] = { path[0], L':', L'\\', L'\0' };
    DWORD serial = 0;
    if (GetVolumeInformationW(root, nullptr, 0, &serial, nullptr, nullptr, nullptr, 0)) {
        wchar_t buf[16]; swprintf(buf, 16, L"%08X", serial); return buf;
    }
    return L"UNKNOWN";
}

bool MetadataManager::fetchWinApiMetadataDirect(const std::wstring& path, std::string& outId128, std::wstring* outFrn, long long* outSize, std::wstring* outType, long long* outCtime, long long* outMtime, long long* outAtime) {
    HANDLE hFile = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    std::wstring vol = getVolumeSerialNumber(path);
    if (hFile == INVALID_HANDLE_VALUE) {
        if (outFrn) *outFrn = generateDeterministicFrn(path);
        outId128 = generateDeterministicSha256Id(path);
        return false;
    }
    BY_HANDLE_FILE_INFORMATION basicInfo;
    if (GetFileInformationByHandle(hFile, &basicInfo)) {
        wchar_t frnBuf[17];
        unsigned long long fullFrn = (static_cast<unsigned long long>(basicInfo.nFileIndexHigh) << 32) | basicInfo.nFileIndexLow;
        swprintf(frnBuf, 17, L"%016llX", fullFrn);
        if (outFrn) *outFrn = frnBuf;
        outId128 = generateFallbackFid(vol, frnBuf);
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

void MetadataManager::syncPhysicalMetadata(const std::wstring& path) { persistAsync(path); }

void MetadataManager::activateItem(const std::wstring& path) {
    std::string fid; std::wstring frn;
    if (fetchWinApiMetadataDirect(path, fid, &frn)) {
        std::wstring nPath = normalizePath(path);
        QFileInfo info(QString::fromStdWString(nPath));
        std::wstring parentDir = info.isDir() ? QDir::toNativeSeparators(info.absoluteFilePath()).toStdWString()
                                              : QDir::toNativeSeparators(info.absolutePath()).toStdWString();
        
        // 注册项自身的 FRN
        AllFrnManager::registerFrn(frn, parentDir);
        
        // 注册所属目录的 .arcmeta FRN
        registerArcmetaFrn(parentDir);
        
        // 激活并持久化
        instance().ensureActivated(nPath);
        instance().syncPhysicalMetadata(nPath);
    }
}

void MetadataManager::tryExtractColor(const std::wstring& path) {
    std::wstring nPath = normalizePath(path);
    if (!instance().getMeta(nPath).color.empty()) return;
    
    QFileInfo info(QString::fromStdWString(nPath));
    QString qPath = QString::fromStdWString(nPath);
    
    if (info.isFile()) {
        if (ArcMeta::UiHelper::isGraphicsFile(info.suffix().toLower())) {
            auto palette = ArcMeta::UiHelper::extractPalette(qPath);
            if (!palette.isEmpty()) {
                QColor dominant = ArcMeta::UiHelper::quantizeColor(palette.first().first);
                instance().setItemVisualMetadata(nPath, dominant.name().toUpper().toStdWString(), palette, false);
            }
        }
    } else if (info.isDir()) {
        QDir subDir(qPath);
        QFileInfoList subFiles = subDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
        for (const auto& sf : subFiles) {
            if (ArcMeta::UiHelper::isGraphicsFile(sf.suffix().toLower())) {
                auto palette = ArcMeta::UiHelper::extractPalette(sf.absoluteFilePath());
                if (!palette.isEmpty()) {
                    QColor dominant = ArcMeta::UiHelper::quantizeColor(palette.first().first);
                    instance().setItemVisualMetadata(nPath, dominant.name().toUpper().toStdWString(), palette, false);
                    break;
                }
            }
        }
    }
}

void MetadataManager::registerArcmetaFrn(const std::wstring& parentDir) {
    std::wstring arcmetaPath = parentDir + L"\\.arcmeta";
    std::wstring arcmetaFrn; std::string arcmetaFid;
    if (fetchWinApiMetadataDirect(arcmetaPath, arcmetaFid, &arcmetaFrn)) {
        AllFrnManager::registerFrn(arcmetaFrn, parentDir);
    }
}

std::string MetadataManager::getFileIdSync(const std::wstring& path) {
    std::string fid;
    if (!fetchWinApiMetadataDirect(path, fid, nullptr)) fid = generateDeterministicSha256Id(path);
    return fid;
}

void MetadataManager::persistAsync(const std::wstring& path) {
    std::wstring nPath = normalizePath(path);
    qDebug() << "[Metadata] 准备持久化元数据到磁盘:" << QString::fromStdWString(nPath);
    QFileInfo info(QString::fromStdWString(nPath));
    
    std::wstring parentDir;
    std::wstring fileName;
    if (info.isDir()) {
        parentDir = QDir::toNativeSeparators(info.absoluteFilePath()).toStdWString();
        fileName = L"";
    } else {
        parentDir = QDir::toNativeSeparators(info.absolutePath()).toStdWString();
        fileName = info.fileName().toStdWString();
    }
    
    RuntimeMeta rMeta = getMeta(nPath);

    if (info.isDir() && info.isRoot()) {
        DriverEntry de;
        de.volumePath = nPath;
        de.rating = rMeta.rating;
        de.color = rMeta.color;
        de.pinned = rMeta.pinned;
        de.note = rMeta.note;
        de.url = rMeta.url;
        for (int i = 0; i < rMeta.tags.size(); ++i) de.tags.push_back(rMeta.tags[i].toStdWString());
        de.palettes = rMeta.palettes;
        DriverRepo::update(de);
    } else {
        // 直接写该文件的 scch (不再读整目录)
        ArcMeta::AmMetaScch loader(parentDir, fileName);
        if (info.isDir()) {
            FolderMeta& folder = loader.folder();
            folder.rating = rMeta.rating; folder.color = rMeta.color;
            folder.pinned = rMeta.pinned; folder.note = rMeta.note;
            folder.url = rMeta.url;
            folder.tags.clear(); for (int i = 0; i < rMeta.tags.size(); ++i) folder.tags.push_back(rMeta.tags[i].toStdWString());
            folder.palettes = rMeta.palettes;
            folder.fileId128 = rMeta.fileId128;
        } else {
            ItemMeta& item = loader.item();
            item.rating = rMeta.rating; item.color = rMeta.color;
            item.pinned = rMeta.pinned; item.encrypted = rMeta.encrypted;
            item.note = rMeta.note; item.url = rMeta.url;
            item.tags.clear(); for (int i = 0; i < rMeta.tags.size(); ++i) item.tags.push_back(rMeta.tags[i].toStdWString());
            item.palettes = rMeta.palettes;
            item.fileId128 = rMeta.fileId128;
            item.type = L"file";
        }
        loader.save();
    }
    
    // 追踪目标改为 .arcmeta 目录的位置
    registerArcmetaFrn(parentDir);
        
    emit metaChanged(QString::fromStdWString(nPath));
}

void MetadataManager::loadDriverMetadata() {
    qint64 start = QDateTime::currentMSecsSinceEpoch();
    std::vector<DriverEntry> drivers = DriverRepo::loadAll();
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    for (const auto& de : drivers) {
        std::wstring nPath = normalizePath(de.volumePath);
        RuntimeMeta rm;
        rm.rating = de.rating;
        rm.color = de.color;
        rm.pinned = de.pinned;
        rm.note = de.note;
        rm.url = de.url;
        for (const auto& t : de.tags) rm.tags << QString::fromStdWString(t);
        rm.palettes = de.palettes;
        rm.isFolder = true;
        
        fetchWinApiMetadataDirect(nPath, rm.fileId128, nullptr, &rm.fileSize, nullptr, &rm.ctime, &rm.mtime, &rm.atime);
        m_cache[nPath] = rm;
        if (!rm.fileId128.empty()) m_fidToPath[rm.fileId128] = nPath;
    }
    qDebug() << "[PERF] 驱动器根目录元数据加载耗时:" << (QDateTime::currentMSecsSinceEpoch() - start) << "ms";
}

bool MetadataManager::hasPendingSync() const { return false; }
QStringList MetadataManager::getPendingSyncDirs() { return {}; }
void MetadataManager::removeFidsFromLog(const QStringList&) {}
void MetadataManager::addToSyncLog(const std::wstring&) {}
void MetadataManager::saveSyncLog() {}

QStringList MetadataManager::searchInCache(const QString& keyword) {
    QStringList results; if (keyword.isEmpty()) return results;
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    for (std::unordered_map<std::wstring, RuntimeMeta>::const_iterator it = m_cache.begin(); it != m_cache.end(); ++it) {
        const std::wstring& path = it->first; const RuntimeMeta& meta = it->second;
        QString qPath = QString::fromStdWString(path); QString qNote = QString::fromStdWString(meta.note);
        bool match = qPath.contains(keyword, Qt::CaseInsensitive) || qNote.contains(keyword, Qt::CaseInsensitive);
        if (!match) { for (int i = 0; i < meta.tags.size(); ++i) { if (meta.tags[i].contains(keyword, Qt::CaseInsensitive)) { match = true; break; } } }
        if (match) results << qPath;
    }
    return results;
}

} // namespace ArcMeta
