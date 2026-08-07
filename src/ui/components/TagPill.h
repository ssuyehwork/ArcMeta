#pragma once
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QPaintEvent>

namespace ArcMeta {

class TagPill : public QWidget {
    Q_OBJECT
public:
    explicit TagPill(const QString& text, QWidget* parent = nullptr);
    void setData(const QString& text);
signals:
    void deleteRequested(const QString& text);
protected:
    void paintEvent(QPaintEvent* event) override;
private:
    QString m_text;
    QLabel* m_label = nullptr;
    QPushButton* m_closeBtn = nullptr;
};

} // namespace ArcMeta
