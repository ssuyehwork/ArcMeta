#pragma once

#include <QDateTime>
#include "../core/ItemRecord.h"
#include <QMap>
#include <unordered_map>
#include <deque>
#include <vector>
#include <QCache>
#include <QStringList>
#include <QTimer>
#include <QWidget>
#include <QListView>
#include <QTreeView>
#include <QStackedWidget>
#include <QPushButton>
#include <QTextBrowser>
#include <QStandardItemModel>
#include <QSortFilterProxyModel>
#include <QVBoxLayout>
#include <QStyledItemDelegate>
#include <QPersistentModelIndex>
#include <QDebug>
#include <QIcon>
#include "ScanStats.h"
#include "FilterPanel.h"
#include "models/DiskItemModel.h"
#include "models/LibraryAssetModel.h"

#include "../core/ModelContract.h"

namespace ArcMeta {

class CategoryLockWidget;

struct RuntimeMeta;

/**
 * @brief 内部代理类：专门处理高级筛选逻辑 (2026-05-25 物理化以修复 static_cast 编译报错)
 */
class FilterProxyModel : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit FilterProxyModel(QObject* parent = nullptr);

    FilterState currentFilter;

    void updateFilter();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;
    bool lessThan(const QModelIndex& source_left, const QModelIndex& source_right) const override;

private:
    void recomputeDuplicateCache();
    std::unordered_set<QString> m_cachedDuplicatePaths; // 缓存当前所有重复项的路径集合
};

/**
 * @brief 虚拟化数据库模型：支持百万级条目瞬时加载 (2026-06-xx 重构)
 */
// 🚨 极致物理重构：ArcMetaVirtualDbModel 已彻底退役并被 DiskItemModel / LibraryAssetModel 继承平替，在此安全移除


/**
 * @brief 内容面板（面板四）：核心业务展示区
 * 支持网格视图（QListView）与列表视图（QTreeView）切换
 */
class ContentPanel : public QFrame {
    Q_OBJECT

public:
    enum class DataSourceType {
        DiskNav,        // 1. 物理磁盘导航模式 (如 D:\Photos，随点随看，离散 JSON 缓存)
        UserCategory,   // 2. 用户自定义逻辑分类 (如 "商业设计原稿"，ID > 0)
        SystemCategory, // 3. 系统逻辑桶 (全部数据, 未分类, 垃圾桶, 最近访问)
        PathList        // 4. 临时路径列表 (搜索结果, 标签筛选)
    };

    DataSourceType dataSourceType() const;
    bool isMirrorSource() const;
    bool isManagedContext() const;
    int currentCategoryId() const { return m_currentCategoryId; }
    bool isContextMenuActive() const { return m_isContextMenuActive; }

    enum SortType {
        SortByName,
        SortByCreateDate,
        SortByModifyDate,
        SortByExtension,
        SortBySize,
        SortByDimension,
        SortByRating,
        SortByAddedDate
    };

    SortType currentSortType() const { return m_sortType; }
    Qt::SortOrder currentSortOrder() const { return m_sortOrder; }


    enum ViewMode {
        ListView,
        GridView,
        JustifiedViewMode
    };

    /**
     * @brief 右键菜单动作枚举 (2026-06-01 按照用户要求：取代弱类型字符串匹配)
     */
    enum ContextAction {
        ActionOpen,
        ActionOpenDefault,
        ActionShowInExplorer,
        ActionNewFolder,
        ActionNewMd,
        ActionNewTxt,
        ActionCategorize,
        ActionPin,
        ActionUnpin,
        ActionColorTag,
        ActionEncrypt,
        ActionDecrypt,
        ActionChangePwd,
        ActionBatchRename,
        ActionRename,
        ActionCopy,
        ActionCut,
        ActionPaste,
        ActionDelete,
        ActionPermanentDelete,
        ActionSecureDelete,
        ActionRestore,
        ActionCopyName,
        ActionCopyPath,
        ActionAddToCategory,
        ActionAddToFavorites,
        ActionRescan,
        ActionRefresh,
        ActionCancelImport,
        ActionBatchCreate
    };

    explicit ContentPanel(QWidget* parent = nullptr);
    ~ContentPanel() override = default;

    // 2026-04-12 关键修复：延迟初始化
    void deferredInit();

    /**
     * @brief 物理定位选中并滚动到对应视图行号
     * @param path 绝对物理路径
     */
    void selectAndScrollToPath(const QString& path);
    void selectAndScrollToItem(const QString& type, const QString& path, int categoryId);

    /**
     * @brief 切换视图模式
     */
    void setViewMode(ViewMode mode);
    ViewMode currentViewMode() const { return m_currentViewMode; }

    /**
     * @brief 拦截空格键（红线：物理拦截 QEvent::KeyPress 且为 Key_Space）
     */
    bool eventFilter(QObject* obj, QEvent* event) override;

