#include "AmMetaJson.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QCoreApplication>
#include <QCryptographicHash>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace ArcMeta {

QString AmMetaJson::getCacheDirectory() {
    // 默认存放在主程序根目录下的 "ArcMeta.cache" 文件夹
    QString appDir = QCoreApplication::applicationDirPath();
    QString cacheDir = QDir::toNativeSeparators(appDir + "/ArcMeta.cache");
    
    QDir dir(cacheDir);
    if (!dir.exists()) {
        dir.mkpath(cacheDir);
        // 在 Windows 下将 ArcMeta.cache 文件夹设为隐藏属性，保持根目录整洁
        #ifdef Q_OS_WIN
        SetFileAttributesW(cacheDir.toStdWString().c_str(), FILE_ATTRIBUTE_HIDDEN);
        #endif
    }
    return cacheDir;
}

std::wstring AmMetaJson::resolveCacheFilePath(const std::wstring& folderPath) {
    if (folderPath.empty()) return L"";

    // 1. 路径归一化（统一为小写与 Windows 标准分隔符，确保同一个文件夹算出的哈希绝对唯一）
    QString normPath = QDir::toNativeSeparators(QDir::cleanPath(QString::fromStdWString(folderPath))).toLower();
    
    // 2. 使用 SHA-256 算法计算物理路径的唯一哈希，彻底规避长路径 (MAX_PATH) 及非法文件名字符问题
    QByteArray hash = QCryptographicHash::hash(normPath.toUtf8(), QCryptographicHash::Sha256).toHex();
    
    // 3. 拼装为 ArcMeta.cache/哈希值.json
    QString cacheFileName = QString::fromLatin1(hash) + ".json";
    QString fullCachePath = getCacheDirectory() + "/" + cacheFileName;
    return QDir::toNativeSeparators(fullCachePath).toStdWString();
}

AmMetaJson::AmMetaJson(const std::wstring& folderPath)
    : m_folderPath(folderPath) {
    // 将输入的物理文件夹路径映射为 ArcMeta.cache 中的高级 JSON 缓存物理路径
    m_filePath = resolveCacheFilePath(folderPath);
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
    if (m_filePath.empty()) return false;

    QJsonObject root;
    root.insert("version", "2"); // 物理对齐 SHA-256 版本
    root.insert("folder", folderToEntry(m_folder));

    QJsonObject itemsObj;
    for (const auto& [name, meta] : m_items) {
        if (meta.hasUserOperations()) {
            itemsObj.insert(toQString(name), itemToEntry(meta));
        }
    }
    root.insert("items", itemsObj);

    QByteArray jsonData = QJsonDocument(root).toJson(QJsonDocument::Indented);
    QString targetPath = toQString(m_filePath);
    QString tmpPath = targetPath + ".tmp";
    
    QFile tmpFile(tmpPath);
    if (!tmpFile.open(QIODevice::WriteOnly)) return false;
    tmpFile.write(jsonData);
    tmpFile.close();

    // 物理原子替换，保障多线程落盘崩溃安全
    if (!MoveFileExW(tmpPath.toStdWString().c_str(), m_filePath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        QFile::remove(tmpPath);
        return false;
    }
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

bool AmMetaJson::migrateFolderCache(const QString& oldFolderPath, const QString& newFolderPath) {
    if (oldFolderPath == newFolderPath) return true;
    std::wstring oldCachePath = resolveCacheFilePath(oldFolderPath.toStdWString());
    std::wstring newCachePath = resolveCacheFilePath(newFolderPath.toStdWString());

    QFile oldFile(toQString(oldCachePath));
    if (oldFile.exists()) {
        // 如果新缓存文件已存在，先清理
        QFile::remove(toQString(newCachePath));
        return oldFile.rename(toQString(newCachePath));
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
    obj.insert("file_id_128", QString::fromStdString(meta.fileId128));
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
    meta.fileId128 = obj.value("file_id_128").toString().toStdString();
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
    obj.insert("file_id_128", QString::fromStdString(meta.fileId128));
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
    meta.fileId128 = obj.value("file_id_128").toString().toStdString();
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