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

    // 2026-07-xx 按照 Plan-119：记录与获取最近访问文件夹
    static void recordRecentVisitedFolder(const std::wstring& path);
    static QStringList getRecentVisitedFolders(const std::wstring& volSerial);

    /**
     * @brief 2026-07-xx 按照 Plan-118：获取磁盘对应的托管库物理绝对路径
     * @param pathOrVolSerial 路径或卷序列号
     */
    static std::wstring getManagedLibraryPath(const std::wstring& pathOrVolSerial);

    /**
     * @brief 2026-07-xx 按照 Plan-118：扫描磁盘上已存在的托管库文件夹并同步至侧边栏
     */
    void scanExistingLibraries();

private slots:
    // 订阅 MftReader 发现的新增条目
    void onEntryAdded(uint64_t key);
    // 2026-07-xx 按照 Plan-120：订阅 USN 触发的更新条目
    void onEntryUpdated(uint64_t key);
    // 去抖超时，合并写入数据库
    void processImportQueue();

private:
    AutoImportManager(QObject* parent = nullptr);
    ~AutoImportManager() override;

    bool checkAndGetManagedPath(const std::wstring& path, std::wstring& outManagedFolder);

    QTimer* m_debounceTimer = nullptr;
    std::vector<std::wstring> m_pendingPaths;
    std::mutex m_queueMutex;
    bool m_isListening = false;
};

} // namespace ArcMeta
