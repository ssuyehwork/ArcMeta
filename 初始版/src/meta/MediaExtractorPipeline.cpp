#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "MediaExtractorPipeline.h"
#include "MetadataManager.h"
#include "../ui/MediaColorExtractor.h"
#include "../core/SyncStatusService.h"
#include "DatabaseManager.h"
#include <QImageReader>
#include <QSvgRenderer>
#include <QFileInfo>
#include <QDir>
#include <QtConcurrent/QtConcurrent>
#include <QDebug>
#include <QCoreApplication>
#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#include <objbase.h>
#endif

namespace ArcMeta {

MediaExtractorPipeline& MediaExtractorPipeline::instance() {
    static MediaExtractorPipeline inst;
    return inst;
}

MediaExtractorPipeline::MediaExtractorPipeline(QObject* parent) : QObject(parent) {
    m_timer = new QTimer(this);
    m_timer->setInterval(1500);
    connect(m_timer, &QTimer::timeout, this, &MediaExtractorPipeline::processNextBatch);

    m_retryTimer = new QTimer(this);
    m_retryTimer->setInterval(3000);
    connect(m_retryTimer, &QTimer::timeout, this, &MediaExtractorPipeline::processRetryQueue);

    // 【修复】必须放在两个定时器创建完成之后：moveToThread 只会带走
    // 调用时已存在的子对象，先创建子对象、最后再整体迁移线程，
    // 才能保证 m_timer/m_retryTimer 真正跟随主线程事件循环运行，避免后台线程因没有事件循环导致定时器哑死的问题。
    if (QCoreApplication::instance()) {
        this->moveToThread(QCoreApplication::instance()->thread());
    }
}

MediaExtractorPipeline::~MediaExtractorPipeline() {
    m_timer->stop();
    m_retryTimer->stop();
}

void MediaExtractorPipeline::cancelAll() {
    m_isCanceled.store(true);
    std::vector<std::wstring> abandoned;
    {
        std::lock_guard<std::mutex> queueLock(m_queueMutex);
        abandoned = std::move(m_queue);
        m_queue.clear();
    }
    {
        std::lock_guard<std::mutex> retryLock(m_retryMutex);
        m_visualRetryQueue.clear();
    }
    m_activeCount.store(0);
    SyncStatusService::instance().updateMediaPending(0);
    qDebug() << "[DB_TRACE] MediaExtractorPipeline::cancelAll 触发全局取消，已安全丢弃排队中的" << abandoned.size() << "个任务";
}

void MediaExtractorPipeline::cancelBatch(const std::vector<std::wstring>& paths) {
    if (paths.empty()) return;
    std::lock_guard<std::mutex> queueLock(m_queueMutex);
    
    // 收集标准化的前缀用于批量匹配过滤
    std::vector<std::wstring> normPrefixes;
    normPrefixes.reserve(paths.size());
    for (const auto& p : paths) {
        normPrefixes.push_back(MetadataManager::normalizePath(p));
    }

    auto isPrefixMatched = [&](const std::wstring& targetPath) {
        std::wstring normTarget = MetadataManager::normalizePath(targetPath);
        for (const auto& prefix : normPrefixes) {
            if (normTarget == prefix) return true;
            if (normTarget.find(prefix + L"\\") == 0 || normTarget.find(prefix + L"/") == 0) return true;
        }
        return false;
    };

    int originalQueueSize = static_cast<int>(m_queue.size());
    m_queue.erase(std::remove_if(m_queue.begin(), m_queue.end(), isPrefixMatched), m_queue.end());
    int removedFromQueue = originalQueueSize - static_cast<int>(m_queue.size());

    {
        std::lock_guard<std::mutex> retryLock(m_retryMutex);
        m_visualRetryQueue.erase(std::remove_if(m_visualRetryQueue.begin(), m_visualRetryQueue.end(), isPrefixMatched), m_visualRetryQueue.end());
    }

    int remaining = static_cast<int>(m_queue.size()) + m_activeCount.load();
    if (remaining < 0) remaining = 0;
    SyncStatusService::instance().updateMediaPending(remaining);

    qDebug() << "[DB_TRACE] MediaExtractorPipeline::cancelBatch 批量取消过滤。从主队列丢弃:" << removedFromQueue;
}

void MediaExtractorPipeline::enqueue(const std::wstring& path) {
    m_isCanceled.store(false); // 投递新任务时自动重置取消状态
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_queue.push_back(path);
    qDebug() << "[DB_TRACE] MediaExtractorPipeline::enqueue 推入提取队列，路径:" << QString::fromStdWString(path) << "总队列大小:" << m_queue.size();
    
    // 🚨 联动通知：特征待提取总项数（排队 + 正在解析数）
    SyncStatusService::instance().updateMediaPending(static_cast<int>(m_queue.size()) + m_activeCount.load());

    QMetaObject::invokeMethod(m_timer, "start", Qt::QueuedConnection);
}

void MediaExtractorPipeline::enqueueBatch(const std::vector<std::wstring>& paths) {
    m_isCanceled.store(false); // 投递新任务时自动重置取消状态
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_queue.insert(m_queue.end(), paths.begin(), paths.end());
    qDebug() << "[DB_TRACE] MediaExtractorPipeline::enqueueBatch 批量推入提取队列，新增数量:" << paths.size() << "总队列大小:" << m_queue.size();
    
    // 🚨 联动通知：特征待提取总项数（排队 + 正在解析数）
    SyncStatusService::instance().updateMediaPending(static_cast<int>(m_queue.size()) + m_activeCount.load());

    QMetaObject::invokeMethod(m_timer, "start", Qt::QueuedConnection);
}

void MediaExtractorPipeline::processNextBatch() {
    std::vector<std::wstring> batch;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (m_queue.empty() || m_isCanceled.load()) {
            m_timer->stop();
            return;
        }
        batch = std::move(m_queue);
        m_queue.clear();
    }
    qDebug() << "[DB_TRACE] MediaExtractorPipeline::processNextBatch 开始处理提取任务批次，任务数量:" << batch.size();

