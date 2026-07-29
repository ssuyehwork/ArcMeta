#include "ListResultView.h"
#include "DropTreeView.h"
#include <QHeaderView>

namespace ArcMeta {

ListResultView::ListResultView(DropTreeView* treeView, QWidget* parent) 
    : IScanResultView(parent), m_treeView(treeView) {
}

ListResultView::~ListResultView() {
}

QWidget* ListResultView::getWidget() {
    return m_treeView;
}

QAbstractItemView* ListResultView::getBaseView() {
    return m_treeView;
}

void ListResultView::setModel(QAbstractItemModel* model) {
    m_treeView->setModel(model);
}

void ListResultView::setIconSize(int size) {
    // 列表视图的图标大小
    m_treeView->setIconSize(QSize(size - 8, size - 8));
}

void ListResultView::refreshLayout() {
}

} // namespace ArcMeta
