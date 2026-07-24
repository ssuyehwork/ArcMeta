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
    qDebug() << "[DB_TRACE] MediaExtractorPipeline::enqueue 将路径推入异步提取队列，当前队列大小:" << m_queue.size() << "路径:" << QString::fromStdWString(path);
    QMetaObject::invokeMethod(m_timer, "start", Qt::QueuedConnection);
}

void MediaExtractorPipeline::enqueueBatch(const std::vector<std::wstring>& paths) {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_queue.insert(m_queue.end(), paths.begin(), paths.end());
    qDebug() << "[DB_TRACE] MediaExtractorPipeline::enqueueBatch 批量推入提取队列，新增数量:" << paths.size() << "总队列大小:" << m_queue.size();
    QMetaObject::invokeMethod(m_timer, "start", Qt::QueuedConnection);
}

void MediaExtractorPipeline::processNextBatch() {
    std::vector<std::wstring> batch;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (m_queue.empty()) {
            m_timer->stop();
            qDebug() << "[DB_TRACE] MediaExtractorPipeline::processNextBatch 队列已空，停止提取定时器。";
            return;
        }
        batch = std::move(m_queue);
        m_queue.clear();
    }

    qDebug() << "[DB_TRACE] MediaExtractorPipeline::processNextBatch 开始执行新一轮后台多线程解析，本批次数量:" << batch.size();
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
        qDebug() << "[DB_TRACE] MediaExtractorPipeline::processNextBatch 本批次完成，投递落盘任务。";
        DatabaseManager::instance().enqueueSyncTask([]() {
            DatabaseManager::instance().flushAll();
        });
    });
}

void MediaExtractorPipeline::processItemDirect(const std::wstring& path) {
    qDebug() << "[DB_TRACE] processItemDirect 开始提取单一元数据。路径:" << QString::fromStdWString(path);
    int w = 0, h = 0;
    extractDimensions(path, w, h);
    if (w > 0 && h > 0) {
        qDebug() << "[DB_TRACE] processItemDirect 成功提取物理尺寸。宽:" << w << "高:" << h << "路径:" << QString::fromStdWString(path);
        MetadataManager::instance().setItemDimensions(path, w, h);
    } else {
        qDebug() << "[DB_TRACE] processItemDirect 未能提取物理尺寸（可能非图片或SVG）。路径:" << QString::fromStdWString(path);
    }

    std::wstring colorStr;
    QVector<QPair<QColor, float>> palette;
    bool success = extractColor(path, colorStr, palette);
    if (success) {
        qDebug() << "[DB_TRACE] processItemDirect 成功提取感知颜色。主色:" << QString::fromStdWString(colorStr) << "调色板尺寸:" << palette.size() << "路径:" << QString::fromStdWString(path);
        MetadataManager::instance().setItemVisualMetadata(path, colorStr, palette, false);
    } else {
        qWarning() << "[DB_TRACE] processItemDirect 颜色提取未成功！路径:" << QString::fromStdWString(path);
    }

    MetadataManager::instance().updateIngestionStatus(path, 1);
    MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::PathUpdate, QString::fromStdWString(path));

    if (!success) {
        QFileInfo info(QString::fromStdWString(path));
        if (info.isDir() || MediaColorExtractor::isGraphicsFile(info.suffix().toLower())) {
            std::lock_guard<std::mutex> lock(m_retryMutex);
            if (std::find(m_visualRetryQueue.begin(), m_visualRetryQueue.end(), path) == m_visualRetryQueue.end()) {
                m_visualRetryQueue.push_back(path);
                qDebug() << "[DB_TRACE] processItemDirect 颜色提取未成功且满足补偿条件，加入异步重试补偿队列。路径:" << QString::fromStdWString(path) << "重试队列当前大小:" << m_visualRetryQueue.size();
                QMetaObject::invokeMethod(m_retryTimer, "start", Qt::QueuedConnection);
            }
        } else {
            qDebug() << "[DB_TRACE] processItemDirect 颜色提取未成功但无需重试（非媒体文件/非图形后缀）。路径:" << QString::fromStdWString(path);
        }
    }
}

void MediaExtractorPipeline::extractDimensions(const std::wstring& path, int& outW, int& outH) {
    QFileInfo info(QString::fromStdWString(path));
    if (!info.isFile()) {
        qDebug() << "[DB_TRACE] extractDimensions 跳过：非物理文件。路径:" << QString::fromStdWString(path);
        return;
    }

    if (info.suffix().toLower() == "svg") {
        QSvgRenderer renderer(info.absoluteFilePath());
        if (renderer.isValid()) {
            QSize sz = renderer.defaultSize();
            outW = sz.width();
            outH = sz.height();
            qDebug() << "[DB_TRACE] extractDimensions 成功解析 SVG 尺寸。宽:" << outW << "高:" << outH << "路径:" << QString::fromStdWString(path);
        } else {
            qWarning() << "[DB_TRACE] extractDimensions 解析 SVG 失败！无效的 SVG 格式。路径:" << QString::fromStdWString(path);
        }
    } else {
        QImageReader reader(info.absoluteFilePath());
        QSize sz = reader.size();
        if (sz.isValid()) {
            outW = sz.width();
            outH = sz.height();
        } else {
            qDebug() << "[DB_TRACE] extractDimensions 读取普通图像尺寸失败（可能是非图、不支持的格式或损坏文件）。Error:" << reader.errorString() << "路径:" << QString::fromStdWString(path);
        }
    }
}

