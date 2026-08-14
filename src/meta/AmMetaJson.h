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
 * @brief 处理 .ArcMeta.json 隐藏配置文件的读写管理类
 * 2026-08-xx 双轨架构重构：
 * 1. 全面采用纯粹、高效、结构化的 JSON 规范。
 * 2. 磁盘模式下采用标准的 .ArcMeta.json 隐藏文件直接保存在物理目录中。
 */
class AmMetaJson {
public:
    /**
     * @brief 物理整体迁移/重命名文件夹缓存接口（历史兼容，在直接保存模式下，重命名会自动由操作系统物理转移子文件）
     */
    static bool migrateFolderCache(const QString& oldFolderPath, const QString& newFolderPath);

    /**
     * @brief 当文件被移动/剪切到新文件夹或新硬盘时，原子化迁移该文件的元数据记录（永不丢失）
     * @param oldFilePath 移动前的物理绝对路径
     * @param newFilePath 移动后的物理绝对路径
     */
    static bool migrateItemMetadata(const QString& oldFilePath, const QString& newFilePath);

    /**
     * @param folderPath 目标物理文件夹的完整路径
     */
    explicit AmMetaJson(const std::wstring& folderPath);

    /**
     * @brief 从对应位置加载 JSON 配置文件
     */
    bool load();

    /**
     * @brief 安全保存当前元数据至对应的 JSON 文件中
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
     * @brief 静态辅助方法：读取指定物理文件夹的全部条目 ItemMeta 集合
     */
    static std::unordered_map<std::wstring, ItemMeta> readFolderMeta(const std::wstring& folderPath);

    /**
     * @brief 静态辅助方法：修改并落盘指定路径对应文件的 ItemMeta 记录
     */
    static void updateItemMeta(const std::wstring& filePath, std::function<void(ItemMeta&)> updater);

private:
    std::wstring m_folderPath;
    std::wstring m_filePath; // 映射到物理文件夹中的 .ArcMeta.json 路径
    
    FolderMeta m_folder;
    std::map<std::wstring, ItemMeta> m_items;

    static QJsonObject folderToEntry(const FolderMeta& meta);
    static FolderMeta entryToFolder(const QJsonObject& obj);
    static QJsonObject itemToEntry(const ItemMeta& meta);
    static ItemMeta entryToItem(const QJsonObject& obj);

    static QString toQString(const std::wstring& ws) { return QString::fromStdWString(ws); }
    static std::wstring toStdWString(const QString& qs) { return qs.toStdWString(); }
};

} // namespace ArcMeta