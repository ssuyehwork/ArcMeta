#include "CategoryModel.h"
#include "../db/CategoryRepo.h"
#include "../db/ItemRepo.h"
#include "../db/FavoritesRepo.h"
#include "UiHelper.h"
#include <QMimeData>
#include <QFileInfo>
#include <QFont>
#include <QTimer>
#include <QSet>
#include <QMap>
#include <QSettings>
#include <QApplication>

namespace ArcMeta {

CategoryModel::CategoryModel(Type type, QObject* parent) 
    : QStandardItemModel(parent), m_type(type) 
{
}

void CategoryModel::setUnlockedIds(const QSet<int>& ids) {
    m_unlockedIds = ids;
}

void CategoryModel::deferredRefresh() {
    refresh();
}

void CategoryModel::refresh() {
    // 2026-06-xx 物理修复：回归 ResetModel 架构并配合状态恢复，解决大量原子操作导致的 UI 假死
    beginResetModel();
    
    // 获取最新数据
    auto sysCounts = CategoryRepo::getSystemCounts();
    auto categories = CategoryRepo::getAll();
    auto countsVec = CategoryRepo::getCounts();
    QMap<int, int> catCounts;
    for (const auto& p : countsVec) catCounts[p.first] = p.second;

    int currentRow = 0;

    // 1. 系统模块 (增量同步)
    if (m_type == System || m_type == Both) {
        struct SysDef { QString name; QString type; QString icon; QString color; int id; };
        static const QList<SysDef> sysDefs = {
            {"全部数据", "all", "all_data", "#3498db", -1},
            {"未分类", "uncategorized", "uncategorized", "#95a5a6", -2},
            {"未标签", "untagged", "untagged", "#7f8c8d", -3},
            {"今日数据", "today", "today", "#2ecc71", -4},
            {"昨日数据", "yesterday", "today", "#f39c12", -5},
            {"最近访问", "recently_visited", "clock", "#9b59b6", -6},
            {"标签管理", "tags", "tag", "#1abc9c", -7},
            {"回收站", "trash", "trash", "#e74c3c", -8}
        };

        for (const auto& def : sysDefs) {
            int count = sysCounts.value(def.type, 0);
            QString display = QString("%1 (%2)").arg(def.name).arg(count);

            if (currentRow < rowCount() && index(currentRow, 0).data(IdRole).toInt() == def.id) {
                QStandardItem* existing = item(currentRow);
                if (existing->text() != display) existing->setText(display);
            } else {
                QStandardItem* newItem = new QStandardItem(display);
                newItem->setData(def.type, TypeRole);
                newItem->setData(def.name, NameRole);
                newItem->setData(def.color, ColorRole);
                newItem->setData(def.id, IdRole);
                newItem->setEditable(false);
                newItem->setIcon(UiHelper::getIcon(def.icon, QColor(def.color), 16));
                insertRow(currentRow, newItem);
            }
            currentRow++;
        }
    }

    // 辅助函数：同步组标题项
    auto syncGroupHeader = [&](int row, const QString& name, const QString& iconName) -> QStandardItem* {
        if (row < rowCount() && index(row, 0).data(NameRole).toString() == name) {
            return item(row);
        } else {
            QStandardItem* group = new QStandardItem(name);
            group->setData(name, NameRole);
            group->setSelectable(false);
            group->setEditable(false);
            if (name == "我的分类") group->setFlags(group->flags() | Qt::ItemIsDropEnabled);
            group->setIcon(UiHelper::getIcon(iconName, QColor("#FFFFFF"), 16));
            QFont font = group->font(); font.setBold(true); group->setFont(font);
            group->setForeground(QColor("#FFFFFF"));
            insertRow(row, group);
            return group;
        }
    };

    // 2. 快速访问
    QStandardItem* favGroup = nullptr;
    if (m_type == Both || m_type == User) {
        favGroup = syncGroupHeader(currentRow++, "快速访问", "folder_filled");
        auto favorites = FavoritesRepo::getAll();

        // 同步书签与置顶分类镜像
        // 由于此处涉及多源数据组合，暂时采用清空子项再重建的方式
        favGroup->removeRows(0, favGroup->rowCount());

        for (const auto& fav : favorites) {
            QStandardItem* bItem = new QStandardItem(QString::fromStdWString(fav.name));
            bItem->setData("bookmark", TypeRole);
            bItem->setData(QString::fromStdWString(fav.path), PathRole);
            bItem->setData(QString::fromStdWString(fav.name), NameRole);
            bItem->setIcon(UiHelper::getIcon("folder_filled", QColor("#555555"), 16));
            favGroup->appendRow(bItem);
        }

        for (const auto& cat : categories) {
            if (cat.pinned) {
                QString name = QString::fromStdWString(cat.name);
                QString color = QString::fromStdWString(cat.color).isEmpty() ? "#555555" : QString::fromStdWString(cat.color);
                QStandardItem* mirror = new QStandardItem(name);
                mirror->setData("category", TypeRole);
                mirror->setData(cat.id, IdRole);
                mirror->setData(color, ColorRole);
                mirror->setData(name, NameRole);
                mirror->setData(true, PinnedRole);
                if (cat.encrypted && !m_unlockedIds.contains(cat.id)) {
                    mirror->setIcon(UiHelper::getIcon("lock", QColor("#aaaaaa"), 16));
                } else {
                    mirror->setIcon(UiHelper::getIcon("folder_filled", QColor(color), 16));
                }
                favGroup->appendRow(mirror);
            }
        }
    }

    // 3. 我的分类
    if (m_type == User || m_type == Both) {
        QStandardItem* userGroup = syncGroupHeader(currentRow++, "我的分类", "folder_filled");
        
        // 2026-06-xx 物理回退：废除复杂的节点缓存对比逻辑
        // 理由：大量的小型 insertRow/removeRow 操作在大型树中性能极差，且难以处理复杂的镜像层级
        // 方案：在 beginResetModel/endResetModel 块内进行纯净的全量构建
        QMap<int, QStandardItem*> itemMap;
        for (const auto& cat : categories) {
            int id = cat.id;
            QString name = QString::fromStdWString(cat.name);
            QString color = QString::fromStdWString(cat.color).isEmpty() ? "#555555" : QString::fromStdWString(cat.color);
            int count = catCounts.value(id, 0);
            QString display = QString("%1 (%2)").arg(name).arg(count);

            QStandardItem* catItem = new QStandardItem(display);
            catItem->setData("category", TypeRole);
            catItem->setData(id, IdRole);
            catItem->setData(color, ColorRole);
            catItem->setData(name, NameRole);
            catItem->setData(cat.pinned, PinnedRole);
            catItem->setData(cat.encrypted, EncryptedRole);
            catItem->setData(QString::fromStdWString(cat.encryptHint), EncryptHintRole);
            catItem->setFlags(catItem->flags() | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
            
            if (cat.encrypted && !m_unlockedIds.contains(id)) {
                catItem->setIcon(UiHelper::getIcon("lock", QColor("#aaaaaa"), 16));
            } else {
                catItem->setIcon(UiHelper::getIcon("folder_filled", QColor(color), 16));
            }
            itemMap[id] = catItem;
        }

        for (const auto& cat : categories) {
            QStandardItem* catItem = itemMap[cat.id];
            if (cat.parentId > 0 && itemMap.contains(cat.parentId)) {
                itemMap[cat.parentId]->appendRow(catItem);
            } else {
                userGroup->appendRow(catItem);
            }
        }
    }
    
    // 清理多余的顶层行
    if (rowCount() > currentRow) {
        removeRows(currentRow, rowCount() - currentRow);
    }

    endResetModel();
}

void CategoryModel::loadCategoryItems(const QModelIndex& parentIndex) {
    Q_UNUSED(parentIndex);
}

QVariant CategoryModel::data(const QModelIndex& index, int role) const {
    if (role == Qt::EditRole) {
        return QStandardItemModel::data(index, NameRole);
    }
    return QStandardItemModel::data(index, role);
}

bool CategoryModel::setData(const QModelIndex& index, const QVariant& val, int role) {
    if (role == Qt::EditRole) {
        QString newName = val.toString().trimmed();
        if (newName.isEmpty()) return false;

        QString type = index.data(TypeRole).toString();
        int id = index.data(IdRole).toInt();
        
        if (type == "category" && id > 0) {
            auto categories = CategoryRepo::getAll();
            for (auto& cat : categories) {
                if (cat.id == id) {
                    cat.name = newName.toStdWString();
                    CategoryRepo::update(cat);
                    break;
                }
            }
            refresh();
            return true;
        }
        return false;
    }
    return QStandardItemModel::setData(index, val, role);
}

Qt::DropActions CategoryModel::supportedDropActions() const {
    // 2026-06-xx 物理修复：扩展支持的动作。界外拖入通常被识别为 Copy 或 Link。
    // 只有在此处声明，Qt 视图才不会在拖入时显示“禁止图标”。
    return Qt::MoveAction | Qt::CopyAction | Qt::LinkAction;
}

bool CategoryModel::dropMimeData(const QMimeData* mimeData, Qt::DropAction action, int row, int column, const QModelIndex& parent) {
    // 2026-06-xx 物理修复：如果是外部 URL/路径拖入，放宽校验限制。
    // 允许在侧边栏任意位置释放，由 CategoryPanel 处理具体的分类归属逻辑。
    if (mimeData->hasUrls() || mimeData->hasFormat("text/plain")) {
        return true;
    }

    Q_UNUSED(action);
    Q_UNUSED(row);
    Q_UNUSED(column);
    
    QModelIndex actualParent = parent;
    if (actualParent.isValid()) {
        QStandardItem* parentItem = itemFromIndex(actualParent);
        if (!parentItem) return false;
        
        QString type = parentItem->data(TypeRole).toString();
        QString name = parentItem->data(NameRole).toString();
        
        // 内部拖拽（Move）依然保持严格校验，仅允许移动到分类、书签或根组
        if (type != "category" && type != "bookmark" && name != "我的分类") {
            return false; 
        }
    }
    return QStandardItemModel::dropMimeData(mimeData, action, row, column, actualParent);
}

} // namespace ArcMeta
