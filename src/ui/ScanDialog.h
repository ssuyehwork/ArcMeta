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
#include <QReadWriteLock>
#include <atomic>
#include <memory>

namespace ArcMeta {

struct ScanConfig {
    QSet<QString> activeDrives;
    QSet<QString> defaultDrives;
    QSet<QString> ignoredDrives;
    QStringList queryHistory;
    QStringList extHistory;

    void load();
    void save();
};

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

class ScanTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit ScanTableModel(QObject* parent = nullptr);
    ~ScanTableModel() override;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void setFilterText(const QString& text);
    void setFilterState(const ScanFilterState& state);
    void triggerSearch();
    void loadMore(int count = 200);
    int totalFilteredCount() const { return m_filteredIndices.size(); }

signals:
    void filterFinished(int count);

private:
    void startAsyncRebuild();
    QVector<int> performRebuild(const QString& filterText, const ScanFilterState& filterState);

    QVector<int> m_filteredIndices;
    QString m_filterText;
    ScanFilterState m_filterState;
    QFutureWatcher<QVector<int>> m_filterWatcher;
    int m_displayLimit = 200;
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
    void refreshDriveList(bool forceProbe = false);
    void updateDriveButtonStyles();
    void updateStatus(const QString& text, bool scanning = false);
    void updateStatusBar();
    QString formatNumber(int64_t n);
    QString formatSize(int64_t bytes);

    struct DriveInfo {
        QString letter;
        QString label;
        bool isNtfs;
        bool hasMedia;
    };
    QVector<DriveInfo> m_cachedDriveInfos;
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
    ScanTableModel* m_tableModel = nullptr;

    QLabel* m_titleStatusLabel = nullptr; 
    QLabel* m_statLabelMain = nullptr;    
    QLabel* m_statLabelTime = nullptr;    
    QLabel* m_statLabelMemory = nullptr; 
    QLabel* m_selectionLabel = nullptr;  
    QPushButton* m_csvBtn = nullptr;     
    QProgressBar* m_progressBar = nullptr;

    int64_t m_lastSearchMs = 0;

    std::unique_ptr<CacheManager> m_cacheManager;
    QFileIconProvider m_iconProvider;
    mutable QHash<QString, QIcon> m_iconCache;
    mutable QReadWriteLock m_iconCacheLock;

    ScanConfig m_config;
};

} // namespace ArcMeta
