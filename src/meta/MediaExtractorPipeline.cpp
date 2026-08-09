#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "MediaExtractorPipeline.h"
#include "MetadataManager.h"
#include "CapsuleMediaExtractor.h"
#include "../ui/MediaColorExtractor.h"
#include "../ui/ImageDecoderFacade.h"
#include "../ui/ColorAlgorithmEngine.h"
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

    if (QCoreApplication::instance()) {
        this->moveToThread(QCoreApplication::instance()->thread());
    }
}

MediaExtractorPipeline::~MediaExtractorPipeline() {
    m_timer->stop();
}

void MediaExtractorPipeline::cancelAll() {
    m_isCanceled.store(true);
    {
        std::lock_guard<std::mutex> queueLock(m_queueMutex);
        m_queue.clear();
    }
    m_activeCount.store(0);
    SyncStatusService::instance().updateMediaPending(0);
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
    Q_UNUSED(removedFromQueue);

    int remaining = static_cast<int>(m_queue.size()) + m_activeCount.load();
    if (remaining < 0) remaining = 0;
    SyncStatusService::instance().updateMediaPending(remaining);
}

void MediaExtractorPipeline::enqueue(const std::wstring& path) {
    m_isCanceled.store(false); // 投递新任务时自动重置取消状态
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_queue.push_back(path);
    
    // 🚨 联动通知：特征待提取总项数（排队 + 正在解析数）
    SyncStatusService::instance().updateMediaPending(static_cast<int>(m_queue.size()) + m_activeCount.load());

    QMetaObject::invokeMethod(m_timer, "start", Qt::QueuedConnection);
}

void MediaExtractorPipeline::enqueueBatch(const std::vector<std::wstring>& paths) {
    m_isCanceled.store(false); // 投递新任务时自动重置取消状态
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_queue.insert(m_queue.end(), paths.begin(), paths.end());
    
    // 🚨 联动通知：特征待提取总项数（排队 + 正在解析数）
    SyncStatusService::instance().updateMediaPending(static_cast<int>(m_queue.size()) + m_activeCount.load());

    QMetaObject::invokeMethod(m_timer, "start", Qt::QueuedConnection);
}

void MediaExtractorPipeline::processNextBatch() {
    std::vector<std::wstring> chunk;
    const size_t CHUNK_SIZE = 16; // 强行规定每批次最多处理 16 个文件
     
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (m_queue.empty() || m_isCanceled.load()) {
            m_timer->stop();
            return;
        }
         
        size_t count = std::min(m_queue.size(), CHUNK_SIZE);
        chunk.assign(m_queue.begin(), m_queue.begin() + count);
        m_queue.erase(m_queue.begin(), m_queue.begin() + count);
    }

    m_activeCount.fetch_add(static_cast<int>(chunk.size()));
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        SyncStatusService::instance().updateMediaPending(static_cast<int>(m_queue.size()) + m_activeCount.load());
    }
     
    // 异步分片执行
    (void)QtConcurrent::run([this, chunk]() {
#ifdef Q_OS_WIN
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
#endif
        for (const auto& path : chunk) {
            if (m_isCanceled.load()) {
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

    QString qPath = QString::fromStdWString(path);
    QFileInfo info(qPath);

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
        if (info.isFile() && MediaColorExtractor::isGraphicsFile(info.suffix().toLower())) {
            // 步骤一：调用 ImageDecoderFacade::loadScaledImage(qPath, 512) 获取 QImage thumb。
            QImage thumb = ImageDecoderFacade::loadScaledImage(qPath, 512);
            if (!thumb.isNull()) {
                // 步骤三：将 thumb 直接（或通过 thumb.scaled(200, 200)）传给 ColorAlgorithmEngine::extractPaletteFromImage(thumb)。
                // 彻底禁止再次发起磁盘读取。
                auto pal = ColorAlgorithmEngine::extractPaletteFromImage(thumb);
                if (!pal.isEmpty()) {
                    QColor dominant = MediaColorExtractor::quantizeColor(pal.first().first);
                    colorStr = dominant.name().toUpper().toStdWString();
                    palette = pal;
                    success = true;
                }
            }
        } else if (info.isDir()) {
            success = extractColor(path, colorStr, palette);
        }
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
        std::lock_guard<std::mutex> guiLock(CapsuleMediaExtractor::s_qtGuiMutex);
        QSvgRenderer renderer(info.absoluteFilePath());
        if (renderer.isValid()) {
            QSize sz = renderer.defaultSize();
            outW = sz.width();
            outH = sz.height();
        }
    } else {
        QSize sz = ImageDecoderFacade::readImageDimensions(info.absoluteFilePath());
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
            QImage img = ImageDecoderFacade::loadScaledImage(qPath, 512);
            if (!img.isNull()) {
                auto palette = ColorAlgorithmEngine::extractPaletteFromImage(img);
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
                QImage img = ImageDecoderFacade::loadScaledImage(sf.absoluteFilePath(), 512);
                if (!img.isNull()) {
                    auto palette = ColorAlgorithmEngine::extractPaletteFromImage(img);
                    if (!palette.isEmpty()) {
                        samples.append({palette.first().first, palette});
                    }
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
                    if (ColorAlgorithmEngine::calculateDeltaE(samples[i].dominant, samples[j].dominant) < 20.0) {
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

} // namespace ArcMeta
