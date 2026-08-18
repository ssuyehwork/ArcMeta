#ifndef QuarkMeta_METADATA_MANAGER_H
#define QuarkMeta_METADATA_MANAGER_H

#include "MetadataDefs.h"
#include <QObject>
#include <QString>
#include <QTimer>
#include <QStringList>
#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>
#include <string>
#include <atomic>
#include <deque>
#include <mutex>
#include <memory>
#include <array>

namespace QuarkMeta {

/**
 * @brief 内存元数据镜像结构
 */
struct RuntimeMeta {
    int rating;
    std::wstring manualColor;
    std::wstring autoColor;
    QStringList tags;
    std::wstring note;
    std::wstring url;
    bool pinned;
    bool encrypted;
    bool isFolder; // 2026-06-xx 物理标记：区分文件夹与文件，用于侧边栏精准统计
    bool isTrash;  // 2026-06-xx 状态标记：是否处于回收站
    bool isManaged; // 2026-06-xx 物理对标：标记该项是否已在数据库中登记
    int ingestionStatus; // 2026-07-xx 状态标记：-1: 未知, 0: 待处理, 1: 已完成
    int width;      // 2026-07-xx 物理尺寸：宽 (像素)
    int height;     // 2026-07-xx 物理尺寸：高 (像素)
    std::wstring originalPath; // 2026-06-xx 路径记忆：用于回收站还原
    std::string folderId; // 2026-06-xx 物理关联：缓存 ID 以供反向查询分类
    std::wstring baseName; // 2026-08-xx 持久化基名，避免重复解析计算
    std::wstring ext;      // 2026-08-xx 持久化后缀名，统一小写
    std::string sha256;    // 新增：储存文件的 SHA256 / FastHash 哈希值
    std::vector<int> categoryIds; // 🚨 SSOT 核心：资产直接在内存中持有其绑定的分类 ID 列表
    
    // 2026-06-xx 物理对标：补充时间戳与大小字段
    long long ctime;
    long long mtime;
    long long atime;
    long long fileSize;
    long long added_at;

    std::vector<PaletteEntry> palettes;

    RuntimeMeta() : rating(0), pinned(false), encrypted(false), isFolder(false), isTrash(false), isManaged(false), ingestionStatus(-1), width(0), height(0), ctime(0), mtime(0), atime(0), fileSize(0), added_at(0) {}

    /**
     * @brief 判定是否有用户操作过的信息，作为“已录入/受控”状态的感应逻辑
     * 2026-06-xx 按照用户要求：只要有任何元数据修改或已登记，即视为数据库已录入项
     */
    bool hasUserOperations() const {
        return isManaged || rating > 0 || !manualColor.empty() || !autoColor.empty() || !tags.isEmpty() || !note.empty() || !url.empty() || pinned || encrypted;
    }
};

struct LightMeta {
    std::wstring path;
    std::string folderId;
    bool isFolder;
    bool isTrash;
    bool tagsEmpty;
    double atime;
    QStringList tags;
};

/**
 * @brief 元数据管理器
 */
class MetadataManager : public QObject {
    Q_OBJECT
public:
    static MetadataManager& instance();

    bool isLoaded() const {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        return m_loaded;
    }

    static std::string generateFallbackFolderId(const std::wstring& vol, const std::wstring& frn);
    static std::string generateDeterministicFolderId(const std::wstring& path);
    static std::wstring generateDeterministicFrn(const std::wstring& path);
    static std::wstring normalizePath(const std::wstring& path);
    
    void initFromDatabase();
    RuntimeMeta getMeta(const std::wstring& path);
    std::wstring getPathByFolderId(const std::string& fid);

    /**
     * @brief 2026-06-xx：在内存模式下执行多维搜索
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
     * @brief 获取权重最高的前 N 个标签 (Plan-82)
     */
    QList<QPair<QString, int>> getTopTags(int limit = 20) const;

    /**
     * @brief 🚨 SSOT 核心：内存分类关联维护接口
     */
    void addCategoryToItemMemory(const std::wstring& path, int categoryId);
    void removeCategoryFromItemMemory(const std::wstring& path, int categoryId);
    void clearCategoriesFromItemMemory(const std::wstring& path);

