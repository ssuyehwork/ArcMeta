#include "CategoryModel.h"
#include "../meta/CategoryRepo.h"
#include "../meta/MetadataManager.h"
#include "../meta/StatisticsService.h"
#include "../core/VolumeOnlineManager.h"

#include "UiHelper.h"
#include <functional>
#include <QtConcurrent>
#include <QMimeData>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QFont>
#include <QTimer>
#include <QSet>
#include <QMap>
#include <algorithm>
#include "../core/AppConfig.h"
#include <QApplication>

namespace ArcMeta {

CategoryModel::CategoryModel(Type type, QObject* parent) 
    : QStandardItemModel(parent), m_type(type) 
{
    connect(&StatisticsService::instance(), &StatisticsService::statisticsUpdated, 
            this, &CategoryModel::updateStatisticsWithSnapshot);
}

void CategoryModel::setUnlockedIds(const QSet<int>& ids) {
    m_unlockedIds = ids;
}

void CategoryModel::deferredRefresh() {
    refresh();
}

void CategoryModel::refresh() {
    m_isFirstLoad = false;

    // 1. 0ms 纯内存只读读取（零 SQL、零磁盘 I/O）
    auto categories = CategoryRepo::getCachedAll();
    StatisticsSnapshot snapshot = StatisticsService::instance().getCachedSnapshot();

    beginResetModel();
    removeRows(0, rowCount());
    
    QStandardItem* root = invisibleRootItem();

    // ----------------------------------------------------
    // 层级一：静态分类区（固定顶部）
    // ----------------------------------------------------
    if (m_type == System || m_type == Both) {
        auto addSystemItem = [&](const QString& name, const QString& type, const QString& icon, const QString& color, int sysId) {
            int count = snapshot.systemCounts.value(type, 0);
            QStandardItem* item = new QStandardItem(QString("%1 (%2)").arg(name).arg(count));
            item->setData(type, TypeRole);
            item->setData(name, NameRole);
            item->setData(color, ColorRole); 
            item->setData(sysId, IdRole);
            item->setEditable(false); 
            item->setIcon(UiHelper::getIcon(icon, QColor(color), 16));
            root->appendRow(item);
        };

        addSystemItem("全部数据", "all", "all_data", "#3498db", -1);
        addSystemItem("未分类", "uncategorized", "uncategorized", "#95a5a6", -2);
        addSystemItem("未标签", "untagged", "untagged", "#7f8c8d", -3);
        addSystemItem("最近访问", "recently_visited", "clock", "#9b59b6", -6);
        addSystemItem("标签管理", "tags", "tag", "#1abc9c", -7);
        addSystemItem("回收站", "trash", "trash", "#e74c3c", -8);
    }

    // ----------------------------------------------------
    // 恢复原版：直接使用数据库原始名称 (arcmeta.library_*) 与图标
    // ----------------------------------------------------
    if (m_type == Both || m_type == User) {
        QSet<QString> onlineDrives = VolumeOnlineManager::instance().getOnlineDrives();
        for (const auto& cat : categories) {
            if (cat.kind == CategoryKind::SystemLibrary && cat.parentId == 0) {
                QString origName = QString::fromStdWString(cat.name).toLower();

                // 🛡️ 离线拦截：如果对应的盘符已拔出/离线，直接跳过，不在侧边栏渲染该节点！
                QString driveLetter = VolumeOnlineManager::extractDriveLetter(origName);
                if (driveLetter.isEmpty() && !cat.physicalPath.empty()) {
                    driveLetter = VolumeOnlineManager::extractDriveLetter(QString::fromStdWString(cat.physicalPath));
                }
                if (!driveLetter.isEmpty() && !onlineDrives.contains(driveLetter.toUpper())) {
                    continue;
                }

                int count = snapshot.libraryCounts.value(cat.id, 0);

                QStandardItem* item = new QStandardItem(QString("%1 (%2)").arg(origName).arg(count));
                item->setData("category", TypeRole);
                item->setData(cat.id, IdRole);
                item->setData("#378ADD", ColorRole);
                item->setData(origName, NameRole);
                item->setData(cat.pinned, PinnedRole);
                item->setData(cat.encrypted, EncryptedRole);
                item->setData(QString::fromStdWString(cat.encryptHint), EncryptHintRole);
                item->setData(static_cast<int>(cat.kind), CategoryKindRole);
                item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                item->setIcon(UiHelper::getIcon("folder_filled", QColor("#378ADD"), 16)); // 恢复原版蓝色文件夹图标
                
                root->appendRow(item);
            }
        }
    }

    // ----------------------------------------------------
    // 层级三：全动态分类区（用户自主管理）
    // ----------------------------------------------------
    QStandardItem* favGroup = nullptr;
    if (m_type == Both || m_type == User) {
        favGroup = new QStandardItem("快速访问");
        favGroup->setData("快速访问", NameRole);
        favGroup->setSelectable(false);
        favGroup->setEditable(false);
        favGroup->setIcon(UiHelper::getIcon("zap_filled", QColor("#F1C40F"), 16)); 
        
        QFont font = favGroup->font();
        font.setBold(true);
        favGroup->setFont(font);
        favGroup->setForeground(QColor("#FFFFFF"));
    }

    QStandardItem* catGroup = nullptr; 
    if (m_type == Both || m_type == User) { 
        catGroup = new QStandardItem(); 
        catGroup->setData("category_root_group", TypeRole); 
        catGroup->setData("文件夹", NameRole); 
        catGroup->setData(CAT_GROUP_SYS_ID, IdRole); 
        catGroup->setSelectable(false); 
        catGroup->setEditable(false); 
        catGroup->setIcon(UiHelper::getIcon("folder_filled", QColor("#378ADD"), 16)); 
 
        QFont font = catGroup->font(); 
        font.setBold(true); 
        catGroup->setFont(font); 
        catGroup->setForeground(QColor("#FFFFFF")); 
    } 

    if (m_type == User || m_type == Both) {
        QMap<int, QStandardItem*> itemMap;

        for (const auto& cat : categories) {
            if (cat.kind == CategoryKind::SystemLibrary) {
                continue; // 系统托管库不纳入自定义文件夹树
            }
            int id = cat.id;
            QString name = QString::fromStdWString(cat.name);
            QString color = QString::fromStdWString(cat.color).isEmpty() ? "#555555" : QString::fromStdWString(cat.color);

            int count = snapshot.userCategoryCounts.value(id, 0);
            QStandardItem* item = new QStandardItem(QString("%1 (%2)").arg(name).arg(count));
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
                QString iconKey = QString::fromStdWString(cat.icon).isEmpty() ? "folder_filled" : QString::fromStdWString(cat.icon);
                item->setIcon(UiHelper::getIcon(iconKey, QColor(color), 16));
            }
            itemMap[id] = item;
        }

        // 组装用户自定义层级树
        for (const auto& cat : categories) {
            if (cat.kind == CategoryKind::SystemLibrary) {
                continue;
            }
            int id = cat.id;
            QStandardItem* item = itemMap[id];
            int parentId = cat.parentId;

            if (parentId == 0) {
                if (catGroup) {
                    catGroup->appendRow(item);
                } else {
                    root->appendRow(item);
                }
            } else if (parentId > 0 && itemMap.contains(parentId)) {
                itemMap[parentId]->appendRow(item);
            }
        }

        // 挂载“快速访问”
        if (favGroup) {
            root->appendRow(favGroup);
        }

        // 挂载用户自定义分类文件夹
        int totalUserFolderCount = 0;
        for (const auto& cat : categories) {
            if (cat.kind != CategoryKind::SystemLibrary) {
                totalUserFolderCount++;
            }
        }

        if (catGroup) {
            catGroup->setText(QString("文件夹 (%1)").arg(totalUserFolderCount));
            root->appendRow(catGroup);
        }

        // 挂载快速访问快捷镜像
        if (favGroup) {
            for (const auto& cat : categories) {
                if (cat.pinned && cat.kind != CategoryKind::SystemLibrary) {
                    int id = cat.id;
                    QString name = QString::fromStdWString(cat.name);
                    QString color = QString::fromStdWString(cat.color).isEmpty() ? "#555555" : QString::fromStdWString(cat.color);
                    
                    int count = snapshot.userCategoryCounts.value(id, 0);
                    QStandardItem* mirror = new QStandardItem(QString("%1 (%2)").arg(name).arg(count));
                    mirror->setData("category", TypeRole);
                    mirror->setData(id, IdRole);
                    mirror->setData(color, ColorRole);
                    mirror->setData(name, NameRole);
                    mirror->setData(true, PinnedRole);
                    
                    if (cat.encrypted && !m_unlockedIds.contains(id)) {
                        mirror->setIcon(UiHelper::getIcon("lock", QColor("#aaaaaa"), 16));
                    } else {
                        QString iconKey = QString::fromStdWString(cat.icon).isEmpty() ? "folder_filled" : QString::fromStdWString(cat.icon);
                        mirror->setIcon(UiHelper::getIcon(iconKey, QColor(color), 16));
                    }
                    favGroup->appendRow(mirror);
                }
            }
        }
    }
    