    // 增加正在处理的计数，并上报最新的待提取总项数
    m_activeCount.fetch_add(static_cast<int>(batch.size()));
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        SyncStatusService::instance().updateMediaPending(static_cast<int>(m_queue.size()) + m_activeCount.load());
    }

    (void)QtConcurrent::run([this, batch]() {
#ifdef Q_OS_WIN
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
#endif
        for (const auto& path : batch) {
            if (m_isCanceled.load()) {
                qDebug() << "[DB_TRACE] MediaExtractorPipeline 检测到全局已取消，平滑丢弃子任务:" << QString::fromStdWString(path);
                int active = m_activeCount.fetch_sub(1) - 1;
                if (active < 0) {
                    m_activeCount.store(0);
                    active = 0;
                }
                SyncStatusService::instance().updateMediaPending(active);
                continue;
            }
            processItemDirect(path);
        }
#ifdef Q_OS_WIN
        CoUninitialize();
#endif
        DatabaseManager::instance().enqueueSyncTask([]() {
            DatabaseManager::instance().flushAll();
        });
    });
}

void MediaExtractorPipeline::processItemDirect(const std::wstring& path) {
    if (m_isCanceled.load()) {
        int active = m_activeCount.fetch_sub(1) - 1;
        if (active < 0) {
            m_activeCount.store(0);
            active = 0;
        }
        std::lock_guard<std::mutex> lock(m_queueMutex);
        SyncStatusService::instance().updateMediaPending(static_cast<int>(m_queue.size()) + active);
        return;
    }

    int w = 0, h = 0;
    extractDimensions(path, w, h);
    if (m_isCanceled.load()) {
        int active = m_activeCount.fetch_sub(1) - 1;
        if (active < 0) { m_activeCount.store(0); active = 0; }
        std::lock_guard<std::mutex> lock(m_queueMutex);
        SyncStatusService::instance().updateMediaPending(static_cast<int>(m_queue.size()) + active);
        return;
    }

    if (w > 0 && h > 0) {
        MetadataManager::instance().setItemDimensions(path, w, h);
    }

    std::wstring colorStr;
    QVector<QPair<QColor, float>> palette;
    bool success = false;
    
    if (!m_isCanceled.load()) {
        success = extractColor(path, colorStr, palette);
    }

    if (m_isCanceled.load()) {
        int active = m_activeCount.fetch_sub(1) - 1;
        if (active < 0) { m_activeCount.store(0); active = 0; }
        std::lock_guard<std::mutex> lock(m_queueMutex);
        SyncStatusService::instance().updateMediaPending(static_cast<int>(m_queue.size()) + active);
        return;
    }

    if (success) {
        MetadataManager::instance().setItemVisualMetadata(path, colorStr, palette, false);
    }

    MetadataManager::instance().updateIngestionStatus(path, 1);
    MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::PathUpdate, QString::fromStdWString(path));

    if (!success && !m_isCanceled.load()) {
        QFileInfo info(QString::fromStdWString(path));
        if (info.isDir() || MediaColorExtractor::isGraphicsFile(info.suffix().toLower())) {
            std::lock_guard<std::mutex> lock(m_retryMutex);
            if (std::find(m_visualRetryQueue.begin(), m_visualRetryQueue.end(), path) == m_visualRetryQueue.end()) {
                m_visualRetryQueue.push_back(path);
                QMetaObject::invokeMethod(m_retryTimer, "start", Qt::QueuedConnection);
            }
        }
    }

    // 递减正在处理的计数并实时通知上报，供主界面进度条平滑由左向右推进
    int active = m_activeCount.fetch_sub(1) - 1;
    if (active < 0) {
        m_activeCount.store(0);
        active = 0;
    }
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        SyncStatusService::instance().updateMediaPending(static_cast<int>(m_queue.size()) + active);
    }
}

