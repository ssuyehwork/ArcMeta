#pragma once

#include "FramelessDialog.h"
#include "../meta/DuplicateDetectorService.h"
#include <QRadioButton>
#include <QCheckBox>
#include <QPushButton>
#include <QButtonGroup>

class QWidget;

namespace ArcMeta {

enum class DuplicateResolveAction {
    UseExisting, // 使用已存在文件导入
    KeepBoth     // 保留两者
};

class DuplicateConflictDialog : public FramelessDialog {
    Q_OBJECT
public:
    // totalCount 代表冲突组总数 N（用于显示标题与复选框文本）
    explicit DuplicateConflictDialog(const DuplicateConflictGroup& conflict, int totalCount, QWidget* parent = nullptr);
    explicit DuplicateConflictDialog(const DuplicateConflictGroup& conflict, QWidget* parent = nullptr);

    DuplicateResolveAction selectedAction() const;
    bool applyToAll() const; // 返回是否勾选了“全部应用(N)”

private:
    QRadioButton* m_radUseExisting = nullptr;
    QRadioButton* m_radKeepBoth = nullptr;
    QCheckBox* m_chkApplyToAll = nullptr; // 对应用户截图中的全部应用复选框
    QPushButton* m_btnSubmit = nullptr;
};

} // namespace ArcMeta
