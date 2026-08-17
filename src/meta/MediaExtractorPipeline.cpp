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
    enqueueBatch({path});
}

void MediaExtractorPipeline::enqueueBatch(const std::vector<std::wstring>& paths) {
    m_isCanceled.store(false); // 投递新任务时自动重置取消状态
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_queue.insert(m_queue.end(), paths.begin(), paths.end());
        SyncStatusService::instance().updateMediaPending(static_cast<int>(m_queue.size()) + m_activeCount.load());
    }

    dispatchWorkersIfNeeded();
    QMetaObject::invokeMethod(m_timer, "start", Qt::QueuedConnection);
}

void MediaExtractorPipeline::prioritizeBatch(const std::vector<std::wstring>& paths) {
    if (paths.empty()) return;
    m_isCanceled.store(false);

    std::vector<std::wstring> normPaths;
    normPaths.reserve(paths.size());
    for (const auto& p : paths) {
        normPaths.push_back(MetadataManager::normalizePath(p));
    }

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        // 1. 先从现有队列中剔除这些路径
        std::unordered_set<std::wstring> targetSet(normPaths.begin(), normPaths.end());
        m_queue.erase(std::remove_if(m_queue.begin(), m_queue.end(), [&](const std::wstring& item) {
            return targetSet.count(MetadataManager::normalizePath(item)) > 0;
        }), m_queue.end());

        // 2. 将这批路径插入到队列最前面 (LIFO 插队优先)
        m_queue.insert(m_queue.begin(), paths.begin(), paths.end());
        SyncStatusService::instance().updateMediaPending(static_cast<int>(m_queue.size()) + m_activeCount.load());
    }

    dispatchWorkersIfNeeded();
    QMetaObject::invokeMethod(m_timer, "start", Qt::QueuedConnection);
}

void MediaExtractorPipeline::dispatchWorkersIfNeeded() {
    size_t qSize = 0;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        qSize = m_queue.size();
    }
    if (qSize == 0) return;

    int maxWorkers = std::max(2, QThread::idealThreadCount());
    int targetWorkers = std::min(maxWorkers, static_cast<int>((qSize + 31) / 32));

    while (m_activeWorkers.load() < targetWorkers) {
        int current = m_activeWorkers.load();
        if (m_activeWorkers.compare_exchange_strong(current, current + 1)) {
            (void)QtConcurrent::run([this]() {
                dispatchWorkerLoop();
            });
        }
    }
}

void MediaExtractorPipeline::processNextBatch() {
    // 1500ms 定时器作为心跳兜底调度，防止在边缘并发场景下工作线程挂起导致队列未消费完
    dispatchWorkersIfNeeded();
}

void MediaExtractorPipeline::dispatchWorkerLoop() {
#ifdef Q_OS_WIN
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
#endif

    while (!m_isCanceled.load()) {
        std::vector<std::wstring> batch;
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if (m_queue.empty()) break;
            size_t batchSize = std::min(m_queue.size(), static_cast<size_t>(32));
            batch.assign(m_queue.begin(), m_queue.begin() + batchSize);
            m_queue.erase(m_queue.begin(), m_queue.begin() + batchSize);

            m_activeCount.fetch_add(static_cast<int>(batch.size()));
            int remaining = static_cast<int>(m_queue.size()) + m_activeCount.load();
            SyncStatusService::instance().updateMediaPending(remaining);
        }

        std::vector<MetadataManager::ExtractedFeatureItem> results;
        results.reserve(batch.size());

        for (const auto& path : batch) {
            if (m_isCanceled.load()) break;

            MetadataManager::ExtractedFeatureItem item;
            item.path = path;
            extractDimensions(path, item.width, item.height);

            QString qPath = QString::fromStdWString(path);
            QFileInfo info(qPath);
            item.mtime = info.lastModified().toMSecsSinceEpoch();
            item.fileSize = info.size();

            if (info.isFile() && MediaColorExtractor::isGraphicsFile(info.suffix().toLower())) {
                QImage thumb = ImageDecoderFacade::loadScaledImage(qPath, 512);
                if (!thumb.isNull()) {
                    auto pal = ColorAlgorithmEngine::extractPaletteFromImage(thumb);
                    if (!pal.isEmpty()) {
                        QColor dominant = MediaColorExtractor::quantizeColor(pal.first().first);
                        item.autoColor = dominant.name().toUpper().toStdWString();
                        item.palettes = pal;
                    }

                    // 🚀【补齐落盘闭环】：将已解码的 512px 缩略图存入缓存，供 UI 直接秒读
                    QString containerDir = info.absolutePath();
                    if (containerDir.endsWith(".arc", Qt::CaseInsensitive)) {
                        QString thumbPath = containerDir + "/" + info.completeBaseName() + "_thumbnail.png";
                        thumb.save(thumbPath, "PNG");
                    } else {
                        QString diskThumbPath = CapsuleMediaExtractor::getDiskThumbCachePath(qPath);
                        thumb.save(diskThumbPath, "PNG");
                    }
                }
            } else if (info.isDir()) {
                std::wstring colorStr;
                QVector<QPair<QColor, float>> palette;
                extractColor(path, colorStr, palette);
                item.autoColor = colorStr;
                item.palettes = palette;
            }
            item.ingestionStatus = 1;
            results.push_back(item);
        }

        if (!results.empty() && !m_isCanceled.load()) {
            MetadataManager::instance().updateExtractedMediaFeaturesBatch(results);
        }

        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            m_activeCount.fetch_sub(static_cast<int>(batch.size()));
            int remaining = static_cast<int>(m_queue.size()) + m_activeCount.load();
            if (remaining < 0) remaining = 0;
            SyncStatusService::instance().updateMediaPending(remaining);
        }
    }

    m_activeWorkers.fetch_sub(1);

