#pragma once

#include <QObject>
#include <QTimer>
#include <QColor>
#include <QPair>
#include <QVector>
#include <vector>
#include <string>
#include <mutex>

namespace ArcMeta {

class MediaExtractorPipeline : public QObject {
    Q_OBJECT
public:
    static MediaExtractorPipeline& instance();

    void enqueue(const std::wstring& path);
    void enqueueBatch(const std::vector<std::wstring>& paths);

private slots:
    void processNextBatch();
    void processRetryQueue();

private:
    MediaExtractorPipeline(QObject* parent = nullptr);
    ~MediaExtractorPipeline() override;

    void processItemDirect(const std::wstring& path);
    void extractDimensions(const std::wstring& path, int& outW, int& outH);
    bool extractColor(const std::wstring& path, std::wstring& outColorStr, QVector<QPair<QColor, float>>& outPalette);

    std::vector<std::wstring> m_queue;
    std::vector<std::wstring> m_visualRetryQueue;
    QTimer* m_timer;
    QTimer* m_retryTimer;
    std::mutex m_queueMutex;
    std::mutex m_retryMutex;
};

} // namespace ArcMeta
