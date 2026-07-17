#include "ListResultView.h"

namespace ArcMeta {

ListResultView::ListResultView(DropTreeView* treeView, QObject* parent) 
    : IScanResultView(parent), m_treeView(treeView) {
}

QWidget* ListResultView::getWidget() {
    return m_treeView;
}

QAbstractItemView* ListResultView::getBaseView() {
    return m_treeView;
}

void ListResultView::setModel(QAbstractItemModel* model) {
    if (m_treeView) {
        m_treeView->setModel(model);
    }
}

void ListResultView::setIconSize(int size) {
    if (m_treeView) {
        // 列表模式下的图标随行高调整 (2026-06-xx 物理修复)
        m_treeView->setIconSize(QSize(size - 8, size - 8));
    }
}

void ListResultView::refreshLayout() {
    if (m_treeView && m_treeView->viewport()) {
        m_treeView->viewport()->update();
    }
}

} // namespace ArcMeta
