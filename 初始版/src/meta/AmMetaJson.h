#pragma once

#include <string>
#include <vector>
#include <map>
#include <QString>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include "MetadataDefs.h"

namespace ArcMeta {

/**
 * @brief 处理 ArcMeta.cache 下高级 JSON 离散缓存 (.json) 的读写管理类
 * 2026-07-xx 双轨架构重构：
 * 1. 彻底摒弃 XMP 格式，全面采用纯粹、高效、结构化的 JSON 规范。
 * 2. 磁盘模式下不污染用户物理文件夹，统一存放在主程序根目录下的 "ArcMeta.cache" 高级缓存文件夹中。
 */
class AmMetaJson {
public:
    /**
     * @param folderPath 目标物理文件夹的完整路径
     */
    explicit AmMetaJson(const std::wstring& folderPath);

    /**
     * @brief 从 ArcMeta.cache 对应位置加载 JSON 缓存文件
     */
    bool load();

    /**
     * @brief 安全保存当前元数据至 ArcMeta.cache 对应的 JSON 文件中
     */
    bool save() const;

    // 数据访问接口
    FolderMeta& folder() { return m_folder; }
    const FolderMeta& folder() const { return m_folder; }

    std::map<std::wstring, ItemMeta>& items() { return m_items; }
    const std::map<std::wstring, ItemMeta>& items() const { return m_items; }

    /**
     * @brief 移除指定文件名的元数据条目
     */
    void remove(const std::wstring& fileName) { m_items.erase(fileName); }

    /**
     * @brief 静态辅助方法：当物理文件重命名时，更新缓存 JSON 里的条目键名
     */
    static bool renameItem(const QString& folderPath, const QString& oldName, const QString& newName);

    /**
     * @brief 静态辅助方法：当物理文件夹整体重命名/移动时，迁移其对应的 ArcMeta.cache JSON 文件
     */
    static bool migrateFolderCache(const QString& oldFolderPath, const QString& newFolderPath);

    /**
     * @brief 获取 ArcMeta.cache 根目录绝对路径（若不存在会自动创建）
     */
    static QString getCacheDirectory();

private:
    std::wstring m_folderPath;
    std::wstring m_filePath; // 映射到 ArcMeta.cache/ 中的真实 .json 物理路径
    
    FolderMeta m_folder;
    std::map<std::wstring, ItemMeta> m_items;

    // 内部转换辅助：根据目标文件夹物理路径计算出 ArcMeta.cache 中唯一的 JSON 路径
    static std::wstring resolveCacheFilePath(const std::wstring& folderPath);

    static QJsonObject folderToEntry(const FolderMeta& meta);
    static FolderMeta entryToFolder(const QJsonObject& obj);
    static QJsonObject itemToEntry(const ItemMeta& meta);
    static ItemMeta entryToItem(const QJsonObject& obj);

    static QString toQString(const std::wstring& ws) { return QString::fromStdWString(ws); }
    static std::wstring toStdWString(const QString& qs) { return qs.toStdWString(); }
};

} // namespace ArcMeta