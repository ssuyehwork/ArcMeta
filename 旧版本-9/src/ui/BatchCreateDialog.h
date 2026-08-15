#pragma once

#include "FramelessDialog.h"
#include <QPlainTextEdit>
#include <QStringList>

#include "CreateRuleRow.h"
#include <QSpinBox>
#include <QComboBox>
#include <QLineEdit>
#include <QScrollArea>
#include <QTimer>

namespace ArcMeta {

class BatchCreateDialog : public FramelessDialog {
    Q_OBJECT
public:
    explicit BatchCreateDialog(const QString& currentDirectory, QWidget* parent = nullptr);
    ~BatchCreateDialog() override = default;

    bool isFile() const;
    QString fileSuffix() const;
    QStringList renderAllNames() const;

private slots:
    void scheduleAutoSave();
    void doAutoSave();

private:
    void initContent();
    void onExecute();
    void onInsertRowAfter(CreateRuleRow* targetRow = nullptr);
    void applyTheme();
    QString renderOne(int index, const std::vector<RenameRule>& rules) const;

    QString m_currentDir;
    
    QSpinBox* m_countSpin = nullptr;
    QComboBox* m_typeCombo = nullptr; // 文件夹 / 文件
    QLineEdit* m_suffixEdit = nullptr; // 后缀名
    
    QWidget* m_rulesContainer = nullptr;
    QVBoxLayout* m_rulesLayout = nullptr;
    QList<CreateRuleRow*> m_ruleRows;

    QTimer* m_autoSaveTimer = nullptr;
};

} // namespace ArcMeta
