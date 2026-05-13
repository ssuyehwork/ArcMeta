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
#include <QReadWriteLock>
#include <atomic>
#include <memory>

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

/**
 * @brief 虚拟化表格模型 (代理 MftReader)
 */
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
    void setEntries(QList<IndexedEntry>&& entries); // 保持签名兼容但内部空实现
    void applyChanges(const QList<UsnChange>& changes);

    // 2026-05-10 新增虚拟分页机制
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
    int m_displayLimit = 200; // 默认仅渲染前 200 条，其余动态加载
};

/**
 * @brief 极致扫描与查找对话框
 */
class ScanDialog : public FramelessDialog {
    Q_OBJECT
    friend class ScanTableModel; // 2026-05-10 物理修复：允许 Model 访问私有图标缓存
public:
    explicit ScanDialog(QWidget* parent = nullptr);
    ~ScanDialog() override;

private slots:
    void onStartScan();
    void onFilterOptionChanged();
    void onCustomContextMenu(const QPoint& pos);
    void onItemDoubleClicked(const QModelIndex& index);
    void onSelectionChanged();
    void onDriveContextMenu(QCheckBox* cb, const QPoint& pos);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void setupUi();

    // UI 组件
    QLineEdit* m_searchEdit = nullptr;
    QLineEdit* m_extEdit = nullptr;
    QCheckBox* m_checkRegex = nullptr;
    QCheckBox* m_checkCase = nullptr;
    QCheckBox* m_checkHidden = nullptr;
    QCheckBox* m_checkSystem = nullptr;
    QList<QCheckBox*> m_driveChecks;
    
    QTableView* m_resultView = nullptr;
    ScanTableModel* m_tableModel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_summaryLabel = nullptr;
    QLabel* m_selectionLabel = nullptr;
    QProgressBar* m_progressBar = nullptr;
    QPushButton* m_btnScan = nullptr;

    std::unique_ptr<CacheManager> m_cacheManager;
    QFileIconProvider m_iconProvider;

    // 2026-05-10 物理修复：移除静态全局缓存，改用成员变量管理图标生命周期
    mutable QHash<QString, QIcon> m_iconCache;
};

} // namespace ArcMeta