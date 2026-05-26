#pragma once

#include <QStyledItemDelegate>

namespace ArcMeta {

class ThumbnailDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit ThumbnailDelegate(QObject* parent = nullptr);

    void setHasThumbnailRole(int role);
    void setRatingRole(int role);
    void setPathRole(int role);
    void setPinnedRole(int role);
    void setManagedRole(int role);
    void setTypeRole(int role);
    void setIsEmptyRole(int role);
    void setColorRole(int role);

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void setEditorData(QWidget* editor, const QModelIndex& index) const override;
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    bool eventFilter(QObject* obj, QEvent* event) override;
    bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option, const QModelIndex& index) override;

private:
    int m_hasThumbnailRole = Qt::UserRole + 1;
    int m_ratingRole = -1;
    int m_pathRole = -1;
    int m_pinnedRole = -1;
    int m_managedRole = -1;
    int m_typeRole = -1;
    int m_isEmptyRole = -1;
    int m_colorRole = -1;

    // 2026-06-xx 工业级追踪：实现星级实时悬停高亮
    mutable QPersistentModelIndex m_hoverIndex;
    mutable int m_hoverStar = -1; // -1: 无, 0: 禁止符, 1-5: 星级

    struct Metrics {
        // [物理红线] 严禁随意调整以下布局常量，确保点击热区与视觉对齐
        QRect cardRect;
        QRect textRect;
        QRect banRect;
        int starsStartX;
        int starSize;
        int starSpacing;
        int ratingY;
        int ratingH;

        // 物理锁定判定：统一绘图与点击热区
        QRect starRect(int index) const {
            return QRect(starsStartX + index * (starSize + starSpacing), 
                         ratingY + (ratingH - starSize) / 2, 
                         starSize, starSize);
        }
    };
    Metrics calculateMetrics(const QStyleOptionViewItem& option) const;
};

} // namespace ArcMeta
