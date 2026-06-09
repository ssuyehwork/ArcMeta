#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QVariant>
#include <QVariantList>
#include <QRecursiveMutex>
#include <QStringList>
#include <QSet>
#include <QMutex>
#include <QTimer>

class DatabaseManager : public QObject {
    Q_OBJECT
public:
    enum MoveDirection { Up, Down, Top, Bottom };
    // 2026-05-xx 按照用户要求：提升单页显示上限至 500 条，大幅减少翻页频率并提升浏览连贯性
    static constexpr int DEFAULT_PAGE_SIZE = 500;
    
    static DatabaseManager& instance();

    bool init(const QString& dbPath = "rapid_notes.db");
    QString getLastError() const { return m_lastError; }
    void logStartup(const QString& msg);
    void closeAndPack();
    
    // 核心 CRUD 操作
    int addNote(const QString& title, const QString& content, const QStringList& tags = QStringList(), 
                const QString& color = "", int categoryId = -1, 
                const QString& itemType = "text", const QByteArray& dataBlob = QByteArray(),
                const QString& sourceApp = "", const QString& sourceTitle = "",
                const QString& remark = "");
    bool updateNote(int id, const QString& title, const QString& content, const QStringList& tags, 
                    const QString& color = "", int categoryId = -1,
                    const QString& itemType = "text", const QByteArray& dataBlob = QByteArray(),
                    const QString& sourceApp = "", const QString& sourceTitle = "",
                    const QString& remark = "");
    bool deleteNotesBatch(const QList<int>& ids);
    bool updateNoteState(int id, const QString& column, const QVariant& value);
    bool updateNoteStateBatch(const QList<int>& ids, const QString& column, const QVariant& value);
    // 批量软删除 (放入回收站)
    bool softDeleteNotes(const QList<int>& ids);
    bool toggleNoteState(int id, const QString& column);
    bool moveNotesToCategory(const QList<int>& noteIds, int catId);
    bool recordAccess(int id);
    bool moveNote(int id, MoveDirection direction, const QString& filterType, const QVariant& filterValue, const QVariantMap& criteria = QVariantMap());
    bool moveNotesToRow(const QList<int>& ids, int targetRow, const QString& filterType, const QVariant& filterValue, const QVariantMap& criteria = QVariantMap());
    bool reorderNotes(const QString& filterType, const QVariant& filterValue, bool ascending, const QVariantMap& criteria = QVariantMap());

    // 分类管理
    int addCategory(const QString& name, int parentId = -1, const QString& color = "");
    /**
     * @brief 获取或创建指定名称的分类
     * 2026-04-xx 按照用户要求：用于色码自动归档功能，确保 "Color" 分类存在
     */
    int getOrCreateCategoryByName(const QString& name, int parentId = -1, const QString& color = "");
    bool renameCategory(int id, const QString& name);
    bool toggleCategoryPinned(int id);
    bool setCategoryColor(int id, const QString& color);
    bool softDeleteCategories(const QList<int>& ids);
    bool hardDeleteCategories(const QList<int>& ids);
    bool restoreCategories(const QList<int>& ids);
    bool moveCategory(int id, MoveDirection direction);
    bool reorderCategories(int parentId, bool ascending);
    bool reorderAllCategories(bool ascending);
    QString getCategorySortMode(int catId);
    bool setCategorySortMode(int catId, const QString& mode);
    bool updateCategoryOrder(int parentId, const QList<int>& categoryIds);
    bool updatePinnedCategoryOrder(const QList<int>& categoryIds); // 2026-04-20 [NEW] 独立存储快速访问排序
    QList<QVariantMap> getAllCategories();
    QList<QVariantMap> getChildCategories(int parentId);
    bool emptyTrash();
    bool restoreAllFromTrash();

    // 分类密码保护
    bool setCategoryPassword(int id, const QString& password, const QString& hint);
    bool removeCategoryPassword(int id);
    bool verifyCategoryPassword(int id, const QString& password);
    bool isCategoryLocked(int id);
    void lockCategory(int id);
    void lockAllCategories();
    void unlockCategory(int id);
    bool isLockedCategoriesHidden() const { return m_lockedCategoriesHidden; }
    void toggleLockedCategoriesVisibility();

    // 预设标签
    bool setCategoryPresetTags(int catId, const QString& tags);
    QString getCategoryPresetTags(int catId);

    // 标签管理
    bool addTagsToNote(int noteId, const QStringList& tags);
    bool renameTagGlobally(const QString& oldName, const QString& newName);
    bool deleteTagGlobally(const QString& tagName);
    // 2026-05-01 按照用户要求：标签物理化同步引擎
    void syncNoteTags(int noteId, const QStringList& tags);

