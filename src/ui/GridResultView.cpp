#include "GridResultView.h"

namespace ArcMeta {

GridResultView::GridResultView(DropJustifiedView* justifiedView, QObject* parent)
    : IScanResultView(parent), m_justifiedView(justifiedView) {
    if (m_justifiedView) {
        m_justifiedView->setLayoutMode(JustifiedView::GridMode);
    }
}

QWidget* GridResultView::getWidget() {
    return m_justifiedView;
}

QAbstractItemView* GridResultView::getBaseView() {
    return m_justifiedView;
}

void GridResultView::setModel(QAbstractItemModel* model) {
    m_justifiedView->setModel(model);
}

void GridResultView::setIconSize(int size) {
    m_justifiedView->setTargetRowHeight(size);
}

void GridResultView::refreshLayout() {
    m_justifiedView->setLayoutMode(JustifiedView::GridMode);
    m_justifiedView->doItemsLayout();
}

} // namespace ArcMeta
