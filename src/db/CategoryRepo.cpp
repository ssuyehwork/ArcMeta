#include "CategoryRepo.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>

namespace ArcMeta {

/**
 * @brief 分类持久层实现
 * 2026-03-xx 物理修复：全面移除隐式 Default Connection，强制通过 getThreadDatabase 获取线程专用连接。
 */

bool CategoryRepo::add(Category& cat) {
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
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
        return true;
    }
    return false;
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
    
    if (q2.exec()) {
        return db.commit();
    }
    db.rollback();
    return false;
}

bool CategoryRepo::reorder(int parentId, bool ascending) {
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase();
    // 逻辑：获取该父级下的所有分类，按名称排序，然后重新赋予 sort_order
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
        return db.commit();
    }
    return false;
}


} // namespace ArcMeta
