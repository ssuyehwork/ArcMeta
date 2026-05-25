#include "CategoryRepo.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>

#include <QFile>
#include <QJsonObject>
#include <algorithm>
#include "../meta/MetadataManager.h"

namespace ArcMeta {

namespace JsonCategoryEngine {

static QJsonObject loadCategoriesJson() {
    QFile file("arcmeta_categories.json");
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

static bool saveCategoriesJson(const QJsonObject& root) {
    QFile file("arcmeta_categories.json");
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.close();
        return true;
    }
    return false;
}

static Category jsonToCategory(const QJsonObject& obj) {
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

static QJsonObject categoryToJson(const Category& cat) {
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
    QJsonObject root = loadCategoriesJson();
    QJsonArray cats = root["categories"].toArray();
    for (const auto& val : cats) {
        results.push_back(jsonToCategory(val.toObject()));
    }
    std::sort(results.begin(), results.end(), [](const Category& a, const Category& b) {
        return a.sortOrder < b.sortOrder;
    });
    return results;
}

static bool add(Category& cat) {
    QJsonObject root = loadCategoriesJson();
    QJsonArray cats = root["categories"].toArray();
    int maxId = 0;
    for (const auto& val : cats) {
        int id = val.toObject()["id"].toInt();
        if (id > maxId) maxId = id;
    }
    cat.id = maxId + 1;
    cats.append(categoryToJson(cat));
    root["categories"] = cats;
    return saveCategoriesJson(root);
}

static bool update(const Category& cat) {
    QJsonObject root = loadCategoriesJson();
    QJsonArray cats = root["categories"].toArray();
    QJsonArray updatedCats;
    bool found = false;
    for (const auto& val : cats) {
        QJsonObject obj = val.toObject();
        if (obj["id"].toInt() == cat.id) {
            updatedCats.append(categoryToJson(cat));
            found = true;
        } else {
            updatedCats.append(obj);
        }
    }
    if (!found) {
        updatedCats.append(categoryToJson(cat));
    }
    root["categories"] = updatedCats;
    return saveCategoriesJson(root);
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
    QJsonObject root = loadCategoriesJson();
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
    std::vector<std::string> fileIds;
    for (const auto& val : items) {
        QJsonObject obj = val.toObject();
        int catId = obj["category_id"].toInt();
        if (std::find(removeIds.begin(), removeIds.end(), catId) == removeIds.end()) {
            remainingItems.append(obj);
        } else {
            fileIds.push_back(obj["file_id_128"].toString().toStdString());
        }
    }
    root["category_items"] = remainingItems;

    if (!fileIds.empty()) {
        QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
        QSqlQuery qMark(db);
        QStringList fids;
        for (const auto& fid : fileIds) fids << QString::fromStdString(fid);
        QString sql = QString("UPDATE items SET deleted = 1 WHERE file_id_128 IN ('%1')").arg(fids.join("','"));
        qMark.exec(sql);
    }

    return saveCategoriesJson(root);
}

static bool reorder(int parentId, bool ascending) {
    QJsonObject root = loadCategoriesJson();
    QJsonArray cats = root["categories"].toArray();
    
    std::vector<Category> targetCats;
    for (const auto& val : cats) {
        Category c = jsonToCategory(val.toObject());
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
    return saveCategoriesJson(root);
}

static bool reorderAll(bool ascending) {
    QJsonObject root = loadCategoriesJson();
    QJsonArray cats = root["categories"].toArray();
    
    std::vector<Category> targetCats;
    for (const auto& val : cats) {
        targetCats.push_back(jsonToCategory(val.toObject()));
    }

    std::sort(targetCats.begin(), targetCats.end(), [ascending](const Category& a, const Category& b) {
        int cmp = a.name.compare(b.name);
        return ascending ? (cmp < 0) : (cmp > 0);
    });

    QJsonArray updatedCats;
    for (size_t i = 0; i < targetCats.size(); ++i) {
        Category c = targetCats[i];
        c.sortOrder = (int)i;
        updatedCats.append(categoryToJson(c));
    }
    root["categories"] = updatedCats;
    return saveCategoriesJson(root);
}

static bool addItemToCategory(int categoryId, const std::string& fileId128) {
    QJsonObject root = loadCategoriesJson();
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
    return saveCategoriesJson(root);
}

static bool removeItemFromCategory(int categoryId, const std::string& fileId128) {
    QJsonObject root = loadCategoriesJson();
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
    return saveCategoriesJson(root);
}

static std::vector<std::string> getFileIdsInCategory(int categoryId) {
    std::vector<std::string> results;
    QJsonObject root = loadCategoriesJson();
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
    QJsonObject root = loadCategoriesJson();
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
    QJsonObject root = loadCategoriesJson();
    QJsonArray cats = root["categories"].toArray();
    
    std::vector<int> subIds;
    collectSubCategoryIds(cats, categoryId, subIds);
    
    for (int sid : subIds) {
        auto subFiles = getFileIdsInCategory(sid);
        results.insert(results.end(), subFiles.begin(), subFiles.end());
    }
    return results;
}

} // namespace JsonCategoryEngine

/**
 * @brief 分类持久层实现
 * 2026-03-xx 物理修复：全面移除隐式 Default Connection，强制通过 getThreadDatabase 获取线程专用连接。
 */

std::vector<Category> CategoryRepo::getRecentlyUsed(int limit) {
    std::vector<Category> results;
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    QSqlQuery q(db);
    // 按添加时间倒序提取最近的分类
    q.prepare("SELECT c.id, c.parent_id, c.name, c.color, c.preset_tags, c.sort_order, c.pinned, c.encrypted, c.encrypt_hint "
              "FROM categories c "
              "JOIN (SELECT category_id, MAX(added_at) as max_at FROM category_items GROUP BY category_id) ci "
              "ON c.id = ci.category_id ORDER BY ci.max_at DESC LIMIT ?");
    q.addBindValue(limit);
    
    if (q.exec()) {
        while (q.next()) {
            Category cat;
            cat.id = q.value(0).toInt();
            cat.parentId = q.value(1).toInt();
            cat.name = q.value(2).toString().toStdWString();
            cat.color = q.value(3).toString().toStdWString();
            QJsonDocument doc = QJsonDocument::fromJson(q.value(4).toByteArray());
            if (doc.isArray()) {
                for (const auto& v : doc.array()) cat.presetTags.push_back(v.toString().toStdWString());
            }
            cat.sortOrder = q.value(5).toInt();
            cat.pinned = q.value(6).toBool();
            cat.encrypted = q.value(7).toBool();
            cat.encryptHint = q.value(8).toString().toStdWString();
            results.push_back(cat);
        }
    }
    return results;
}

bool CategoryRepo::add(Category& cat) {
    // 1. 写入 SQLite 并获取自增 ID
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    QSqlQuery q(db);
    q.prepare("INSERT OR REPLACE INTO categories (id, parent_id, name, color, preset_tags, sort_order, pinned, created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    q.addBindValue(cat.id);
    q.addBindValue(cat.parentId);
    q.addBindValue(QString::fromStdWString(cat.name));
    q.addBindValue(QString::fromStdWString(cat.color));

    QJsonArray tagsArr;
    for (const auto& t : cat.presetTags) tagsArr.append(QString::fromStdWString(t));
    q.addBindValue(QJsonDocument(tagsArr).toJson(QJsonDocument::Compact));

    q.addBindValue(cat.sortOrder);
    q.addBindValue(cat.pinned ? 1 : 0);
    q.addBindValue((double)QDateTime::currentMSecsSinceEpoch());

    return q.exec();
}

bool CategoryRepo::reorderAll(bool ascending) {
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    QSqlQuery q(db);
    q.prepare("SELECT id FROM categories ORDER BY name " + QString(ascending ? "ASC" : "DESC"));
    
    if (q.exec()) {
        int order = 0;
        db.transaction();
        while (q.next()) {
            int id = q.value(0).toInt();
            QSqlQuery upd(db);
            upd.prepare("UPDATE categories SET sort_order = ? WHERE id = ?");
            upd.addBindValue(order++);
            upd.addBindValue(id);
            upd.exec();
        }
        return db.commit();
    }
    return false;
}

bool CategoryRepo::update(const Category& cat) {
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    QSqlQuery q(db);
    q.prepare("UPDATE categories SET parent_id = ?, name = ?, color = ?, sort_order = ?, pinned = ?, encrypted = ?, encrypt_hint = ? WHERE id = ?");
    q.addBindValue(cat.parentId);
    q.addBindValue(QString::fromStdWString(cat.name));
    q.addBindValue(QString::fromStdWString(cat.color));
    q.addBindValue(cat.sortOrder);
    q.addBindValue(cat.pinned ? 1 : 0);
    q.addBindValue(cat.encrypted ? 1 : 0);
    q.addBindValue(QString::fromStdWString(cat.encryptHint));
    q.addBindValue(cat.id);
    return q.exec();
}

bool CategoryRepo::addItemToCategory(int categoryId, const std::string& fileId128) {
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    QSqlQuery q(db);
    q.prepare("INSERT OR IGNORE INTO category_items (category_id, file_id_128, added_at) VALUES (?, ?, ?)");
    q.addBindValue(categoryId);
    q.addBindValue(QString::fromStdString(fileId128));
    q.addBindValue((double)QDateTime::currentMSecsSinceEpoch());
    return q.exec();
}

std::vector<Category> CategoryRepo::getAll() {
    std::vector<Category> results;
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    // 2026-06-xx 按照用户要求：彻底解耦置顶干扰，移除 pinned DESC，回归纯粹的 sort_order 排序
    // 理由：确保“我的分类”实体区域层级稳定。置顶状态仅由模型层实现“移动”到快速访问区。
    QSqlQuery q("SELECT id, parent_id, name, color, preset_tags, sort_order, pinned, encrypted, encrypt_hint FROM categories ORDER BY sort_order ASC", db);
    while (q.next()) {
        Category cat;
        cat.id = q.value(0).toInt();
        cat.parentId = q.value(1).toInt();
        cat.name = q.value(2).toString().toStdWString();
        cat.color = q.value(3).toString().toStdWString();
        
        QJsonDocument doc = QJsonDocument::fromJson(q.value(4).toByteArray());
        if (doc.isArray()) {
            for (const auto& v : doc.array()) cat.presetTags.push_back(v.toString().toStdWString());
        }

        cat.sortOrder = q.value(5).toInt();
        cat.pinned = q.value(6).toBool();
        cat.encrypted = q.value(7).toBool();
        cat.encryptHint = q.value(8).toString().toStdWString();
        results.push_back(cat);
    }
    return results;
}

bool CategoryRepo::removeItemFromCategory(int categoryId, const std::string& fileId128) {
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    QSqlQuery q(db);
    q.prepare("DELETE FROM category_items WHERE category_id = ? AND file_id_128 = ?");
    q.addBindValue(categoryId);
    q.addBindValue(QString::fromStdString(fileId128));
    return q.exec();
}

std::vector<std::string> CategoryRepo::getFileIdsInCategory(int categoryId) {
    std::vector<std::string> results;
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    QSqlQuery q(db);
    q.prepare("SELECT file_id_128 FROM category_items WHERE category_id = ? ORDER BY added_at DESC");
    q.addBindValue(categoryId);
    if (q.exec()) {
        while (q.next()) {
            results.push_back(q.value(0).toString().toStdString());
        }
    }
    return results;
}

std::vector<std::pair<int, int>> CategoryRepo::getCounts() {
    std::vector<std::pair<int, int>> counts;
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    // 2026-06-xx 物理修复：基于非空 Fallback ID 机制回归最高性能 SQL。
    // 铁律：必须物理对齐 i.deleted = 0，基于唯一的 file_id_128 计数。
    QSqlQuery q("SELECT ci.category_id, COUNT(DISTINCT i.file_id_128) "
                "FROM category_items ci "
                "JOIN items i ON ci.file_id_128 = i.file_id_128 "
                "WHERE i.deleted = 0 AND i.type = 'file' "
                "GROUP BY ci.category_id", db);
    while (q.next()) {
        counts.push_back({q.value(0).toInt(), q.value(1).toInt()});
    }
    return counts;
}

int CategoryRepo::getUniqueItemCount() {
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    QSqlQuery q("SELECT COUNT(DISTINCT item_path) FROM category_items", db);
    if (q.next()) return q.value(0).toInt();
    return 0;
}

int CategoryRepo::getUncategorizedItemCount() {
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    // 2026-06-xx 物理修复：基于非空 Fallback ID 机制回归。
    QSqlQuery q("SELECT COUNT(DISTINCT i.file_id_128) FROM items i "
                "WHERE i.deleted = 0 AND i.type = 'file' "
                "AND NOT EXISTS ("
                "  SELECT 1 FROM category_items ci "
                "  WHERE ci.file_id_128 = i.file_id_128"
                ")", db);
    if (q.exec() && q.next()) return q.value(0).toInt();
    return 0; 
}

QMap<QString, int> CategoryRepo::getSystemCounts() {
    QMap<QString, int> counts;
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    double now = (double)QDateTime::currentMSecsSinceEpoch();
    double startOfToday = (double)QDateTime(QDate::currentDate(), QTime(0, 0)).toMSecsSinceEpoch();
    double startOfYesterday = (double)QDateTime(QDate::currentDate().addDays(-1), QTime(0, 0)).toMSecsSinceEpoch();
    
    // 2026-06-xx 物理修复：基于非空 Fallback ID 机制回归最高性能。
    // 铁律：由于 ID 现已保证非空，直接使用 COUNT(DISTINCT file_id_128)。

    // 全部数据
    QSqlQuery qAll("SELECT COUNT(DISTINCT file_id_128) FROM items WHERE deleted=0 AND type='file'", db);
    if (qAll.next()) counts["all"] = qAll.value(0).toInt();

    // 今日
    QSqlQuery qToday(db);
    qToday.prepare("SELECT COUNT(DISTINCT file_id_128) FROM items WHERE deleted=0 AND type='file' AND (ctime >= ? OR mtime >= ?)");
    qToday.addBindValue(startOfToday);
    qToday.addBindValue(startOfToday);
    if (qToday.exec() && qToday.next()) counts["today"] = qToday.value(0).toInt();

    // 昨日
    QSqlQuery qYesterday(db);
    qYesterday.prepare("SELECT COUNT(DISTINCT file_id_128) FROM items WHERE deleted=0 AND type='file' AND (ctime >= ? OR mtime >= ?) AND (ctime < ? OR mtime < ?)");
    qYesterday.addBindValue(startOfYesterday);
    qYesterday.addBindValue(startOfYesterday);
    qYesterday.addBindValue(startOfToday);
    qYesterday.addBindValue(startOfToday);
    if (qYesterday.exec() && qYesterday.next()) counts["yesterday"] = qYesterday.value(0).toInt();

    // 最近访问 (24小时内)
    QSqlQuery qRecent(db);
    qRecent.prepare("SELECT COUNT(DISTINCT file_id_128) FROM items WHERE deleted=0 AND type='file' AND atime >= ?");
    qRecent.addBindValue(now - 86400000.0);
    if (qRecent.exec() && qRecent.next()) counts["recently_visited"] = qRecent.value(0).toInt();

    // 未分类
    counts["uncategorized"] = getUncategorizedItemCount();

    // 未标签
    QSqlQuery qUntagged("SELECT COUNT(DISTINCT file_id_128) FROM items WHERE deleted=0 AND type='file' AND (tags IS NULL OR tags = '' OR tags = '[]')", db);
    if (qUntagged.next()) counts["untagged"] = qUntagged.value(0).toInt();

    // 2026-06-xx 按照用户要求：新增“标签管理”系统项统计
    QSqlQuery qTags("SELECT COUNT(*) FROM tags", db); 
    if (qTags.next()) counts["tags"] = qTags.value(0).toInt();

    // 回收站 (物理锁定 type='file')
    QSqlQuery qTrash("SELECT COUNT(DISTINCT file_id_128) FROM items WHERE deleted=1 AND type='file'", db);
    if (qTrash.next()) counts["trash"] = qTrash.value(0).toInt();

    return counts;
}

bool CategoryRepo::remove(int id) {
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    if (!db.transaction()) return false;

    // 2026-06-xx 物理同步：实现“删除分类时同步删除绑定数据”的核心要求。
    // 逻辑：元数据级联删除。被删除分类下的所有文件在 items 表中标记为 deleted=1。
    QSqlQuery qFid(db);
    qFid.prepare("SELECT file_id_128 FROM category_items WHERE category_id = ?");
    qFid.addBindValue(id);
    
    if (qFid.exec()) {
        QStringList fids;
        while (qFid.next()) {
            QString fid = qFid.value(0).toString();
            if (!fid.isEmpty()) fids << fid;
        }

        if (!fids.isEmpty()) {
            QSqlQuery qMark(db);
            // 物理性能优化：使用 IN 子句进行批量标记，杜绝循环查询。
            QString sql = QString("UPDATE items SET deleted = 1 WHERE file_id_128 IN ('%1')")
                          .arg(fids.join("','"));
            qMark.exec(sql);
        }
    }

    QSqlQuery q1(db);
    q1.prepare("DELETE FROM category_items WHERE category_id = ?");
    q1.addBindValue(id);
    q1.exec();

    QSqlQuery q2(db);
    q2.prepare("DELETE FROM categories WHERE id = ?");
    q2.addBindValue(id);
    
    bool dbOk = false;
    if (q2.exec()) {
        return db.commit();
    } else {
        db.rollback();
    }
    return false;
}

bool CategoryRepo::reorder(int parentId, bool ascending) {
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    // 逻辑：获取该父级下的所有分类，按名称排序，然后重新赋予 sort_order
    QSqlQuery q(db);
    q.prepare("SELECT id FROM categories WHERE parent_id = ? ORDER BY name " + QString(ascending ? "ASC" : "DESC"));
    q.addBindValue(parentId);
    
    bool dbOk = false;
    if (q.exec()) {
        int order = 0;
        db.transaction();
        while (q.next()) {
            int id = q.value(0).toInt();
            QSqlQuery upd(db);
            upd.prepare("UPDATE categories SET sort_order = ? WHERE id = ?");
            upd.addBindValue(order++);
            upd.addBindValue(id);
            upd.exec();
        }
        return db.commit();
    }
    return false;
}

std::vector<std::string> CategoryRepo::getFileIdsRecursive(int categoryId) {
    std::vector<std::string> results = getFileIdsInCategory(categoryId);
    
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    QSqlQuery q(db);
    q.prepare("SELECT id FROM categories WHERE parent_id = ?");
    q.addBindValue(categoryId);
    
    if (q.exec()) {
        std::vector<int> subIds;
        while (q.next()) subIds.push_back(q.value(0).toInt());
        
        for (int sid : subIds) {
            auto subFiles = getFileIdsRecursive(sid);
            results.insert(results.end(), subFiles.begin(), subFiles.end());
        }
    }
    
    return results;
}

void CategoryRepo::exportToJsonSnapshot() {
    QJsonObject root;
    QJsonArray jsonCats;
    
    auto categories = getAll();
    for (const auto& cat : categories) {
        jsonCats.append(JsonCategoryEngine::categoryToJson(cat));
    }
    root["categories"] = jsonCats;

    QJsonArray jsonItems;
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    QSqlQuery q("SELECT category_id, file_id_128, added_at FROM category_items", db);
    while (q.next()) {
        QJsonObject obj;
        obj["category_id"] = q.value(0).toInt();
        obj["file_id_128"] = q.value(1).toString();
        obj["added_at"] = q.value(2).toDouble();
        jsonItems.append(obj);
    }
    root["category_items"] = jsonItems;

    JsonCategoryEngine::saveCategoriesJson(root);
}


} // namespace ArcMeta
