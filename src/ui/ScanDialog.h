#pragma once

#include "FramelessDialog.h"
#include "../core/IndexedEntry.h"
#include "../core/CacheManager.h"
#include "../mft/UsnWatcher.h"
#include <QListWidget>
#include <QCheckBox>
#include <QFrame>
#include <QProgressBar>
#include <QFuture>
#include <QFutureWatcher>
#include <QCloseEvent>
#include <QLineEdit>
#include <QTableView>
#include <QAbstractTableModel>
#include <QSortFilterProxyModel>
#include <QScrollArea>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QMap>
#include <QTimer>
#include <QReadWriteLock>
#include <QStackedWidget>
#include <QListView>
#include <QActionGroup>
#include <atomic>
#include <memory>

#include "../core/ScanController.h"

namespace ArcMeta {

struct ScanFilterState {
    QStringList extensionList; 
    bool useRegex = false;
    bool caseSensitive = false;
    bool includeHidden = true;
    bool includeSystem = true;

    bool isEmpty() const { 
        return extensionList.isEmpty() && !useRegex && !caseSensitive && includeHidden && includeSystem; 
    }
};

class IDataProvider {
public:
    virtual ~IDataProvider() = default;
    virtual int totalCount() const = 0;
    virtual QVector<int> search(const QString& query, const ScanFilterState& state) = 0;
    virtual QString getName(int index) const = 0;
    virtual QString getFullPath(int index) const = 0;
    virtual int64_t getSize(int index) const = 0;
    virtual int64_t getModifyTime(int index) const = 0;
    virtual bool isDirectory(int index) const = 0;
    virtual QIcon getCachedIcon(const QString& ext, bool isDir) = 0;
    virtual bool isMetadataFetched(int index) const = 0;
    virtual void requestMetadata(int index) = 0;
};

class ScanTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit ScanTableModel(IDataProvider* provider, QObject* parent = nullptr);
    ~ScanTableModel() override;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;

    void setFilterText(const QString& text);
    void setFilterState(const ScanFilterState& state);
    void triggerSearch();

    bool canFetchMore(const QModelIndex& parent) const override;
    void fetchMore(const QModelIndex& parent) override;

    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;
    int totalFilteredCount() const { return m_filteredIndices.size(); }

    Qt::DropActions supportedDragActions() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;

signals:
    void filterFinished(int count);

private:
    void startAsyncRebuild();
    QVector<int> performRebuild(const QString& filterText, const ScanFilterState& filterState);

    QVector<int> m_filteredIndices;
    QString m_filterText;
    ScanFilterState m_filterState;
    IDataProvider* m_provider = nullptr;
    QFutureWatcher<QVector<int>> m_filterWatcher;
    std::unordered_map<int, int> m_actualToRow; // 2026-05-14 新增：O(1) 定位索引
    QSet<int> m_pendingRows;  // 2026-05-14 信号聚合：待刷新的行号集合
    QTimer* m_throttleTimer = nullptr;
    int m_displayLimit = 1000000; // 2026-06-xx 性能解封：对标 Rust 版支持百万级即时显示
};

class ScanDialog : public FramelessDialog {
    Q_OBJECT
    friend class ScanTableModel;
public:
    explicit ScanDialog(QWidget* parent = nullptr);
    ~ScanDialog() override;

private slots:
    void onStartScan();
    void onTriggerSearch();
    void onFilterOptionChanged();
    void onCustomContextMenu(const QPoint& pos);
    void onItemDoubleClicked(const QModelIndex& index);
    void onSelectionChanged();
    void onDriveContextMenu(const QString& drive, const QPoint& pos);
    void onIgnoredDriveContextMenu(const QString& drive, const QPoint& pos);
    void onRenameTriggered();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setupUi();
    void updateDriveButtonStyles();
    void updateStatus(const QString& text, bool scanning = false);
    void updateStatusBar();
    void handleMetadataShortcut(QKeyEvent* event);
    QString formatNumber(int64_t n);
    QString formatSize(int64_t bytes);

    QMap<QString, QPushButton*> m_driveButtonMap;

    QLineEdit* m_searchEdit = nullptr;
    QLineEdit* m_extEdit = nullptr;
    QPushButton* m_searchBtn = nullptr;
    QCheckBox* m_checkRegex = nullptr;
    QCheckBox* m_checkCase = nullptr;
    QCheckBox* m_checkHidden = nullptr;
    QCheckBox* m_checkSystem = nullptr;
    
    QHBoxLayout* m_driveLayout = nullptr;
    QWidget* m_driveContainer = nullptr;
    
    QTableView* m_resultView = nullptr;
    QListView* m_iconView = nullptr;
    QStackedWidget* m_viewStack = nullptr;
    ScanTableModel* m_tableModel = nullptr;

    QPushButton* m_prevBtn = nullptr;
    QPushButton* m_nextBtn = nullptr;
    QLabel* m_pageLabel = nullptr;
    int m_currentPage = 0;
    const int m_pageSize = 1000;

    QLabel* m_titleStatusLabel = nullptr; 
    QLabel* m_statLabelMain = nullptr;    
    QLabel* m_statLabelTime = nullptr;    
    QLabel* m_statLabelMemory = nullptr; 
    QLabel* m_selectionLabel = nullptr;  
    QPushButton* m_csvBtn = nullptr;     
    QProgressBar* m_progressBar = nullptr;

    int64_t m_lastSearchMs = 0;
    QTimer* m_searchThrottleTimer = nullptr;

    std::unique_ptr<CacheManager> m_cacheManager;
};

} // namespace ArcMeta
