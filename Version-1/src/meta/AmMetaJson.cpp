#include "AmMetaJson.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace ArcMeta {

AmMetaJson::AmMetaJson(const std::wstring& folderPath)
    : m_folderPath(folderPath) {
    std::wstring path = folderPath;
    if (!path.empty() && path.back() != L'\\' && path.back() != L'/') {
        path += L'\\';
    }
    // 🚨 彻底废除 .am_meta.json，唯一物理文件名：.ArcMeta.json
    m_filePath = path + L".ArcMeta.json";
}

bool AmMetaJson::load() {
    QFile file(toQString(m_filePath));
    if (!file.exists()) {
        m_folder = FolderMeta();
        m_items.clear();
        return true;
    }

    if (!file.open(QIODevice::ReadOnly)) return false;
    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject()) return false;

    QJsonObject root = doc.object();
    if (root.contains("folder") && root.value("folder").isObject()) {
        m_folder = entryToFolder(root.value("folder").toObject());
    }
    
    m_items.clear();
    if (root.contains("items") && root.value("items").isObject()) {
        QJsonObject itemsObj = root.value("items").toObject();
        for (auto it = itemsObj.begin(); it != itemsObj.end(); ++it) {
            m_items[toStdWString(it.key())] = entryToItem(it.value().toObject());
        }
    }
    return true;
}

