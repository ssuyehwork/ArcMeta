#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "MediaExtractorPipeline.h"
#include "MetadataManager.h"
#include "../ui/MediaColorExtractor.h"
#include "DatabaseManager.h"
#include "../core/IoTaskScheduler.h"
#include <QImageReader>
#include <QSvgRenderer>
#include <QFileInfo>
#include <QDir>
#include <QtConcurrent/QtConcurrent>
#include <QDebug>
#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#include <objbase.h>
#endif

namespace ArcMeta {

class MediaExtractTask : public IoTask {
public:
    MediaExtractTask(const std::wstring& path, TaskPriority priority = TaskPriority::BackgroundBatch)
        : m_path(path) {
        setPriority(priority);
    }

    std::wstring category() const override { return L"media_extract"; }
    std::wstring id() const override { return m_path; }

    void run() override {
        if (isCancelled()) return;

#ifdef Q_OS_WIN
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
#endif

        MediaExtractorPipeline::instance().processItemDirect(m_path);

#ifdef Q_OS_WIN
        CoUninitialize();
#endif

        DatabaseManager::instance().enqueueSyncTask([]() {
            DatabaseManager::instance().flushAll();
        });
    }

private:
    std::wstring m_path;
};

MediaExtractorPipeline& MediaExtractorPipeline::instance() {
    static MediaExtractorPipeline inst;
    return inst;
}

MediaExtractorPipeline::MediaExtractorPipeline(QObject* parent) : QObject(parent) {
    m_timer = nullptr;

    m_retryTimer = new QTimer(this);
    m_retryTimer->setInterval(3000);
    connect(m_retryTimer, &QTimer::timeout, this, &MediaExtractorPipeline::processRetryQueue);
}

MediaExtractorPipeline::~MediaExtractorPipeline() {
    m_retryTimer->stop();
}

void MediaExtractorPipeline::enqueue(const std::wstring& path) {
    auto task = std::make_shared<MediaExtractTask>(path, TaskPriority::BackgroundBatch);
    IoTaskScheduler::instance().submit(task);
}

void MediaExtractorPipeline::enqueueBatch(const std::vector<std::wstring>& paths) {
    for (const auto& path : paths) {
        auto task = std::make_shared<MediaExtractTask>(path, TaskPriority::BackgroundBatch);
        IoTaskScheduler::instance().submit(task);
    }
}

void MediaExtractorPipeline::processNextBatch() {
    // 统一交由调度器处理，本函数已退化，保持空实现
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

    // 将重试特征提取任务作为普通后台优先级提交给统一任务调度器
    for (const auto& path : batch) {
        auto task = std::make_shared<MediaExtractTask>(path, TaskPriority::BackgroundBatch);
        IoTaskScheduler::instance().submit(task);
    }

    // 假设在任务运行完成后，由其自身的完成/重试逻辑进行自我除名。
    // 为了与原有逻辑对账兼容：在 processRetryQueue 触发后，将当前已提交的重试项从队列里清除。
    {
        std::lock_guard<std::mutex> lock(m_retryMutex);
        for (const auto& p : batch) {
            auto it = std::find(m_visualRetryQueue.begin(), m_visualRetryQueue.end(), p);
            if (it != m_visualRetryQueue.end()) {
                m_visualRetryQueue.erase(it);
            }
        }
        if (m_visualRetryQueue.empty()) {
            m_retryTimer->stop();
        }
    }
}

} // namespace ArcMeta
