#include "CategoryModel.h"
#include "../meta/CategoryRepo.h"

#include "UiHelper.h"
#include <QMimeData>
#include <QFileInfo>
#include <QFont>
#include <QTimer>
#include <QSet>
#include <QMap>
#include "../core/AppConfig.h"
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
    // 2026-06-xx 物理修复：移除对 m_isFirstLoad 的强行拦截。
    // 理由：refresh() 必须允许重建树结构，否则在应用期间增删改“分类”时 UI 无法感知。
    // 异步高频刷新应由 CategoryPanel::requestRefresh 及其内部的 updateStatistics 承载。
    m_isFirstLoad = false;

    // 2026-06-xx 物理修复：废除破坏性的 clear()，改用 beginResetModel 手动管理。
    // 理由：clear() 会提前发射重置信号，导致 UI 在数据还没填充时就尝试恢复展开状态，引发折叠。
    beginResetModel();
    
    // 清理旧项
    removeRows(0, rowCount());
    
    QStandardItem* root = invisibleRootItem();

    // 1. 系统模块 (同步构建 - 8项)
    if (m_type == System || m_type == Both) {
        // 2026-06-xx 物理削峰：refresh 仅构建树结构，计数逻辑剥离至异步 updateStatistics
        // 理由：getSystemCounts() 涉及全量内存盘点，在 UI 线程执行会导致假死。
        auto addSystemItem = [&](const QString& name, const QString& type, const QString& icon, const QString& color, int sysId) {
            // 2026-06-xx 物理修复：杜绝采用“...”占位符，默认显示为 (0)
            QStandardItem* item = new QStandardItem(QString("%1 (0)").arg(name));
            item->setData(type, TypeRole);
            item->setData(name, NameRole);
            item->setData(color, ColorRole); 
            // 2026-06-xx 物理修复：为系统项分配负数 ID，彻底消除与数据库 ID (0/正数) 的歧义冲突
            item->setData(sysId, IdRole);
            item->setEditable(false); 
            item->setIcon(UiHelper::getIcon(icon, QColor(color), 16));
            root->appendRow(item);
        };

        // [还原] 还原原始设计的语义化图标与配色
        // 物理分配负值 ID 空间
        addSystemItem("全部数据", "all", "all_data", "#3498db", -1);
        addSystemItem("未标签", "untagged", "untagged", "#7f8c8d", -3);
        addSystemItem("最近访问", "recently_visited", "clock", "#9b59b6", -6);
        addSystemItem("失效数据", "invalid_data", "invalid_data", "#f1c40f", -9);
        addSystemItem("标签管理", "tags", "tag", "#1abc9c", -7);
        addSystemItem("回收站", "trash", "trash", "#e74c3c", -8);
    }

    // 2. 快速访问模块
    QStandardItem* favGroup = nullptr;
    if (m_type == Both || m_type == User) {
        favGroup = new QStandardItem("快速访问");
        favGroup->setData("快速访问", NameRole);
        favGroup->setSelectable(false);
        favGroup->setEditable(false);
        favGroup->setIcon(UiHelper::getIcon("folder_filled", QColor("#FFFFFF"), 16));
        
        QFont font = favGroup->font();
        font.setBold(true);
        favGroup->setFont(font);
        favGroup->setForeground(QColor("#FFFFFF"));
        root->appendRow(favGroup);

    }

    // 3. 我的分类模块
    QStandardItem* userGroup = nullptr;
    if (m_type == User || m_type == Both) {
        userGroup = new QStandardItem("我的分类");
        userGroup->setData("我的分类", NameRole);
        userGroup->setSelectable(false);
        userGroup->setEditable(false);
        userGroup->setFlags(userGroup->flags() | Qt::ItemIsDropEnabled);
        userGroup->setIcon(UiHelper::getIcon("folder_filled", QColor("#FFFFFF"), 16));
        
        QFont font = userGroup->font();
        font.setBold(true);
        userGroup->setFont(font);
        userGroup->setForeground(QColor("#FFFFFF"));
        root->appendRow(userGroup);

        auto categories = CategoryRepo::getAll();
        QMap<int, QStandardItem*> itemMap;
        QMap<int, Category> catMap;

        // 先创建所有分类节点，但不挂载
        for (const auto& cat : categories) {
            catMap[cat.id] = cat;
            int id = cat.id;
            QString name = QString::fromStdWString(cat.name);
            QString color = QString::fromStdWString(cat.color).isEmpty() ? "#555555" : QString::fromStdWString(cat.color);

            // 2026-06-xx 物理修复：杜绝采用“...”占位符，默认显示为 (0)
            QStandardItem* item = new QStandardItem(QString("%1 (0)").arg(name));
            item->setData("category", TypeRole);
            item->setData(id, IdRole);
            item->setData(color, ColorRole);
            item->setData(name, NameRole);
            item->setData(cat.pinned, PinnedRole);
            item->setData(cat.encrypted, EncryptedRole);
            item->setData(QString::fromStdWString(cat.encryptHint), EncryptHintRole);
            item->setFlags(item->flags() | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
            
            if (cat.encrypted && !m_unlockedIds.contains(id)) {
                item->setIcon(UiHelper::getIcon("lock", QColor("#aaaaaa"), 16));
            } else {
                item->setIcon(UiHelper::getIcon("folder_filled", QColor(color), 16));
            }
            itemMap[id] = item;
        }

        // 2026-06-xx 按照用户要求回归“镜像模式”：实体保留，置顶生成快捷镜像
        // 逻辑：1. 在“我的分类”中构建完整树；2. 将置顶项镜像一份到“快速访问”
        
        // 1. 在“我的分类”构建完整原始树 (不收置顶状态位移干扰)
        for (const auto& cat : categories) {
            int id = cat.id;
            QStandardItem* item = itemMap[id];
            int parentId = cat.parentId;

            if (parentId > 0 && itemMap.contains(parentId)) {
                itemMap[parentId]->appendRow(item);
            } else if (userGroup) {
                userGroup->appendRow(item);
            }
        }

        // 2. 为置顶项在“快速访问”中创建虚拟镜像 (快捷入口)
        if (favGroup) {
            for (const auto& cat : categories) {
                if (cat.pinned) {
                    int id = cat.id;
                    QString name = QString::fromStdWString(cat.name);
                    QString color = QString::fromStdWString(cat.color).isEmpty() ? "#555555" : QString::fromStdWString(cat.color);
                    
                    // 2026-07-xx 视觉对齐：镜像节点初始也应携带 (0) 占位符，符合《红线规范》
                    QStandardItem* mirror = new QStandardItem(QString("%1 (0)").arg(name));
                    mirror->setData("category", TypeRole);
                    mirror->setData(id, IdRole);
                    mirror->setData(color, ColorRole);
                    mirror->setData(name, NameRole);
                    mirror->setData(true, PinnedRole);
                    
                    if (cat.encrypted && !m_unlockedIds.contains(id)) {
                        mirror->setIcon(UiHelper::getIcon("lock", QColor("#aaaaaa"), 16));
                    } else {
                        mirror->setIcon(UiHelper::getIcon("folder_filled", QColor(color), 16));
                    }
                    favGroup->appendRow(mirror);
                }
            }
        }
    }
    
    endResetModel();
}

