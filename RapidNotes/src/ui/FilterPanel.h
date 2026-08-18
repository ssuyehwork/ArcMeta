#ifndef FILTERPANEL_H
#define FILTERPANEL_H

#include <QWidget>
#include <QVariantMap>
#include <QVBoxLayout>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QPushButton>
#include <QLabel>
#include <QMouseEvent>
#include <QGraphicsDropShadowEffect>
#include <QFutureWatcher>

class FilterPanel : public QWidget {
    Q_OBJECT
public:
    explicit FilterPanel(QWidget* parent = nullptr);
    void updateStats(const QString& keyword, const QString& type, const QVariant& value);
    QVariantMap getCheckedCriteria() const;
    void resetFilters();
    void toggleAllGroups(); // [NEW] 2026-04-xx 快捷键触发折叠/展开

signals:
    void filterChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void initUI();
    void setupTree();
    void updateToggleIcon();
    void onStatsReady();
    void onItemChanged(QTreeWidgetItem* item, int column);
    void onItemClicked(QTreeWidgetItem* item, int column);
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);
    void refreshNode(const QString& key, const QList<QVariantMap>& items, bool isCol = false);

    QWidget* m_container = nullptr;
    QTreeWidget* m_tree = nullptr;
    QPushButton* m_btnReset = nullptr;
    QPushButton* m_btnToggle = nullptr;

    QMap<QString, QTreeWidgetItem*> m_roots;
    bool m_blockItemClick = false;
    QTreeWidgetItem* m_lastChangedItem = nullptr;

    QFutureWatcher<QVariantMap> m_statsWatcher;
    QString m_pendingKeyword;
    QString m_pendingType;
    QVariant m_pendingValue;
};

#endif // FILTERPANEL_H
