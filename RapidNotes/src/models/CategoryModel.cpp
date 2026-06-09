#include "CategoryModel.h"
#include "../core/DatabaseManager.h"
#include "../ui/IconHelper.h"
#include <QMimeData>
#include <QFont>
#include <QTimer>
#include <QSet>

CategoryModel::CategoryModel(Type type, QObject* parent) 
    : QStandardItemModel(parent), m_type(type) 
{
    refresh();
}

void CategoryModel::refresh() {
    beginResetModel();
    removeRows(0, rowCount());
    
    DatabaseManager& db = DatabaseManager::instance();
    QVariantMap counts = DatabaseManager::instance().getCounts();

    // 1. 系统模块 (System Group)
    if (m_type == System || m_type == Both) {
        auto addSystemItem = [&](const QString& name, const QString& type, const QString& icon, const QString& color = "#aaaaaa") {
            int count = counts.value(type, 0).toInt();
            QString display = QString("%1 (%2)").arg(name).arg(count);
            QStandardItem* item = new QStandardItem(display);
            item->setData(type, TypeRole);
            item->setData(name, NameRole);
            item->setData(color, ColorRole); 
            item->setEditable(false); 
            item->setIcon(IconHelper::getIcon(icon, color));
            invisibleRootItem()->appendRow(item);
        };

        addSystemItem("全部数据", "all", "all_data", "#3498db");
        // 2026-05-03 按照用户要求：将“未分类”和“未标签”项目移动到“今日数据”上方
        addSystemItem("未分类", "uncategorized", "uncategorized", "#e67e22");
        addSystemItem("未标签", "untagged", "untagged", "#62BAC1");
        addSystemItem("今日数据", "today", "today", "#2ecc71");
        addSystemItem("昨日数据", "yesterday", "today", "#f39c12");
        addSystemItem("最近访问", "recently_visited", "clock", "#9b59b6");
        addSystemItem("回收站", "trash", "trash", "#e74c3c");
        addSystemItem("收藏", "bookmark", "bookmark_filled", "#2ECC71"); // 修改为翠绿色
    }
    
    // 2. 快速访问与我的分类 (User & Both Mode)
    if (m_type == User || m_type == Both) {
        QStandardItem* root = invisibleRootItem();
        // [A] 定义容器组
        QStandardItem* favGroup = new QStandardItem("快速访问");
        favGroup->setData("快速访问", NameRole);
        favGroup->setSelectable(false);
        favGroup->setEditable(false);
        favGroup->setFlags(favGroup->flags() | Qt::ItemIsDropEnabled); // 2026-04-20 [NEW] 开放快速访问接收拖拽
        favGroup->setIcon(IconHelper::getIcon("zap", "#f1c40f")); // 修改为黄金色
        QFont groupFont = favGroup->font();        groupFont.setBold(true);
        favGroup->setFont(groupFont);
        favGroup->setForeground(QColor("#FFFFFF"));
        root->appendRow(favGroup);

        int allCount = counts.value("all", 0).toInt();
        int uncatCount = counts.value("uncategorized", 0).toInt();
        int userTotalCount = qMax(0, allCount - uncatCount);
        
        QStandardItem* userGroup = new QStandardItem(QString("我的分类 (%1)").arg(userTotalCount));
        userGroup->setData("我的分类", NameRole);
        userGroup->setSelectable(false);
        userGroup->setEditable(false);
        userGroup->setFlags(userGroup->flags() | Qt::ItemIsDropEnabled);
        userGroup->setIcon(IconHelper::getIcon("branch", "#FFFFFF"));
        userGroup->setFont(groupFont);
        userGroup->setForeground(QColor("#FFFFFF"));
        root->appendRow(userGroup);

        auto categories = DatabaseManager::instance().getAllCategories();
        QMap<int, QStandardItem*> itemMap;
        bool hideLocked = DatabaseManager::instance().isLockedCategoriesHidden();
        
        // --- 第一步：构建物理实体节点 ---
        for (const auto& cat : categories) {
            int id = cat["id"].toInt();
            bool hasPassword = !cat["password"].toString().isEmpty();
            if (hideLocked && hasPassword) continue;

            int count = counts.value("cat_" + QString::number(id), 0).toInt();
            QString name = cat["name"].toString();
            QString color = cat["color"].toString();
            bool isPinned = cat["is_pinned"].toBool();
            
            QStandardItem* item = new QStandardItem(QString("%1 (%2)").arg(name).arg(count));
            item->setData("category", TypeRole);
            item->setData(id, IdRole);
            item->setData(color, ColorRole);
            item->setData(name, NameRole);
            item->setData(isPinned, PinnedRole);
            item->setData(hasPassword, HasPasswordRole);
            item->setFlags(item->flags() | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
            
            bool isLocked = DatabaseManager::instance().isCategoryLocked(id);
            if (hasPassword) {
                item->setIcon(IconHelper::getIcon(isLocked ? "lock" : "unlock", isLocked ? "#aaaaaa" : color));
            } else {
                // 检查是否为自动归档目标，且开关处于开启状态
                int activeCategoryId = DatabaseManager::instance().activeCategoryId();
                bool autoCatEnabled = DatabaseManager::instance().isAutoCategorizeEnabled();
                
                if (id == activeCategoryId && autoCatEnabled) {
                    // 仅在功能开启时，自动归档目标显示 switch_on 图标
                    item->setIcon(IconHelper::getIcon("switch_on", "#00A650"));
                } else {
                    // 非自动归档目标或功能关闭时，显示圆圈图标
                    item->setIcon(IconHelper::getIcon("circle_filled", color));
                }
            }
            itemMap[id] = item;
        }

        // --- 第二步：挂载物理层级树 ---
        for (const auto& cat : categories) {
            int id = cat["id"].toInt();
            if (!itemMap.contains(id)) continue;

            QStandardItem* item = itemMap[id];
            
            // B. [物理挂载逻辑]：保持分类原有的父子层级
            int parentId = cat["parent_id"].toInt();
            if (parentId > 0 && itemMap.contains(parentId)) {
                itemMap[parentId]->appendRow(item);
            } else {
                userGroup->appendRow(item);
            }
        }

        // A. [镜像模式逻辑]：提取置顶项独立排序兵分别挂载到快速访问中
        QList<QVariantMap> pinnedCats;
        for (const auto& cat : categories) {
            if (cat["is_pinned"].toBool()) {
                pinnedCats.append(cat);
            }
        }
        std::sort(pinnedCats.begin(), pinnedCats.end(), [](const QVariantMap& a, const QVariantMap& b) {
            return a["pinned_sort_order"].toInt() < b["pinned_sort_order"].toInt();
        });

        for (const auto& cat : pinnedCats) {
            int id = cat["id"].toInt();
            if (!itemMap.contains(id)) continue;

            QString name = cat["name"].toString();
            QString color = cat["color"].toString();
            bool hasPassword = !cat["password"].toString().isEmpty();
            int count = counts.value("cat_" + QString::number(id), 0).toInt();

            QString display = QString("%1 (%2)").arg(name).arg(count);
            QStandardItem* mirror = new QStandardItem(display); 
            mirror->setData("category", TypeRole);
            mirror->setData(id, IdRole);
            mirror->setData(color, ColorRole);
            mirror->setData(name, NameRole);
            mirror->setData(true, PinnedRole);
            mirror->setData(hasPassword, HasPasswordRole);
            mirror->setData(true, FavMirrorRole); // 2026-04-20 [NEW] 打上标记
            mirror->setEditable(false); 
            // 开启拖拽许可，但拒绝被 Drop (保护不产生子级)
            mirror->setFlags(mirror->flags() | Qt::ItemIsDragEnabled);
                
            // 镜像项根据锁定状态显示对应图标
            bool isLocked = DatabaseManager::instance().isCategoryLocked(id);
            if (hasPassword) {
                mirror->setIcon(IconHelper::getIcon(isLocked ? "lock" : "unlock", isLocked ? "#aaaaaa" : color));
            } else {
                mirror->setIcon(IconHelper::getIcon("pin_vertical", color));
            }
            favGroup->appendRow(mirror);
        }
    }
    endResetModel();
}

QVariant CategoryModel::data(const QModelIndex& index, int role) const {
    if (role == Qt::EditRole) {
        return QStandardItemModel::data(index, NameRole);
    }
    return QStandardItemModel::data(index, role);
}

bool CategoryModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (role == Qt::EditRole) {
        QString newName = value.toString().trimmed();
        int id = index.data(IdRole).toInt();
        if (!newName.isEmpty() && id > 0) {
            // 2026-04-18 移除冗余的异步刷新，DatabaseManager::renameCategory 已发出 categoriesChanged 信号
            if (DatabaseManager::instance().renameCategory(id, newName)) {
                return true;
            }
        }
        return false;
    }
    return QStandardItemModel::setData(index, value, role);
}

