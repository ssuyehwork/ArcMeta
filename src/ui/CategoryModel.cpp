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
    // 2026-06-xx 极致优化：仅在结构发生重大变化时调用 resetModel。
    // 如果仅是数字（Count）或元数据（Rating/Color）变化，采用原地更新，彻底根治滚动条跳变问题。
    
    // 获取最新数据
    auto sysCounts = CategoryRepo::getSystemCounts();
    auto categories = CategoryRepo::getAll();
    auto countsVec = CategoryRepo::getCounts();
    QMap<int, int> catCounts;
    for (const auto& p : countsVec) catCounts[p.first] = p.second;

    // 结构校验：如果行数不一致，或分类 ID 集合发生变化，则触发 Reset
    static int s_lastStructureHash = 0;
    int currentHash = categories.size() + (m_type * 1000000);
    for(const auto& c : categories) currentHash += c.id + c.parentId;

    if (s_lastStructureHash != currentHash) {
        beginResetModel();
        s_lastStructureHash = currentHash;
    } else {
        // 如果结构未变，我们仅更新文本和图标，不触发 ResetModel
    }

    int currentRow = 0;

    // 1. 系统模块 (原地同步)
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
                if (s_lastStructureHash == currentHash) {
                     // 结构一致但项目不存在（极罕见），补充插入
                     insertRow(currentRow, newItem);
                } else {
                     appendRow(newItem);
                }
            }
            currentRow++;
        }
    }

    // 辅助函数：原地更新或创建组标题
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
            if (s_lastStructureHash == currentHash) insertRow(row, group);
            else appendRow(group);
            return group;
        }
    };

    // 2. 快速访问
    if (m_type == Both || m_type == User) {
        QStandardItem* favGroup = syncGroupHeader(currentRow++, "快速访问", "folder_filled");
        auto favorites = FavoritesRepo::getAll();

        // 快速访问通常项不多，Reset 其子项影响较小，但为求极致，此处也尝试原地更新
        int favRow = 0;
        for (const auto& fav : favorites) {
            QString name = QString::fromStdWString(fav.name);
            if (favRow < favGroup->rowCount() && favGroup->child(favRow)->data(NameRole).toString() == name) {
                // 原地保持
            } else {
                QStandardItem* bItem = new QStandardItem(name);
                bItem->setData("bookmark", TypeRole);
                bItem->setData(QString::fromStdWString(fav.path), PathRole);
                bItem->setData(name, NameRole);
                bItem->setIcon(UiHelper::getIcon("folder_filled", QColor("#555555"), 16));
                favGroup->insertRow(favRow, bItem);
            }
            favRow++;
        }

        for (const auto& cat : categories) {
            if (cat.pinned) {
                QString name = QString::fromStdWString(cat.name);
                if (favRow < favGroup->rowCount() && favGroup->child(favRow)->data(IdRole).toInt() == cat.id) {
                     // 更新数字
                     int count = catCounts.value(cat.id, 0);
                     favGroup->child(favRow)->setText(QString("%1 (%2)").arg(name).arg(count));
                } else {
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
                    favGroup->insertRow(favRow, mirror);
                }
                favRow++;
            }
        }
        if (favGroup->rowCount() > favRow) favGroup->removeRows(favRow, favGroup->rowCount() - favRow);
    }

    // 3. 我的分类
    if (m_type == User || m_type == Both) {
        QStandardItem* userGroup = syncGroupHeader(currentRow++, "我的分类", "folder_filled");

        // 递归查找/更新函数
        std::function<void(QStandardItem*, const std::vector<Category>&)> syncLevel;
        syncLevel = [&](QStandardItem* parentItem, const std::vector<Category>& levelCats) {
            int row = 0;
            for (const auto& cat : levelCats) {
                int id = cat.id;
                QString name = QString::fromStdWString(cat.name);
                int count = catCounts.value(id, 0);
                QString display = QString("%1 (%2)").arg(name).arg(count);

                QStandardItem* catItem = nullptr;
                if (row < parentItem->rowCount() && parentItem->child(row)->data(IdRole).toInt() == id) {
                    catItem = parentItem->child(row);
                    if (catItem->text() != display) catItem->setText(display);
                } else {
                    QString color = QString::fromStdWString(cat.color).isEmpty() ? "#555555" : QString::fromStdWString(cat.color);
                    catItem = new QStandardItem(display);
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
                    parentItem->insertRow(row, catItem);
                }

                // 递归处理子分类
                std::vector<Category> children;
                for(const auto& c : categories) if(c.parentId == id) children.push_back(c);
                syncLevel(catItem, children);
                row++;
            }
            if (parentItem->rowCount() > row) parentItem->removeRows(row, parentItem->rowCount() - row);
        };

        std::vector<Category> roots;
        for(const auto& c : categories) if(c.parentId <= 0) roots.push_back(c);
        syncLevel(userGroup, roots);
    }
    
    // 清理多余的顶层行
    if (rowCount() > currentRow) {
        removeRows(currentRow, rowCount() - currentRow);
    }

    if (s_lastStructureHash == currentHash) {
        // 未 Reset，显式通知视图数据已变
        emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1));
    } else {
        endResetModel();
    }
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
