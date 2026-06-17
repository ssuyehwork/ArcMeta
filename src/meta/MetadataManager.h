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
    int rating;
    std::wstring color;
    QStringList tags;
    std::wstring note;
    std::wstring url;
    bool pinned;
    bool encrypted;
    bool isFolder; // 2026-06-xx 物理标记：区分文件夹与文件，用于侧边栏精准统计
    bool isTrash;  // 2026-06-xx 状态标记：是否处于回收站
    bool isInvalid; // 2026-06-xx 物理校验：是否为第三方删除导致的失效数据
    bool isManaged; // 2026-06-xx 物理对标：标记该项是否已在数据库中登记
    int width;      // 2026-07-xx 物理尺寸：宽 (像素)
    int height;     // 2026-07-xx 物理尺寸：高 (像素)
    std::wstring originalPath; // 2026-06-xx 路径记忆：用于回收站还原
    std::string fileId128; // 2026-06-xx 物理关联：缓存 ID 以供反向查询分类
    
    // 2026-06-xx 物理对标：补充时间戳与大小字段
    long long ctime;
    long long mtime;
    long long atime;
    long long fileSize;

    std::vector<PaletteEntry> palettes;

    RuntimeMeta() : rating(0), pinned(false), encrypted(false), isFolder(false), isTrash(false), isInvalid(false), isManaged(false), width(0), height(0), ctime(0), mtime(0), atime(0), fileSize(0) {}

    /**
     * @brief 判定是否有用户操作过的信息，作为“已录入/受控”状态的感应逻辑
     * 2026-06-xx 按照用户要求：只要有任何元数据修改或已登记，即视为数据库已录入项
     */
    bool hasUserOperations() const {
        return isManaged || rating > 0 || !color.empty() || !tags.isEmpty() || !note.empty() || !url.empty() || pinned || encrypted;
    }
};

/**
 * @brief 元数据管理器
 */
class MetadataManager : public QObject {
    Q_OBJECT
public:
    static MetadataManager& instance();

    static std::string generateFallbackFid(const std::wstring& vol, const std::wstring& frn);
    static std::string generateDeterministicSha256Id(const std::wstring& path);
    static std::wstring generateDeterministicFrn(const std::wstring& path);
    static std::wstring normalizePath(const std::wstring& path);
    
    void initFromScchMode();
    RuntimeMeta getMeta(const std::wstring& path);
    std::wstring getPathByFid(const std::string& fid);

    /**
     * @brief 2026-06-xx 按照用户要求：在 SCCH 内存模式下执行多维搜索
     * @param keyword 关键词
     * @param scopeSource 范围来源 ("category" 或 "nav")
     * @param categoryId 分类 ID (当 scopeSource 为 "category" 时有效)
     * @param parentPath 物理路径 (当 scopeSource 为 "nav" 时有效)
     * @return 匹配的物理路径列表
     */
    QStringList searchInCache(const QString& keyword, const QString& scopeSource = "", int categoryId = 0, const QString& parentPath = "");

    /**
     * @brief 获取所有标签及其引用计数
     * @return 标签名 -> 引用次数
     */
    QMap<QString, int> getAllTags() const;

    /**
     * @brief 物理刷新级别
     */
    enum class RefreshLevel {
        CountsOnly,   // 仅刷新侧边栏计数
        PathUpdate,   // 刷新特定文件
        FullRebuild   // 全量 UI 重建
    };

    /**
     * @brief 语义化 UI 信号通知
     */
    void notifyUI(RefreshLevel level, const QString& path = "");

    /**
     * @brief 2026-06-xx 架构重构：语义化通知接口
     * 替代 emit metaChanged("__RELOAD_COUNT__")
     */
    void notifyCategoryCountChanged();

    /**
     * @brief 2026-06-xx 架构重构：语义化通知接口
     * 替代 emit metaChanged("__RELOAD_ALL__")
     */
    void notifyFullUIRebuild();

    /**
     * @brief 一站式项目注册流程
     * 整合 FID 获取、物理属性同步及视觉预热
     */
    void registerItem(const std::wstring& path);

    void ensureActivated(const std::wstring& nPath);

    void setRating(const std::wstring& path, int rating, bool notify = true);
    void setColor(const std::wstring& path, const std::wstring& color, bool notify = true);
    void setPinned(const std::wstring& path, bool pinned, bool notify = true);
    void setTags(const std::wstring& path, const QStringList& tags, bool notify = true);
    void setNote(const std::wstring& path, const std::wstring& note, bool notify = true);
    void setURL(const std::wstring& path, const std::wstring& url, bool notify = true);
    void setEncrypted(const std::wstring& path, bool encrypted, bool notify = true);
    void setInvalid(const std::wstring& path, bool invalid, bool notify = true);
    void setManaged(const std::wstring& path, bool managed, bool notify = true);
    void setPalettes(const std::wstring& path, const QVector<QPair<QColor, float>>& palettes, bool notify = true);

    /**
     * @brief 2026-07-xx 按照用户要求：开启 Scoped 模式，后续持久化将同时写入局部库
     * @param folderFid 局部库对应的文件夹 FID
     */
    void setScopedMode(const std::string& folderFid);

    /**
     * @brief 从 Scoped DB 批量恢复元数据到内存缓存
     */
    void loadFromScopedDb(const std::string& folderFid);

    /**
     * @brief 2026-07-xx 按照用户要求：迁移局部库数据到卷库/全局库
     */
    void migrateScopedData(const std::string& folderFid);

