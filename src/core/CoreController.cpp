#include "CoreController.h"
#include "../meta/CategoryRepo.h"
#include "../meta/MetadataManager.h"
#include <QThreadPool>
#include <QDebug>
#include <QDateTime>
#include <QDirIterator>
#include <QtConcurrent>

namespace ArcMeta {

CoreController& CoreController::instance() {
    static CoreController inst;
    return inst;
}

CoreController::CoreController(QObject* parent) : QObject(parent) {
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

QStringList CoreController::performSearch(const QString& keyword, const QString& rootPath) {
    if (keyword.isEmpty()) return {};
    qDebug() << "[Core] 触发核心搜索接口, Keyword:" << keyword << "RootPath:" << rootPath;

    m_currentSearchId++;
    int searchId = m_currentSearchId;

    // 1. 内存/数据库搜索
    QStringList results = MetadataManager::instance().searchInCache(keyword, rootPath);

    // 2. 如果指定了物理路径，启动异步物理扫描
    if (!rootPath.isEmpty() && rootPath != "computer://") {
        QPointer<CoreController> weakThis(this);
        (void)QtConcurrent::run([weakThis, keyword, rootPath, searchId]() {
            qDebug() << "[Core] 启动异步物理扫描, SearchId:" << searchId << "Path:" << rootPath;
            QDirIterator it(rootPath, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
            std::vector<ItemRecord> batch;
            int count = 0;

            int scanLimit = 50000; // 扫描文件总数上限，防止在 C:\Windows 等目录下陷入死循环
            int totalScanned = 0;

            while (it.hasNext() && totalScanned < scanLimit) {
                if (!weakThis || weakThis->m_currentSearchId != searchId) return;
                QString path = it.next();
                QString fileName = it.fileName();
                totalScanned++;

                if (fileName.contains(keyword, Qt::CaseInsensitive)) {
                    batch.push_back(ItemRecord::create(path));
                    count++;
                }

                // 每 50 个结果推送一次，防止信号风暴
                if (batch.size() >= 50) {
                    QMetaObject::invokeMethod(weakThis.data(), "searchResultFound", Qt::QueuedConnection, Q_ARG(int, searchId), Q_ARG(std::vector<ArcMeta::ItemRecord>, batch));
                    batch.clear();
                }

                // 性能保护：匹配结果上限（设为 10000）
                if (count >= 10000) break;
            }

            if (!batch.empty() && weakThis && weakThis->m_currentSearchId == searchId) {
                QMetaObject::invokeMethod(weakThis.data(), "searchResultFound", Qt::QueuedConnection, Q_ARG(int, searchId), Q_ARG(std::vector<ArcMeta::ItemRecord>, batch));
            }
            if (weakThis && weakThis->m_currentSearchId == searchId) {
                QMetaObject::invokeMethod(weakThis.data(), "searchFinished", Qt::QueuedConnection, Q_ARG(int, searchId));
            }
            qDebug() << "[Core] 异步物理扫描结束, SearchId:" << searchId << "共找到项目:" << count;
        });
    }

    return results;
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
