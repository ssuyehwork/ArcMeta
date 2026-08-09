#pragma once

#include "FramelessDialog.h"
#include "../meta/DuplicateDetectorService.h"
#include <QRadioButton>
#include <QPushButton>

namespace ArcMeta {

enum class DuplicateResolveAction {
    UseExisting, // 使用已存在文件导入（放弃写入新文件，仅做关联）
    KeepBoth     // 保留两者
};

class DuplicateConflictDialog : public FramelessDialog {
    Q_OBJECT
public:
    explicit DuplicateConflictDialog(const DuplicateConflictGroup& conflict, QWidget* parent = nullptr);

    DuplicateResolveAction selectedAction() const;

private:
    QRadioButton* m_radUseExisting = nullptr;
    QRadioButton* m_radKeepBoth = nullptr;
    QPushButton* m_btnSubmit = nullptr;
};

} // namespace ArcMeta
