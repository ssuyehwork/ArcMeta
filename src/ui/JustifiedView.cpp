#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "JustifiedView.h"
#include <QPainter>
#include <QScrollBar>
#include <QResizeEvent>
#include <QStyleOptionViewItem>
#include <QAbstractItemDelegate>
#include <QTimer>
#include <algorithm>

namespace ArcMeta {

JustifiedView::JustifiedView(QWidget* parent) : QAbstractItemView(parent) {
    horizontalScrollBar()->setRange(0, 0);
    verticalScrollBar()->setSingleStep(20);
    
    // 2026-06-xx 物理加固：彻底消除背景穿透。
    setAutoFillBackground(true);
    viewport()->setAutoFillBackground(true);
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent);
    
    QPalette pal = viewport()->palette();
    pal.setColor(QPalette::Window, QColor("#1E1E1E"));
    viewport()->setPalette(pal);
    setPalette(pal);
}

void JustifiedView::setTargetRowHeight(int h) {
    if (m_targetRowHeight != h) {
        m_targetRowHeight = h;
        doLayout();
    }
}

void JustifiedView::reset() {
    QAbstractItemView::reset();
    doLayout();
}

void JustifiedView::doItemsLayout() {
    doLayout();
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
    int count = (int)m_geometries.size();
    if (row < 0 || row >= count) return current;

    if (cursorAction == MoveLeft) {
        row = std::max(0, row - 1);
    } else if (cursorAction == MoveRight) {
        row = std::min(count - 1, row + 1);
    } else if (cursorAction == MoveUp || cursorAction == MoveDown) {
        QRect currentRect = m_geometries[row].rect;
        int centerX = currentRect.center().x();
        int bestIdx = -1;
        int minDistance = 1000000;

        for (int i = 0; i < count; ++i) {
            if (i == row) continue;
            QRect targetRect = m_geometries[i].rect;
            
            if (cursorAction == MoveUp && targetRect.bottom() < currentRect.top()) {
                int dy = currentRect.top() - targetRect.bottom();
                int dx = std::abs(targetRect.center().x() - centerX);
                int dist = dy * 100 + dx; 
                if (dist < minDistance) {
                    minDistance = dist;
                    bestIdx = i;
                }
            } else if (cursorAction == MoveDown && targetRect.top() > currentRect.bottom()) {
                int dy = targetRect.top() - currentRect.bottom();
                int dx = std::abs(targetRect.center().x() - centerX);
                int dist = dy * 100 + dx;
                if (dist < minDistance) {
                    minDistance = dist;
                    bestIdx = i;
                }
            }
        }
        if (bestIdx != -1) row = bestIdx;
    }
    
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
    // 2026-06-xx 物理修复：在开启 TranslucentBackground 时手动填充坚实背景，防止透明穿透
    painter.fillRect(viewport()->rect(), QColor("#1E1E1E"));
    
    painter.translate(0, -verticalScrollBar()->value());
    
    for (int i = 0; i < (int)m_geometries.size(); ++i) {
        const auto& geo = m_geometries[i];
        if (geo.rect.bottom() < verticalScrollBar()->value()) continue;
        if (geo.rect.top() > verticalScrollBar()->value() + viewport()->height()) break;

        QModelIndex idx = model()->index(geo.index, 0);
        QStyleOptionViewItem option;
        initViewItemOption(&option); 
        option.rect = geo.rect;
        
        if (selectionModel()->isSelected(idx))
            option.state |= QStyle::State_Selected;
        if (currentIndex() == idx)
            option.state |= QStyle::State_HasFocus;

        // 2026-05-20 物理适配：使用接口推荐的 itemDelegateForIndex
        itemDelegateForIndex(idx)->paint(&painter, option, idx);
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

    if (viewport()->width() < 50) {
        QTimer::singleShot(100, this, [this]() { doLayout(); });
        return;
    }

    m_geometries.resize(count);
    int containerWidth = viewport()->width() - 25; 
    if (containerWidth <= 0) return;

    int currentY = 10; 
    int i = 0;
    const int textHeight = 30; // 预留文件名显示高度（卡片下方）
    
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
                itemWidth = containerWidth + 10 - currentX; // 最后一个补齐行宽
            }

            m_geometries[itemIdx] = { QRect(currentX, currentY, itemWidth, actualHeight + textHeight), itemIdx };
            currentX += itemWidth + 5; 
        }
        currentY += actualHeight + textHeight + 5; // 统一行高推进
    }

    m_totalHeight = currentY + 10;
    updateGeometries();
    viewport()->update();
}

} // namespace ArcMeta