    // --- 业务接口 ---
    QAbstractItemModel* model() const { return m_model; }
    QSortFilterProxyModel* getProxyModel() const { return m_proxyModel; }
    QModelIndexList getSelectedIndexes() const {
        return (m_viewStack->currentWidget() == m_gridView) ? 
                m_gridView->selectionModel()->selectedIndexes() : 
                m_treeView->selectionModel()->selectedIndexes();
    }

    /**
     * @brief 物理定位：在当前视图模型中寻找与 currentPath 相邻的文件路径
     * @param delta 偏移方向 (-1 为上一个, 1 为下一个)
     */
    QString getAdjacentFilePath(const QString& currentPath, int delta);

signals:
    /**
     * @brief 缩放比例与视图模式变更信号 (Modification_Plan-47)
     */
    void zoomLevelChanged(int level);
    void viewModeChanged(ViewMode mode);

signals:
    /**
     * @brief 请求 QuickLook 预览信号
     * @param path 物理路径
     */
    void requestQuickLook(const QString& path);

    /**
     * @brief 选中项发生变化时通知元数据面板刷新
     * @param paths 选中条目的物理路径列表
     */
    void selectionChanged(const QStringList& paths);
    void directorySelected(const QString& path);

    /**
     * @brief 请求将指定路径添加至收藏夹的信号 (对应用户原话：“把选中的项目收藏到收藏区里”)
     * @param paths 选中的项目绝对物理路径列表 (对应用户原话：“选中某个项目”)
     */
    void requestAddFavorite(const QStringList& paths);

    /**
     * @brief 数据源变更信号，用于焦点线管理
     * @param source 数据源标识
     */
    void dataSourceChanged(const QString& source);

    /**
     * @brief 目录装载完成后发出，携带统计数据供 FilterPanel 填充
     */
    void directoryStatsReady(const ArcMeta::ScanStats& stats);

private:
    void initUi();
    void initGridView();
    void restoreActiveView();
    void restoreSelections();
    void initListView();
    void setupContextMenu();
    void updateLayersButtonState();

    /**
     * @brief 内部业务辅助逻辑
     */
    void performCopy(bool cutMode);
    void performPaste();
    void performBatchRename();

    QVBoxLayout* m_mainLayout = nullptr;
    QStackedWidget* m_viewStack = nullptr;
    CategoryLockWidget* m_lockWidget = nullptr;
    QPushButton* m_btnLayers = nullptr;
    QPushButton* m_btnLayersBlue = nullptr;
    QPushButton* m_btnToggleFolders = nullptr; // 2026-07-xx 按照 Plan-73：显示/隐藏文件夹切换
    QPushButton* m_btnToggleFiles = nullptr;   // 2026-07-xx 按照 Plan-73：显示/隐藏文件切换
    QTextBrowser* m_textPreview = nullptr;
    QLabel* m_imagePreview = nullptr;

    // 视图组件
    QAbstractItemView* m_gridView = nullptr;
    QTreeView* m_treeView = nullptr;
    DiskItemModel* m_diskModel = nullptr;       // 负责纯物理磁盘导航模型 (0)
    LibraryAssetModel* m_libraryModel = nullptr; // 负责内存托管逻辑资产模型 (1)
    ItemModelBase* m_model = nullptr;           // 当前多态激活指针合约

    QTimer* m_visibleTimer = nullptr;
    void refreshVisibleThumbnails();
    QSortFilterProxyModel* m_proxyModel = nullptr;


    FilterState m_currentFilter;

    int m_zoomLevel = 64;
    QString m_currentPath;
    QSet<QString> m_pendingSelectNames;
    bool m_isPendingEdit = false;
    int m_currentCategoryId = -1;
    QString m_currentCategoryType; // 用于驱动差异化右键菜单
    bool m_isRecursive = false;
    bool m_isCategoryRecursive = false;
    bool m_showFolders = true;
    bool m_showFiles = true;
    ViewMode m_currentViewMode = GridView;
    SortType m_sortType = SortByName;
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;
    std::atomic<bool> m_isLoading{false}; // 2026-06-16 物理状态锁：防止加载数据时的布局抖动覆盖用户配置
    bool m_isContextMenuActive = false;
    std::atomic<int> m_loadRequestId{0}; // 2026-07-xx 物理请求 ID：防止异步回调导致的视图内容乱跳

    // --- 2026-06-xx 性能优化：递归扫描指纹缓存 ---
    struct ScanCacheEntry {
        qint64 lastModified; // 根目录的时间戳
        std::vector<ItemRecord> records;
    };
    QMap<QString, ScanCacheEntry> m_recursiveCache; 
    QTimer* m_selectionTimer = nullptr; // 选中防抖定时器
    void updateGridSize();
    void updateStatusBarStats();
    void recalculateAndEmitStats();

