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

void MediaExtractorPipeline::enqueue(const std::wstring& path) {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_queue.push_back(path);
    qDebug() << "[DB_TRACE] MediaExtractorPipeline::enqueue 推入提取队列，路径:" << QString::fromStdWString(path) << "总队列大小:" << m_queue.size();

    // 🚨 联动通知：特征待提取总项数（排队 + 正在解析数）
    SyncStatusService::instance().updateMediaPending(static_cast<int>(m_queue.size()) + m_activeCount.load());

    QMetaObject::invokeMethod(m_timer, "start", Qt::QueuedConnection);
}

void MediaExtractorPipeline::enqueueBatch(const std::vector<std::wstring>& paths) {
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
        if (m_queue.empty()) {
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
    int w = 0, h = 0;
    extractDimensions(path, w, h);
    if (w > 0 && h > 0) {
        MetadataManager::instance().setItemDimensions(path, w, h);
    }

    std::wstring colorStr;
    QVector<QPair<QColor, float>> palette;
    bool success = extractColor(path, colorStr, palette);
    if (success) {
        MetadataManager::instance().setItemVisualMetadata(path, colorStr, palette, false);
    }

    MetadataManager::instance().updateIngestionStatus(path, 1);
    MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::PathUpdate, QString::fromStdWString(path));

    if (!success) {
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
        if (m_visualRetryQueue.empty()) {
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
            std::wstring colorStr;
            QVector<QPair<QColor, float>> palette;
            bool ok = extractColor(path, colorStr, palette);
            if (ok) {
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