bool AmMetaJson::save() const {
    QJsonObject root;
    root.insert("version", "2");
    root.insert("folder", folderToEntry(m_folder));

    QJsonObject itemsObj;
    for (const auto& [name, meta] : m_items) {
        if (meta.hasUserOperations()) {
            itemsObj.insert(toQString(name), itemToEntry(meta));
        }
    }
    root.insert("items", itemsObj);

    QByteArray jsonData = QJsonDocument(root).toJson(QJsonDocument::Indented);
    QString tmpPath = toQString(m_filePath) + ".tmp";
    
    QFile tmpFile(tmpPath);
    if (!tmpFile.open(QIODevice::WriteOnly)) return false;
    tmpFile.write(jsonData);
    tmpFile.close();

    if (!MoveFileExW(tmpPath.toStdWString().c_str(), m_filePath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        QFile::remove(tmpPath);
        return false;
    }
    // 赋予 Windows 隐藏文件属性
    SetFileAttributesW(m_filePath.c_str(), FILE_ATTRIBUTE_HIDDEN);
    return true;
}

bool AmMetaJson::renameItem(const QString& folderPath, const QString& oldName, const QString& newName) {
    if (oldName == newName) return true;
    AmMetaJson meta(folderPath.toStdWString());
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

bool AmMetaJson::migrateItemMetadata(const QString& oldFilePath, const QString& newFilePath) { 
    if (oldFilePath == newFilePath) return true; 
 
    QFileInfo oldInfo(oldFilePath); 
    QFileInfo newInfo(newFilePath); 
 
    QString oldParent = QDir::toNativeSeparators(oldInfo.absolutePath()); 
    QString newParent = QDir::toNativeSeparators(newInfo.absolutePath()); 
    std::wstring oldFileName = oldInfo.fileName().toStdWString(); 
    std::wstring newFileName = newInfo.fileName().toStdWString(); 
 
    // 1. 从源目录的 .ArcMeta.json 读取元数据 
    AmMetaJson oldJson(oldParent.toStdWString()); 
    if (!oldJson.load()) return false; 
 
    auto& oldItems = oldJson.items(); 
    auto it = oldItems.find(oldFileName); 
    if (it == oldItems.end()) { 
        // 源目录无特殊元数据，无需迁移 
        return true; 
    } 
 
    ItemMeta metaCopy = it->second; // 完整复制元数据（星级、颜色、标签、备注等） 
 
    // 2. 从源目录抹除该条目并物理保存 
    oldItems.erase(it); 
    oldJson.save(); 
 
    // 3. 将元数据注入到目标目录的 .ArcMeta.json 
    AmMetaJson newJson(newParent.toStdWString()); 
    newJson.load(); // 加载或自动初始化 
    newJson.items()[newFileName] = metaCopy; 
    return newJson.save(); // 100% 物理落盘并保持隐藏属性 
}

bool AmMetaJson::migrateFolderCache(const QString& oldFolderPath, const QString& newFolderPath) {
    if (oldFolderPath == newFolderPath) return true;
    
    // 物理自愈：当发生物理文件夹重命名时，自动原子迁移其目录内部隐藏的 .ArcMeta.json 配置
    QString oldMetaFile = oldFolderPath + "/.ArcMeta.json";
    QString newMetaFile = newFolderPath + "/.ArcMeta.json";

    if (QFile::exists(oldMetaFile)) {
        // 创建新物理目录（如果不存在）
        QDir().mkpath(newFolderPath);
        
        if (QFile::exists(newMetaFile)) {
            QFile::remove(newMetaFile);
        }
        if (QFile::copy(oldMetaFile, newMetaFile)) {
            QFile::remove(oldMetaFile);
            
            // 赋予 Windows 环境隐藏文件属性
            SetFileAttributesW(newMetaFile.toStdWString().c_str(), FILE_ATTRIBUTE_HIDDEN);
            return true;
        }
    }
    return true;
}

// --- 内部 JSON 结构转换实现 ---

QJsonObject AmMetaJson::folderToEntry(const FolderMeta& meta) {
    QJsonObject obj;
    obj.insert("sort_by", toQString(meta.sortBy));
    obj.insert("sort_order", toQString(meta.sortOrder));
    obj.insert("rating", meta.rating);
    obj.insert("color", toQString(meta.color));
    obj.insert("pinned", meta.pinned);
    obj.insert("note", toQString(meta.note));
    obj.insert("url", toQString(meta.url));
    obj.insert("encrypted", meta.encrypted);
    // 🚨 保持兼容性：磁盘上存储的旧版 JSON 配置文件依然使用 "file_id_128"，内存映射使用统一的 folderId
    obj.insert("file_id_128", QString::fromStdString(meta.folderId));
    QJsonArray tagsArr; for (const auto& t : meta.tags) tagsArr.append(toQString(t));
    obj.insert("tags", tagsArr);
    if (!meta.palettes.empty()) {
        QJsonArray palArr;
        for (const auto& p : meta.palettes) {
            QJsonObject pObj;
            QJsonArray cArr;
            cArr.append(p.color.red()); cArr.append(p.color.green()); cArr.append(p.color.blue());
            pObj.insert("color", cArr);
            pObj.insert("ratio", (double)p.ratio);
            palArr.append(pObj);
        }
        obj.insert("palettes", palArr);
    }
    return obj;
}

FolderMeta AmMetaJson::entryToFolder(const QJsonObject& obj) {
    FolderMeta meta;
    meta.sortBy = toStdWString(obj.value("sort_by").toString("name"));
    meta.sortOrder = toStdWString(obj.value("sort_order").toString("asc"));
    meta.rating = obj.value("rating").toInt();
    meta.color = toStdWString(obj.value("color").toString());
    meta.pinned = obj.value("pinned").toBool();
    meta.note = toStdWString(obj.value("note").toString());
    meta.url = toStdWString(obj.value("url").toString());
    meta.encrypted = obj.value("encrypted").toBool();
    meta.folderId = obj.value("file_id_128").toString().toStdString();
    if (obj.contains("tags") && obj.value("tags").isArray()) {
        for (const auto& v : obj.value("tags").toArray()) meta.tags.push_back(toStdWString(v.toString()));
    }
    if (obj.contains("palettes") && obj.value("palettes").isArray()) {
        for (const auto& v : obj.value("palettes").toArray()) {
            QJsonObject pObj = v.toObject();
            QJsonArray cArr = pObj.value("color").toArray();
            if (cArr.size() >= 3) {
                meta.palettes.push_back({QColor(cArr.at(0).toInt(), cArr.at(1).toInt(), cArr.at(2).toInt()), (float)pObj.value("ratio").toDouble()});
            }
        }
    }
    return meta;
}

QJsonObject AmMetaJson::itemToEntry(const ItemMeta& meta) {
    QJsonObject obj;
    obj.insert("type", toQString(meta.type));
    obj.insert("rating", meta.rating);
    obj.insert("color", toQString(meta.color));
    obj.insert("pinned", meta.pinned);
    obj.insert("note", toQString(meta.note));
    obj.insert("url", toQString(meta.url));
    obj.insert("encrypted", meta.encrypted);
    obj.insert("encrypt_salt", QString::fromStdString(meta.encryptSalt));
    obj.insert("encrypt_iv", QString::fromLatin1(QByteArray::fromStdString(meta.encryptIv).toBase64()));
    obj.insert("encrypt_verify_hash", QString::fromStdString(meta.encryptVerifyHash));
    obj.insert("original_name", toQString(meta.originalName));
    obj.insert("volume", toQString(meta.volume));
    obj.insert("frn", toQString(meta.frn));
    // 🚨 保持兼容性：磁盘上存储的旧版 JSON 配置文件依然使用 "file_id_128"，内存映射使用统一的 folderId
    obj.insert("file_id_128", QString::fromStdString(meta.folderId));
    
    // 2026-07-xx 1:1对等字段写入
    obj.insert("width", meta.width);
    obj.insert("height", meta.height);
    obj.insert("auto_color", toQString(meta.autoColor));
    obj.insert("added_at", meta.addedAt);

    QJsonArray tagsArr; for (const auto& t : meta.tags) tagsArr.append(toQString(t));
    obj.insert("tags", tagsArr);
    if (!meta.palettes.empty()) {
        QJsonArray palArr;
        for (const auto& p : meta.palettes) {
            QJsonObject pObj;
            QJsonArray cArr;
            cArr.append(p.color.red()); cArr.append(p.color.green()); cArr.append(p.color.blue());
            pObj.insert("color", cArr);
            pObj.insert("ratio", (double)p.ratio);
            palArr.append(pObj);
        }
        obj.insert("palettes", palArr);
    }
    return obj;
}

ItemMeta AmMetaJson::entryToItem(const QJsonObject& obj) {
    ItemMeta meta;
    meta.type = toStdWString(obj.value("type").toString("file"));
    meta.rating = obj.value("rating").toInt();
    meta.color = toStdWString(obj.value("color").toString());
    meta.pinned = obj.value("pinned").toBool();
    meta.note = toStdWString(obj.value("note").toString());
    meta.url = toStdWString(obj.value("url").toString());
    meta.encrypted = obj.value("encrypted").toBool();
    meta.encryptSalt = obj.value("encrypt_salt").toString().toStdString();
    meta.encryptIv = QByteArray::fromBase64(obj.value("encrypt_iv").toString().toLatin1()).toStdString();
    meta.encryptVerifyHash = obj.value("encrypt_verify_hash").toString().toStdString();
    meta.originalName = toStdWString(obj.value("original_name").toString());
    meta.volume = toStdWString(obj.value("volume").toString());
    meta.frn = toStdWString(obj.value("frn").toString());
    meta.folderId = obj.value("file_id_128").toString().toStdString();

    // 2026-07-xx 1:1对等字段读取
    meta.width = obj.value("width").toInt(0);
    meta.height = obj.value("height").toInt(0);
    meta.autoColor = toStdWString(obj.value("auto_color").toString());
    meta.addedAt = obj.value("added_at").toVariant().toLongLong();

    if (obj.contains("tags") && obj.value("tags").isArray()) {
        for (const auto& v : obj.value("tags").toArray()) meta.tags.push_back(toStdWString(v.toString()));
    }
    if (obj.contains("palettes") && obj.value("palettes").isArray()) {
        for (const auto& v : obj.value("palettes").toArray()) {
            QJsonObject pObj = v.toObject();
            QJsonArray cArr = pObj.value("color").toArray();
            if (cArr.size() >= 3) {
                meta.palettes.push_back({QColor(cArr.at(0).toInt(), cArr.at(1).toInt(), cArr.at(2).toInt()), (float)pObj.value("ratio").toDouble()});
            }
        }
    }
    return meta;
}

} // namespace ArcMeta