void CategoryModel::updateSystemCounts() {
    auto counts = CategoryRepo::getSystemCounts();
    for (int i = 0; i < invisibleRootItem()->rowCount(); ++i) {
        QStandardItem* item = invisibleRootItem()->child(i);
        QString type = item->data(TypeRole).toString();
        if (counts.contains(type)) {
            QString name = item->data(NameRole).toString();
            item->setText(QString("%1 (%2)").arg(name).arg(counts[type]));
        }
    }
}

void CategoryModel::updateStatistics(const QMap<QString, int>& sysCounts, const QMap<int, int>& catCounts) {
    // 2026-06-xx 极致性能优化：采用深度遍历进行局部 setData 更新，杜绝 beginResetModel 引发视图抖动
    std::function<void(QStandardItem*)> updateItem;
    updateItem = [&](QStandardItem* parent) {
        for (int i = 0; i < parent->rowCount(); ++i) {
            QStandardItem* item = parent->child(i);
            QString type = item->data(TypeRole).toString();
            QString name = item->data(NameRole).toString();
            int id = item->data(IdRole).toInt();

            bool changed = false;
            if (id < 0) { // 系统项
                int count = sysCounts.value(type, 0);
                QString newText = QString("%1 (%2)").arg(name).arg(count);
                if (item->text() != newText) {
                    item->setText(newText);
                    changed = true;
                }
            } else if (type == "category" && id > 0) { // 用户分类
                int count = catCounts.value(id, 0);
                QString newText = QString("%1 (%2)").arg(name).arg(count);
                if (item->text() != newText) {
                    item->setText(newText);
                    changed = true;
                }
            }

            // 递归处理子项
            if (item->hasChildren()) {
                updateItem(item);
            }
        }
    };

    updateItem(invisibleRootItem());
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
                    // 2026-11-xx 按照 Plan-113：双向重命名同步 (逻辑 -> 物理)
                    // 核心逻辑：若该分类是顶级分类且其名称与某个托管库对应，则同步修改物理文件夹
                    if (cat.parentId == 0) {
                        const auto drives = QDir::drives();
                        for (const auto& drive : drives) {
                            QString driveRoot = drive.absolutePath();
                            QString oldName = QString::fromStdWString(cat.name);
                            QString oldPath = driveRoot + oldName;
                            QString newPath = driveRoot + newName;

                            // 仅当物理文件夹确实存在且符合 ArcMeta.Library_ 规范时执行
                            if (oldName.startsWith("ArcMeta.Library_") && QDir(oldPath).exists()) {
                                ShellHelper::renameItem(oldPath, newPath);
                                // 注意：物理重命名成功后，USN 会捕获变动并由物理层反向更新逻辑名
                                // 此处先更新 DB 以确保即时性
                            }
                        }
                    }

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
