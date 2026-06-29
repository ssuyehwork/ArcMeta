#pragma once
#include <QObject>
#include <QTimer>
#include <vector>
#include <string>
#include <mutex>

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

    // 2026-11-15 按照 Plan-115：占位接口，用于 MainWindow 交互
    void startTask(const QString& drive) { Q_UNUSED(drive); }
    void pauseTask(const QString& drive) { Q_UNUSED(drive); }

signals:
    void taskFinished(const QString& drive);

private slots:
    // 订阅 MftReader 发现的新增条目
    void onEntryAdded(uint64_t key);
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
};

} // namespace ArcMeta
