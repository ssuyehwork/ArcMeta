#pragma once

#include "FramelessDialog.h"
#include <QPlainTextEdit>
#include <QStringList>

#include "RuleRow.h"
#include <QSpinBox>
#include <QComboBox>
#include <QLineEdit>
#include <QScrollArea>

namespace ArcMeta {

class BatchCreateDialog : public FramelessDialog {
    Q_OBJECT
public:
    explicit BatchCreateDialog(const QString& currentDirectory, QWidget* parent = nullptr);
    ~BatchCreateDialog() override = default;

private:
    void initContent();
    void onExecute();
    void onAddRow();
    void applyTheme();
    QString renderOne(int index, const std::vector<RenameRule>& rules);

    QString m_currentDir;

    QSpinBox* m_countSpin = nullptr;
    QComboBox* m_typeCombo = nullptr; // 文件夹 / 文件
    QLineEdit* m_suffixEdit = nullptr; // 后缀名 (.txt 等)

    QWidget* m_rulesContainer = nullptr;
    QVBoxLayout* m_rulesLayout = nullptr;
    QList<RuleRow*> m_ruleRows;
};

} // namespace ArcMeta
