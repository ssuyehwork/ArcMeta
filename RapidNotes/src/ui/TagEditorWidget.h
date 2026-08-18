#ifndef TAGEDITORWIDGET_H
#define TAGEDITORWIDGET_H

#include <QFrame>
#include <QStringList>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include "FlowLayout.h"

class TagEditorWidget : public QFrame {
    Q_OBJECT
public:
    explicit TagEditorWidget(QWidget* parent = nullptr);

    void setTags(const QStringList& tags);
    QStringList tags() const { return m_tags; }
    
    void addTag(const QString& tag);
    void removeTag(const QString& tag);
    void clear();

signals:
    void tagsChanged();
    void doubleClicked();

protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    void updateChips();
    QWidget* createChip(const QString& tag);

    QStringList m_tags;
    FlowLayout* m_flow;
};

#endif // TAGEDITORWIDGET_H
