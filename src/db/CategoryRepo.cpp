#include "CategoryRepo.h"
#include "Database.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QMap>

namespace ArcMeta {

std::vector<Category> CategoryRepo::getRecentlyUsed(int limit) {
    std::vector<Category> results;
    QSqlDatabase db = Database::instance().getThreadDatabase();
    QSqlQuery q(db);
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
    QSqlDatabase db = Database::instance().getThreadDatabase();
    QSqlQuery q(db);
    q.prepare("INSERT INTO categories (parent_id, name, color, preset_tags, sort_order, pinned, created_at) VALUES (?, ?, ?, ?, ?, ?, ?)");
    q.addBindValue(cat.parentId);
    q.addBindValue(QString::fromStdWString(cat.name));
    q.addBindValue(QString::fromStdWString(cat.color));

    QJsonArray tagsArr;
    for (const auto& t : cat.presetTags) tagsArr.append(QString::fromStdWString(t));
    q.addBindValue(QJsonDocument(tagsArr).toJson(QJsonDocument::Compact));

    q.addBindValue(cat.sortOrder);
    q.addBindValue(cat.pinned ? 1 : 0);
    q.addBindValue((double)QDateTime::currentMSecsSinceEpoch());

    if (q.exec()) {
        cat.id = q.lastInsertId().toInt();
        Database::instance().markDirty();
        return true;
    }
    return false;
}

bool CategoryRepo::reorderAll(bool ascending) {
    QSqlDatabase db = Database::instance().getThreadDatabase();
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
        bool ok = db.commit();
        if (ok) Database::instance().markDirty();
        return ok;
    }
    return false;
}

bool CategoryRepo::update(const Category& cat) {
    QSqlDatabase db = Database::instance().getThreadDatabase();
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
    bool ok = q.exec();
    if (ok) Database::instance().markDirty();
    return ok;
}

bool CategoryRepo::addItemToCategory(int categoryId, const std::string& fileId128, const std::wstring& volSerial) {
    QSqlDatabase db = Database::instance().getThreadDatabase();
    QSqlQuery q(db);
    q.prepare("INSERT OR IGNORE INTO category_items (category_id, file_id_128, vol_serial, added_at) VALUES (?, ?, ?, ?)");
    q.addBindValue(categoryId);
    q.addBindValue(QString::fromStdString(fileId128));
    q.addBindValue(QString::fromStdWString(volSerial));
    q.addBindValue((double)QDateTime::currentMSecsSinceEpoch());
    bool ok = q.exec();
    if (ok) Database::instance().markDirty();
    return ok;
}

bool CategoryRepo::removeItemFromCategory(int categoryId, const std::string& fileId128, const std::wstring& volSerial) {
    QSqlDatabase db = Database::instance().getThreadDatabase();
    QSqlQuery q(db);
    q.prepare("DELETE FROM category_items WHERE category_id = ? AND file_id_128 = ? AND vol_serial = ?");
    q.addBindValue(categoryId);
    q.addBindValue(QString::fromStdString(fileId128));
    q.addBindValue(QString::fromStdWString(volSerial));
    bool ok = q.exec();
    if (ok) Database::instance().markDirty();
    return ok;
}

std::vector<Category> CategoryRepo::getAll() {
    std::vector<Category> results;
    QSqlDatabase db = Database::instance().getThreadDatabase();
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

std::vector<std::pair<std::string, std::wstring>> CategoryRepo::getFileIdsInCategory(int categoryId) {
    std::vector<std::pair<std::string, std::wstring>> results;
    QSqlDatabase db = Database::instance().getThreadDatabase();
    QSqlQuery q(db);
    q.prepare("SELECT file_id_128, vol_serial FROM category_items WHERE category_id = ? ORDER BY added_at DESC");
    q.addBindValue(categoryId);
    if (q.exec()) {
        while (q.next()) {
            results.push_back({q.value(0).toString().toStdString(), q.value(1).toString().toStdWString()});
        }
    }
    return results;
}

std::vector<std::pair<int, int>> CategoryRepo::getCounts() {
    std::vector<std::pair<int, int>> counts;
    QSqlDatabase db = Database::instance().getThreadDatabase();
    QSqlQuery q("SELECT category_id, COUNT(*) FROM category_items GROUP BY category_id", db);
    while (q.next()) {
        counts.push_back({q.value(0).toInt(), q.value(1).toInt()});
    }
    return counts;
}

int CategoryRepo::getUncategorizedItemCount() {
    return 0; 
}

QMap<QString, int> CategoryRepo::getSystemCounts() {
    QMap<QString, int> counts;
    counts["all"] = 0;
    counts["today"] = 0;
    counts["yesterday"] = 0;
    counts["recently_visited"] = 0;
    counts["uncategorized"] = 0;
    counts["untagged"] = 0;
    counts["tags"] = 0;
    counts["trash"] = 0;
    return counts;
}

bool CategoryRepo::remove(int id) {
    QSqlDatabase db = Database::instance().getThreadDatabase();
    if (!db.transaction()) return false;

    QSqlQuery q1(db);
    q1.prepare("DELETE FROM category_items WHERE category_id = ?");
    q1.addBindValue(id);
    q1.exec();

    QSqlQuery q2(db);
    q2.prepare("DELETE FROM categories WHERE id = ?");
    q2.addBindValue(id);
    
    if (q2.exec()) {
        bool ok = db.commit();
        if (ok) Database::instance().markDirty();
        return ok;
    } else {
        db.rollback();
        return false;
    }
}

bool CategoryRepo::reorder(int parentId, bool ascending) {
    QSqlDatabase db = Database::instance().getThreadDatabase();
    QSqlQuery q(db);
    q.prepare("SELECT id FROM categories WHERE parent_id = ? ORDER BY name " + QString(ascending ? "ASC" : "DESC"));
    q.addBindValue(parentId);
    
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
        bool ok = db.commit();
        if (ok) Database::instance().markDirty();
        return ok;
    }
    return false;
}

std::vector<std::string> CategoryRepo::getFileIdsRecursive(int categoryId) {
    return {};
}

} // namespace ArcMeta