    /**
     * @brief 物理刷新级别
     */
    enum class RefreshLevel {
        CountsOnly,   // 仅刷新侧边栏计数
        PathUpdate,   // 刷新特定文件
        FullRebuild,  // 全量 UI 重建
        CategoryOnly  // 仅物理更新分类，避免数据全量重载
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
     * @brief 🚨 SSOT 重构核心：单一权威资产入库登记管线
     */
    bool registerAsset(const std::string& folderId, const std::wstring& assetPath, int targetCatId);

    /**
     * @brief 🚨 SSOT 重构核心：跨盘托管库胶囊物理迁移（跨盘 1:1 重锚定）
     */
    std::string migrateCapsuleToLibrary(const std::string& assetId, const QString& targetLibraryPath);

    /**
     * @brief 一站式项目注册流程（受控模式）
     * 2026-07-xx 仅允许受信任的来源调用
     * @param path 物理路径
     * @param authorized 是否经过授权（只有 true 才能创建新记录）
     */
    void registerItem(const std::wstring& path, bool authorized = false);

    /**
     * @brief 异步批量注册项目 (Plan-88 性能重构)
     * 2026-07-xx 按照 Plan-116：UI 层主动调用的批量注册将受到严格拦截
     */
    void registerItemsAsync(const QStringList& paths, bool authorized = false);

    /**
     * @brief 登记项目（待处理状态 0）
     * 2026-07-xx 按照 Plan-117：标记项目并递归标记子项
     */
    void markAsRegistered(const std::wstring& path);

    /**
     * @brief 标记项目已完成解析（完成状态 1）
     */
    void markAsIngested(const std::wstring& path);

    /**
     * @brief 原子化更新项目的登记状态并同步父目录进度
     * 2026-07-xx 按照 Development_Plan 3.3：专属原子函数负责标记值更新与比例值同步
     * @param path 物理路径
     * @param newStatus 新状态 (0: 待处理, 1: 已完成)
     */
    void updateIngestionStatus(const std::wstring& path, int newStatus);

    /**
     * @brief 计算并持久化指定目录的进度百分比
     * 2026-07-xx 按照 Development_Plan 3.1 & 3.2
     */
    void calculateAndPersistProgress(const std::wstring& folderPath);

    /**
     * @brief 从数据库加载持久化的进度值
     */
    double getProgressFromDb(const std::wstring& folderPath);

    /**
     * @brief 判定指定目录在缓存中是否存在子项 (Plan-124)
     * 依靠 m_parentToChildren 索引实现 O(1) 判定，用于废除物理磁盘空判定
     */
    bool hasChildrenInCache(const std::wstring& folderPath);

    /**
     * @brief 从缓存中获取指定目录的直接子项 (Plan-124)
     * 返回路径与元数据的副本，调用者无需在耗时操作中持有锁
     */
    std::vector<std::pair<std::wstring, RuntimeMeta>> getChildrenFromCache(const std::wstring& folderPath);

    void ensureActivated(const std::wstring& nPath);

    void setRating(const std::wstring& path, int rating, bool notify = true);
    void setSha256(const std::wstring& path, const std::string& sha256, bool notify = false);
    void setAddedAt(const std::wstring& path, long long addedAt, bool notify = true);
    void setColor(const std::wstring& path, const std::wstring& color, bool notify = true);
    void setPinned(const std::wstring& path, bool pinned, bool notify = true);
    void setTags(const std::wstring& path, const QStringList& tags, bool notify = true);
    void setNote(const std::wstring& path, const std::wstring& note, bool notify = true);
    void setURL(const std::wstring& path, const std::wstring& url, bool notify = true);
    void setEncrypted(const std::wstring& path, bool encrypted, bool notify = true);

    void setManaged(const std::wstring& path, bool managed, bool notify = true);
    void setPalettes(const std::wstring& path, const QVector<QPair<QColor, float>>& palettes, bool notify = true);

    struct ExtractedFeatureItem {
        std::wstring path;
        int width{0};
        int height{0};
        int64_t mtime{0};
        int64_t fileSize{0};
        std::wstring autoColor;
        QVector<QPair<QColor, float>> palettes;
        int ingestionStatus{1};
    };

    void updateExtractedMediaFeatures(
        const std::wstring& path, 
        int width, 
        int height, 
        const std::wstring& autoColor, 
        const QVector<QPair<QColor, float>>& palettes, 
        int ingestionStatus = 1
    );

    void updateExtractedMediaFeaturesBatch(const std::vector<ExtractedFeatureItem>& items);

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

