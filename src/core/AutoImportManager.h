#pragma once
#include <QObject>
#include <QTimer>
#include <vector>
#include <string>
#include <mutex>
#include <atomic>

namespace ArcMeta {

/**
 * @brief 2026-07-xx 按照 Plan-67/68：NTFS 托管文件夹自动入库管理器
 */
class AutoImportManager : public QObject {
    Q_OBJECT
public:
    static AutoImportManager& instance();

    // 启动/停止监听
    void startListening();
    void stopListening();

    // 2026-06-26 按照 Plan-108：任务执行流
    void startTask(const QString& drive);
    void pauseTask(const QString& drive);

signals:
    void taskFinished(const QString& drive);

private slots:
    // 订阅 MftReader 发现的新增条目
    void onEntryAdded(uint64_t key);
    void onEntryRemoved(uint64_t key);
    // 去抖超时，合并写入数据库
    void processImportQueue();

private:
    AutoImportManager(QObject* parent = nullptr);
    ~AutoImportManager() override;

    bool checkAndGetManagedPath(const std::wstring& path, std::wstring& outManagedFolder);
    std::wstring getManagedFolderAbsolutePath(const std::wstring& volSerial);

    QTimer* m_debounceTimer = nullptr;
    std::vector<std::wstring> m_pendingPaths;
    std::mutex m_queueMutex;
    bool m_isListening = false;
    std::atomic<bool> m_globalPaused{false};
};

} // namespace ArcMeta
