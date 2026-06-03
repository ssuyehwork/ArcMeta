#include "CoreController.h"
#include "../db/CategoryRepo.h"
#include "../meta/MetadataManager.h"
#include <QThreadPool>
#include <QDebug>
#include <QDateTime>

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
 * 彻底废除数据库模式，全面转向纯 SCCH 架构
 */
void CoreController::startSystem() {
    QThreadPool::globalInstance()->start([this]() {
        try {
            qint64 startTime = QDateTime::currentMSecsSinceEpoch();
            qDebug() << "[Core] >>> 开始后台异步初始化 (SCCH 架构) <<<";
            
            QMetaObject::invokeMethod(this, [this]() {
                setStatus("正在载入元数据缓存...", true);
            }, Qt::QueuedConnection);
            
            // 仅执行 SCCH 模式初始化
            MetadataManager::instance().initFromScchMode();
            
            QMetaObject::invokeMethod(this, [this, startTime]() {
                setStatus("系统就绪", false);
                qDebug() << "[Core] !!! SCCH 架构初始化就绪，耗时:" << (QDateTime::currentMSecsSinceEpoch() - startTime) << "ms";
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

QStringList CoreController::performSearch(const QString& keyword) {
    if (keyword.isEmpty()) return {};
    // 彻底废除数据库搜索，强制使用 SCCH 内存搜索
    return MetadataManager::instance().searchInCache(keyword);
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
