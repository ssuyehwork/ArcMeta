#pragma once

#include <QWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QStackedWidget>
#include <QPushButton>
#include <QHBoxLayout>
#include "../meta/BatchRenameEngine.h"

namespace ArcMeta {

/**
 * @brief 批量创建专属规则行组件（无原文件名选项）
 */
class CreateRuleRow : public QWidget {
    Q_OBJECT
public:
    explicit CreateRuleRow(QWidget* parent = nullptr);
    
    RenameRule getRule() const;
    void setRule(const RenameRule& rule);

signals:
    void changed();
    void addRequested();
    void removeRequested();

private:
    void initUi();

    QComboBox* m_typeCombo = nullptr;
    QStackedWidget* m_paramStack = nullptr;
    
    // Params for Text
    QLineEdit* m_textEdit = nullptr;
    
    // Params for Sequence
    QSpinBox* m_startSpin = nullptr;
    QComboBox* m_paddingCombo = nullptr;
    
    // Params for Date
    QComboBox* m_dateFormatCombo = nullptr;
    
    QPushButton* m_btnAdd = nullptr;
    QPushButton* m_btnRemove = nullptr;
};

} // namespace ArcMeta
