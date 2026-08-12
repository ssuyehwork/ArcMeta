#include "CategoryModel.h"
#include "../meta/CategoryRepo.h"
#include "../meta/MetadataManager.h"

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
}

void CategoryModel::setUnlockedIds(const QSet<int>& ids) {
    m_unlockedIds = ids;
}

void CategoryModel::deferredRefresh() {
    refresh();
}

void CategoryModel::refresh() {
    m_isFirstLoad = false;

    auto sysCounts = CategoryRepo::getSystemCounts();
    auto catCountsVec = CategoryRepo::getCounts();
    QMap<int, int> catCounts;
    for (const auto& entry : catCountsVec) {
        catCounts[entry.first] = entry.second;
    }

    beginResetModel();
    removeRows(0, rowCount());
    
    QStandardItem* root = invisibleRootItem();

    // 1. 系统逻辑桶
    if (m_type == System || m_type == Both) {
        auto addSystemItem = [&](const QString& name, const QString& type, const QString& icon, const QString& color, int sysId) {
            int count = sysCounts.value(type, 0);
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

    // 2. “快速访问”分组节点
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

    // 3. “文件夹”主标题节点
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
        auto categories = CategoryRepo::getAll();
        QMap<int, QStandardItem*> itemMap;
        QMap<int, Category> catMap;

        for (const auto& cat : categories) {
            catMap[cat.id] = cat;
            int id = cat.id;
            QString name = QString::fromStdWString(cat.name);
            QString color = QString::fromStdWString(cat.color).isEmpty() ? "#555555" : QString::fromStdWString(cat.color);

            int count = catCounts.value(id, 0);
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

        // 4. 物理托管库根分类
        for (const auto& cat : categories) {
            int id = cat.id;
            QStandardItem* item = itemMap[id];
            int parentId = cat.parentId;

            if (parentId == 0) {
                if (cat.kind == CategoryKind::SystemLibrary) {
                    root->appendRow(item);
                }
            } else if (parentId > 0 && itemMap.contains(parentId)) {
                itemMap[parentId]->appendRow(item);
            }
        }

        // 5. 挂载“快速访问”
        if (favGroup) {
            root->appendRow(favGroup);
        }

        // 6. 挂载用户自定义分类至“分类”主标题下
        // 按照产品标准：全量统计全树所有深度的自定义文件夹总数（对应用户原话：“按照产品标准：全量统计全树所有深度的自定义文件夹总数”）
        int totalUserFolderCount = 0;
        for (const auto& cat : categories) {
            if (cat.kind != CategoryKind::SystemLibrary) {
                totalUserFolderCount++;
            }
        }

        for (const auto& cat : categories) {
            int id = cat.id;
            QStandardItem* item = itemMap[id];
            int parentId = cat.parentId;

            if (parentId == 0) {
                if (cat.kind != CategoryKind::SystemLibrary) {
                    if (catGroup) {
                        catGroup->appendRow(item);
                    } else {
                        root->appendRow(item);
                    }
                }
            }
        }

        if (catGroup) {
            catGroup->setText(QString("文件夹 (%1)").arg(totalUserFolderCount));
            root->appendRow(catGroup);
        }

        // 7. 挂载快速访问快捷镜像
        if (favGroup) {
            for (const auto& cat : categories) {
                if (cat.pinned) {
                    int id = cat.id;
                    QString name = QString::fromStdWString(cat.name);
                    QString color = QString::fromStdWString(cat.color).isEmpty() ? "#555555" : QString::fromStdWString(cat.color);
                    
                    int count = catCounts.value(id, 0);
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
    std::function<void(QStandardItem*)> updateItem;
    updateItem = [&](QStandardItem* parent) {
        for (int i = 0; i < parent->rowCount(); ++i) {
            QStandardItem* item = parent->child(i);
            QString type = item->data(TypeRole).toString();
            QString name = item->data(NameRole).toString();
            int id = item->data(IdRole).toInt();

            if (id == CAT_GROUP_SYS_ID) {
                // 动态更新时，同样显示包含所有深度的子文件夹总数（对应用户原话：“动态更新时，同样显示包含所有深度的子文件夹总数”）
                std::function<int(QStandardItem*)> countCategories;
                countCategories = [&](QStandardItem* node) -> int {
                    int c = 0;
                    for (int j = 0; j < node->rowCount(); ++j) {
                        QStandardItem* child = node->child(j);
                        if (child->data(TypeRole).toString() == "category") {
                            c++;
                        }
                        if (child->hasChildren()) {
                            c += countCategories(child);
                        }
                    }
                    return c;
                };
                int totalFolders = countCategories(item);
                item->setText(QString("文件夹 (%1)").arg(totalFolders));
            } else if (id < 0) { 
                int count = sysCounts.value(type, 0);
                QString newText = QString("%1 (%2)").arg(name).arg(count);
                if (item->text() != newText) {
                    item->setText(newText);
                }
            } else if (type == "category" && id > 0) { 
                int count = catCounts.value(id, 0);
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

            (void)QtConcurrent::run([this, targetCat, newName]() mutable {
                bool renameSuccess = true;
                bool physicalRenamed = false;
                QString oldPath;
                QString newPath;
                if (!targetCat.physicalPath.empty()) {
                    oldPath = QString::fromStdWString(targetCat.physicalPath);
                    QFileInfo oldInfo(oldPath);
                    newPath = QDir::toNativeSeparators(oldInfo.absoluteDir().absoluteFilePath(newName));
                    if (oldPath != newPath) {
                        if (QFile::rename(oldPath, newPath)) {
                            targetCat.physicalPath = newPath.toStdWString();
                            physicalRenamed = true;
                        } else {
                            renameSuccess = false;
                            qWarning() << "[CategoryModel] QFile::rename failed from" << oldPath << "to" << newPath;
                        }
                    }
                }

                if (renameSuccess) {
                    targetCat.name = newName.toStdWString();
                    CategoryRepo::update(targetCat);
                    
                    if (physicalRenamed) {
                        MetadataManager::instance().renameItem(oldPath.toStdWString(), newPath.toStdWString());
                    }
                }

                QMetaObject::invokeMethod(this, [this]() {
                    refresh();
                }, Qt::QueuedConnection);
            });

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

    draggedCat.parentId = targetParentId;
    siblings.insert(siblings.begin() + insertRow, draggedCat);

    // 6. 100% 物理写盘：批量重新计算并更新 SQLite 中的 sortOrder 序号与 parentId
    for (size_t i = 0; i < siblings.size(); ++i) {
        siblings[i].sortOrder = static_cast<int>(i);
        CategoryRepo::update(siblings[i]);
    }

    // 7. 彻底阻断 Qt 原生深拷贝克隆坏行为，投递异步 refresh() 从数据库权威重绘！
    QMetaObject::invokeMethod(this, [this]() {
        refresh();
    }, Qt::QueuedConnection);

    return true; // 物理阻断 Qt 原生深拷贝！
}

} // namespace ArcMeta