Qt::DropActions CategoryModel::supportedDropActions() const {
    return Qt::MoveAction;
}

bool CategoryModel::dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent) {
    QModelIndex actualParent = parent;
    if (m_draggingId != -1) {
        bool needsRedirect = false;
        
        bool isTargetMirrorGroup = false;
        if (actualParent.isValid()) {
            QStandardItem* tempParent = itemFromIndex(actualParent);
            if (tempParent) {
                if (tempParent->data(NameRole).toString() == "快速访问") {
                    isTargetMirrorGroup = true;
                } else if (tempParent->data(FavMirrorRole).toBool()) {
                    isTargetMirrorGroup = true;
                }
            }
        }

        // 核心约束拦截墙：任何跨组的越权拖放一律视为非法的强制拒敌
        if (m_draggingMirror != isTargetMirrorGroup && actualParent.isValid()) {
            return false;
        }

        if (!actualParent.isValid()) {
            needsRedirect = true;
        } else {
            QStandardItem* targetItem = itemFromIndex(actualParent);
            if (targetItem) {
                QString type = targetItem->data(TypeRole).toString();
                if (type != "category" && targetItem->data(NameRole).toString() != "我的分类" && targetItem->data(NameRole).toString() != "快速访问") {
                    needsRedirect = true;
                }
            } else {
                needsRedirect = true;
            }
        }

        if (needsRedirect) {
            QString targetGroupName = m_draggingMirror ? "快速访问" : "我的分类";
            for (int i = 0; i < rowCount(); ++i) {
                QStandardItem* it = item(i);
                if (it && it->data(NameRole).toString() == targetGroupName) {
                    actualParent = index(i, 0);
                    // 2026-05-04 修复拖拽位置偏移：重定向时保持原始row意图
                    if (row == -1) {
                        // 保持拖拽到末尾的意图
                        row = it->rowCount();
                    } else if (row > it->rowCount()) {
                        // 防止超出范围
                        row = it->rowCount();
                    }
                    break;
                }
            }
        }
    }

    // 提前声明parentItem，避免作用域问题
    QStandardItem* parentItem = nullptr;
    
    if (actualParent.isValid()) {
        parentItem = itemFromIndex(actualParent);
        if (!parentItem) return false;
        
        QString type = parentItem->data(TypeRole).toString();
        QString name = parentItem->data(NameRole).toString();
        if (type != "category" && name != "我的分类" && name != "快速访问") {
            return false; 
        }
    } else {
        return false; 
    }

    // 2026-05-04 修复拖拽位置偏移问题：确保row参数正确传递
    int actualRow = row;
    if (actualRow == -1) {
        // 拖拽到容器末尾时的处理
        actualRow = parentItem->rowCount();
    }
    
    bool ok = QStandardItemModel::dropMimeData(data, action, actualRow, column, actualParent);
    if (ok && action == Qt::MoveAction) {
        QPersistentModelIndex persistentParent = actualParent;
        QTimer::singleShot(0, this, [this, persistentParent]() {
            syncOrders(persistentParent);
        });
    } else {
        m_draggingId = -1; 
    }
    return ok;
}

