#pragma once

#include <QSortFilterProxyModel>
#include <QMimeData>
#include "../core/ModelContract.h"

namespace QuarkMeta {

/**
 * @brief 分类递归过滤代理模型
 * 2026-xx-xx 按照 Plan-98：实现“临时分类”专项递归过滤
 * 彻底修复：添加拖拽权限透传与自定义排序控制，实现 100% 手动拖拽调整顺序落盘
 */
class CategoryFilterProxyModel : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit CategoryFilterProxyModel(QObject* parent = nullptr) : QSortFilterProxyModel(parent) {
        setRecursiveFilteringEnabled(false); 
        setDynamicSortFilter(false); // 屏蔽代理模型默认按字母强行重排，完全交由数据库 sortOrder 决定
    }

    void setFilterText(const QString& text) {
        m_filterText = text;
        beginFilterChange();
        endFilterChange();
    }

    QString filterText() const { return m_filterText; }

    // 1. 拖拽权限透传：确保代理模型向 QTreeView 开放 Drag & Drop
    Qt::ItemFlags flags(const QModelIndex& index) const override {
        Qt::ItemFlags f = QSortFilterProxyModel::flags(index);
        if (index.isValid()) {
            f |= Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
        } else {
            f |= Qt::ItemIsDropEnabled;
        }
        return f;
    }

    Qt::DropActions supportedDropActions() const override {
        return Qt::MoveAction | Qt::CopyAction;
    }

    // 2. Drop 动作桥接：将代理模型上的放下动作精确映射并转发至底层 CategoryModel 源模型
    bool dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent) override {
        QModelIndex sourceParent = mapToSource(parent);
        return sourceModel()->dropMimeData(data, action, row, column, sourceParent);
    }

protected:
    // 3. 自定义排序控制：杜绝默认按名称拼音强行字母重排，严守 CategoryRepo 数据库权威 sortOrder 序号
    bool lessThan(const QModelIndex& source_left, const QModelIndex& source_right) const override {
        // 如果未开启过滤搜索，直接尊重 CategoryModel 原生源顺序（即数据库 sortOrder 顺序）
        if (m_filterText.isEmpty()) {
            return source_left.row() < source_right.row();
        }
        return QSortFilterProxyModel::lessThan(source_left, source_right);
    }

    bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override {
        if (m_filterText.isEmpty()) return true;

        QModelIndex index = sourceModel()->index(source_row, 0, source_parent);
        int id = index.data(IdRole).toInt();
        QString name = index.data(NameRole).toString();
        
        // 1. 系统项（ID < 0）及“分类”主标题，始终可见不参与过滤
        if (id < 0) return true;

        // 2. 根容器处理
        if (name == "快速访问" || name == "分类") {
            return hasMatchingChild(index);
        }

        // 3. 子树匹配逻辑
        if (name.contains(m_filterText, Qt::CaseInsensitive)) return true;

        // 4. 递归检查子项
        return hasMatchingChild(index);
    }

private:
    bool hasMatchingChild(const QModelIndex& parent) const {
        int rowCount = sourceModel()->rowCount(parent);
        for (int i = 0; i < rowCount; ++i) {
            QModelIndex child = sourceModel()->index(i, 0, parent);
            QString name = child.data(NameRole).toString();
            if (name.contains(m_filterText, Qt::CaseInsensitive)) return true;
            if (hasMatchingChild(child)) return true;
        }
        return false;
    }

    QString m_filterText;
};

} // namespace QuarkMeta