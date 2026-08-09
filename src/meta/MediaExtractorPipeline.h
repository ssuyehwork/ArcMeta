#pragma once

#include <QObject>
#include <QTimer>
#include <QColor>
#include <QPair>
#include <QVector>
#include <vector>
#include <string>
#include <mutex>
#include <atomic>
#include "CapsuleMediaExtractor.h"  // 复用其中声明的 s_qtGuiMutex

namespace ArcMeta {

class MediaExtractorPipeline : public QObject {
    Q_OBJECT
public:
    static MediaExtractorPipeline& instance();

    void enqueue(const std::wstring& path);
    void enqueueBatch(const std::vector<std::wstring>& paths);

    // 2026-07-27 按照 Plan-107：安全、平滑取消与中止接口
    void cancelAll();
    void cancelBatch(const std::vector<std::wstring>& paths);

private slots:
    void processNextBatch();

private:
    MediaExtractorPipeline(QObject* parent = nullptr);
    ~MediaExtractorPipeline() override;

    void processItemDirect(const std::wstring& path);
    void extractDimensions(const std::wstring& path, int& outW, int& outH);
    bool extractColor(const std::wstring& path, std::wstring& outColorStr, QVector<QPair<QColor, float>>& outPalette);

    std::vector<std::wstring> m_queue;
    QTimer* m_timer;
    std::mutex m_queueMutex;
    std::atomic<int> m_activeCount{0}; // 正在处理解析中 of 任务数量
    std::atomic<bool> m_isCanceled{false}; // 2026-07-27 按照 Plan-107：原子取消中止标记
};

} // namespace ArcMeta
