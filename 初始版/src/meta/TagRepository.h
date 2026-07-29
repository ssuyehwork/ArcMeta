#ifndef ARCMETA_TAG_REPOSITORY_H
#define ARCMETA_TAG_REPOSITORY_H

#include <QString>
#include <QStringList>
#include <QList>
#include "sqlite3.h"

namespace ArcMeta {

class TagRepository {
public:
    struct TagGroup {
        int id;
        QString name;
        QString color;
        QStringList tags;
    };

    // 标签组核心 CRUD
    static QList<TagGroup> getAllGroups();
    static int createGroup(const QString& name, const QString& color = "#3498db");
    static bool renameGroup(int groupId, const QString& newName);
    static bool deleteGroup(int groupId);

    // 标签组子项（关系映射表）管理
    static bool addTagToGroup(const QString& tagName, int groupId);
    static bool removeTagFromGroup(const QString& tagName, int groupId = -1);

    // 数据迁移接口
    static void checkAndMigrate();
};

} // namespace ArcMeta

#endif // ARCMETA_TAG_REPOSITORY_H
