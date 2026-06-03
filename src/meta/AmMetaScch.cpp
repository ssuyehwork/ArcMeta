#include <QFile>
#include <QFileInfo>
#include <QDataStream>
#include <QDir>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "AmMetaScch.h"
#include <windows.h>

namespace ArcMeta {

// 2026-06-xx 二进制协议 v3 定义
#pragma pack(push, 1)

// 紧凑型调色板数据 (24字节)
struct BinaryPalette {
    uint8_t r, g, b;
    float ratio;
};
#pragma pack(pop)

AmMetaScch::AmMetaScch(const std::wstring& folderPath)
    : m_folderPath(folderPath) {
    std::wstring path = folderPath;
    if (!path.empty() && path.back() != L'\\' && path.back() != L'/') {
        path += L'\\';
    }
    m_filePath = path + L"metadata.scch";
}

// 辅助：QDataStream 扩展支持 std::string 和 std::wstring
static QDataStream& operator<<(QDataStream& ds, const std::string& s) {
    ds << QString::fromStdString(s);
    return ds;
}
static QDataStream& operator>>(QDataStream& ds, std::string& s) {
    QString qs; ds >> qs; s = qs.toStdString();
    return ds;
}

static QDataStream& operator<<(QDataStream& ds, const std::wstring& ws) {
    ds << QString::fromStdWString(ws);
    return ds;
}
static QDataStream& operator>>(QDataStream& ds, std::wstring& ws) {
    QString s; ds >> s; ws = s.toStdWString();
    return ds;
}

// 辅助：序列化 PaletteEntry
static QDataStream& operator<<(QDataStream& ds, const PaletteEntry& p) {
    ds << (uint8_t)p.color.red() << (uint8_t)p.color.green() << (uint8_t)p.color.blue() << p.ratio;
    return ds;
}
static QDataStream& operator>>(QDataStream& ds, PaletteEntry& p) {
    uint8_t r, g, b; float ratio;
    ds >> r >> g >> b >> ratio;
    p.color = QColor(r, g, b); p.ratio = ratio;
    return ds;
}

bool AmMetaScch::load() {
    QFile file(toQString(m_filePath));
    if (!file.exists()) {
        m_folder = FolderMeta();
        m_items.clear();
        return true;
    }

    if (!file.open(QIODevice::ReadOnly)) return false;
    QDataStream ds(&file);
    ds.setVersion(QDataStream::Qt_6_0);

    AmMetaScch::BinaryHeader header;
    file.read((char*)&header, sizeof(header));
    if (memcmp(header.magic, "SCCH", 4) != 0 || header.version != 3) {
        // 2026-06-xx 物理清算：若检测到非二进制 v3 格式（如旧版 JSON），直接视为失效
        file.close();
        return false; 
    }

    // 1. 加载文件夹元数据
    ds >> m_folder.sortBy >> m_folder.sortOrder >> m_folder.rating >> m_folder.color;
    int tagCount; ds >> tagCount;
    m_folder.tags.clear();
    for (int i = 0; i < tagCount; ++i) { std::wstring t; ds >> t; m_folder.tags.push_back(t); }
    ds >> m_folder.pinned >> m_folder.note >> m_folder.url >> m_folder.encrypted >> m_folder.fileId128;
    int palCount; ds >> palCount;
    m_folder.palettes.clear();
    for (int i = 0; i < palCount; ++i) { PaletteEntry p; ds >> p; m_folder.palettes.push_back(p); }

    // 2. 加载项目元数据
    m_items.clear();
    for (uint32_t i = 0; i < header.itemCount; ++i) {
        std::wstring name; ds >> name;
        ItemMeta itm;
        ds >> itm.type >> itm.rating >> itm.color;
        int iTagCount; ds >> iTagCount;
        for (int k = 0; k < iTagCount; ++k) { std::wstring t; ds >> t; itm.tags.push_back(t); }
        ds >> itm.pinned >> itm.note >> itm.url >> itm.encrypted >> itm.encryptSalt >> itm.encryptIv >> itm.encryptVerifyHash;
        ds >> itm.originalName >> itm.volume >> itm.frn >> itm.fileId128 >> itm.size >> itm.creationTime >> itm.modificationTime >> itm.accessTime;
        int iPalCount; ds >> iPalCount;
        for (int k = 0; k < iPalCount; ++k) { PaletteEntry p; ds >> p; itm.palettes.push_back(p); }
        m_items[name] = itm;
    }

    return true;
}

bool AmMetaScch::save() const {
    QString tmpPath = toQString(m_filePath) + ".tmp";
    QFile file(tmpPath);
    if (!file.open(QIODevice::WriteOnly)) return false;

    QDataStream ds(&file);
    ds.setVersion(QDataStream::Qt_6_0);

    AmMetaScch::BinaryHeader header;
    header.itemCount = (uint32_t)m_items.size();
    file.write((char*)&header, sizeof(header));

    // 1. 序列化文件夹元数据
    ds << m_folder.sortBy << m_folder.sortOrder << m_folder.rating << m_folder.color;
    ds << (int)m_folder.tags.size();
    for (const auto& t : m_folder.tags) ds << t;
    ds << m_folder.pinned << m_folder.note << m_folder.url << m_folder.encrypted << m_folder.fileId128;
    ds << (int)m_folder.palettes.size();
    for (const auto& p : m_folder.palettes) ds << p;

    // 2. 序列化项目元数据
    for (std::map<std::wstring, ItemMeta>::const_iterator it = m_items.begin(); it != m_items.end(); ++it) {
        const std::wstring& name = it->first;
        const ItemMeta& itm = it->second;
        ds << name << itm.type << itm.rating << itm.color;
        ds << (int)itm.tags.size();
        for (size_t i = 0; i < itm.tags.size(); ++i) ds << itm.tags[i];
        ds << itm.pinned << itm.note << itm.url << itm.encrypted << itm.encryptSalt << itm.encryptIv << itm.encryptVerifyHash;
        ds << itm.originalName << itm.volume << itm.frn << itm.fileId128 << itm.size << itm.creationTime << itm.modificationTime << itm.accessTime;
        ds << (int)itm.palettes.size();
        for (size_t i = 0; i < itm.palettes.size(); ++i) ds << itm.palettes[i];
    }

    file.close();

    // 原子替换
    if (!MoveFileExW(tmpPath.toStdWString().c_str(), m_filePath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        QFile::remove(tmpPath);
        return false;
    }
    SetFileAttributesW(m_filePath.c_str(), FILE_ATTRIBUTE_HIDDEN);
    return true;
}

bool AmMetaScch::renameItem(const QString& folderPath, const QString& oldName, const QString& newName) {
    if (oldName == newName) return true;
    AmMetaScch meta(folderPath.toStdWString());
    if (!meta.load()) return false;
    auto& items = meta.items();
    auto it = items.find(oldName.toStdWString());
    if (it != items.end()) {
        items[newName.toStdWString()] = it->second;
        items.erase(it);
        return meta.save();
    }
    return true;
}

} // namespace ArcMeta
