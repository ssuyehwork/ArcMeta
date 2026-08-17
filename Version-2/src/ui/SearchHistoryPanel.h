#pragma once

#include <QFrame>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStringList>

namespace ArcMeta {

/**
 * @brief 搜索历史悬浮面板
 */
class SearchHistoryPanel : public QFrame {
    Q_OBJECT

public:
    explicit SearchHistoryPanel(QWidget* parent = nullptr);

    void setCategory(const QString& category) { m_category = category; }
    QString category() const { return m_category; }

    void setHistory(const QStringList& history, const QString& title = "最近搜索");
    void showBelow(QWidget* anchor);

signals:
    void historyItemClicked(const QString& keyword);

private slots:
    void onSearchHistoryChanged(const QString& category, const QStringList& newHistory);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void rebuild();

    QVBoxLayout* m_layout   = nullptr;
    QStringList  m_history;
    QString      m_currentTitle = "最近搜索";
    QString      m_category = "global";
};

} // namespace ArcMeta
