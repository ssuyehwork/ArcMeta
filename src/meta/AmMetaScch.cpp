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

/**
 * @brief 匿名命名空间：放置序列化操作符，彻底解决 MSVC 的内部链接引用报错
 */
namespace {
    QDataStream& operator<<(QDataStream& ds, const std::string& s) {
        ds << QString::fromStdString(s);
        return ds;
    }
    QDataStream& operator>>(QDataStream& ds, std::string& s) {
        QString qs; ds >> qs; s = qs.toStdString();
        return ds;
    }

    QDataStream& operator<<(QDataStream& ds, const std::wstring& ws) {
        ds << QString::fromStdWString(ws);
        return ds;
    }
    QDataStream& operator>>(QDataStream& ds, std::wstring& ws) {
        QString s; ds >> s; ws = s.toStdWString();
        return ds;
    }

    QDataStream& operator<<(QDataStream& ds, const PaletteEntry& p) {
        ds << static_cast<uint8_t>(p.color.red()) << static_cast<uint8_t>(p.color.green()) << static_cast<uint8_t>(p.color.blue()) << p.ratio;
        return ds;
    }
    QDataStream& operator>>(QDataStream& ds, PaletteEntry& p) {
        uint8_t r = 0, g = 0, b = 0; float ratio = 0.0f;
        ds >> r >> g >> b >> ratio;
        p.color = QColor(r, g, b); p.ratio = ratio;
        return ds;
    }
}

AmMetaScch::AmMetaScch(const std::wstring& folderPath)
    : m_folderPath(folderPath) {
    std::wstring path = folderPath;
    if (!path.empty() && path.back() != L'\\' && path.back() != L'/') {
        path += L'\\';
    }
    m_filePath = path + L"metadata.scch";
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

    BinaryHeader header;
    if (file.read(reinterpret_cast<char*>(&header), sizeof(header)) != sizeof(header)) return false;
    if (memcmp(header.magic, "SCCH", 4) != 0 || header.version != 3) {
        file.close();
        return false; 
    }

    ds >> m_folder.sortBy >> m_folder.sortOrder >> m_folder.rating >> m_folder.color;
    int tagCount = 0; ds >> tagCount;
    m_folder.tags.clear();
    for (int i = 0; i < tagCount; ++i) { std::wstring t; ds >> t; m_folder.tags.push_back(t); }
    ds >> m_folder.pinned >> m_folder.note >> m_folder.url >> m_folder.encrypted >> m_folder.fileId128;
    int palCount = 0; ds >> palCount;
    m_folder.palettes.clear();
    for (int i = 0; i < palCount; ++i) { PaletteEntry p; ds >> p; m_folder.palettes.push_back(p); }

    m_items.clear();
    for (uint32_t i = 0; i < header.itemCount; ++i) {
        std::wstring name; ds >> name;
        ItemMeta itm;
        ds >> itm.type >> itm.rating >> itm.color;
        int iTagCount = 0; ds >> iTagCount;
        for (int k = 0; k < iTagCount; ++k) { std::wstring t; ds >> t; itm.tags.push_back(t); }
        ds >> itm.pinned >> itm.note >> itm.url >> itm.encrypted >> itm.encryptSalt >> itm.encryptIv >> itm.encryptVerifyHash;
        ds >> itm.originalName >> itm.volume >> itm.frn >> itm.fileId128 >> itm.size >> itm.creationTime >> itm.modificationTime >> itm.accessTime;
        int iPalCount = 0; ds >> iPalCount;
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

    BinaryHeader header;
    header.itemCount = static_cast<uint32_t>(m_items.size());
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));

    ds << m_folder.sortBy << m_folder.sortOrder << m_folder.rating << m_folder.color;
    ds << static_cast<int>(m_folder.tags.size());
    for (size_t i = 0; i < m_folder.tags.size(); ++i) ds << m_folder.tags[i];
    ds << m_folder.pinned << m_folder.note << m_folder.url << m_folder.encrypted << m_folder.fileId128;
    ds << static_cast<int>(m_folder.palettes.size());
    for (size_t i = 0; i < m_folder.palettes.size(); ++i) ds << m_folder.palettes[i];

    for (std::map<std::wstring, ItemMeta>::const_iterator it = m_items.begin(); it != m_items.end(); ++it) {
        const std::wstring& name = it->first;
        const ItemMeta& itm = it->second;
        ds << name << itm.type << itm.rating << itm.color;
        ds << static_cast<int>(itm.tags.size());
        for (size_t i = 0; i < itm.tags.size(); ++i) ds << itm.tags[i];
        ds << itm.pinned << itm.note << itm.url << itm.encrypted << itm.encryptSalt << itm.encryptIv << itm.encryptVerifyHash;
        ds << itm.originalName << itm.volume << itm.frn << itm.fileId128 << itm.size << itm.creationTime << itm.modificationTime << itm.accessTime;
        ds << static_cast<int>(itm.palettes.size());
        for (size_t i = 0; i < itm.palettes.size(); ++i) ds << itm.palettes[i];
    }

    file.close();

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
    std::map<std::wstring, ItemMeta>& items = meta.items();
    std::map<std::wstring, ItemMeta>::iterator it = items.find(oldName.toStdWString());
    if (it != items.end()) {
        items[newName.toStdWString()] = it->second;
        items.erase(it);
        return meta.save();
    }
    return true;
}

} // namespace ArcMeta
