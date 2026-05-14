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
#include <QReadWriteLock>
#include <atomic>
#include <memory>

namespace ArcMeta {

/**
 * @brief 简单的配置结构，对标 Rust 的 AppConfig
 */
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
    void refreshDriveList();
    void updateStatus(const QString& text, bool scanning = false);
    void updateStatusBar();
    QString formatNumber(int64_t n);
    QString formatSize(int64_t bytes);

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

    // 2026-05-14 对标原版界面组件 (物理对标 P0：确保变量名唯一，防止指针覆盖导致闪退)
    QLabel* m_titleStatusLabel = nullptr; // 标题栏 "READY - 0"
    QLabel* m_statLabelMain = nullptr;    // 状态栏 "共 X 条 | 本页 Y 条"
    QLabel* m_statLabelTime = nullptr;    // 状态栏 "耗时 X ms"
    QLabel* m_statLabelMemory = nullptr; // 状态栏 "数据占用 X MB"
    QLabel* m_selectionLabel = nullptr;  // 状态栏 "已选 X 项 | 合计大小 Z"
    QPushButton* m_csvBtn = nullptr;     // 状态栏 "导出所选为 CSV"
    QProgressBar* m_progressBar = nullptr;

    int64_t m_lastSearchMs = 0;

    std::unique_ptr<CacheManager> m_cacheManager;
    QFileIconProvider m_iconProvider;
    mutable QHash<QString, QIcon> m_iconCache;

    ScanConfig m_config;
};

} // namespace ArcMeta