void CategoryModel::syncOrders(const QModelIndex& parent) {
    QStandardItem* parentItem = parent.isValid() ? itemFromIndex(parent) : invisibleRootItem();
    
    // 如果重定向上来的不是我们要的有效容器，寻找正确的
    if (parentItem == invisibleRootItem() || (parentItem->data(TypeRole).toString() != "category" && 
        parentItem->data(NameRole).toString() != "我的分类" && parentItem->data(NameRole).toString() != "快速访问")) {
        
        QString fallbackName = m_draggingMirror ? "快速访问" : "我的分类";
        for (int i = 0; i < rowCount(); ++i) {
            QStandardItem* it = item(i);
            if (it->data(NameRole).toString() == fallbackName) {
                parentItem = it;
                break;
            }
        }
    }

    QList<int> categoryIds;
    int parentId = -1;
    bool isFavGroup = false;

    QString parentType = parentItem->data(TypeRole).toString();
    QString parentName = parentItem->data(NameRole).toString();
    if (parentType == "category") {
        parentId = parentItem->data(IdRole).toInt();
    } else if (parentName == "我的分类") {
        parentId = -1; 
    } else if (parentName == "快速访问") {
        isFavGroup = true;
    } else {
        return;
    }

    QSet<int> seenIds;
    for (int i = 0; i < parentItem->rowCount(); ++i) {
        QStandardItem* child = parentItem->child(i);
        if (child->data(TypeRole).toString() == "category") {
            int id = child->data(IdRole).toInt();
            if (seenIds.contains(id)) continue;
            seenIds.insert(id);
            categoryIds << id;
        }
    }
    
    if (!categoryIds.isEmpty()) {
        if (isFavGroup) {
            DatabaseManager::instance().updatePinnedCategoryOrder(categoryIds);
        } else {
            DatabaseManager::instance().updateCategoryOrder(parentId, categoryIds);
        }
    }
    
    // 2026-05-04 修复快速访问拖拽排序跳转问题：延迟重置拖拽状态，避免与refreshSidebar产生竞争
    QTimer::singleShot(0, this, [this]() {
        m_draggingId = -1;
        m_draggingMirror = false;
    });
}