    /**
     * @brief 设置图片/媒体的宽高
     */
    void setItemDimensions(const std::wstring& path, int width, int height);

    QVector<QColor> getPalettes(const std::wstring& path);

    void renameItem(const std::wstring& oldPath, const std::wstring& newPath);

    /**
     * @brief 异步批量重命名元数据（单事务高性能版）
     * @param rawPathPairs 旧路径 -> 新路径 映射对列表
     * @param onCompleted 完成后的回调函数，参数为成功更新的元数据条数
     */
    void renameBatchAsync(
        const std::vector<std::pair<std::wstring, std::wstring>>& rawPathPairs,
        std::function<void(int successCount)> onCompleted = nullptr
    );

    void removeMetadataSync(const std::wstring& path);

    /**
     * @brief 在物理移动/剪切完成后，统一调用该函数进行元数据和统计对账
     */
    void syncAfterMove(const std::wstring& oldPath, const std::wstring& newPath);

    /**
     * @brief 2026-08-xx 按照 Plan-128：高性能批量删除元数据
     * 支持内存单事务与磁盘单任务大事务，解决大规模删除导致的 I/O 阻塞。
     */
    void removeMetadataBatchSync(const QStringList& paths);

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
     * @brief 统一注册 .QuarkMeta 目录的 FRN
     */
    static void registerQuarkMetaFrn(const std::wstring& parentDir);

    /**
     * @brief 同步获取文件的 128-bit File ID (或 Fallback ID)
     * 2026-06-15 物理加固：确保在建立分类关联前指纹已就绪
     */
    std::string getFolderIdSync(const std::wstring& path);

    /**
     * @brief 获取路径所在磁盘的卷序列号
     */
    static std::wstring getVolumeSerialNumber(const std::wstring& path);

    /**
     * @brief 判定给定路径是否位于任何磁盘的资源库文件夹内部
     * 2026-07-xx 按照 Plan-117：收拢物理路径归属判定逻辑
     */
    static bool isInsideManagedLibrary(const std::wstring& path);

    /**
     * @brief 获取指定卷的资源库绝对路径
     * 2026-07-xx 按照 Plan-118：整合配置查询与约定兜底逻辑，确保全系统识别一致性
     */
    static std::wstring getManagedLibraryPath(const std::wstring& volSerial, const QString& driveLetter);

    /**
     * @brief 物理操作原子事务计数，精确闭锁生命周期
     */
    void beginInternalOperation() {
        m_internalOpsCount.fetch_add(1);
        m_isInternalOperating.store(true);
    }

    void endInternalOperation() {
        int count = m_internalOpsCount.fetch_sub(1) - 1;
        if (count <= 0) {
            m_internalOpsCount.store(0);
            m_isInternalOperating.store(false);
        }
    }

    /**
     * @brief 设置内部操作标志位，用于抑制冗余信号刷新
     */
    void setInternalOperating(bool operating) { 
        if (operating) {
            beginInternalOperation();
        } else {
            endInternalOperation();
        }
    }
    bool isInternalOperating() const { return m_isInternalOperating.load(); }

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
    std::wstring getVolumeFromFolderId(const std::string& fid);

    /**
     * @brief 卸载指定卷的名称/后缀索引映射（驱动器拔出时）
     */
    void unloadVolumeNameCache(const std::wstring& volSerial);

    /**
     * @brief 加载指定卷的名称/后缀索引映射（驱动器插入或初始化时）
     */
    void loadVolumeNameCache(const std::wstring& volSerial);

    /**
     * @brief 2026-08-xx Sliding window functions for recently_visited
     */
    void recordAccess(const std::wstring& path);
    void slideRecentWindow();
    double getCachedAtime(const std::wstring& path);

    /**
     * @brief 隔离查询 API
     */
    std::vector<std::string> getFolderIdsByName(const std::wstring& filename);
    std::vector<std::string> getSubFolderIdsByName(const std::wstring& foldername);
    std::vector<std::string> getFolderIdsByExtension(const std::wstring& extension);

    /**
     * @brief 只读遍历内存缓存，用于统计等场景（无锁 RCU 读取）
     * 2026-06-xx 物理同步：回调参数包含 (path, RuntimeMeta)
     */
    template<typename Func>
    void forEachCachedItem(Func&& fn) const {
        // [1.1.4 规范] 256分片弱一致性遍历：逐分片获取 shared_lock 读取，读毕即释
        for (size_t i = 0; i < NUM_SHARDS; ++i) {
            std::shared_lock<std::shared_mutex> lock(m_shards[i].mutex);
            for (const auto& pair : m_shards[i].items) {
                fn(pair.first, pair.second);
            }
        }
    }