    endResetModel();

    // 3. 异步触发后台核对账本（算完自动通过 statisticsUpdated 槽函数回填数字）
    StatisticsService::instance().requestFullRecountAsync();
}

void CategoryModel::updateSystemCounts() {
    auto snapshot = StatisticsService::instance().getCachedSnapshot();
    auto counts = snapshot.systemCounts;
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
    // 兼容传统接口
    StatisticsSnapshot snapshot;
    snapshot.systemCounts = sysCounts;
    for (auto it = catCounts.begin(); it != catCounts.end(); ++it) {
        snapshot.libraryCounts[it.key()] = it.value();
        snapshot.userCategoryCounts[it.key()] = it.value();
    }
    updateStatisticsWithSnapshot(snapshot);
}

void CategoryModel::updateStatisticsWithSnapshot(const StatisticsSnapshot& snapshot) {
    std::function<void(QStandardItem*)> updateItem;
    updateItem = [&](QStandardItem* parent) {
        for (int i = 0; i < parent->rowCount(); ++i) {
            QStandardItem* item = parent->child(i);
            QString type = item->data(TypeRole).toString();
            QString name = item->data(NameRole).toString();
            int id = item->data(IdRole).toInt();

            if (id == CAT_GROUP_SYS_ID) { 
                std::function<int(QStandardItem*)> countAllSubFolders; 
                countAllSubFolders = [&](QStandardItem* node) -> int { 
                    int c = 0; 
                    for (int j = 0; j < node->rowCount(); ++j) { 
                        QStandardItem* child = node->child(j); 
                        if (child->data(TypeRole).toString() == "category") c++; 
                        if (child->hasChildren()) c += countAllSubFolders(child); 
                    } 
                    return c; 
                }; 
                int totalFolders = countAllSubFolders(item); 
                item->setText(QString("文件夹 (%1)").arg(totalFolders)); 
            } else if (id < 0) { 
                int count = snapshot.systemCounts.value(type, 0);
                QString newText = QString("%1 (%2)").arg(name).arg(count);
                if (item->text() != newText) {
                    item->setText(newText);
                }
            } else if (type == "category" && id > 0) { 
                int count = 0;
                // 优化：直接从节点属性获取 CategoryKindRole，避免高频调用 RCU 缓存或数据库
                int kind = item->data(CategoryKindRole).toInt();
                if (kind == static_cast<int>(CategoryKind::SystemLibrary)) {
                    count = snapshot.libraryCounts.value(id, 0);
                } else {
                    count = snapshot.userCategoryCounts.value(id, 0);
                }
                QString newText = QString("%1 (%2)").arg(name).arg(count);
                if (item->text() != newText) {
                    item->setText(newText);
                }
            }

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

        if (id == CAT_GROUP_SYS_ID) return false;
        
        if (type == "category" && id > 0) {
            auto categories = CategoryRepo::getAll();
            Category targetCat;
            bool found = false;
            for (const auto& cat : categories) {
                if (cat.id == id) {
                    targetCat = cat;
                    found = true;
                    break;
                }
            }
            if (!found) return false;

            if (targetCat.kind == CategoryKind::SystemLibrary && targetCat.parentId == 0) {
                return false; 
            }

            // 🚀 【重构净化】：Model 不直接跑线程和修改磁盘/数据库，直接发射重命名信号由控制器接收处理
            emit categoryRenameRequested(id, newName);
            return true;
        }
        return false;
    }
    return QStandardItemModel::setData(index, val, role);
}

// -------------------------------------------------------------------------
// 🚨 【拖拽核心重构】：自定义 MimeData + 纯数据库物理重排落盘
// -------------------------------------------------------------------------

QMimeData* CategoryModel::mimeData(const QModelIndexList& indexes) const {
    QMimeData* mimeData = QStandardItemModel::mimeData(indexes);
    if (!indexes.isEmpty() && mimeData) {
        QModelIndex idx = indexes.first();
        int catId = idx.data(IdRole).toInt();
        if (catId > 0) {
            // 打包真实分类 ID
            mimeData->setData("application/x-arcmeta-catid", QByteArray::number(catId));
        }
    }
    return mimeData;
}

Qt::DropActions CategoryModel::supportedDropActions() const {
    return Qt::MoveAction | Qt::CopyAction;
}

bool CategoryModel::dropMimeData(const QMimeData* mimeData, Qt::DropAction action, int row, int column, const QModelIndex& parent) {
    if (!mimeData) return false;

    // 1. 如果是外部物理文件拖入，放行
    if (mimeData->hasUrls()) return true;

    // 2. 如果不是分类内部拖拽，回退
    if (!mimeData->hasFormat("application/x-arcmeta-catid")) {
        return QStandardItemModel::dropMimeData(mimeData, action, row, column, parent);
    }

    int draggedCatId = mimeData->data("application/x-arcmeta-catid").toInt();
    if (draggedCatId <= 0) return false;

    // 3. 计算全新的 targetParentId
    int targetParentId = 0; // 默认挂载在“分类”主组节点下 (parentId = 0)
    if (parent.isValid()) {
        int pId = parent.data(IdRole).toInt();
        if (pId > 0) {
            targetParentId = pId; // 嵌套进入子分类
        }
    }

    // 4. 从数据库获取所有同级分类
    auto allCats = CategoryRepo::getAll();
    std::vector<Category> siblings;
    Category draggedCat;
    bool foundDragged = false;

    for (const auto& cat : allCats) {
        if (cat.id == draggedCatId) {
            draggedCat = cat;
            foundDragged = true;
        } else if (cat.parentId == targetParentId && cat.kind != CategoryKind::SystemLibrary) {
            siblings.push_back(cat);
        }
    }

    if (!foundDragged) return false;

    // 按已有的 sortOrder 升序排列同级项
    std::sort(siblings.begin(), siblings.end(), [](const Category& a, const Category& b) {
        return a.sortOrder < b.sortOrder;
    });

    // 5. 计算全新的插入索引 row
    int insertRow = row;
    if (insertRow < 0 || insertRow > static_cast<int>(siblings.size())) {
        insertRow = static_cast<int>(siblings.size()); // 默认插入尾部
    }

    // 🚀 【重构净化】：拖拽落盘排序动作直接向上派发通知，Model 保持纯净只读
    emit categoryOrderChanged(draggedCatId, targetParentId, insertRow);
    return true; // 物理阻断 Qt 原生深拷贝！
}

} // namespace ArcMeta