void MediaExtractorPipeline::extractDimensions(const std::wstring& path, int& outW, int& outH) {
    QFileInfo info(QString::fromStdWString(path));
    if (!info.isFile()) return;

    if (info.suffix().toLower() == "svg") {
        QSvgRenderer renderer(info.absoluteFilePath());
        if (renderer.isValid()) {
            QSize sz = renderer.defaultSize();
            outW = sz.width();
            outH = sz.height();
        }
    } else {
        QImageReader reader(info.absoluteFilePath());
        QSize sz = reader.size();
        if (sz.isValid()) {
            outW = sz.width();
            outH = sz.height();
        }
    }
}

bool MediaExtractorPipeline::extractColor(const std::wstring& path, std::wstring& outColorStr, QVector<QPair<QColor, float>>& outPalette) {
    QFileInfo info(QString::fromStdWString(path));
    QString qPath = QString::fromStdWString(path);
    bool success = false;

    if (info.isFile()) {
        if (MediaColorExtractor::isGraphicsFile(info.suffix().toLower())) {
            QImage img = MediaColorExtractor::getImageForAnalysis(qPath, 256);
            if (!img.isNull()) {
                auto palette = MediaColorExtractor::extractPalette(qPath);
                if (!palette.isEmpty()) {
                    QColor dominant = MediaColorExtractor::quantizeColor(palette.first().first);
                    outColorStr = dominant.name().toUpper().toStdWString();
                    outPalette = palette;
                    success = true;
                }
            }
        }
    } else if (info.isDir()) {
        QDir subDir(qPath);
        QFileInfoList subFiles = subDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
        
        struct Sample { QColor dominant; QVector<QPair<QColor, float>> palette; };
        QVector<Sample> samples;

        for (const auto& sf : subFiles) {
            if (MediaColorExtractor::isGraphicsFile(sf.suffix().toLower())) {
                auto palette = MediaColorExtractor::extractPalette(sf.absoluteFilePath());
                if (!palette.isEmpty()) {
                    samples.append({palette.first().first, palette});
                }
                if (samples.size() >= 10) break;
            }
        }

        if (!samples.isEmpty()) {
            int bestIdx = 0;
            int maxVotes = 0;
            for (int i = 0; i < samples.size(); ++i) {
                int votes = 0;
                for (int j = 0; j < samples.size(); ++j) {
                    if (MediaColorExtractor::calculateDeltaE(samples[i].dominant, samples[j].dominant) < 20.0) {
                        votes++;
                    }
                }
                if (votes > maxVotes) {
                    maxVotes = votes;
                    bestIdx = i;
                }
            }

            if (samples.size() == 1 || (maxVotes >= 2 && maxVotes >= samples.size() * 0.3)) {
                QColor dominant = MediaColorExtractor::quantizeColor(samples[bestIdx].dominant);
                outColorStr = dominant.name().toUpper().toStdWString();
                outPalette = samples[bestIdx].palette;
                success = true;
            }
        }
    }
    return success;
}

void MediaExtractorPipeline::processRetryQueue() {
    std::vector<std::wstring> batch;
    {
        std::lock_guard<std::mutex> lock(m_retryMutex);
        if (m_visualRetryQueue.empty() || m_isCanceled.load()) {
            m_retryTimer->stop();
            return;
        }
        size_t count = std::min(m_visualRetryQueue.size(), (size_t)5);
        for (size_t i = 0; i < count; ++i) {
            batch.push_back(m_visualRetryQueue[i]);
        }
    }

    (void)QtConcurrent::run([this, batch]() {
#ifdef Q_OS_WIN
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
#endif
        std::vector<std::wstring> finished;
        for (const auto& path : batch) {
            if (m_isCanceled.load()) break;

            std::wstring colorStr;
            QVector<QPair<QColor, float>> palette;
            bool ok = extractColor(path, colorStr, palette);
            if (ok && !m_isCanceled.load()) {
                MetadataManager::instance().setItemVisualMetadata(path, colorStr, palette, true);
            }

            QFileInfo info(QString::fromStdWString(path));
            bool isGraphics = MediaColorExtractor::isGraphicsFile(info.suffix().toLower());
            if (ok || (!isGraphics && !info.isDir())) {
                finished.push_back(path);
            }
        }
#ifdef Q_OS_WIN
        CoUninitialize();
#endif

        if (!finished.empty()) {
            QMetaObject::invokeMethod(this, [this, finished]() {
                std::lock_guard<std::mutex> lock(m_retryMutex);
                for (const auto& p : finished) {
                    auto it = std::find(m_visualRetryQueue.begin(), m_visualRetryQueue.end(), p);
                    if (it != m_visualRetryQueue.end()) {
                        m_visualRetryQueue.erase(it);
                    }
                }
            }, Qt::QueuedConnection);
        }
    });
}

} // namespace ArcMeta
