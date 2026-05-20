#ifndef SEARCHLINEEDIT_H
#define SEARCHLINEEDIT_H

#include <QLineEdit>
#include <QMouseEvent>

class SearchHistoryPopup;

class SearchLineEdit : public QLineEdit {
    Q_OBJECT
public:
    explicit SearchLineEdit(QWidget* parent = nullptr);
    void addHistoryEntry(const QString& text);
    QStringList getHistory() const;
    void clearHistory();
    void removeHistoryEntry(const QString& text);

    void setHistoryKey(const QString& key) { m_historyKey = key; }
    void setHistoryTitle(const QString& title) { m_historyTitle = title; }
    QString historyKey() const { return m_historyKey; }
    QString historyTitle() const { return m_historyTitle; }

protected:
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    void showPopup();
    SearchHistoryPopup* m_popup = nullptr;
    QString m_historyKey = "SearchHistory";
    QString m_historyTitle = "搜索历史";
};

#endif // SEARCHLINEEDIT_H
