#pragma once

#include <QAbstractItemView>
#include <QScrollBar>
#include <QPainter>
#include <QResizeEvent>
#include <QStyleOptionViewItem>
#include <vector>

namespace ArcMeta {

/**
 * @brief Justified 布局视图 (固定行高，宽度自适应)
 */
class JustifiedView : public QAbstractItemView {
    Q_OBJECT
public:
    explicit JustifiedView(QWidget* parent = nullptr) : QAbstractItemView(parent) {
        horizontalScrollBar()->setRange(0, 0);
        verticalScrollBar()->setSingleStep(20);
    }

    void setTargetRowHeight(int h) {
        if (m_targetRowHeight != h) {
            m_targetRowHeight = h;
            doLayout();
        }
    }

    // --- QAbstractItemView 核心实现 ---

    QRect visualRect(const QModelIndex& index) const override {
        if (!index.isValid() || index.row() >= (int)m_geometries.size()) return QRect();
        QRect r = m_geometries[index.row()].rect;
        r.translate(0, -verticalScrollBar()->value());
        return r;
    }

    void scrollTo(const QModelIndex& index, ScrollHint hint) override {
        QRect rect = visualRect(index);
        if (rect.isEmpty()) return;

        int viewportHeight = viewport()->height();
        int scrollValue = verticalScrollBar()->value();

        if (hint == EnsureVisible) {
            if (rect.top() < 0) verticalScrollBar()->setValue(scrollValue + rect.top());
            else if (rect.bottom() > viewportHeight) verticalScrollBar()->setValue(scrollValue + rect.bottom() - viewportHeight);
        }
        // 其他 hint 实现省略...
    }

    QModelIndex indexAt(const QPoint& point) const override {
        int y = point.y() + verticalScrollBar()->value();
        // 二分查找行，或者简单的线性查找（考虑到行数不会极其夸张）
        for (const auto& geo : m_geometries) {
            if (geo.rect.contains(point.x(), y)) {
                return model()->index(geo.index, 0);
            }
        }
        return QModelIndex();
    }

protected slots:
    void dataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight, const QVector<int>& roles) override {
        // 如果是尺寸变化相关的角色，需要重排
        if (roles.isEmpty() || roles.contains(Qt::UserRole + 2)) {
            doLayout();
        }
        QAbstractItemView::dataChanged(topLeft, bottomRight, roles);
    }

    void rowsInserted(const QModelIndex& parent, int start, int end) override {
        doLayout();
        QAbstractItemView::rowsInserted(parent, start, end);
    }

    void rowsAboutToBeRemoved(const QModelIndex& parent, int start, int end) override {
        doLayout();
        QAbstractItemView::rowsAboutToBeRemoved(parent, start, end);
    }

protected:
    QModelIndex moveCursor(CursorAction cursorAction, Qt::KeyboardModifiers /*modifiers*/) override {
        QModelIndex current = currentIndex();
        if (!current.isValid()) return model()->index(0, 0);

        int row = current.row();
        switch (cursorAction) {
            case MoveLeft: row--; break;
            case MoveRight: row++; break;
            // MoveUp/Down 需要查找上方/下方的物理位置，这里简化处理
            case MoveUp: row -= 5; break;
            case MoveDown: row += 5; break;
            default: break;
        }
        row = qBound(0, row, model()->rowCount() - 1);
        return model()->index(row, 0);
    }

    int horizontalOffset() const override { return 0; }
    int verticalOffset() const override { return verticalScrollBar()->value(); }
    bool isIndexHidden(const QModelIndex& /*index*/) const override { return false; }

    void setSelection(const QRect& rect, QItemSelectionModel::SelectionFlags command) override {
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

    QRegion visualRegionForSelection(const QItemSelection& selection) const override {
        QRegion region;
        for (const auto& range : selection) {
            for (int i = range.top(); i <= range.bottom(); ++i) {
                region += visualRect(model()->index(i, 0));
            }
        }
        return region;
    }

    void paintEvent(QPaintEvent* event) override {
        QPainter painter(viewport());
        painter.translate(0, -verticalScrollBar()->value());

        QStyleOptionViewItem option = viewOptions();
        int firstRow = 0; // 可以优化为只绘制可见区域

        for (int i = firstRow; i < (int)m_geometries.size(); ++i) {
            const auto& geo = m_geometries[i];
            if (geo.rect.bottom() < verticalScrollBar()->value()) continue;
            if (geo.rect.top() > verticalScrollBar()->value() + viewport()->height()) break;

            option.rect = geo.rect;
            option.state = QStyle::State_Enabled;
            if (selectionModel()->isSelected(model()->index(geo.index, 0)))
                option.state |= QStyle::State_Selected;
            if (currentIndex() == model()->index(geo.index, 0))
                option.state |= QStyle::State_HasFocus;

            itemDelegate()->paint(&painter, option, model()->index(geo.index, 0));
        }
    }

    void resizeEvent(QResizeEvent* event) override {
        doLayout();
        QAbstractItemView::resizeEvent(event);
    }

    void updateGeometries() override {
        verticalScrollBar()->setPageStep(viewport()->height());
        verticalScrollBar()->setRange(0, qMax(0, m_totalHeight - viewport()->height()));
        QAbstractItemView::updateGeometries();
    }

private:
    void doLayout() {
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
        int containerWidth = viewport()->width() - 20; // 留出滚动条空间
        if (containerWidth <= 0) return;

        int currentY = 10; // 顶部间距
        int i = 0;

        while (i < count) {
            int rowStart = i;
            double rowAspectRatioSum = 0;
            std::vector<double> aspectRatios;

            // 填充一行
            while (i < count) {
                // 获取宽高比，默认为 1.0 (正方形)
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

            // 计算本行实际高度
            int actualHeight = m_targetRowHeight;
            bool isLastRow = (i == count);

            if (!isLastRow || (rowAspectRatioSum * m_targetRowHeight > containerWidth * 0.8)) {
                // 如果不是最后一行，或者最后一行也足够满，则拉伸填满宽度
                actualHeight = qRound(containerWidth / rowAspectRatioSum);
            }

            // 限制最大高度，防止单张图过大
            if (actualHeight > m_targetRowHeight * 1.5) actualHeight = m_targetRowHeight * 1.5;

            // 布局本行图片
            int currentX = 10;
            for (int j = 0; j < numInRow; ++j) {
                int itemIdx = rowStart + j;
                int itemWidth = qRound(aspectRatios[j] * actualHeight);

                // 补偿舍入误差（最后一张图补齐）
                if (j == numInRow - 1 && !isLastRow) {
                    itemWidth = containerWidth + 10 - currentX;
                }

                m_geometries[itemIdx] = { QRect(currentX, currentY, itemWidth, actualHeight), itemIdx };
                currentX += itemWidth + 5; // 5px 间距
            }
            currentY += actualHeight + 5;
        }

        m_totalHeight = currentY + 10;
        updateGeometries();
        viewport()->update();
    }

    struct ItemGeometry {
        QRect rect;
        int index;
    };
    std::vector<ItemGeometry> m_geometries;
    int m_totalHeight = 0;
    int m_targetRowHeight = 128;
};

} // namespace ArcMeta
