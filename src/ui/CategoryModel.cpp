#include "CategoryModel.h"
#include "../meta/CategoryRepo.h"
#include "../meta/MetadataManager.h"

#include "UiHelper.h"
#include <QtConcurrent>
#include <QMimeData>
#include <QFileInfo>
#include <QFile>
#include <QDir>
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

    // 1. 渲染系统逻辑桶 (全部数据、未分类、未标签、最近访问、标签管理、回收站)
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

    // 2. 渲染“快速访问”分组节点（仅承载手动固定的快捷镜像）
    QStandardItem* favGroup = nullptr;
    if (m_type == Both || m_type == User) {
        favGroup = new QStandardItem("快速访问");
        favGroup->setData("快速访问", NameRole);
        favGroup->setSelectable(false);
        favGroup->setEditable(false);
        // 物理更新：图标从 "folder_filled" 替换为 "zap_filled"
        favGroup->setIcon(UiHelper::getIcon("zap_filled", QColor("#F1C40F"), 16)); 
        
        QFont font = favGroup->font();
        font.setBold(true);
        favGroup->setFont(font);
        favGroup->setForeground(QColor("#FFFFFF"));
    }

    // 3. 构建一等公民专属主标题节点：“分类 (子分类数)”
    QStandardItem* catGroup = nullptr;
    if (m_type == Both || m_type == User) {
        catGroup = new QStandardItem();
        catGroup->setData("category_root_group", TypeRole);
        catGroup->setData("分类", NameRole);
        catGroup->setData(CAT_GROUP_SYS_ID, IdRole);
        catGroup->setSelectable(false);
        catGroup->setEditable(false);
        catGroup->setIcon(UiHelper::getIcon("category", QColor("#378ADD"), 16));

        QFont font = catGroup->font();
        font.setBold(true);
        catGroup->setFont(font);
        catGroup->setForeground(QColor("#FFFFFF"));
    }

    if (m_type == User || m_type == Both) {
        auto categories = CategoryRepo::getAll();
        QMap<int, QStandardItem*> itemMap;
        QMap<int, Category> catMap;

        // 构建内部节点映射表
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

        // 4. 渲染物理托管库根分类 (ArcMeta.Library_) 到 root
        for (const auto& cat : categories) {
            int id = cat.id;
            QStandardItem* item = itemMap[id];
            int parentId = cat.parentId;

            if (parentId == 0) {
                QString name = QString::fromStdWString(cat.name);
                if (name.startsWith("ArcMeta.Library_", Qt::CaseInsensitive)) {
                    root->appendRow(item);
                }
            } else if (parentId > 0 && itemMap.contains(parentId)) {
                itemMap[parentId]->appendRow(item);
            }
        }

        // 5. 挂载“快速访问”分组节点
        if (favGroup) {
            root->appendRow(favGroup);
        }

        // 6. 【一等公民架构重构】：统计并挂载所有用户自定义顶级分类至“分类”主标题下
        int userTopCatCount = 0;
        for (const auto& cat : categories) {
            int id = cat.id;
            QStandardItem* item = itemMap[id];
            int parentId = cat.parentId;

            if (parentId == 0) {
                QString name = QString::fromStdWString(cat.name);
                if (!name.startsWith("ArcMeta.Library_", Qt::CaseInsensitive)) {
                    userTopCatCount++;
                    if (catGroup) {
                        catGroup->appendRow(item); // 物理强行挂载至“分类”主节点下方
                    } else {
                        root->appendRow(item);
                    }
                }
            }
        }

        // 设置主标题文字，精准反映包含的子分类总个数
        if (catGroup) {
            catGroup->setText(QString("分类 (%1)").arg(userTopCatCount));
            root->appendRow(catGroup);
        }

        // 7. 挂载已手固定的“快速访问”快捷镜像
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
                // 精准刷新“分类”主标题后的子分类总个数
                item->setText(QString("分类 (%1)").arg(item->rowCount()));
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

        // 禁改系统主标题
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

            if (!targetCat.physicalPath.empty()) {
                QString oldPath = QString::fromStdWString(targetCat.physicalPath);
                QFileInfo oldInfo(oldPath);
                if (oldInfo.fileName().startsWith("ArcMeta.Library_", Qt::CaseInsensitive) && targetCat.parentId == 0) {
                    return false; 
                }
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

Qt::DropActions CategoryModel::supportedDropActions() const {
    return Qt::MoveAction | Qt::CopyAction | Qt::LinkAction;
}

bool CategoryModel::dropMimeData(const QMimeData* mimeData, Qt::DropAction action, int row, int column, const QModelIndex& parent) {
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
        
        if (type != "category" && type != "bookmark" && type != "category_root_group") {
            return false; 
        }

        if (!mimeData->hasUrls() && !mimeData->hasFormat("text/plain")) {
            int parentId = parentItem->data(IdRole).toInt();
            Category parentCat = CategoryRepo::getById(parentId);
            if (parentCat.id > 0 && parentCat.parentId == 0 && !parentCat.physicalPath.empty()) {
                return false;
            }
        }
    }
    return QStandardItemModel::dropMimeData(mimeData, action, row, column, actualParent);
}

} // namespace ArcMeta