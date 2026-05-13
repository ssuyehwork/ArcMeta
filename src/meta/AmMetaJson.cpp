#include "AmMetaJson.h"
#include <windows.h>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>

namespace ArcMeta {

AmMetaJson::AmMetaJson(const std::wstring& folderPath)
    : m_folderPath(folderPath) {
    std::wstring path = folderPath;
    if (!path.empty() && path.back() != L'\\' && path.back() != L'/') {
        path += L'\\';
    }
    m_filePath = path + L".am_meta.json";
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
    if (root.contains("folder") && root["folder"].isObject()) {
        m_folder = entryToFolder(root["folder"].toObject());
    }
    
    m_items.clear();
    if (root.contains("items") && root["items"].isObject()) {
        QJsonObject itemsObj = root["items"].toObject();
        for (auto it = itemsObj.begin(); it != itemsObj.end(); ++it) {
            m_items[toStdWString(it.key())] = entryToItem(it.value().toObject());
        }
    }
    return true;
}

bool AmMetaJson::save() const {
    QJsonObject root;
    root["version"] = "2"; // 2026-06-xx 物理加固：版本升至 2 以对齐 SHA-256
    root["folder"] = folderToEntry(m_folder);

    QJsonObject itemsObj;
    for (const auto& [name, meta] : m_items) {
        if (meta.hasUserOperations()) {
            itemsObj[toQString(name)] = itemToEntry(meta);
        }
    }
    root["items"] = itemsObj;

    QByteArray jsonData = QJsonDocument(root).toJson(QJsonDocument::Indented);
    QString tmpPath = toQString(m_filePath) + ".tmp";
    
    QFile tmpFile(tmpPath);
    if (!tmpFile.open(QIODevice::WriteOnly)) return false;
    tmpFile.write(jsonData);
    tmpFile.close();

    // 2026-06-xx 按照用户要求：原子替换并设置隐藏属性
    if (!MoveFileExW(tmpPath.toStdWString().c_str(), m_filePath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        QFile::remove(tmpPath);
        return false;
    }
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

// --- 内部转换实现 ---

QJsonObject AmMetaJson::folderToEntry(const FolderMeta& meta) {
    QJsonObject obj;
    obj["sort_by"] = toQString(meta.sortBy);
    obj["sort_order"] = toQString(meta.sortOrder);
    obj["rating"] = meta.rating;
    obj["color"] = toQString(meta.color);
    obj["pinned"] = meta.pinned;
    obj["note"] = toQString(meta.note);
    obj["encrypted"] = meta.encrypted;
    obj["file_id_128"] = QString::fromStdString(meta.fileId128);
    QJsonArray tagsArr; for (const auto& t : meta.tags) tagsArr.append(toQString(t));
    obj["tags"] = tagsArr;
    return obj;
}

FolderMeta AmMetaJson::entryToFolder(const QJsonObject& obj) {
    FolderMeta meta;
    meta.sortBy = toStdWString(obj["sort_by"].toString("name"));
    meta.sortOrder = toStdWString(obj["sort_order"].toString("asc"));
    meta.rating = obj["rating"].toInt();
    meta.color = toStdWString(obj["color"].toString());
    meta.pinned = obj["pinned"].toBool();
    meta.note = toStdWString(obj["note"].toString());
    meta.encrypted = obj["encrypted"].toBool();
    meta.fileId128 = obj["file_id_128"].toString().toStdString();
    if (obj.contains("tags") && obj["tags"].isArray()) {
        for (const auto& v : obj["tags"].toArray()) meta.tags.push_back(toStdWString(v.toString()));
    }
    return meta;
}

QJsonObject AmMetaJson::itemToEntry(const ItemMeta& meta) {
    QJsonObject obj;
    obj["type"] = toQString(meta.type);
    obj["rating"] = meta.rating;
    obj["color"] = toQString(meta.color);
    obj["pinned"] = meta.pinned;
    obj["note"] = toQString(meta.note);
    obj["encrypted"] = meta.encrypted;
    obj["encrypt_salt"] = QString::fromStdString(meta.encryptSalt);
    obj["encrypt_iv"] = QString::fromLatin1(QByteArray::fromStdString(meta.encryptIv).toBase64());
    obj["encrypt_verify_hash"] = QString::fromStdString(meta.encryptVerifyHash);
    obj["original_name"] = toQString(meta.originalName);
    obj["volume"] = toQString(meta.volume);
    obj["frn"] = toQString(meta.frn);
    obj["file_id_128"] = QString::fromStdString(meta.fileId128);
    QJsonArray tagsArr; for (const auto& t : meta.tags) tagsArr.append(toQString(t));
    obj["tags"] = tagsArr;
    return obj;
}

ItemMeta AmMetaJson::entryToItem(const QJsonObject& obj) {
    ItemMeta meta;
    meta.type = toStdWString(obj["type"].toString("file"));
    meta.rating = obj["rating"].toInt();
    meta.color = toStdWString(obj["color"].toString());
    meta.pinned = obj["pinned"].toBool();
    meta.note = toStdWString(obj["note"].toString());
    meta.encrypted = obj["encrypted"].toBool();
    meta.encryptSalt = obj["encrypt_salt"].toString().toStdString();
    meta.encryptIv = QByteArray::fromBase64(obj["encrypt_iv"].toString().toLatin1()).toStdString();
    meta.encryptVerifyHash = obj["encrypt_verify_hash"].toString().toStdString();
    meta.originalName = toStdWString(obj["original_name"].toString());
    meta.volume = toStdWString(obj["volume"].toString());
    meta.frn = toStdWString(obj["frn"].toString());
    meta.fileId128 = obj["file_id_128"].toString().toStdString();
    if (obj.contains("tags") && obj["tags"].isArray()) {
        for (const auto& v : obj["tags"].toArray()) meta.tags.push_back(toStdWString(v.toString()));
    }
    return meta;
}

} // namespace ArcMeta
