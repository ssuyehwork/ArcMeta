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

namespace QuarkMeta {

class BatchCreateDialog : public FramelessDialog {
    Q_OBJECT
public:
    explicit BatchCreateDialog(const QString& currentDirectory, bool isMemoryMode = false, QWidget* parent = nullptr);
    ~BatchCreateDialog() override = default;

    bool isFile() const;
    QString fileSuffix() const;
    QStringList renderAllNames() const;
    QString selectedLibraryPath() const;

private slots:
    void scheduleAutoSave();
    void doAutoSave();

private:
    void initContent();
    void onExecute();
    void onInsertRowAfter(CreateRuleRow* targetRow = nullptr);
    void applyTheme();
    void updateLibraryControlState();
    QString renderOne(int index, const std::vector<RenameRule>& rules) const;

    QString m_currentDir;
    bool m_isMemoryMode = false;
    
    QSpinBox* m_countSpin = nullptr;
    QComboBox* m_typeCombo = nullptr; // 文件夹 / 文件
    QLineEdit* m_suffixEdit = nullptr; // 后缀名

    QWidget* m_libraryGroupWidget = nullptr; // 资源库整组容器
    QComboBox* m_libraryCombo = nullptr;     // 资源库下拉框
    QPushButton* m_btnOk = nullptr;          // 确定按钮
    
    QWidget* m_rulesContainer = nullptr;
    QVBoxLayout* m_rulesLayout = nullptr;
    QList<CreateRuleRow*> m_ruleRows;

    QTimer* m_autoSaveTimer = nullptr;
};

} // namespace QuarkMeta
