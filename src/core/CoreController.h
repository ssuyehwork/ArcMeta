#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <memory>
#include <atomic>

namespace ArcMeta {

/**
 * @brief 核心中控类
 * 负责协调底层服务初始化、管理系统全局状态、并为 UI 提供异步通知接口。
 */
class CoreController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool isIndexing READ isIndexing NOTIFY isIndexingChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)

public:
    static CoreController& instance();

    /**
     * @brief 启动异步初始化序列
     */
    void startSystem();

    bool isIndexing() const { return m_isIndexing; }
    QString statusText() const { return m_statusText; }

    /**
     * @brief 统一搜索接口 (2026-07-xx 按照 Plan-57 升级为异步模式)
     * @param keyword 关键词
     * @param scopeSource 范围来源 ("category" 或 "nav")
     * @param categoryId 分类 ID (当 scopeSource 为 "category" 时有效)
     * @param parentPath 物理路径 (当 scopeSource 为 "nav" 时有效)
     */
    void performSearch(const QString& keyword, const QString& scopeSource = "", int categoryId = 0, const QString& parentPath = "");

    /**
     * @brief 中止当前正在进行的搜索任务
     */
    void abortSearch();

    /**
     * @brief 创建托管文件夹并注册逻辑分类 (2026-07-xx 按照 Analysis_Modification_Plan-120)
     * @param driveLetter 盘符 (如 "C:")
     * @return 是否成功创建
     */
    bool createManagedFolder(const QString& driveLetter);

    /**
     * @brief 更新项目星级 (中转接口)
     */
    void setItemRating(const QString& path, int rating);

    /**
     * @brief 更新项目颜色 (中转接口)
     */
    void setItemColor(const QString& path, const QString& color);

    /**
     * @brief 更新项目标签 (中转接口)
     */
    void setItemTags(const QString& path, const QStringList& tags);

    /**
     * @brief 触发托管库强制重扫 (2026-07-xx 按照 Analysis_Modification_Plan-120)
     */
    void rescanDrive(const QString& managedPath);

    /**
     * @brief 获取指定盘符的托管库绝对路径
     */
    QString getManagedFolderPath(const QString& driveLetter);

    /**
     * @brief 判定指定路径是否位于托管库内部
     */
    bool isInsideManagedLibrary(const QString& path);

public slots:
    /**
     * @brief 响应硬件设备变更
     */
    void onDeviceChanged(bool arrival);

signals:
    /**
     * @brief 搜索结果流式返回
     * @param results 新发现的路径列表
     * @param isIncremental 是否为增量结果
     */
    void searchResultsAvailable(const QStringList& results, bool isIncremental);
    void searchStarted();
    void searchFinished(int totalFound);

    void isIndexingChanged(bool indexing);
    void statusTextChanged(const QString& text);
    void initializationFinished();

private:
    CoreController(QObject* parent = nullptr);
    ~CoreController() override;

    void setStatus(const QString& text, bool indexing);

    bool m_isIndexing = false;
    QString m_statusText = "就绪";
    
    // 2026-07-xx 按照 Plan-57：搜索状态管理
    std::atomic<bool> m_isSearchAborted{false};
    std::atomic<bool> m_isSearching{false};
    std::atomic<int> m_currentSearchId{0}; // 物理搜索 ID：用于识别并中止过期的异步扫描任务
};

} // namespace ArcMeta
