#pragma once

#include "FramelessDialog.h"
#include <vector>
#include <string>
#include <QString>
#include <QEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QShowEvent>

namespace ArcMeta {

class FlowLayout;
class TagPickerPopover;

/**
 * @brief 界面一：设置自动标签主对话框 (图 2)
 */
class CategoryPresetTagsDialog : public FramelessDialog {
    Q_OBJECT
public:
    explicit CategoryPresetTagsDialog(const QString& folderName,
                                     const std::vector<std::wstring>& initialTags,
                                     QWidget* parent = nullptr);
    ~CategoryPresetTagsDialog() override;

    /**
     * @brief 获取最终用户设置的预设标签列表
     */
    std::vector<std::wstring> getPresetTags() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onTagSelectedFromPicker(const QString& tagName);
    void onRemoveTag(const QString& tagName);

private:
    void initLayout();
    void addTagPill(const QString& tagName);

    // 核心信息
    QString m_folderName;
    std::vector<std::wstring> m_initialTags;

    // UI 组件
    QWidget* m_tagsContainer = nullptr;
    FlowLayout* m_tagsFlow = nullptr;
    TagPickerPopover* m_pickerPopover = nullptr;
};

} // namespace ArcMeta