bool MediaExtractorPipeline::extractColor(const std::wstring& path, std::wstring& outColorStr, QVector<QPair<QColor, float>>& outPalette) {
    QFileInfo info(QString::fromStdWString(path));
    QString qPath = QString::fromStdWString(path);
    bool success = false;

    qDebug() << "[DB_TRACE] extractColor 开始提取颜色，类型:" << (info.isDir() ? "【文件夹】" : "【文件】") << "路径:" << qPath;

    if (info.isFile()) {
        QString ext = info.suffix().toLower();
        if (MediaColorExtractor::isGraphicsFile(ext)) {
            QImage img = MediaColorExtractor::getImageForAnalysis(qPath, 256);
            if (!img.isNull()) {
                auto palette = MediaColorExtractor::extractPalette(qPath);
                if (!palette.isEmpty()) {
                    QColor dominant = MediaColorExtractor::quantizeColor(palette.first().first);
                    outColorStr = dominant.name().toUpper().toStdWString();
                    outPalette = palette;
                    success = true;
                    qDebug() << "[DB_TRACE] extractColor 文件提取成功。感知代表色:" << QString::fromStdWString(outColorStr) << "调色板数量:" << palette.size() << "路径:" << qPath;
                } else {
                    qWarning() << "[DB_TRACE] extractColor 文件提取失败：获取调色板为空。路径:" << qPath;
                }
            } else {
                qWarning() << "[DB_TRACE] extractColor 文件提取失败：缩略解析图 img.isNull()。路径:" << qPath;
            }
        } else {
            qDebug() << "[DB_TRACE] extractColor 跳过：非支持的图形后缀(" << ext << ")。路径:" << qPath;
        }
    } else if (info.isDir()) {
        QDir subDir(qPath);
        QFileInfoList subFiles = subDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
        qDebug() << "[DB_TRACE] extractColor 开始扫描文件夹子文件。子文件总数:" << subFiles.size() << "路径:" << qPath;
        
        struct Sample { QColor dominant; QVector<QPair<QColor, float>> palette; };
        QVector<Sample> samples;

        for (const auto& sf : subFiles) {
            if (MediaColorExtractor::isGraphicsFile(sf.suffix().toLower())) {
                auto palette = MediaColorExtractor::extractPalette(sf.absoluteFilePath());
                if (!palette.isEmpty()) {
                    samples.append({palette.first().first, palette});
                    qDebug() << "[DB_TRACE] extractColor 发现子图片样本。路径:" << sf.absoluteFilePath() << "主色:" << palette.first().first.name();
                }
                if (samples.size() >= 10) {
                    qDebug() << "[DB_TRACE] extractColor 达到子图片样本上限(10)，停止扫描。路径:" << qPath;
                    break;
                }
            }
        }

        if (!samples.isEmpty()) {
            qDebug() << "[DB_TRACE] extractColor 收集到有效媒体样本，开始投票对齐。样本数:" << samples.size() << "路径:" << qPath;
            int bestIdx = 0;
            int maxVotes = 0;
            for (int i = 0; i < samples.size(); ++i) {
                int votes = 0;
                for (int j = 0; j < samples.size(); ++j) {
                    double deltaE = MediaColorExtractor::calculateDeltaE(samples[i].dominant, samples[j].dominant);
                    if (deltaE < 20.0) {
                        votes++;
                    }
                }
                if (votes > maxVotes) {
                    maxVotes = votes;
                    bestIdx = i;
                }
            }

            double voteRatio = (double)maxVotes / samples.size();
            qDebug() << "[DB_TRACE] extractColor 投票结算。最佳索引:" << bestIdx << "最高票数:" << maxVotes << "占比:" << voteRatio << "路径:" << qPath;

            if (samples.size() == 1 || (maxVotes >= 2 && voteRatio >= 0.3)) {
                QColor dominant = MediaColorExtractor::quantizeColor(samples[bestIdx].dominant);
                outColorStr = dominant.name().toUpper().toStdWString();
                outPalette = samples[bestIdx].palette;
                success = true;
                qDebug() << "[DB_TRACE] extractColor 文件夹颜色判定通过！感知代表色:" << QString::fromStdWString(outColorStr) << "路径:" << qPath;
            } else {
                qWarning() << "[DB_TRACE] extractColor 文件夹判定未通过：投票数不满足占比阈值(>= 0.3 or maxVotes >= 2)。路径:" << qPath;
            }
        } else {
            qWarning() << "[DB_TRACE] extractColor 文件夹提取未成功：未在文件夹中扫描到任何有效的媒体图片。路径:" << qPath;
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
            qDebug() << "[DB_TRACE] processRetryQueue 补偿重试队列已空，关闭补偿定时器。";
            return;
        }
        size_t count = std::min(m_visualRetryQueue.size(), (size_t)5);
        for (size_t i = 0; i < count; ++i) {
            batch.push_back(m_visualRetryQueue[i]);
        }
    }

    qDebug() << "[DB_TRACE] processRetryQueue 开始异步批量处理重试，本批次重试项数:" << batch.size();
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
                qDebug() << "[DB_TRACE] processRetryQueue 重试提取成功！路径:" << QString::fromStdWString(path) << "颜色:" << QString::fromStdWString(colorStr);
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
            qDebug() << "[DB_TRACE] processRetryQueue 收集到可移出队列的项数:" << finished.size();
            QMetaObject::invokeMethod(this, [this, finished]() {
                std::lock_guard<std::mutex> lock(m_retryMutex);
                for (const auto& p : finished) {
                    auto it = std::find(m_visualRetryQueue.begin(), m_visualRetryQueue.end(), p);
                    if (it != m_visualRetryQueue.end()) {
                        m_visualRetryQueue.erase(it);
                        qDebug() << "[DB_TRACE] processRetryQueue 成功将项移出补偿重试队列。路径:" << QString::fromStdWString(p);
                    }
                }
            }, Qt::QueuedConnection);
        }
    });
}

} // namespace ArcMeta