    /**
     * @brief 统一判断粘贴/拖拽导入的目的地。
     * @param outCatId 输出参数：解析出的目标分类 ID（DiskNav 场景下无意义，忽略）
     * @return true 表示可以继续执行导入；false 表示应终止（已在内部完成提示或已被用户取消）
     */
    bool resolvePasteDestination(int& outCatId);

    void addItemsFromDirectory(const QString& path, bool recursive,
                               QMap<int, int>& ratingCounts,
                               QMap<QString, int>& colorCounts,
                               QMap<QString, int>& tagCounts,
                               QMap<QString, int>& typeCounts,
                               QMap<QString, int>& createDateCounts,
                               QMap<QString, int>& modifyDateCounts,
                               int& noTagCount);

public slots:
    /**
     * @brief 设置缩放比例，限制在 96~128px 之间 (Modification_Plan-47)
     */
    void setZoomLevel(int level);

public slots:
    void onSelectionChanged();
    void onCustomContextMenuRequested(const QPoint& pos);
    void onDoubleClicked(const QModelIndex& index);
    void onPathsDropped(const QStringList& paths, const QModelIndex& targetIndex);

    /**
     * @brief 加载并显示目录内容
     */
    void loadDirectory(const QString& path, bool recursive = false);

    /**
     * @brief 设置待选中的项名称，并在下次加载完成后自动定位
     * @param name 文件名
     * @param edit 是否进入编辑模式
     */
    void setPendingSelectName(const QString& name, bool edit = false) { 
        m_pendingSelectNames.clear();
        if (!name.isEmpty()) m_pendingSelectNames.insert(name);
        m_isPendingEdit = edit;
    }

    /**
     * @brief 强制重新加载当前视图的所有内容
     */
    void refreshAll();

    /**
     * @brief 局部更新某项的元数据（星级、颜色、标签等）
     */
    void updateItemMetadata(const QString& path);

    /**
     * @brief 2026-07-26 极致重构：平滑更名缩略图与宽高比缓存 Key
     */
    void migrateModelCache(const QString& oldPath, const QString& newPath);

    /**
     * @brief 2026-07-27 按照 Plan-107：物理清除被擦除文件夹对应的缩略图与宽高比等高级缓存
     */
    void clearFolderCache(const QString& folderPath);

    /**
     * @brief 全局/本地搜索
     */
    void search(const QString& query);

    /**
     * @brief 应用当前筛选器
     */
    void applyFilters(const FilterState& state);
    void applyFilters(); // 使用保存的状态重新应用

    /**
     * @brief 创建新条目（文件夹/Markdown/Txt）
     */
    void createNewItem(const QString& type);

    /**
     * @brief 预览文件内容 (支持文本、Markdown、图片等)
     */
    void previewFile(const QString& path);

    /**
     * @brief 加载指定路径列表 (分类联动使用)
     * @param reqId 可选的请求 ID。若为 0，则自动生成新 ID。
     */
    void loadPaths(const QStringList& paths, int reqId = 0);

    /**
     * @brief 2026-07-xx 按照 Plan-57：增量追加路径列表 (异步搜索流式返回使用)
     * @param reqId 可选的请求 ID。只有当 ID 与当前 ID 一致时才会执行追加。
     */
    void appendPaths(const QStringList& paths, int reqId = 0);

    /**
     * @brief 获取当前最新的加载请求 ID
     */
    int currentLoadRequestId() const { return m_loadRequestId.load(); }

    /**
     * @brief 2026-06-xx 彻底重构：加载分类及其子项 (分类 ID 联动)
     */
    void loadCategory(int categoryId);
    void loadCategories(const QList<int>& categoryIds);

    /**
     * @brief 获取/设置当前分类类型，用于驱动右键菜单差异化
     */
    QString getCurrentCategoryType() const { return m_currentCategoryType; }
    void setCurrentCategoryType(const QString& type) { m_currentCategoryType = type; }

signals:
    /**
     * @brief 在内存模式下，请求在指定分类下创建 logical 子分类（对应用户原话：“在内存模式下，请求在指定分类下创建逻辑子分类”）
     */
    void requestCreateSubCategory(int parentCategoryId);

signals:
    /**
     * @brief 当在内容区点击子分类时触发，告知 MainWindow 切换侧边栏选中状态
     */
    void categoryClicked(int categoryId);

    /**
     * @brief 状态栏统计信息信号
     * @param fileCount 文件数量
     * @param folderCount 文件夹数量
     * @param totalCount 总项目数量
     */
    void statusBarStatsUpdated(int fileCount, int folderCount, int totalCount);


protected:
    void wheelEvent(QWheelEvent* event) override;
};

} // namespace ArcMeta