#ifdef Q_OS_WIN
    CoUninitialize();
#endif
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

    std::wstring colorStr;
    QVector<QPair<QColor, float>> palette;
    
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
                }
            }
        } else if (info.isDir()) {
            extractColor(path, colorStr, palette);
        }
    }

    if (m_isCanceled.load()) {
        int active = m_activeCount.fetch_sub(1) - 1;
        if (active < 0) { m_activeCount.store(0); active = 0; }
        std::lock_guard<std::mutex> lock(m_queueMutex);
        SyncStatusService::instance().updateMediaPending(static_cast<int>(m_queue.size()) + active);
        return;
    }

    MetadataManager::instance().updateExtractedMediaFeatures(path, w, h, colorStr, palette, 1);

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
        // 🚀【改造点 3】：轻量级 XML 正则/文本快速解析 SVG 尺寸，避免实例化 QSvgRenderer 争抢全局 s_qtGuiMutex
        QFile file(info.absoluteFilePath());
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QByteArray header = file.read(4096); // 仅读取前 4KB xml 头部，开销极微
            file.close();

            QString xmlHead = QString::fromUtf8(header);
            static QRegularExpression regW("width\\s*=\\s*\"([0-9.]+)(?:px)?\"", QRegularExpression::CaseInsensitiveOption);
            static QRegularExpression regH("height\\s*=\\s*\"([0-9.]+)(?:px)?\"", QRegularExpression::CaseInsensitiveOption);
            static QRegularExpression regVB("viewBox\\s*=\\s*\"\\s*[-0-9.]+\\s+[-0-9.]+\\s+([0-9.]+)\\s+([0-9.]+)\\s*\"", QRegularExpression::CaseInsensitiveOption);

            auto matchW = regW.match(xmlHead);
            auto matchH = regH.match(xmlHead);
            if (matchW.hasMatch() && matchH.hasMatch()) {
                outW = qRound(matchW.captured(1).toDouble());
                outH = qRound(matchH.captured(1).toDouble());
            }

            if (outW <= 0 || outH <= 0) {
                auto matchVB = regVB.match(xmlHead);
                if (matchVB.hasMatch()) {
                    outW = qRound(matchVB.captured(1).toDouble());
                    outH = qRound(matchVB.captured(2).toDouble());
                }
            }
        }

        // 后备逻辑：当快速正则未能解析出宽高时，才尝试加锁使用 QSvgRenderer 兜底
        if (outW <= 0 || outH <= 0) {
            std::lock_guard<std::mutex> guiLock(CapsuleMediaExtractor::s_qtGuiMutex);
            QSvgRenderer renderer(info.absoluteFilePath());
            if (renderer.isValid()) {
                QSize sz = renderer.defaultSize();
                if (sz.isEmpty() || sz.width() <= 0 || sz.height() <= 0) {
                    QRectF vb = renderer.viewBoxF();
                    sz = vb.size().toSize();
                }
                outW = sz.width();
                outH = sz.height();
            }
        }

        if (outW <= 0 || outH <= 0) {
            outW = 512;
            outH = 512;
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
