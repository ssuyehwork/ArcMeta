#pragma once

#include "FramelessDialog.h"
#include <QPlainTextEdit>
#include <QStringList>

namespace ArcMeta {

class BatchCreateDialog : public FramelessDialog {
    Q_OBJECT
public:
    explicit BatchCreateDialog(const QString& currentDirectory, QWidget* parent = nullptr);
    ~BatchCreateDialog() override = default;

private:
    void initContent();
    void onExecute();

    QString m_currentDir;
    QPlainTextEdit* m_textEdit = nullptr;
};

} // namespace ArcMeta
