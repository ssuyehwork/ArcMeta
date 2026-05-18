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
    // 2026-03-22 [NEW] 核心修复：归类目标变化时，使用局部更新函数 updateExtensionIcons 替代 refresh，
    // 从而防止 refresh -> clear() 导致的 QTreeView 节点全部折叠的问题。
    connect(&DatabaseManager::instance(), &DatabaseManager::extensionTargetCategoryIdChanged, this, &CategoryModel::updateExtensionIcons);
    refresh();
}

void CategoryModel::refresh() {
    clear();
    QStandardItem* root = invisibleRootItem();
    QVariantMap counts = DatabaseManager::instance().getCounts();

    if (m_type == System || m_type == Both) {
        auto addSystemItem = [&](const QString& name, const QString& type, const QString& icon, const QString& color = "#aaaaaa") {
            int count = counts.value(type, 0).toInt();
            QString display = QString("%1 (%2)").arg(name).arg(count);
            QStandardItem* item = new QStandardItem(display);
            item->setData(type, TypeRole);
            item->setData(name, NameRole);
            item->setData(color, ColorRole); // 设置颜色角色
            item->setEditable(false); // 系统项目不可重命名
            item->setIcon(IconHelper::getIcon(icon, color));
            root->appendRow(item);
        };

        addSystemItem("全部数据", "all", "all_data", "#3498db");
        addSystemItem("今日数据", "today", "today", "#2ecc71");
        addSystemItem("昨日数据", "yesterday", "today", "#f39c12"); // 使用橙色区分
        addSystemItem("最近访问", "recently_visited", "clock", "#9b59b6");
        addSystemItem("未分类", "uncategorized", "uncategorized", "#e67e22");
        // 2026-03-13 按照用户要求：修改“未标签”项的图标颜色为 #62BAC1
        addSystemItem("未标签", "untagged", "untagged", "#62BAC1");
        // 2026-03-13 按照用户要求：修改“收藏”项的图标为 bookmark_filled，颜色为 #F2B705
        addSystemItem("收藏", "bookmark", "bookmark_filled", "#F2B705");
        // 2026-03-13 按照用户最高指令：凡是 trash 图标必须为红色 (#e74c3c)
        addSystemItem("回收站", "trash", "trash", "#e74c3c");
    }
    
    if (m_type == User || m_type == Both) {
        // 用户分类
        // [CRITICAL] 锁定：必须使用 NameRole 稳定识别，DisplayRole 包含计数
        int allCount = counts.value("all", 0).toInt();
        int uncatCount = counts.value("uncategorized", 0).toInt();
        int userTotalCount = qMax(0, allCount - uncatCount);
        
        QStandardItem* userGroup = new QStandardItem(QString("我的分类 (%1)").arg(userTotalCount));
        userGroup->setData("我的分类", NameRole);
        userGroup->setSelectable(false);
        userGroup->setEditable(false);
        userGroup->setFlags(userGroup->flags() | Qt::ItemIsDropEnabled);
        userGroup->setIcon(IconHelper::getIcon("branch", "#FFFFFF"));
        
        // 设为粗体白色
        QFont font = userGroup->font();
        font.setBold(true);
        userGroup->setFont(font);
        userGroup->setForeground(QColor("#FFFFFF"));
        
        root->appendRow(userGroup);

        auto categories = DatabaseManager::instance().getAllCategories();
        QMap<int, QStandardItem*> itemMap;

        bool hideLocked = DatabaseManager::instance().isLockedCategoriesHidden();
        
        for (const auto& cat : categories) {
            int id = cat["id"].toInt();
            bool hasPassword = !cat["password"].toString().isEmpty();
            
            // [USER_REQUEST] 如果开启了隐藏加锁分类，且该分类有密码，则跳过展示
            if (hideLocked && hasPassword) continue;

            int count = counts.value("cat_" + QString::number(id), 0).toInt();
            QString name = cat["name"].toString();
            bool isPinned = cat["is_pinned"].toBool();
            QString display = QString("%1 (%2)").arg(name).arg(count);
            QStandardItem* item = new QStandardItem(display);
            item->setData("category", TypeRole);
            item->setData(id, IdRole);
            item->setData(cat["color"], ColorRole);
            item->setData(name, NameRole);
            item->setData(isPinned, PinnedRole);
            item->setData(hasPassword, HasPasswordRole); // 2026-03-22 [NEW] 记录密码状态供局部更新
            item->setFlags(item->flags() | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
            
            // 2026-03-15 按照用户要求：锁住显 lock，解锁显 unlock，有枷锁分类严禁显示圆圈
            bool isLocked = DatabaseManager::instance().isCategoryLocked(id);
            int extensionTargetId = DatabaseManager::instance().extensionTargetCategoryId();

            if (id == extensionTargetId) {
                // 2026-03-22 [NEW] 按照用户要求：如果该分类被标记为“归类到此分类”，图标显示为 toggle_right
                item->setIcon(IconHelper::getIcon("toggle_right", cat["color"].toString()));
            } else if (hasPassword) {
                if (isLocked) {
                    item->setIcon(IconHelper::getIcon("lock", "#aaaaaa"));
                } else {
                    // 已解锁状态，显式使用新 unlock 图标，颜色遵循分类原色
                    item->setIcon(IconHelper::getIcon("unlock", cat["color"].toString()));
                }
            } else if (isPinned) {
                // 2026-03-xx 核心修正：恢复分类置顶图标跟随分类自身颜色的原有逻辑
                item->setIcon(IconHelper::getIcon("pin_vertical", cat["color"].toString()));
            } else {
                item->setIcon(IconHelper::getIcon("circle_filled", cat["color"].toString()));
            }
            itemMap[cat["id"].toInt()] = item;
        }

        for (const auto& cat : categories) {
            int id = cat["id"].toInt();
            // [CRITICAL] 物理消除空白行：仅当该分类本身是可见（已在 itemMap 中）时，才处理其挂载逻辑。
            if (!itemMap.contains(id)) continue;

            int parentId = cat["parent_id"].toInt();
            if (parentId > 0) {
                if (itemMap.contains(parentId)) {
                    // 挂载到父分类
                    itemMap[parentId]->appendRow(itemMap[id]);
                } else {
                    // [BUG_FIX] 彻底根除空白项逻辑缺陷：
                    // 如果父分类被隐藏（不在 itemMap 中），则其子分类即便可见也必须同步隐藏，
                    // 严禁让子项由于找不到父节点而漂移到根目录下，从而导致层级混乱和空白间隙。
                    continue; 
                }
            } else {
                // 顶级分类，挂载到“我的分类”
                userGroup->appendRow(itemMap[id]);
            }
        }
    }
}

QVariant CategoryModel::data(const QModelIndex& index, int role) const {
    // [CRITICAL] 必须在此处拦截 EditRole 并返回 NameRole（不含计数），否则会因 QStandardItem 的默认逻辑导致 DisplayRole 计数丢失。
    if (role == Qt::EditRole) {
        return QStandardItemModel::data(index, NameRole);
    }
    return QStandardItemModel::data(index, role);
}

bool CategoryModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    // [CRITICAL] 拦截编辑提交，同步更新数据库
    if (role == Qt::EditRole) {
        QString newName = value.toString().trimmed();
        int id = index.data(IdRole).toInt();
        if (!newName.isEmpty() && id > 0) {
            if (DatabaseManager::instance().renameCategory(id, newName)) {
                // 异步刷新以确保 UI 显示最新的名称及计数
                QTimer::singleShot(0, [this]() { this->refresh(); });
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
    /* [MODIFIED] 2026-03-11 排序重定向逻辑增强 */
    QModelIndex actualParent = parent;
    
    // 处理正在拖拽分类的情况（m_draggingId != -1）
    if (m_draggingId != -1) {
        bool needsRedirect = false;
        if (!actualParent.isValid()) {
            needsRedirect = true;
        } else {
            QStandardItem* targetItem = itemFromIndex(actualParent);
            if (targetItem) {
                QString type = targetItem->data(TypeRole).toString();
                // [CRITICAL] 锁定：使用 NameRole 稳定匹配。如果目标不是分类也不是“我的分类”容器，强制归位于容器
                if (type != "category" && targetItem->data(NameRole).toString() != "我的分类") {
                    needsRedirect = true;
                }
            } else {
                needsRedirect = true;
            }
        }

        if (needsRedirect) {
            // 寻找 "我的分类" 容器索引
            for (int i = 0; i < rowCount(); ++i) {
                QStandardItem* it = item(i);
                if (it && it->data(NameRole).toString() == "我的分类") {
                    actualParent = index(i, 0);
                    break;
                }
            }
        }
    }

    // 再次检查重定向后的合法性，防止非法释放到系统分类中
    if (actualParent.isValid()) {
        QStandardItem* parentItem = itemFromIndex(actualParent);
        if (!parentItem) return false;
        
        QString type = parentItem->data(TypeRole).toString();
        QString name = parentItem->data(NameRole).toString();
        if (type != "category" && name != "我的分类") {
            return false; 
        }
    } else {
        return false; 
    }

    // [MODIFIED] 2026-03-11 原生排序数据释放
    bool ok = QStandardItemModel::dropMimeData(data, action, row, column, actualParent);
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

void CategoryModel::updateExtensionIcons() {
    // 2026-03-22 [NEW] 按照用户要求：局部更新“归类到此分类”图标，不触发表重载以保持展开状态。
    int targetId = DatabaseManager::instance().extensionTargetCategoryId();
    
    // 递归遍历所有项
    QList<QStandardItem*> stack;
    for (int i = 0; i < rowCount(); ++i) stack.append(item(i));

    while (!stack.isEmpty()) {
        QStandardItem* item = stack.takeLast();
        if (item->data(TypeRole).toString() == "category") {
            int id = item->data(IdRole).toInt();
            QString color = item->data(ColorRole).toString();
            bool isPinned = item->data(PinnedRole).toBool();
            bool hasPassword = item->data(HasPasswordRole).toBool();
            
            if (id == targetId) {
                // 2026-03-22 [REPAIR] 修正颜色：归类目标图标颜色应遵循分类原色，严禁硬编码绿色。
                item->setIcon(IconHelper::getIcon("toggle_right", color));
            } else {
                // 恢复默认图标逻辑（参考 refresh 内部逻辑）
                if (hasPassword) {
                    bool isLocked = DatabaseManager::instance().isCategoryLocked(id);
                    item->setIcon(IconHelper::getIcon(isLocked ? "lock" : "unlock", isLocked ? "#aaaaaa" : color));
                } else if (isPinned) {
                    item->setIcon(IconHelper::getIcon("pin_vertical", color));
                } else {
                    item->setIcon(IconHelper::getIcon("circle_filled", color));
                }
            }
        }
        for (int i = 0; i < item->rowCount(); ++i) stack.append(item->child(i));
    }
}

void CategoryModel::syncOrders(const QModelIndex& parent) {
    QStandardItem* parentItem = parent.isValid() ? itemFromIndex(parent) : invisibleRootItem();
    
    // [CRITICAL] 锁定：核心同步逻辑，必须通过 NameRole 匹配“我的分类”来定位用户分类根节点
    if (parentItem == invisibleRootItem() || (parentItem->data(TypeRole).toString() != "category" && parentItem->data(NameRole).toString() != "我的分类")) {
        for (int i = 0; i < rowCount(); ++i) {
            QStandardItem* it = item(i);
            if (it->data(NameRole).toString() == "我的分类") {
                parentItem = it;
                break;
            }
        }
    }

    QList<int> categoryIds;
    int parentId = -1;
    
    // 再次确认父节点类型，确保同步到正确的数据库父 ID
    QString parentType = parentItem->data(TypeRole).toString();
    if (parentType == "category") {
        parentId = parentItem->data(IdRole).toInt();
    } else if (parentItem->data(NameRole).toString() == "我的分类") {
        // [CRITICAL] 锁定：匹配“我的分类”时，强制将 parentId 设为 -1，代表顶级分类
        parentId = -1; 
    } else {
        return; // 依然找不到有效的用户分类容器，放弃同步以防破坏数据
    }

    // 收集所有分类 ID，维护当前物理顺序
    QSet<int> seenIds;
    for (int i = 0; i < parentItem->rowCount(); ++i) {
        QStandardItem* child = parentItem->child(i);
        if (child->data(TypeRole).toString() == "category") {
            int id = child->data(IdRole).toInt();
            if (seenIds.contains(id)) continue; // 深度防御：跳过重复项
            seenIds.insert(id);
            categoryIds << id;
        }
    }
    
    if (!categoryIds.isEmpty()) {
        DatabaseManager::instance().updateCategoryOrder(parentId, categoryIds);
    }
    
    m_draggingId = -1; // 完成同步后重置
}
