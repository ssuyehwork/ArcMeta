#pragma once

#include <string>
#include <vector>
#include <map>
#include <QString>
#include "MetadataDefs.h"

namespace ArcMeta {

/**
 * @brief 处理 metadata.scch 的读写类 (Binary SCCH v3)
 */
class AmMetaScch {
public:
    /**
     * @brief 二进制头结构：显式初始化
     */
    struct BinaryHeader {
        char magic[4];
        uint32_t version;
        uint32_t itemCount;
        uint32_t reserved;

        BinaryHeader() : version(3), itemCount(0), reserved(0) {
            magic[0] = 'S'; magic[1] = 'C'; magic[2] = 'C'; magic[3] = 'H';
        }
    };

    explicit AmMetaScch(const std::wstring& folderPath, const std::wstring& fileName = L"__LEGACY__");
    bool load();
    bool save() const;

    FolderMeta& folder() { return m_folder; }
    const FolderMeta& folder() const { return m_folder; }

    ItemMeta& item() { return m_item; }
    const ItemMeta& item() const { return m_item; }
    void setItem(const ItemMeta& item) { m_item = item; }

    // Deprecated: switch to file-mode for performance
    std::map<std::wstring, ItemMeta>& items() { return m_items; }
    const std::map<std::wstring, ItemMeta>& items() const { return m_items; }

    void remove(const std::wstring& fileName) { m_items.erase(fileName); }

    void setItemColor(const std::wstring& fileName, const std::wstring& color) {
        if (m_isFileMode) m_item.color = color;
        else m_items[fileName].color = color;
    }

    static bool renameItem(const QString& folderPath, const QString& oldName, const QString& newName);

private:
    std::wstring m_folderPath;
    std::wstring m_arcmetaDir;
    std::wstring m_filePath;
    bool m_isFileMode;
    
    FolderMeta m_folder;
    ItemMeta m_item;
    std::map<std::wstring, ItemMeta> m_items;

    static QString toQString(const std::wstring& ws) { return QString::fromStdWString(ws); }
};

} // namespace ArcMeta
