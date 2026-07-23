#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "MediaExtractorPipeline.h"
#include "MetadataManager.h"
#include "../ui/MediaColorExtractor.h"
#include "DatabaseManager.h"
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
}

MediaExtractorPipeline::~MediaExtractorPipeline() {
    m_timer->stop();
    m_retryTimer->stop();
}

void MediaExtractorPipeline::enqueue(const std::wstring& path) {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_queue.push_back(path);
    QMetaObject::invokeMethod(m_timer, "start", Qt::QueuedConnection);
}

void MediaExtractorPipeline::enqueueBatch(const std::vector<std::wstring>& paths) {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_queue.insert(m_queue.end(), paths.begin(), paths.end());
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
    QFileInfo info(QString::fromStdWString(path));
    bool isDir = info.isDir();
    bool isGraphics = MediaColorExtractor::isGraphicsFile(info.suffix().toLower());
    bool isMedia = isDir || isGraphics;

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

    // 只有当提取成功时才允许标记 ingestionStatus = 1。如果是目录，只需颜色提取成功；如果是文件，需颜色和尺寸都提取成功。
    bool extractionSuccessful = !isMedia || (success && (isDir || (w > 0 && h > 0)));
    if (extractionSuccessful) {
        MetadataManager::instance().updateIngestionStatus(path, 1);
    }
    MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::PathUpdate, QString::fromStdWString(path));

    if (!extractionSuccessful) {
        if (isMedia) {
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

    (void)QtConcurrent::run([this, batch]() {
#ifdef Q_OS_WIN
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
#endif
        std::vector<std::wstring> finished;
        for (const auto& path : batch) {
            std::wstring colorStr;
            QVector<QPair<QColor, float>> palette;
            bool ok = extractColor(path, colorStr, palette);

            QFileInfo info(QString::fromStdWString(path));
            bool isGraphics = MediaColorExtractor::isGraphicsFile(info.suffix().toLower());
            bool isDir = info.isDir();
            bool isMedia = isGraphics || isDir;

            bool extractionSuccessful = false;
            if (ok) {
                int w = 0, h = 0;
                if (!isDir) {
                    extractDimensions(path, w, h);
                    if (w > 0 && h > 0) {
                        MetadataManager::instance().setItemDimensions(path, w, h);
                    }
                }
                MetadataManager::instance().setItemVisualMetadata(path, colorStr, palette, true);

                // 只有当是目录，或者文件且成功提取了尺寸时，才算真正重试成功
                if (isDir || (w > 0 && h > 0)) {
                    MetadataManager::instance().updateIngestionStatus(path, 1);
                    extractionSuccessful = true;
                }
            }

            if (extractionSuccessful || !isMedia) {
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
