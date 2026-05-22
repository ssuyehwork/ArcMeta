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

void JustifiedView::mouseDoubleClickEvent(QMouseEvent* event) {
    QModelIndex idx = indexAt(event->pos());
    if (!idx.isValid()) {
        QAbstractItemView::mouseDoubleClickEvent(event);
        return;
    }

    const int textHeight = 36;
    QRect itemRect = visualRect(idx);
    // 文字区域 = 卡片底部 textHeight 像素
    QRect textRect(itemRect.left(), itemRect.bottom() - textHeight, itemRect.width(), textHeight);

    if (textRect.contains(event->pos())) {
        // 双击在文字区域 → 触发行内重命名
        edit(idx);
    } else {
        // 双击在缩略图区域 → 触发打开文件（发射 doubleClicked 信号）
        emit doubleClicked(idx);
    }
    // 不调用父类，防止父类再次触发默认编辑逻辑
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
    const int margin = 10;
    const int spacing = 5;
    // 可用宽度：视口宽度 - 左边距 - 右边距
    int containerWidth = viewport()->width() - (margin * 2); 
    if (containerWidth <= 0) return;

    int currentY = margin; 
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
            
            int numInRow = (int)aspectRatios.size();
            // 2026-06-xx 物理修正：考虑 ThumbnailDelegate 的内边距 (左右各 3px = 6px)
            // 预估宽度 = (宽高比总和 * 目标高度) + (内边距补偿 6px * 数量) + (项间距 * 间距数量)
            double estimatedWidth = (rowAspectRatioSum * m_targetRowHeight) + (6 * numInRow) + (spacing * (numInRow - 1));
            if (estimatedWidth > containerWidth) {
                // 如果单项就超过了容器宽度，则强制独占一行
                if (numInRow > 1) {
                    aspectRatios.pop_back();
                    rowAspectRatioSum -= ar;
                } else {
                    i++;
                }
                break; 
            }
            i++;
        }

        int rowEnd = i;
        int numInRow = rowEnd - rowStart;
        if (numInRow <= 0) break;

        int actualHeight = m_targetRowHeight;
        bool isLastRow = (i == count);

        // 2026-06-xx 物理修正：考虑 ThumbnailDelegate 的内边距 (左右各 3px = 6px)
        // 实际图片可用总宽度 = 容器宽度 - (项间距) - (所有项的 6px 内边距)
        int availableImageWidth = containerWidth - (spacing * (numInRow - 1)) - (6 * numInRow);

        if (!isLastRow || (rowAspectRatioSum * m_targetRowHeight > containerWidth * 0.8)) {
            actualHeight = qRound(availableImageWidth / rowAspectRatioSum);
        }

        // 防止行高过大，限制在目标高度的 1.5 倍
        if (actualHeight > m_targetRowHeight * 1.5) actualHeight = qRound(m_targetRowHeight * 1.5);

        int currentX = margin;
        const int textHeight = 36;
        for (int j = 0; j < numInRow; ++j) {
            int itemIdx = rowStart + j;
            // 2026-06-xx 物理修正：itemWidth 需要包含左右内边距 (6px)
            int itemWidth = qRound(aspectRatios[j] * actualHeight) + 6;
            
            // 最后一个项目：物理对齐右边缘 (针对非最后一行)
            if (j == numInRow - 1 && !isLastRow) {
                itemWidth = (containerWidth + margin) - currentX;
            }

            // 总高度 = 图片高度 (actualHeight) + 上下内边距 (6px) + 文字区域高度 (36px)
            m_geometries[itemIdx] = { QRect(currentX, currentY, itemWidth, actualHeight + 6 + textHeight), itemIdx };
            currentX += itemWidth + spacing; 
        }
        currentY += actualHeight + 6 + textHeight + spacing; // 统一行高推进
    }

    m_totalHeight = currentY + 10;
    updateGeometries();
    viewport()->update();
}

} // namespace ArcMeta
