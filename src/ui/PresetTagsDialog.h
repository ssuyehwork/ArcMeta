#pragma once

#include "FramelessDialog.h"
#include "components/FlowLayout.h"
#include "components/TagPill.h"
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QFrame>

namespace ArcMeta {

class TagSelectorOverlay;

class PresetTagsDialog : public FramelessDialog {
    Q_OBJECT
public:
    explicit PresetTagsDialog(int categoryId, QWidget* parent = nullptr);
    ~PresetTagsDialog() override;

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void onSaveClicked();
    void onCancelClicked();
    void onTagContainerClicked();
    void onTagDeleted(const QString& tag);

private:
    void initUi();
    void loadTags();
    void populateTagPills();
    void updateDialogHeight();

    int m_categoryId;
    QString m_categoryName;
    QStringList m_presetTags;

    QLineEdit* m_folderNameEdit = nullptr;
    QFrame* m_tagContainer = nullptr;
    FlowLayout* m_flowLayout = nullptr;
    TagSelectorOverlay* m_selectorOverlay = nullptr;
};

} // namespace ArcMeta
