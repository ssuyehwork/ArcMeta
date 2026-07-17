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
    if (m_justifiedView) {
        m_justifiedView->setModel(model);
    }
}

void GridResultView::setIconSize(int size) {
    if (m_justifiedView) {
        m_justifiedView->setTargetRowHeight(size);
    }
}

void GridResultView::refreshLayout() {
    if (m_justifiedView) {
        m_justifiedView->setLayoutMode(JustifiedView::GridMode);
        m_justifiedView->doItemsLayout();
    }
}

} // namespace ArcMeta
