#include "CategoryRepo.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QFile>
#include <QJsonObject>
#include <algorithm>
#include "../meta/MetadataManager.h"

namespace ArcMeta {


namespace ScchCategoryEngine {

static QJsonObject loadCategoriesScch() {
    QFile file("arcmeta_categories.scch");
    if (file.exists() && file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        if (doc.isObject()) return doc.object();
    }
    QJsonObject root;
    root["categories"] = QJsonArray();
    root["category_items"] = QJsonArray();
    return root;
}

static bool saveCategoriesScch(const QJsonObject& root) {
    QFile file("arcmeta_categories.scch");
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.close();
        return true;
    }
    return false;
}

static Category scchToCategory(const QJsonObject& obj) {
    Category cat;
    cat.id = obj["id"].toInt();
    cat.parentId = obj["parent_id"].toInt();
    cat.name = obj["name"].toString().toStdWString();
    cat.color = obj["color"].toString().toStdWString();
    QJsonArray tagsArr = obj["preset_tags"].toArray();
    for (const auto& t : tagsArr) {
        cat.presetTags.push_back(t.toString().toStdWString());
    }
    cat.sortOrder = obj["sort_order"].toInt();
    cat.pinned = obj["pinned"].toInt() == 1;
    cat.encrypted = obj["encrypted"].toInt() == 1;
    cat.encryptHint = obj["encrypt_hint"].toString().toStdWString();
    return cat;
}

static QJsonObject categoryToScch(const Category& cat) {
    QJsonObject obj;
    obj["id"] = cat.id;
    obj["parent_id"] = cat.parentId;
    obj["name"] = QString::fromStdWString(cat.name);
    obj["color"] = QString::fromStdWString(cat.color);
    QJsonArray tagsArr;
    for (const auto& t : cat.presetTags) {
        tagsArr.append(QString::fromStdWString(t));
    }
    obj["preset_tags"] = tagsArr;
    obj["sort_order"] = cat.sortOrder;
    obj["pinned"] = cat.pinned ? 1 : 0;
    obj["encrypted"] = cat.encrypted ? 1 : 0;
    obj["encrypt_hint"] = QString::fromStdWString(cat.encryptHint);
    return obj;
}

static std::vector<Category> getAll() {
    std::vector<Category> results;
    QJsonObject root = loadCategoriesScch();
    QJsonArray cats = root["categories"].toArray();
    for (const auto& val : cats) {
        results.push_back(scchToCategory(val.toObject()));
    }
    std::sort(results.begin(), results.end(), [](const Category& a, const Category& b) {
        return a.sortOrder < b.sortOrder;
    });
    return results;
}

static bool add(Category& cat) {
    QJsonObject root = loadCategoriesScch();
    QJsonArray cats = root["categories"].toArray();
    int maxId = 0;
    for (const auto& val : cats) {
        int id = val.toObject()["id"].toInt();
        if (id > maxId) maxId = id;
    }
    cat.id = maxId + 1;
    cats.append(categoryToScch(cat));
    root["categories"] = cats;
    return saveCategoriesScch(root);
}

static bool update(const Category& cat) {
    QJsonObject root = loadCategoriesScch();
    QJsonArray cats = root["categories"].toArray();
    QJsonArray updatedCats;
    bool found = false;
    for (const auto& val : cats) {
        QJsonObject obj = val.toObject();
        if (obj["id"].toInt() == cat.id) {
            updatedCats.append(categoryToScch(cat));
            found = true;
        } else {
            updatedCats.append(obj);
        }
    }
    if (!found) {
        updatedCats.append(categoryToScch(cat));
    }
    root["categories"] = updatedCats;
    return saveCategoriesScch(root);
}

static void collectSubCategoryIds(const QJsonArray& cats, int parentId, std::vector<int>& ids) {
    for (const auto& val : cats) {
        QJsonObject obj = val.toObject();
        if (obj["parent_id"].toInt() == parentId) {
            int childId = obj["id"].toInt();
            ids.push_back(childId);
            collectSubCategoryIds(cats, childId, ids);
        }
    }
}

static bool remove(int id) {
    QJsonObject root = loadCategoriesScch();
    QJsonArray cats = root["categories"].toArray();
    
    std::vector<int> removeIds;
    removeIds.push_back(id);
    collectSubCategoryIds(cats, id, removeIds);

    QJsonArray remainingCats;
    for (const auto& val : cats) {
        QJsonObject obj = val.toObject();
        int catId = obj["id"].toInt();
        if (std::find(removeIds.begin(), removeIds.end(), catId) == removeIds.end()) {
            remainingCats.append(obj);
        }
    }
    root["categories"] = remainingCats;

    QJsonArray items = root["category_items"].toArray();
    QJsonArray remainingItems;
    for (const auto& val : items) {
        QJsonObject obj = val.toObject();
        int catId = obj["category_id"].toInt();
        if (std::find(removeIds.begin(), removeIds.end(), catId) == removeIds.end()) {
            remainingItems.append(obj);
        }
    }
    root["category_items"] = remainingItems;

    return saveCategoriesScch(root);
}

static bool reorder(int parentId, bool ascending) {
    QJsonObject root = loadCategoriesScch();
    QJsonArray cats = root["categories"].toArray();
    
    std::vector<Category> targetCats;
    for (const auto& val : cats) {
        Category c = scchToCategory(val.toObject());
        if (c.parentId == parentId) {
            targetCats.push_back(c);
        }
    }

    std::sort(targetCats.begin(), targetCats.end(), [ascending](const Category& a, const Category& b) {
        int cmp = a.name.compare(b.name);
        return ascending ? (cmp < 0) : (cmp > 0);
    });

    QMap<int, int> orderMap;
    for (size_t i = 0; i < targetCats.size(); ++i) {
        orderMap[targetCats[i].id] = (int)i;
    }

    QJsonArray updatedCats;
    for (const auto& val : cats) {
        QJsonObject obj = val.toObject();
        int id = obj["id"].toInt();
        if (orderMap.contains(id)) {
            obj["sort_order"] = orderMap[id];
        }
        updatedCats.append(obj);
    }
    root["categories"] = updatedCats;
    return saveCategoriesScch(root);
}

static bool reorderAll(bool ascending) {
    QJsonObject root = loadCategoriesScch();
    QJsonArray cats = root["categories"].toArray();
    
    std::vector<Category> targetCats;
    for (const auto& val : cats) {
        targetCats.push_back(scchToCategory(val.toObject()));
    }

    std::sort(targetCats.begin(), targetCats.end(), [ascending](const Category& a, const Category& b) {
        int cmp = a.name.compare(b.name);
        return ascending ? (cmp < 0) : (cmp > 0);
    });

    QJsonArray updatedCats;
    for (size_t i = 0; i < targetCats.size(); ++i) {
        Category c = targetCats[i];
        c.sortOrder = (int)i;
        updatedCats.append(categoryToScch(c));
    }
    root["categories"] = updatedCats;
    return saveCategoriesScch(root);
}

static bool addItemToCategory(int categoryId, const std::string& fileId128) {
    QJsonObject root = loadCategoriesScch();
    QJsonArray items = root["category_items"].toArray();
    QString qFid = QString::fromStdString(fileId128);
    for (const auto& val : items) {
        QJsonObject obj = val.toObject();
        if (obj["category_id"].toInt() == categoryId && obj["file_id_128"].toString() == qFid) {
            return true;
        }
    }
    QJsonObject newItem;
    newItem["category_id"] = categoryId;
    newItem["file_id_128"] = qFid;
    newItem["added_at"] = (double)QDateTime::currentMSecsSinceEpoch();
    items.append(newItem);
    root["category_items"] = items;
    return saveCategoriesScch(root);
}

static bool removeItemFromCategory(int categoryId, const std::string& fileId128) {
    QJsonObject root = loadCategoriesScch();
    QJsonArray items = root["category_items"].toArray();
    QJsonArray remainingItems;
    QString qFid = QString::fromStdString(fileId128);
    for (const auto& val : items) {
        QJsonObject obj = val.toObject();
        if (obj["category_id"].toInt() == categoryId && obj["file_id_128"].toString() == qFid) {
            continue;
        }
        remainingItems.append(obj);
    }
    root["category_items"] = remainingItems;
    return saveCategoriesScch(root);
}

static std::vector<std::string> getFileIdsInCategory(int categoryId) {
    std::vector<std::string> results;
    QJsonObject root = loadCategoriesScch();
    QJsonArray items = root["category_items"].toArray();
    for (const auto& val : items) {
        QJsonObject obj = val.toObject();
        if (obj["category_id"].toInt() == categoryId) {
            results.push_back(obj["file_id_128"].toString().toStdString());
        }
    }
    return results;
}

static std::vector<std::pair<int, int>> getCounts() {
    std::vector<std::pair<int, int>> counts;
    QJsonObject root = loadCategoriesScch();
    QJsonArray items = root["category_items"].toArray();
    QMap<int, int> countMap;
    for (const auto& val : items) {
        QJsonObject obj = val.toObject();
        int catId = obj["category_id"].toInt();
        countMap[catId] = countMap.value(catId, 0) + 1;
    }
    for (auto it = countMap.begin(); it != countMap.end(); ++it) {
        counts.push_back({it.key(), it.value()});
    }
    return counts;
}

static std::vector<std::string> getFileIdsRecursive(int categoryId) {
    std::vector<std::string> results = getFileIdsInCategory(categoryId);
    QJsonObject root = loadCategoriesScch();
    QJsonArray cats = root["categories"].toArray();
    
    std::vector<int> subIds;
    collectSubCategoryIds(cats, categoryId, subIds);
    
    for (int sid : subIds) {
        auto subFiles = getFileIdsInCategory(sid);
        results.insert(results.end(), subFiles.begin(), subFiles.end());
    }
    return results;
}

} // namespace ScchCategoryEngine

