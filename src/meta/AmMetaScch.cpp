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

AmMetaScch::AmMetaScch(const std::wstring& folderPath, const std::wstring& fileName)
    : m_folderPath(folderPath) {
    m_arcmetaDir = folderPath + L"\\.arcmeta";

    if (fileName.empty()) {
        // 文件夹自身元数据模式
        m_filePath = m_arcmetaDir + L"\\__folder__.scch";
        m_isFileMode = false;
    } else {
        // 文件级元数据模式
        m_filePath = m_arcmetaDir + L"\\" + fileName + L".scch";
        m_isFileMode = true;
    }
}

bool AmMetaScch::load() {
    QFile file(toQString(m_filePath));
    qDebug() << "[AmMetaScch] 尝试加载元数据文件:" << toQString(m_filePath);
    bool isLegacy = false;
    if (!file.exists()) {
        // 迁移支持：如果 __folder__.scch 不存在，尝试读取旧版 metadata.scch
        if (!m_isFileMode) {
            QString legacyPath = toQString(m_folderPath) + "/metadata.scch";
            if (QFile::exists(legacyPath)) {
                file.setFileName(legacyPath);
                isLegacy = true;
            }
        }
        
        if (!isLegacy) {
            m_folder = FolderMeta();
            m_item = ItemMeta();
            m_items.clear();
            return true;
        }
    }

    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "[AmMetaScch] 无法打开元数据文件进行读取:" << toQString(m_filePath);
        return false;
    }
    QDataStream ds(&file);
    ds.setVersion(QDataStream::Qt_6_0);

    BinaryHeader header;
    if (file.read(reinterpret_cast<char*>(&header), sizeof(header)) != sizeof(header)) return false;
    if (memcmp(header.magic, "SCCH", 4) != 0 || header.version != 3) {
        file.close();
        return false; 
    }

    if (m_isFileMode) {
        ds >> m_item.type >> m_item.rating >> m_item.color;
        int iTagCount = 0; ds >> iTagCount;
        m_item.tags.clear();
        for (int k = 0; k < iTagCount; ++k) { std::wstring t; ds >> t; m_item.tags.push_back(t); }
        ds >> m_item.pinned >> m_item.note >> m_item.url >> m_item.encrypted >> m_item.encryptSalt >> m_item.encryptIv >> m_item.encryptVerifyHash;
        ds >> m_item.originalName >> m_item.volume >> m_item.frn >> m_item.fileId128 >> m_item.size >> m_item.creationTime >> m_item.modificationTime >> m_item.accessTime;
        int iPalCount = 0; ds >> iPalCount;
        m_item.palettes.clear();
        for (int k = 0; k < iPalCount; ++k) { PaletteEntry p; ds >> p; m_item.palettes.push_back(p); }
        
        // 兼容性：如果加载的是旧版目录级 scch（通过构造函数误用），清空 items
        m_items.clear();
    } else {
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
    }

    return true;
}

bool AmMetaScch::save() const {
    // 按需创建 .arcmeta 目录
    QDir dir(toQString(m_arcmetaDir));
    if (!dir.exists()) {
        dir.mkpath(".");
        SetFileAttributesW(m_arcmetaDir.c_str(), FILE_ATTRIBUTE_HIDDEN);
    }

    QString tmpPath = toQString(m_filePath) + ".tmp";
    QFile file(tmpPath);
    if (!file.open(QIODevice::WriteOnly)) return false;

    QDataStream ds(&file);
    ds.setVersion(QDataStream::Qt_6_0);

    BinaryHeader header;
    header.itemCount = m_isFileMode ? 1 : 0; // 文件夹模式下不在此保存项目列表
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));

    if (m_isFileMode) {
        ds << m_item.type << m_item.rating << m_item.color;
        ds << static_cast<int>(m_item.tags.size());
        for (size_t i = 0; i < m_item.tags.size(); ++i) ds << m_item.tags[i];
        ds << m_item.pinned << m_item.note << m_item.url << m_item.encrypted << m_item.encryptSalt << m_item.encryptIv << m_item.encryptVerifyHash;
        ds << m_item.originalName << m_item.volume << m_item.frn << m_item.fileId128 << m_item.size << m_item.creationTime << m_item.modificationTime << m_item.accessTime;
        ds << static_cast<int>(m_item.palettes.size());
        for (size_t i = 0; i < m_item.palettes.size(); ++i) ds << m_item.palettes[i];
    } else {
        ds << m_folder.sortBy << m_folder.sortOrder << m_folder.rating << m_folder.color;
        ds << static_cast<int>(m_folder.tags.size());
        for (size_t i = 0; i < m_folder.tags.size(); ++i) ds << m_folder.tags[i];
        ds << m_folder.pinned << m_folder.note << m_folder.url << m_folder.encrypted << m_folder.fileId128;
        ds << static_cast<int>(m_folder.palettes.size());
        for (size_t i = 0; i < m_folder.palettes.size(); ++i) ds << m_folder.palettes[i];
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

    // 1. 尝试处理文件级元数据
    QString oldScchPath = folderPath + "/.arcmeta/" + oldName + ".scch";
    QString newScchPath = folderPath + "/.arcmeta/" + newName + ".scch";
    if (QFile::exists(oldScchPath)) {
        // 如果目标已存在，先删除（覆盖逻辑）
        if (QFile::exists(newScchPath)) QFile::remove(newScchPath);
        return QFile::rename(oldScchPath, newScchPath);
    }

    // 2. 回退到旧版目录级元数据
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
