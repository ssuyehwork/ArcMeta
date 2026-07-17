#include "JustifiedResultView.h"

namespace ArcMeta {

JustifiedResultView::JustifiedResultView(DropJustifiedView* justifiedView, QObject* parent) 
    : IScanResultView(parent), m_justifiedView(justifiedView) {
    if (m_justifiedView) {
        m_justifiedView->setLayoutMode(JustifiedView::JustifiedMode);
    }
}

QWidget* JustifiedResultView::getWidget() {
    return m_justifiedView;
}

QAbstractItemView* JustifiedResultView::getBaseView() {
    return m_justifiedView;
}

void JustifiedResultView::setModel(QAbstractItemModel* model) {
    if (m_justifiedView) {
        m_justifiedView->setModel(model);
    }
}

void JustifiedResultView::setIconSize(int size) {
    if (m_justifiedView) {
        m_justifiedView->setTargetRowHeight(size);
    }
}

void JustifiedResultView::refreshLayout() {
    if (m_justifiedView) {
        m_justifiedView->setLayoutMode(JustifiedView::JustifiedMode);
        m_justifiedView->doItemsLayout();
    }
}

} // namespace ArcMeta
