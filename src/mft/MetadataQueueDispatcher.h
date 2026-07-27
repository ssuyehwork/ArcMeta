#ifndef ARCMETA_METADATA_QUEUE_DISPATCHER_H
#define ARCMETA_METADATA_QUEUE_DISPATCHER_H

#include <QObject>
#include <QString>
#include <vector>
#include <string>
#include <mutex>
#include <atomic>

namespace ArcMeta {

class MetadataQueueDispatcher : public QObject {
    Q_OBJECT
public:
    static MetadataQueueDispatcher& instance();

    struct MetadataTask {
        int index;
        uint64_t frn;
        std::wstring volume;
    };

    void requestMetadata(int index, uint64_t frn, const std::wstring& volume);
    void clear();

private:
    MetadataQueueDispatcher(QObject* parent = nullptr);
    ~MetadataQueueDispatcher() override = default;

    void processMetadataQueue();

    std::vector<MetadataTask> m_metadata_queue;
    std::mutex m_queueMutex;
    std::atomic<int> m_active_metadata_tasks{0};
};

} // namespace ArcMeta

#endif // ARCMETA_METADATA_QUEUE_DISPATCHER_H