    /**
     * @brief 全局重命名标签
     */
    void renameTag(const QString& oldName, const QString& newName);

    /**
     * @brief 全局删除标签
     */
    void removeTag(const QString& tagName);
    
    /**
     * @brief 原子化设置视觉元数据（颜色与色板），仅触发一次信号
     * 2026-06-xx 物理优化：解决信号风暴
     */
    void setItemVisualMetadata(const std::wstring& path, const std::wstring& color, const QVector<QPair<QColor, float>>& palettes, bool notify = true);

    QVector<QColor> getPalettes(const std::wstring& path);

    void renameItem(const std::wstring& oldPath, const std::wstring& newPath);
    void removeMetadataSync(const std::wstring& path);
    void markAsTrash(const std::wstring& path, bool isTrash, const std::wstring& origPath = L"");
    void setTrash(const std::wstring& path, bool isTrash);
    void deletePermanently(const std::wstring& path);

    /**
     * @brief 物理同步元数据
     * 2026-06-xx 按照用户要求：支持主动触发物理元数据（File ID 等）的获取与保存
     */
    void syncPhysicalMetadata(const std::wstring& path, bool notify = true);

    /**
     * @brief 激活并初始化项的元数据
     * 2026-06-xx 提取公共逻辑：整合获取 FID、注册 FRN、物理同步及计数逻辑
     */
    static void activateItem(const std::wstring& path);

    /**
     * @brief 尝试提取视觉元数据（颜色与色板）
     * 2026-06-xx 提取公共逻辑：封装颜色解析与文件夹代表色逻辑
     */
    static void tryExtractColor(const std::wstring& path);

    /**
     * @brief 尝试提取图像尺寸 (Plan-29)
     */
    static void tryExtractDimensions(const std::wstring& path);

    /**
     * @brief 统一注册 .arcmeta 目录的 FRN
     */
    static void registerArcmetaFrn(const std::wstring& parentDir);

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
     * @brief 安全解析路径组件
     * @param normalizedPath 标准化后的路径
     * @param isFolder 是否为文件夹
     * @param outName 输出名称（文件含后缀，文件夹仅名称）
     * @param outExt 输出后缀（仅文件，统一小写）
     */
    void parsePathComponents(const std::wstring& normalizedPath, bool isFolder, std::wstring& outName, std::wstring& outExt);

    /**
     * @brief 从 FID 中提取卷序列号
     */
    std::wstring getVolumeFromFid(const std::string& fid);

    /**
     * @brief 卸载指定卷的名称/后缀索引映射（驱动器拔出时）
     */
    void unloadVolumeNameCache(const std::wstring& volSerial);

    /**
     * @brief 加载指定卷的名称/后缀索引映射（驱动器插入或初始化时）
     */
    void loadVolumeNameCache(const std::wstring& volSerial);

    /**
     * @brief 隔离查询 API
     */
    std::vector<std::string> getFileFidsByName(const std::wstring& filename);
    std::vector<std::string> getFolderFidsByName(const std::wstring& foldername);
    std::vector<std::string> getFidsByExtension(const std::wstring& extension);

    /**
     * @brief 只读遍历内存缓存，用于统计等场景（持有读锁）
     * 2026-06-xx 物理同步：回调参数包含 (path, RuntimeMeta)
     */
    template<typename Func>
    void forEachCachedItem(Func&& fn) const {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        for (std::unordered_map<std::wstring, RuntimeMeta>::const_iterator it = m_cache.begin(); it != m_cache.end(); ++it) {
            fn(it->first, it->second);
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
    std::unordered_map<std::string, std::wstring> m_fidToPath;
    std::string m_activeScopedFid; // 当前激活的局部库 FID

    // 2026-07-xx 隔离式倒排索引：物理隔离文件、文件夹及后缀
    // 1. 仅文件 (Key: L"resume.pdf", Value: file_ids)
    std::unordered_map<std::wstring, std::vector<std::string>> m_fileNameToFids;
    // 2. 仅文件夹 (Key: L"projects", Value: folder_ids)
    std::unordered_map<std::wstring, std::vector<std::string>> m_folderNameToFids;
    // 3. 仅后缀 (Key: L"pdf", Value: file_ids)
    std::unordered_map<std::wstring, std::vector<std::string>> m_extensionToFids;

    mutable std::shared_mutex m_mutex;
    bool m_loaded = false; // 2026-06-xx 物理加固：加载状态标记
    
    // 2026-05-25 按照用户要求：改用单例计时器与脏路径集，彻底解决计时器风暴
    QTimer* m_batchTimer = nullptr;
    std::unordered_set<std::wstring, std::hash<std::wstring>> m_dirtyPaths;

    // 2026-06-xx 性能加固：信号攒批机制，防止 5 万级数据扫描导致 UI 信号淹没
    QTimer* m_uiSignalTimer = nullptr;
    std::unordered_set<QString> m_pendingUiPaths;

    // 2026-07-xx 按照用户要求：视觉元数据（解析颜色）异步补偿队列
    QTimer* m_retryTimer = nullptr;
    std::vector<std::wstring> m_visualRetryQueue;
    void processVisualRetryQueue();

    void persistAsync(const std::wstring& path, bool notify = true);
    void debouncePersist(const std::wstring& path);
};

} // namespace ArcMeta

#endif // ARCMETA_METADATA_MANAGER_H
