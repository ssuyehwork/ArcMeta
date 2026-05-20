#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "JustifiedView.h"
#include <QPainter>
#include <QScrollBar>
#include <QResizeEvent>
#include <QStyleOptionViewItem>
#include <QAbstractItemDelegate>
#include <algorithm>

namespace ArcMeta {

JustifiedView::JustifiedView(QWidget* parent) : QAbstractItemView(parent) {
    horizontalScrollBar()->setRange(0, 0);
    verticalScrollBar()->setSingleStep(20);
    // 2026-05-20 物理修复：显式设置视口背景，防止 QSS 穿透导致的重绘异常
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent);
}

void JustifiedView::setTargetRowHeight(int h) {
    if (m_targetRowHeight != h) {
        m_targetRowHeight = h;
        doLayout();
    }
}

QRect JustifiedView::visualRect(const QModelIndex& index) const {
    if (!index.isValid() || index.row() >= (int)m_geometries.size()) return QRect();
    QRect r = m_geometries[index.row()].rect;
    r.translate(0, -verticalScrollBar()->value());
    return r;
}

void JustifiedView::scrollTo(const QModelIndex& index, ScrollHint hint) {
    QRect rect = visualRect(index);
    if (rect.isEmpty()) return;

    int viewportHeight = viewport()->height();
    int scrollValue = verticalScrollBar()->value();

    if (hint == EnsureVisible) {
        if (rect.top() < 0) verticalScrollBar()->setValue(scrollValue + rect.top());
        else if (rect.bottom() > viewportHeight) verticalScrollBar()->setValue(scrollValue + rect.bottom() - viewportHeight);
    }
}

QModelIndex JustifiedView::indexAt(const QPoint& point) const {
    int y = point.y() + verticalScrollBar()->value();
    for (const auto& geo : m_geometries) {
        if (geo.rect.contains(point.x(), y)) {
            return model()->index(geo.index, 0);
        }
    }
    return QModelIndex();
}

void JustifiedView::dataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight, const QList<int>& roles) {
    // 2026-05-20 逻辑对标：仅当宽高比（UserRole+2）变化时才触发昂贵的全局重排
    if (roles.isEmpty() || roles.contains(Qt::UserRole + 2)) {
        doLayout();
    }
    QAbstractItemView::dataChanged(topLeft, bottomRight, roles);
}

void JustifiedView::rowsInserted(const QModelIndex& parent, int start, int end) {
    doLayout();
    QAbstractItemView::rowsInserted(parent, start, end);
}

void JustifiedView::rowsAboutToBeRemoved(const QModelIndex& parent, int start, int end) {
    doLayout();
    QAbstractItemView::rowsAboutToBeRemoved(parent, start, end);
}

QModelIndex JustifiedView::moveCursor(CursorAction cursorAction, Qt::KeyboardModifiers) {
    QModelIndex current = currentIndex();
    if (!current.isValid()) return model()->index(0, 0);

    int row = current.row();
    switch (cursorAction) {
        case MoveLeft: row--; break;
        case MoveRight: row++; break;
        case MoveUp: row -= 5; break;
        case MoveDown: row += 5; break;
        default: break;
    }
    row = std::max(0, std::min(row, (int)model()->rowCount() - 1));
    return model()->index(row, 0);
}

int JustifiedView::horizontalOffset() const { return 0; }
int JustifiedView::verticalOffset() const { return verticalScrollBar()->value(); }
bool JustifiedView::isIndexHidden(const QModelIndex&) const { return false; }

void JustifiedView::setSelection(const QRect& rect, QItemSelectionModel::SelectionFlags command) {
    QRect contentsRect = rect.translated(0, verticalScrollBar()->value());
    QItemSelection selection;
    for (const auto& geo : m_geometries) {
        if (geo.rect.intersects(contentsRect)) {
            QModelIndex idx = model()->index(geo.index, 0);
            selection.select(idx, idx);
        }
    }
    selectionModel()->select(selection, command);
}

