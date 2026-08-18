#pragma once

#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QTimer>
#include <QEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include "components/ElasticEdit.h"
#include "components/TagPill.h"
#include "components/FlowLayout.h"
#include "components/ColorPill.h"

namespace QuarkMeta {

class MetaPanel : public QFrame {
    Q_OBJECT
public:
    explicit MetaPanel(QWidget* parent = nullptr);
    ~MetaPanel() override = default;

    void updateInfo(const QString& name, const QString& type, const QString& size,
                    const QString& ctime, const QString& mtime, const QString& atime,
                    const QString& path, bool encrypted, int width = 0, int height = 0);

    void setSelectedPaths(const QStringList& paths) { m_selectedPaths = paths; }
    void setPalettes(const QVector<QPair<QColor, float>>& palette);
    void setTags(const QStringList& tags);
    void setNote(const QString& note);
    void setNote(const std::wstring& note);
    void setURL(const QString& url);
    void setURL(const std::wstring& url);
    void setCategory(const QString& category);
    void setCategoryPills(const std::vector<std::pair<int, QString>>& categories);
    void setDiskPathMode(bool isDiskMode, const QString& rawPath);

    // 兼容层占位
    void setRating(int rating) { Q_UNUSED(rating); }
    void setColor(const std::wstring& color) { Q_UNUSED(color); }
    void setPinned(bool pinned) { Q_UNUSED(pinned); }

signals:
    void tagAddRequested(const QStringList& paths, const QString& tag);
    void tagRemoveRequested(const QStringList& paths, const QString& tag);
    void metadataChanged(int rating, const std::wstring& color);
    void noteEdited(const QStringList& paths, const QString& newNote);
    void linkEdited(const QStringList& paths, const QString& newLink);
    void primaryColorChanged(const QString& path, const QColor& color);
    void tagsChanged(const QStringList& paths, const QStringList& tags);
    void searchByColor(const QColor& color);
    void renameRequested(const QString& oldPath, const QString& newPath);
    void unbindCategoryRequested(const QString& path, int categoryId);
    void bindCategoryRequested(const QString& path);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void initUi();
    void adjustFlowHeights();
    void addInfoRow(const QString& label, QLabel*& valueLabel);
    QFrame* createSeparator();

    QVBoxLayout* m_mainLayout = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_container = nullptr;
    QVBoxLayout* m_containerLayout = nullptr;
    
    ElasticEdit* m_nameEdit = nullptr;
    QLabel* lblType = nullptr, *lblSize = nullptr, *lblDimensions = nullptr;
    QLabel* lblCtime = nullptr, *lblMtime = nullptr, *lblAtime = nullptr;
    ElasticEdit* m_pathEdit = nullptr;
    QLabel* lblEncrypted = nullptr;
    
    QWidget* m_paletteBox = nullptr;
    FlowLayout* m_paletteFlowLayout = nullptr;
    
    QWidget* m_tagBox = nullptr;
    QWidget* m_tagContainer = nullptr;
    FlowLayout* m_tagFlowLayout = nullptr;
    ElasticEdit* m_tagEdit = nullptr;
    
    ElasticEdit* m_noteEdit = nullptr;
    ElasticEdit* m_linkEdit = nullptr;
    ElasticEdit* m_categoryEdit = nullptr;
    QVBoxLayout* m_categoryLayoutBox = nullptr;

    bool m_isDiskNavMode = false;
    QWidget* m_categoryBox = nullptr;
    QWidget* m_categoryContainer = nullptr;
    FlowLayout* m_categoryFlowLayout = nullptr;

    QStringList m_selectedPaths;
    QList<TagPill*> m_tagPool;
    QList<ColorPill*> m_colorPool;
    QTimer* m_adjustTimer = nullptr;
    bool m_isInternalUpdating = false;
    bool m_isUserEditing = false; // 增加编辑态锁，防护焦点与异步刷新冲刷

private slots:
    void onTagAdded();
    void onTagDeleted(const QString& text);
    void setAsPrimaryColor(const QColor& color);
};

} // namespace QuarkMeta
