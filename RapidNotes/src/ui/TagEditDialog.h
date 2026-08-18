#ifndef TAGEDITDIALOG_H
#define TAGEDITDIALOG_H

#include "FramelessDialog.h"
#include "TagEditorWidget.h"

class TagEditDialog : public FramelessDialog {
    Q_OBJECT
public:
    explicit TagEditDialog(const QString& currentTags, QWidget* parent = nullptr);
    QString getTags() const;

signals:
    void tagsConfirmed(const QString& tags);

private slots:
    void openTagSelector();

private:
    TagEditorWidget* m_tagEditor;
};

#endif // TAGEDITDIALOG_H
