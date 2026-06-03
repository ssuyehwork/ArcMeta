#pragma once

#include <string>
#include <vector>
#include <map>
#include <QString>
#include "MetadataDefs.h"

namespace ArcMeta {

/**
 * @brief 处理 metadata.scch 的读写类 (Binary SCCH v3)
 * 2026-06-xx 物理加固：彻底废除 JSON 格式，切换为全二进制流存储，对标工业级性能。
 */
class AmMetaScch {
public:
    /**
     * @param folderPath 目标文件夹的完整路径（不含文件名）
     */
    explicit AmMetaScch(const std::wstring& folderPath);

    /**
     * @brief 加载 metadata.scch 二进制文件
     */
    bool load();

    /**
     * @brief 安全保存为 metadata.scch 二进制格式
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
     * @brief 静态辅助方法：重命名元数据条目
     */
    static bool renameItem(const QString& folderPath, const QString& oldName, const QString& newName);

private:
    std::wstring m_folderPath;
    std::wstring m_filePath;
    
    FolderMeta m_folder;
    std::map<std::wstring, ItemMeta> m_items;

    // 2026-06-xx 物理重构：二进制序列化辅助 (不再使用 QJson)
    struct BinaryHeader;
    struct BinaryItemRecord;
    struct BinaryFolderRecord;

    static QString toQString(const std::wstring& ws) { return QString::fromStdWString(ws); }
    static std::wstring toStdWString(const QString& qs) { return qs.toStdWString(); }
};

} // namespace ArcMeta
