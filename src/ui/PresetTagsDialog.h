#pragma once

#include "FramelessDialog.h"
#include "components/FlowLayout.h"
#include "components/TagPill.h"
#include "TagSelectorOverlay.h"
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QFrame>
#include <QPointer>

namespace QuarkMeta {

class PresetTagsDialog : public FramelessDialog {
    Q_OBJECT
public:
    explicit PresetTagsDialog(int categoryId, QWidget* parent = nullptr);
    ~PresetTagsDialog() override;

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private slots:
    void onSaveClicked();
    void onCancelClicked();
    void onTagContainerClicked();
    void onTagDeleted(const QString& tag);

private:
    void initUi();
    void loadTags();
    void populateTagPills();
    void recalculateAdaptiveHeight();

    int m_categoryId;
    QString m_categoryName;
    QStringList m_presetTags;

    QLineEdit* m_folderNameEdit = nullptr;
    QFrame* m_tagContainer = nullptr;
    FlowLayout* m_flowLayout = nullptr;
    QPointer<TagSelectorOverlay> m_selectorOverlay;
};

} // namespace QuarkMeta
