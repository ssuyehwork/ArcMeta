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
#include <QtConcurrent>
#include <QPointer>
#include <functional>

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
    // 2026-07-05 启动卡死专项修复：
    // 铁律：refresh() 严禁在主线程同步调用任何重型聚合统计。
    // 分两阶段：1. 同步加载结构 (O(1))； 2. 异步填充统计。

    // 2026-07-05 修复：CategoryRepo 成员引用错误，正确引用 ArcMeta 命名空间下的 Category 结构
    auto categories = CategoryRepo::getAll();

    // 1. 结构哈希校验：若结构未变则杜绝 ResetModel，确保滚动条不动
    static int s_lastStructureHash = 0;
    int currentHash = (int)categories.size() + (m_type * 1000000);
    for(const auto& c : categories) currentHash += c.id + c.parentId;

    bool needsReset = (s_lastStructureHash != currentHash);
    s_lastStructureHash = currentHash;

    if (needsReset) {
        beginResetModel();
    }

    int currentRow = 0;

    // --- A. 系统项加载 (结构优先) ---
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
            QString display = QString("%1 (...)").arg(def.name);
            if (currentRow < rowCount() && index(currentRow, 0).data(IdRole).toInt() == def.id) {
                item(currentRow)->setData(display, Qt::DisplayRole);
            } else {
                QStandardItem* newItem = new QStandardItem(display);
                newItem->setData(def.type, TypeRole); newItem->setData(def.name, NameRole);
                newItem->setData(def.color, ColorRole); newItem->setData(def.id, IdRole);
                newItem->setEditable(false);
                newItem->setIcon(UiHelper::getIcon(def.icon, QColor(def.color), 16));
                if (needsReset) appendRow(newItem); else insertRow(currentRow, newItem);
            }
            currentRow++;
        }
    }

    // 辅助函数：同步组标题
    auto syncGroupHdr = [&](int row, const QString& name, const QString& iconKey) -> QStandardItem* {
        if (row < rowCount() && index(row, 0).data(NameRole).toString() == name) {
            return item(row);
        } else {
            QStandardItem* g = new QStandardItem(name);
            g->setData(name, NameRole); g->setSelectable(false); g->setEditable(false);
            if (name == "我的分类") g->setFlags(g->flags() | Qt::ItemIsDropEnabled);
            g->setIcon(UiHelper::getIcon(iconKey, QColor("#FFFFFF"), 16));
            QFont f = g->font(); f.setBold(true); g->setFont(f);
            g->setForeground(QColor("#FFFFFF"));
            if (needsReset) appendRow(g); else insertRow(row, g);
            return g;
        }
    };

    // --- B. 快速访问 (书签) ---
    if (m_type == Both || m_type == User) {
        QStandardItem* favGroup = syncGroupHdr(currentRow++, "快速访问", "folder_filled");
        auto favorites = FavoritesRepo::getAll();

        int favRow = 0;
        for (const auto& fav : favorites) {
            QString name = QString::fromStdWString(fav.name);
            if (favRow < favGroup->rowCount() && favGroup->child(favRow)->data(NameRole).toString() == name) {
            } else {
                QStandardItem* b = new QStandardItem(name);
                b->setData("bookmark", TypeRole); b->setData(QString::fromStdWString(fav.path), PathRole);
                b->setData(name, NameRole); b->setIcon(UiHelper::getIcon("folder_filled", QColor("#555555"), 16));
                favGroup->insertRow(favRow, b);
            }
            favRow++;
        }
        for (const auto& cat : categories) {
            if (cat.pinned) {
                QString name = QString::fromStdWString(cat.name);
                QString display = QString("%1 (...)").arg(name);
                if (favRow < favGroup->rowCount() && favGroup->child(favRow)->data(IdRole).toInt() == cat.id) {
                    favGroup->child(favRow)->setData(display, Qt::DisplayRole);
                } else {
                    QString color = QString::fromStdWString(cat.color).isEmpty() ? "#555555" : QString::fromStdWString(cat.color);
                    QStandardItem* mirror = new QStandardItem(QString("%1 (...)").arg(name));
                    mirror->setData("category", TypeRole); mirror->setData(cat.id, IdRole);
                    mirror->setData(color, ColorRole); mirror->setData(name, NameRole);
                    mirror->setData(true, PinnedRole);
                    mirror->setIcon(UiHelper::getIcon(cat.encrypted && !m_unlockedIds.contains(cat.id) ? "lock" : "folder_filled", QColor(color), 16));
                    favGroup->insertRow(favRow, mirror);
                }
                favRow++;
            }
        }
        if (favGroup->rowCount() > favRow) favGroup->removeRows(favRow, favGroup->rowCount() - favRow);
    }

    // --- C. 我的分类 (业务树) ---
    if (m_type == User || m_type == Both) {
        QStandardItem* userGroup = syncGroupHdr(currentRow++, "我的分类", "folder_filled");

        // 2026-07-05 修复：显式指定 std::function 模板参数并补全捕获，杜绝类型推导失败。
        std::function<void(QStandardItem*, const std::vector<Category>&)> syncLevel;
        syncLevel = [this, &categories, &syncLevel](QStandardItem* parentItem, const std::vector<Category>& levelCats) {
            int row = 0;
            for (const auto& cat : levelCats) {
                QString name = QString::fromStdWString(cat.name);
                QString display = QString("%1 (...)").arg(name);
                QStandardItem* catItem = nullptr;
                if (row < parentItem->rowCount() && parentItem->child(row)->data(IdRole).toInt() == cat.id) {
                    catItem = parentItem->child(row);
                    catItem->setData(display, Qt::DisplayRole);
                } else {
                    QString color = QString::fromStdWString(cat.color).isEmpty() ? "#555555" : QString::fromStdWString(cat.color);
                    catItem = new QStandardItem(display);
                    catItem->setData("category", TypeRole); catItem->setData(cat.id, IdRole);
                    catItem->setData(color, ColorRole); catItem->setData(name, NameRole);
                    catItem->setData(cat.pinned, PinnedRole); catItem->setData(cat.encrypted, EncryptedRole);
                    catItem->setData(QString::fromStdWString(cat.encryptHint), EncryptHintRole);
                    catItem->setFlags(catItem->flags() | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
                    catItem->setIcon(UiHelper::getIcon(cat.encrypted && !m_unlockedIds.contains(cat.id) ? "lock" : "folder_filled", QColor(color), 16));
                    parentItem->insertRow(row, catItem);
                }

                std::vector<Category> children;
                for(const auto& c : categories) if(c.parentId == cat.id) children.push_back(c);
                syncLevel(catItem, children);
                row++;
            }
            if (parentItem->rowCount() > row) parentItem->removeRows(row, parentItem->rowCount() - row);
        };
        std::vector<Category> roots;
        for(const auto& c : categories) if(c.parentId <= 0) roots.push_back(c);
        syncLevel(userGroup, roots);
    }

    if (rowCount() > currentRow) removeRows(currentRow, rowCount() - currentRow);

    if (needsReset) {
        endResetModel();
    } else {
        // 如果结构未变，仅触发数据变更信号，让统计数字（...）异步更新
        emit dataChanged(index(0, 0), index(rowCount() - 1, 0));
    }

    // 2. 异步统计填充
    if (!m_isCounting) {
        m_isCounting = true;
        QPointer<CategoryModel> weakThis(this);
        (void)QtConcurrent::run([weakThis]() {
            if (!weakThis) return;
            auto sysCounts = CategoryRepo::getSystemCounts();
            auto countsVec = CategoryRepo::getCounts();
            QMap<int, int> catCounts;
            for (const auto& p : countsVec) catCounts[p.first] = p.second;
            QMetaObject::invokeMethod(weakThis.data(), [weakThis, sysCounts, catCounts]() {
                if (weakThis) weakThis->updateCounts(sysCounts, catCounts);
            }, Qt::QueuedConnection);
        });
    }
}

