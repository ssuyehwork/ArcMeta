#pragma once
#include <QString>
#include <QStringList>
#include <vector>
#include <string>
#include <QMap>

namespace ArcMeta {

struct TagGroup {
    int id = 0;
    QString name;
    int sortOrder = 0;
};

struct TagDef {
    QString name;
    bool isFavorite = false;
    int groupId = 0;
    QString color;
    int sortOrder = 0;
    int usageCount = 0; // 仅运行时有效
};

/**
 * @brief 标签持久化与逻辑层
 */
class TagRepo {
public:
    // --- 分组管理 ---
    static std::vector<TagGroup> getGroups();
    static int addGroup(const QString& name);
    static bool updateGroup(const TagGroup& group);
    static bool deleteGroup(int id); // 删除分组不删除标签，标签将归入默认组(0)

    // --- 标签定义管理 ---
    static std::vector<TagDef> getAllTags();
    static bool setTagFavorite(const QString& name, bool favorite);
    static bool setTagGroup(const QString& name, int groupId);
    static bool deleteTagDef(const QString& name);

    // --- 全局业务逻辑 ---
    /**
     * @brief 全局重命名标签
     * 将数据库中及内存缓存中所有文件的该标签替换为新名称
     */
    static bool renameTagGlobal(const QString& oldName, const QString& newName);

    /**
     * @brief 物理删除标签
     * 从所有文件的标签列表中移除该标签，并删除标签定义
     */
    static bool deleteTagGlobal(const QString& name);

    // --- 统计 ---
    /**
     * @brief 获取当前所有已知标签的实时使用计数
     */
    static QMap<QString, int> getTagUsageStats();

    /**
     * @brief 获取常用标签列表
     */
    static QStringList getFavoriteTags();
};

} // namespace ArcMeta
