#ifndef ARCMETA_METADATA_MANAGER_H
#define ARCMETA_METADATA_MANAGER_H

#include "MetadataDefs.h"
#include <QObject>
#include <QString>
#include <QTimer>
#include <QStringList>
#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>
#include <string>

namespace ArcMeta {

/**
 * @brief 内存元数据镜像结构
 */
struct RuntimeMeta {
    int rating = 0;
    std::wstring color;
    QStringList tags;
    std::wstring note;
    std::wstring url;
    bool pinned = false;
    bool encrypted = false;
    bool isFolder = false; // 2026-06-xx 物理标记：区分文件夹与文件，用于侧边栏精准统计
    std::string fileId128; // 2026-06-xx 物理关联：缓存 ID 以供反向查询分类

    // 2026-06-xx 物理对标：补充时间戳与大小字段
    long long ctime = 0;
    long long mtime = 0;
    long long atime = 0;
    long long fileSize = 0;

    std::vector<PaletteEntry> palettes;

    /**
     * @brief 判定是否有用户操作过的信息，作为“已录入/受控”状态的感应逻辑
     * 2026-06-xx 按照用户要求：只要有任何元数据修改，即视为数据库已录入项
     */
    bool hasUserOperations() const {
        return rating > 0 || !color.empty() || !tags.isEmpty() || !note.empty() || !url.empty() || pinned || encrypted;
    }
};

/**
 * @brief 元数据管理器
 */
class MetadataManager : public QObject {
    Q_OBJECT
public:
    static MetadataManager& instance();

    
    void initFromScchMode();
    RuntimeMeta getMeta(const std::wstring& path);

    /**
     * @brief 2026-06-xx 按照用户要求：在 SCCH 内存模式下执行多维搜索
     * @param keyword 关键词
     * @return 匹配的物理路径列表
     */
    QStringList searchInCache(const QString& keyword);

    void setRating(const std::wstring& path, int rating);
    void setColor(const std::wstring& path, const std::wstring& color);
    void setPinned(const std::wstring& path, bool pinned);
    void setTags(const std::wstring& path, const QStringList& tags);
    void setNote(const std::wstring& path, const std::wstring& note);
    void setURL(const std::wstring& path, const std::wstring& url);
    void setEncrypted(const std::wstring& path, bool encrypted);
    void setPalettes(const std::wstring& path, const QVector<QPair<QColor, float>>& palettes);
    QVector<QColor> getPalettes(const std::wstring& path);

    void renameItem(const std::wstring& oldPath, const std::wstring& newPath);
    void removeMetadataSync(const std::wstring& path);

    /**
     * @brief 物理同步元数据
     * 2026-06-xx 按照用户要求：支持主动触发物理元数据（File ID 等）的获取与保存
     */
    void syncPhysicalMetadata(const std::wstring& path);

    /**
     * @brief 同步获取文件的 128-bit File ID (或 Fallback ID)
     * 2026-06-15 物理加固：确保在建立分类关联前指纹已就绪
     */
    std::string getFileIdSync(const std::wstring& path);

    /**
     * @brief 获取路径所在磁盘的卷序列号
     */
    static std::wstring getVolumeSerialNumber(const std::wstring& path);

    /**
     * @brief 只读遍历内存缓存，用于统计等场景（持有读锁）
     * 2026-06-xx 物理同步：回调参数包含 (path, RuntimeMeta)
     */
    template<typename Func>
    void forEachCachedItem(Func&& fn) const {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        for (const auto& [path, meta] : m_cache) {
            fn(path, meta);
        }
    }

    // 2026-06-xx 废弃接口：保留为空实现以维持二进制/ABI兼容（若需要），或在完成清理后移除
    bool hasPendingSync() const;
    QStringList getPendingSyncDirs();
    void removeFidsFromLog(const QStringList& fids);
    void addToSyncLog(const std::wstring& dirPath);

    /**
     * @brief 内部辅助：通过 WinAPI 获取 File ID 和基础元数据
     * 2026-06-xx 物理修复：已升级为公开静态成员，支持跨模块同步入库
     * 2026-06-xx 物理补完：增加 outFrn 参数以获取物理索引，彻底杜绝数据库主键冲突
     */
    static bool fetchWinApiMetadataDirect(const std::wstring& path, std::string& outId128, std::wstring* outFrn = nullptr, long long* outSize = nullptr, std::wstring* outType = nullptr, long long* outCtime = nullptr, long long* outMtime = nullptr, long long* outAtime = nullptr);

signals:
    // 2026-05-27 物理修复：信号参数由 std::wstring 改为 QString
    // 理由：std::wstring 未注册为元类型，导致跨线程发射时（如数据库预热阶段）触发 QueuedConnection 失败从而引起崩溃。
    void metaChanged(const QString& path);

    /**
     * @brief 待同步状态变更信号
     * @param hasPending 是否存在待处理数据
     */
    void pendingSyncChanged(bool hasPending);

private:
    MetadataManager(QObject* parent = nullptr);
    ~MetadataManager() override = default;

    std::unordered_map<std::wstring, RuntimeMeta> m_cache;
    mutable std::shared_mutex m_mutex;
    
    // 2026-05-25 按照用户要求：改用单例计时器与脏路径集，彻底解决计时器风暴
    QTimer* m_batchTimer = nullptr;
    std::unordered_set<std::wstring, std::hash<std::wstring>> m_dirtyPaths;

    void persistAsync(const std::wstring& path);
    void debouncePersist(const std::wstring& path);

    void saveSyncLog();
};

} // namespace ArcMeta

#endif // ARCMETA_METADATA_MANAGER_H