void CategoryModel::updateCounts(const QMap<QString, int>& sysCounts, const QMap<int, int>& catCounts) {
    m_isCounting = false;
    std::function<void(const QModelIndex&)> updateItem;
    updateItem = [&](const QModelIndex& parent) {
        for (int i = 0; i < rowCount(parent); ++i) {
            QModelIndex idx = index(i, 0, parent);
            QString type = idx.data(TypeRole).toString();
            QString name = idx.data(NameRole).toString();
            int id = idx.data(IdRole).toInt();
            if (type == "category" && id > 0) {
                setData(idx, QString("%1 (%2)").arg(name).arg(catCounts.value(id, 0)), Qt::DisplayRole);
            } else if (id < 0) {
                setData(idx, QString("%1 (%2)").arg(name).arg(sysCounts.value(type, 0)), Qt::DisplayRole);
            }
            if (hasChildren(idx)) updateItem(idx);
        }
    };
    updateItem(QModelIndex());
}

void CategoryModel::loadCategoryItems(const QModelIndex& parentIndex) {
    Q_UNUSED(parentIndex);
}

QVariant CategoryModel::data(const QModelIndex& index, int role) const {
    if (role == Qt::EditRole) return QStandardItemModel::data(index, NameRole);
    return QStandardItemModel::data(index, role);
}

bool CategoryModel::setData(const QModelIndex& index, const QVariant& val, int role) {
    if (role == Qt::EditRole) {
        QString newName = val.toString().trimmed();
        if (newName.isEmpty()) return false;
        int id = index.data(IdRole).toInt();
        if (index.data(TypeRole).toString() == "category" && id > 0) {
            auto categories = CategoryRepo::getAll();
            for (auto& cat : categories) {
                if (cat.id == id) { cat.name = newName.toStdWString(); CategoryRepo::update(cat); break; }
            }
            refresh(); return true;
        }
        return false;
    }
    return QStandardItemModel::setData(index, val, role);
}

Qt::DropActions CategoryModel::supportedDropActions() const {
    return Qt::MoveAction | Qt::CopyAction | Qt::LinkAction;
}

bool CategoryModel::dropMimeData(const QMimeData* mimeData, Qt::DropAction action, int row, int column, const QModelIndex& parent) {
    if (mimeData->hasUrls() || mimeData->hasFormat("text/plain")) return true;
    QModelIndex actualParent = parent;
    if (actualParent.isValid()) {
        QStandardItem* pItem = itemFromIndex(actualParent);
        if (!pItem) return false;
        QString type = pItem->data(TypeRole).toString();
        QString name = pItem->data(NameRole).toString();
        if (type != "category" && type != "bookmark" && name != "我的分类") return false;
    }
    return QStandardItemModel::dropMimeData(mimeData, action, row, column, actualParent);
}

} // namespace ArcMeta