    // 搜索与查询
    QList<QVariantMap> searchNotes(const QString& keyword, const QString& filterType = "all", const QVariant& filterValue = -1, int page = -1, int pageSize = DEFAULT_PAGE_SIZE, const QVariantMap& criteria = QVariantMap());
    int getNotesCount(const QString& keyword, const QString& filterType = "all", const QVariant& filterValue = -1, const QVariantMap& criteria = QVariantMap());
    QStringList getAllTags();
    QList<QVariantMap> getRecentTagsWithCounts(int limit = 20);
    QVariantMap getNoteById(int id);
    int getLastCreatedNoteId();

    // 统计
    QVariantMap getCounts();
    QVariantMap getFilterStats(const QString& keyword = "", const QString& filterType = "all", const QVariant& filterValue = -1, const QVariantMap& criteria = QVariantMap());

    // 待办事项管理
    struct Todo {
        int id = -1;
        QString title;
        QString content;
        QDateTime startTime;
        QDateTime endTime;
        int status = 0; // 0:待办, 1:已完成, 2:已逾期
        QDateTime reminderTime;
        int priority = 0;
        QString color;
        int noteId = -1;       // 关联笔记ID
        int repeatMode = 0;    // 0:None, 1:Daily, 2:Weekly, 3:Monthly, 4:Hourly, 5:Minutely, 6:Secondly
        int parentId = -1;     // 父任务ID (子任务支持)
        int progress = 0;      // 完成进度 (0-100)
        QDateTime createdAt;
        QDateTime updatedAt;
    };
    int addTodo(const Todo& todo);
    bool updateTodo(const Todo& todo);
    bool deleteTodo(int id);
    QList<Todo> getTodosByDate(const QDate& date);
    QList<Todo> getAllPendingTodos();
    QList<Todo> getAllTodos();

    // 授权管理
    QVariantMap getTrialStatus(bool validate = true);
    void resetActivation();
    bool verifyActivationCode(const QString& code);

    // 异步操作
    void addNoteAsync(const QString& title, const QString& content, const QStringList& tags = QStringList(),
                      const QString& color = "", int categoryId = -1,
                      const QString& itemType = "text", const QByteArray& dataBlob = QByteArray(),
                      const QString& sourceApp = "", const QString& sourceTitle = "",
                      const QString& remark = "");

    // 批量导入模式优化
    void beginBatch();
    void endBatch();
    void rollbackBatch();

    // 全局状态同步 (用于自动归档逻辑)
    bool isAutoCategorizeEnabled() const { return m_autoCategorizeEnabled; }
    void setAutoCategorizeEnabled(bool enabled);
    int activeCategoryId() const { return m_activeCategoryId; }
    void setActiveCategoryId(int id);

    QString getCategoryNameById(int id);
    /**
     * @brief 获取指定分类所属的最顶层主分类信息
     * [任务1] 用于右键菜单显示主分类名称
     */
    QVariantMap getRootCategory(int catId);
    
    // 应用锁定状态变更触发
    void notifyAppLockSettingsChanged() { emit appLockSettingsChanged(); }

signals:
    // 【修改】现在信号携带具体数据，实现增量更新
    void noteAdded(const QVariantMap& note);
    void noteUpdated(); // 用于普通刷新
    void categoriesChanged();
    void todoChanged();
    void autoCategorizeEnabledChanged(bool enabled);
    void activeCategoryIdChanged(int id);
    void appLockSettingsChanged();

private:
    DatabaseManager(QObject* parent = nullptr);
    ~DatabaseManager();
    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    bool createTables();
    void applySecurityFilter(QString& whereClause, QVariantList& params, const QString& filterType);
    void applyCommonFilters(QString& whereClause, QVariantList& params, const QString& filterType, const QVariant& filterValue, const QVariantMap& criteria);
    void backupDatabase();
    void backupDatabaseLatest();
    bool flushDatabase(const QString& source = "Unknown");
    bool tryRecoverFromBackup();

private slots:
    void handleAutoSave();

private:
    void markDirty();
    // 试用信息加密文件操作
    void saveTrialToFile(const QVariantMap& status);
    QVariantMap loadTrialFromFile();

    QSqlDatabase m_db;
    QString m_dbPath;      // 当前正在使用的内核路径 (.notes_core)
    QString m_realDbPath;  // 最终持久化的外壳路径 (notes.db)
    QString m_lastError;
    QRecursiveMutex m_mutex;

    QTimer* m_autoSaveTimer = nullptr;
    bool m_isDirty = false;
    QDateTime m_lastActivityTime;       // 最后一次数据变动的时间
    
    bool m_isBatchMode = false;
    bool m_isInitialized = false;
    QVariantMap m_cachedTrialStatus;

    QSet<int> m_unlockedCategories; // 仅存储当前会话已解锁的分类 ID
    
    bool m_autoCategorizeEnabled = false;
    int m_activeCategoryId = -1;
    bool m_lockedCategoriesHidden = false;

    // 标签剪贴板 (全局静态)
    static QStringList s_tagClipboard;
    static QMutex s_tagClipboardMutex;

public:
    static void setTagClipboard(const QStringList& tags);
    static QStringList getTagClipboard();
};

#endif // DATABASEMANAGER_H
