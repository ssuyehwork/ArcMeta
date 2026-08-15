#include "CoreController.h"
#include "AutoImportManager.h"
#include "NativeFolderWatcher.h"
#include "../mft/MftReader.h"
#include "AppConfig.h"
#include "../meta/CategoryRepo.h"
#include "../meta/MetadataManager.h"
#include "../meta/DatabaseManager.h"
#include "../meta/MediaExtractorPipeline.h"
#include "../ui/Logger.h"
#include <QThreadPool>
#include <QDebug>
#include <QTimer>
#include <QDateTime>
#include <QDir>
#include <QtConcurrent>
#include <unordered_set>
#include "PhysicalDiskSearchExtractor.h"
#include "../util/AssetImporter.h"

namespace ArcMeta {

CoreController& CoreController::instance() {
    static CoreController inst;
    return inst;
}

void CoreController::initializeCoreComponents() {
    qint64 metaInitStart = QDateTime::currentMSecsSinceEpoch();
    
    // 1. 底层持久化 SQLite 连接启动与定时机制保障
    ArcMeta::DatabaseManager::instance();
    
    // 2. 元数据内存索引结构预热
    ArcMeta::MetadataManager::instance();
    
    // 3. 分类依赖库拉起与同步机制保障
    ArcMeta::CategoryRepo::initialize();
    
    // 4. 后台提取特征管道、定时器及事件队列预热
    ArcMeta::MediaExtractorPipeline::instance();
    
    qDebug() << "[PERF] [CoreController] CoreComponents 单例拓扑链预热耗时:" << (QDateTime::currentMSecsSinceEpoch() - metaInitStart) << "ms";
}

CoreController::CoreController(QObject* parent) : QObject(parent) {
    // [Plan-115] 注册 Qt 元类型，防止 QueuedConnection 因未注册自定义类型而分发失败
    qRegisterMetaType<QList<ArcMeta::FileWatcherEvent>>("QList<ArcMeta::FileWatcherEvent>");

    // [Plan-115] 绑定 NativeFolderWatcher 纯净自定义批次变动信号到具体业务单例，彻底断开两端硬编码耦合
    connect(&NativeFolderWatcher::instance(), &NativeFolderWatcher::filesChanged, this, [this](const QList<ArcMeta::FileWatcherEvent>& events) {
        // 读取当前的 "DriveBar/CustomMonitoredFolders" 配置，用于前缀匹配
        QStringList customFolders = AppConfig::instance().getValue("DriveBar/CustomMonitoredFolders").toStringList();
        
        // 记录当前批次正在迁移的顶级路径，防止重入/死循环
        static std::unordered_set<std::wstring> s_currentlyMigrating;

        for (const auto& ev : events) {
            std::wstring normNewPath = MetadataManager::normalizePath(ev.newPath.toStdWString());
            QString qNewPath = QString::fromStdWString(normNewPath);

            // 1. 进行 "自动导入路径" 前缀精准匹配
            bool isAutoImportMatch = false;
            QString matchedPrefix;
            for (const QString& folder : customFolders) {
                if (qNewPath.compare(folder, Qt::CaseInsensitive) == 0) {
                    isAutoImportMatch = true;
                    matchedPrefix = folder;
                    break;
                }
                QString folderWithSep = folder;
                if (!folderWithSep.endsWith('/') && !folderWithSep.endsWith('\\')) {
                    folderWithSep += QDir::separator();
                }
                if (qNewPath.startsWith(folderWithSep, Qt::CaseInsensitive)) {
                    isAutoImportMatch = true;
                    matchedPrefix = folder;
                    break;
                }
            }

            if (isAutoImportMatch) {
                // 触发 自动导入剪切迁移机制
                if (ev.action == ArcMeta::WatcherAction::Added || ev.action == ArcMeta::WatcherAction::Modified) {
                    // 计算顶级项目（Top-level Item）物理路径
                    // 计算相对路径成分名，取出相对路径的第一级，拼接到 matchedPrefix 后面
                    QString relative = qNewPath.mid(matchedPrefix.length());
                    if (relative.startsWith('\\') || relative.startsWith('/')) {
                        relative = relative.mid(1);
                    }
                    // 健壮性优化：将所有路径分隔符统一替换为正斜杠 '/' 进行安全分段提取
                    QString topLevelComponent = relative.replace('\\', '/').section('/', 0, 0);
                    if (topLevelComponent.isEmpty()) {
                        // 变动的就是监控文件夹本身，跳过
                        continue;
                    }
                    QString topLevelPath = QDir::toNativeSeparators(QDir(matchedPrefix).absoluteFilePath(topLevelComponent));
                    std::wstring wTopLevelPath = topLevelPath.toStdWString();

                    // 检查物理路径是否真实存在，且其未被列入当前正在迁移的待处理批次中
                    if (QFileInfo(topLevelPath).exists() && s_currentlyMigrating.find(wTopLevelPath) == s_currentlyMigrating.end()) {
                        s_currentlyMigrating.insert(wTopLevelPath);

                        qDebug() << "[CoreController] 检测到自动导入目录变动，开始执行静默迁移:" << topLevelPath;

                        // 直接调用资产打包导入器进行剪切迁移入库 (targetCatId = 0)，后台静默进行
                        AssetImporter::importAssets(QStringList() << topLevelPath, 0, nullptr, [wTopLevelPath]() {
                            s_currentlyMigrating.erase(wTopLevelPath);
                            // 迁移完成后，自动调用 MetadataManager::instance().notifyFullUIRebuild() 进行自愈式刷新
                            MetadataManager::instance().notifyFullUIRebuild();
                        }, true);
                    }
                } else if (ev.action == ArcMeta::WatcherAction::Removed) {
                    // 处理 Removed 事件：在 CoreController 中处理 WatcherAction::Removed 时，若该路径是外部监控目录，调用 removeMetadataSync 即使返回未注册也不产生任何影响
                    MetadataManager::instance().removeMetadataSync(normNewPath);
                }
                continue;
            }

            // 2. 原普通资源库/常规文件的 IOCP 变动响应逻辑
            if (ev.action == ArcMeta::WatcherAction::Added || ev.action == ArcMeta::WatcherAction::Modified) {
                if (ev.isDirectory) {
                    (void)QtConcurrent::run([normNewPath]() {
                        AutoImportManager::instance().handleRecursiveIngestion(normNewPath);
                    });
                } else {
                    MetadataManager::instance().registerItemsAsync(QStringList() << ev.newPath, true);
                }
            } else if (ev.action == ArcMeta::WatcherAction::Removed) {
                emit NativeFolderWatcher::instance().managedFolderRemoved(normNewPath);
                MetadataManager::instance().removeMetadataSync(normNewPath);
            } else if (ev.action == ArcMeta::WatcherAction::Renamed) {
                std::wstring normOldPath = MetadataManager::normalizePath(ev.oldPath.toStdWString());
                MetadataManager::instance().syncAfterMove(normOldPath, normNewPath);
            }
        }
    }, Qt::QueuedConnection);
}

CoreController::~CoreController() {}

/**
 * @brief 启动系统初始化链条
 * 彻底废除分布式文件模式，全面转向 SQLite 内存模式 (One-Drive-One-DB)
 */
void CoreController::startSystem() {
    QThreadPool::globalInstance()->start([this]() {
        try {
            qint64 startTime = QDateTime::currentMSecsSinceEpoch();
            qDebug() << "[Core] >>> 开始后台异步初始化 (SQLite 内存模式) <<<";
            
            QMetaObject::invokeMethod(this, [this]() {
                setStatus("正在载入元数据缓存...", true);
            }, Qt::QueuedConnection);
            
            // 仅执行 SQLite 模式初始化
            MetadataManager::instance().initFromScchMode();

            // 在系统顶层统一提取一次“上次是否正常关闭”状态，提取后立刻置脏
            bool wasCleanShutdown = AppConfig::instance().getValue("System/LastCleanShutdown", false).toBool();
            AppConfig::instance().setValue("System/LastCleanShutdown", false);
            AppConfig::instance().sync();

            // 启动原生监控服务 (对应用户原话："采用NativeFolderWatcher (IOCP) 机制的方式")
            // 资源库无需开启 IOCP 监控（已取消）
            const auto drives = QDir::drives();
            for (const QFileInfo& d : drives) {
                std::wstring wPath = d.absolutePath().toStdWString();
                std::wstring volSerial = MetadataManager::getVolumeSerialNumber(wPath);
                QString letter = d.absolutePath().left(1).toUpper();

                if (volSerial != L"UNKNOWN") {
                    std::wstring managedAbsW = MetadataManager::getManagedLibraryPath(volSerial, letter);
                    if (!managedAbsW.empty()) {
                        qDebug() << "[Core] 识别到资源库，已取消开启 IOCP 监控:" << QString::fromStdWString(managedAbsW);
                        // NativeFolderWatcher::instance().addWatch(managedAbsW);
                    }
                }
            }

            // 2026-08-xx 物理同步：初始化完成后执行一次全量物理库对账 (在后台线程执行，避免阻塞 UI)
            AutoImportManager::instance().syncAllManagedLibraries(wasCleanShutdown);

            QMetaObject::invokeMethod(this, [this, startTime]() {
                setStatus("系统就绪", false);
                qDebug() << "[Core] !!! SQLite 内存模式初始化就绪，耗时:" << (QDateTime::currentMSecsSinceEpoch() - startTime) << "ms";
                emit initializationFinished();
            }, Qt::QueuedConnection);

        } catch (...) {
            qCritical() << "[Core] 初始化过程中发生异常";
            QMetaObject::invokeMethod(this, [this]() {
                setStatus("初始化失败", false);
                emit initializationFinished();
            }, Qt::QueuedConnection);
        }
    });
}

void CoreController::performSearch(const QString& keyword, const QString& scopeSource, int categoryId, const QString& parentPath) {
    // 1. 物理中止旧任务：无论新词是否为空，只要发起 performSearch 就必须清理前序任务
    abortSearch();
    
    ArcMeta::Logger::log(QString("[Core] performSearch 触发 -> 词: %1 | 来源: %2 | 路径: %3")
                        .arg(keyword).arg(scopeSource).arg(parentPath));

    if (keyword.isEmpty()) {
        ArcMeta::Logger::log("[Core] 关键词为空，跳过执行检索流程");
        return;
    }
    
    m_isSearchAborted = false;
    m_isSearching = true;
    int searchId = ++m_currentSearchId;

    ArcMeta::Logger::log(QString("[Core] 搜索任务已启动 [%1]，正在发射 searchStarted 信号...").arg(searchId));
    emit searchStarted();

    // 2. 异步启动双轨搜索任务
    (void)QtConcurrent::run([this, keyword, scopeSource, categoryId, parentPath, searchId]() {
        QStringList cacheResults;
        std::unordered_set<std::wstring> seenPaths;
        int totalFound = 0;

        // --- 第一阶段：内存缓存检索 (极速响应) ---
        cacheResults = MetadataManager::instance().searchInCache(keyword, scopeSource, categoryId, parentPath);
        for (const auto& p : cacheResults) {
            seenPaths.insert(p.toStdWString());
        }
        totalFound = static_cast<int>(cacheResults.size());

        // 发射第一批缓存结果
        if (!m_isSearchAborted && m_currentSearchId == searchId) {
            ArcMeta::Logger::log(QString("[Core] 缓存阶段发现 %1 条结果 [%2]，正在流式传输...").arg(cacheResults.size()).arg(searchId));
            emit searchResultsAvailable(cacheResults, false);
        }

        // --- 第二阶段：如果是物理导航模式，执行 I/O 扫描补全 (Plan-57) ---
        if (scopeSource == "nav" && !parentPath.isEmpty() && !m_isSearchAborted && m_currentSearchId == searchId) {
            int foundInDisk = PhysicalDiskSearchExtractor::performDiskSearch(
                parentPath, keyword, m_isSearchAborted, m_currentSearchId, searchId, seenPaths,
                [this](const QStringList& batch) {
                    emit searchResultsAvailable(batch, true);
                }
            );
            totalFound += foundInDisk;
        }

        m_isSearching = false;
        if (!m_isSearchAborted && m_currentSearchId == searchId) {
            ArcMeta::Logger::log(QString("[Core] 搜索总计发现 %1 项 [%2]，发送 searchFinished 信号").arg(totalFound).arg(searchId));
            emit searchFinished(totalFound);
        } else {
            ArcMeta::Logger::log(QString("[Core] 搜索任务被中途中止或作废 [%1]").arg(searchId));
        }
    });
}

void CoreController::abortSearch() {
    m_isSearchAborted = true;
    // 等待现有搜索任务退出的轻量化处理（实际生产环境可能需要更复杂的等待机制）
}

void CoreController::handleDeviceChange(unsigned long wParam, unsigned long long lParam) {
#ifdef Q_OS_WIN
    // 2026-05-24 按照用户要求：捕捉硬件变更，硬盘插入时触发 GLOB 扫描对账
    // [Plan-131 方案 E] 从 MainWindow 迁移至此
    if (wParam == 0x8000 /* DBT_DEVICEARRIVAL */ || wParam == 0x8004 /* DBT_DEVICEREMOVECOMPLETE */) {
        qDebug() << "[Core] [Plan-131] 检测到磁盘硬件变更，触发全量 GLOB 对账...";
        // 异步触发扫描，防止阻塞 UI
        (void)QtConcurrent::run([]() {
            // 或者 AutoImportManager::instance().syncAllManagedLibraries();
        });
    }
#endif
    Q_UNUSED(lParam);
}

void CoreController::setStatus(const QString& text, bool indexing) {
    if (m_statusText != text) {
        m_statusText = text;
        emit statusTextChanged(m_statusText);
    }
    if (m_isIndexing != indexing) {
        m_isIndexing = indexing;
        emit isIndexingChanged(m_isIndexing);
    }
}

} // namespace ArcMeta