QRegion JustifiedView::visualRegionForSelection(const QItemSelection& selection) const {
    QRegion region;
    for (const auto& range : selection) {
        for (int i = range.top(); i <= range.bottom(); ++i) {
            region += visualRect(model()->index(i, 0));
        }
    }
    return region;
}

void JustifiedView::paintEvent(QPaintEvent*) {
    QPainter painter(viewport());
    painter.translate(0, -verticalScrollBar()->value());

    // 2026-05-20 物理修复：使用 initViewItemOption 替代 viewOptions 以符合 Qt 6 规范
    for (int i = 0; i < (int)m_geometries.size(); ++i) {
        const auto& geo = m_geometries[i];
        if (geo.rect.bottom() < verticalScrollBar()->value()) continue;
        if (geo.rect.top() > verticalScrollBar()->value() + viewport()->height()) break;

        QModelIndex idx = model()->index(geo.index, 0);
        QStyleOptionViewItem option;
        initViewItemOption(&option); // 正确初始化基类选项
        option.rect = geo.rect;

        if (selectionModel()->isSelected(idx))
            option.state |= QStyle::State_Selected;
        if (currentIndex() == idx)
            option.state |= QStyle::State_HasFocus;

        itemDelegate(idx)->paint(&painter, option, idx);
    }
}

void JustifiedView::resizeEvent(QResizeEvent* event) {
    doLayout();
    QAbstractItemView::resizeEvent(event);
}

void JustifiedView::updateGeometries() {
    verticalScrollBar()->setPageStep(viewport()->height());
    verticalScrollBar()->setRange(0, std::max(0, m_totalHeight - viewport()->height()));
    QAbstractItemView::updateGeometries();
}

void JustifiedView::doLayout() {
    if (!model()) return;
    int count = model()->rowCount();
    if (count == 0) {
        m_geometries.clear();
        m_totalHeight = 0;
        updateGeometries();
        viewport()->update();
        return;
    }

    m_geometries.resize(count);
    int containerWidth = viewport()->width() - 20;
    if (containerWidth <= 0) return;

    int currentY = 10;
    int i = 0;

    while (i < count) {
        int rowStart = i;
        double rowAspectRatioSum = 0;
        std::vector<double> aspectRatios;

        while (i < count) {
            double ar = model()->data(model()->index(i, 0), Qt::UserRole + 2).toDouble();
            if (ar <= 0) ar = 1.0;

            aspectRatios.push_back(ar);
            rowAspectRatioSum += ar;

            double estimatedWidth = rowAspectRatioSum * m_targetRowHeight;
            if (estimatedWidth > containerWidth) {
                i++;
                break;
            }
            i++;
        }

        int rowEnd = i;
        int numInRow = rowEnd - rowStart;
        int actualHeight = m_targetRowHeight;
        bool isLastRow = (i == count);

        if (!isLastRow || (rowAspectRatioSum * m_targetRowHeight > containerWidth * 0.8)) {
            actualHeight = qRound(containerWidth / rowAspectRatioSum);
        }

        if (actualHeight > m_targetRowHeight * 1.5) actualHeight = m_targetRowHeight * 1.5;

        int currentX = 10;
        for (int j = 0; j < numInRow; ++j) {
            int itemIdx = rowStart + j;
            int itemWidth = qRound(aspectRatios[j] * actualHeight);

            if (j == numInRow - 1 && !isLastRow) {
                itemWidth = containerWidth + 10 - currentX;
            }

            m_geometries[itemIdx] = { QRect(currentX, currentY, itemWidth, actualHeight), itemIdx };
            currentX += itemWidth + 5;
        }
        currentY += actualHeight + 5;
    }

    m_totalHeight = currentY + 10;
    updateGeometries();
    viewport()->update();
}

} // namespace ArcMeta
