#pragma once
#include <QObject>
#include <QTimer>
#include <vector>
#include <string>
#include <map>
#include <mutex>
#include <atomic>

namespace ArcMeta {

/**
 * @brief NTFS 托管文件夹自动入库管理器
 *
 * 架构约定（禁止修改）：
 *  - 入库唯一触发：文件被 Move 到 ArcMeta.Library_[盘符] 后由 USN Journal 感知
 *  - 注册唯一入口：processPath()
 *  - 断电恢复入口：startTask()，处理 DB 中 ingestionStatus=0 的未完成项
 *  - 禁止在槽函数或 startTask 里直接调用 registerItem
 *  - 禁止使用 getPathByFrn / CreateFileW 进行路径反查
 */
class AutoImportManager : public QObject {
    Q_OBJECT
public:
    static AutoImportManager& instance();

    void startListening();
    void stopListening();

    // 断电恢复：处理指定盘符下未完成的入库任务
    void startTask(const QString& drive);
    void pauseTask(const QString& drive);

    // 路径工具
    static std::wstring getManagedLibraryPath(const std::wstring& pathInDrive);
    static bool isPathInManagedLibrary(const std::wstring& path);
    static void ensureManagedFolderExists(const std::wstring& driveRoot);

signals:
    void taskFinished(const QString& drive);

private slots:
    void onEntryAdded(uint64_t key);
    void onEntryUpdated(uint64_t key);
    void onEntriesBatchAdded(int driveIdx, const QList<uint64_t>& frns);
    void onEntriesBatchUpdated(int driveIdx, const QList<uint64_t>& frns);
    void onEntryRemoved(uint64_t key);
    void processImportQueue();

private:
    AutoImportManager(QObject* parent = nullptr);
    ~AutoImportManager() override;

    // 唯一的注册入口：检查路径 → registerItem → debounce队列
    void processPath(const std::wstring& path);

    std::wstring getManagedFolderAbsolutePath(const std::wstring& volSerial);
    bool checkAndGetManagedPath(const std::wstring& path, std::wstring& outManagedFolder);

    QTimer*                    m_debounceTimer = nullptr;
    std::vector<std::wstring>  m_pendingPaths;
    std::mutex                 m_queueMutex;
    bool                       m_isListening = false;
    std::atomic<bool>          m_globalPaused{false};
};

} // namespace ArcMeta