/**
 * @brief 分类持久层实现
 */

std::vector<Category> CategoryRepo::getRecentlyUsed(int limit) {
    (void)limit;
    return ScchCategoryEngine::getAll();
}

bool CategoryRepo::add(Category& cat) {
    return ScchCategoryEngine::add(cat);
}

bool CategoryRepo::reorderAll(bool ascending) {
    return ScchCategoryEngine::reorderAll(ascending);
}

bool CategoryRepo::update(const Category& cat) {
    return ScchCategoryEngine::update(cat);
}

bool CategoryRepo::addItemToCategory(int categoryId, const std::string& fileId128) {
    return ScchCategoryEngine::addItemToCategory(categoryId, fileId128);
}

std::vector<Category> CategoryRepo::getAll() {
    return ScchCategoryEngine::getAll();
}

bool CategoryRepo::removeItemFromCategory(int categoryId, const std::string& fileId128) {
    return ScchCategoryEngine::removeItemFromCategory(categoryId, fileId128);
}

std::vector<std::string> CategoryRepo::getFileIdsInCategory(int categoryId) {
    return ScchCategoryEngine::getFileIdsInCategory(categoryId);
}

std::vector<std::pair<int, int>> CategoryRepo::getCounts() {
    return ScchCategoryEngine::getCounts();
}

int CategoryRepo::getUniqueItemCount() {
    return 0; // 彻底废除数据库，不再进行此类统计
}

int CategoryRepo::getUncategorizedItemCount() {
    return 0; // 彻底废除数据库，不再进行此类统计
}

QMap<QString, int> CategoryRepo::getSystemCounts() {
    return QMap<QString, int>(); // 彻底废除数据库，侧边栏系统项计数暂设为 0
}

bool CategoryRepo::remove(int id) {
    return ScchCategoryEngine::remove(id);
}

bool CategoryRepo::reorder(int parentId, bool ascending) {
    return ScchCategoryEngine::reorder(parentId, ascending);
}

std::vector<std::string> CategoryRepo::getFileIdsRecursive(int categoryId) {
    return ScchCategoryEngine::getFileIdsRecursive(categoryId);
}


} // namespace ArcMeta