    /**
     * @brief 获取极轻量级内存缓存快照以避免长持读锁对账
     */
    std::vector<LightMeta> getLightweightCacheSnapshot() const;
    std::shared_mutex& getMutex() const { return m_mutex; }

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

    /**
     * @brief 异步持久化项元数据
     * 2026-07-xx 按照 Plan-116：增加授权标志位，严禁非法入库
     * @param authorized 是否允许创建新记录（只有文件变化触发时为 true）
     */
    void persistAsync(const std::wstring& path, bool notify = true, bool authorized = false);

signals:
    // 2026-05-27 物理修复：信号参数由 std::wstring 改为 QString
    // 理由：std::wstring 未注册为元类型，导致跨线程发射时（如数据库预热阶段）触发 QueuedConnection 失败从而引起崩溃。
    void metaChanged(const QString& path);

    /**
     * @brief 待同步状态变更信号
     * @param hasPending 是否存在待处理数据
     */
    void pendingSyncChanged(bool hasPending);

private slots:
    void triggerUiSignalTimer() {
        if (m_uiSignalTimer && !m_uiSignalTimer->isActive()) {
            m_uiSignalTimer->start();
        }
    }

private:
    MetadataManager(QObject* parent = nullptr);
    ~MetadataManager() override = default;

    // 256 分片并发哈希容器：替代全量深拷贝 COW 快照
    struct MetaShard {
        mutable std::shared_mutex mutex;
        std::unordered_map<std::wstring, RuntimeMeta> items;
    };
    static constexpr size_t NUM_SHARDS = 256;
    std::array<MetaShard, NUM_SHARDS> m_shards;

    inline size_t getShardIndex(const std::wstring& path) const {
        return std::hash<std::wstring>{}(normalizePath(path)) % NUM_SHARDS;
    }

    std::unordered_map<std::string, std::wstring> m_folderIdToPath;

    // 2026-xx-xx 按照 Plan-124：快速层级倒排索引与进度缓存
    // Key: 标准化父级目录路径 (结尾不含斜杠), Value: 直接子项的完整标准化路径集合
    std::unordered_map<std::wstring, std::vector<std::wstring>> m_parentToChildren;
    std::unordered_map<std::wstring, double> m_folderProgressCache;

    // 2026-08-xx Sliding window for recently_visited
    std::deque<std::wstring> m_recentVisitedQueue;
    std::unordered_set<std::wstring> m_recentVisitedSet;
    std::mutex m_recentMutex;

    // 2026-07-xx 隔离式倒排索引：物理隔离文件、文件夹及后缀
    // 1. 仅文件 (Key: L"resume.pdf", Value: file_ids)
    std::unordered_map<std::wstring, std::vector<std::string>> m_assetNameToFolderIds;
    // 2. 仅文件夹 (Key: L"projects", Value: folder_ids)
    std::unordered_map<std::wstring, std::vector<std::string>> m_subFolderNameToFolderIds;
    // 3. 仅后缀 (Key: L"pdf", Value: file_ids)
    std::unordered_map<std::wstring, std::vector<std::string>> m_extensionToFolderIds;

    mutable std::shared_mutex m_mutex;
    bool m_loaded = false; // 2026-06-xx 物理加固：加载状态标记
    std::atomic<bool> m_isInternalOperating{false}; // 2026-xx-xx 按照 Plan-105：信号抑制标志位
    std::atomic<int> m_internalOpsCount{0}; // 物理原子事务操作计数器
    
    // 2026-06-xx 性能加固：信号攒批机制，防止 5 万级数据扫描导致 UI 信号淹没
    QTimer* m_uiSignalTimer = nullptr;
    std::unordered_set<QString> m_pendingUiPaths;

    /**
     * @brief 异步批量持久化元数据 (Plan-119 性能加固)
     * 自动按数据库分组并执行单任务大事务落盘。
     */
    void persistBatchAsync(const std::vector<std::wstring>& paths, bool authorized = false);
};

} // namespace QuarkMeta

#endif // QuarkMeta_METADATA_MANAGER_H
