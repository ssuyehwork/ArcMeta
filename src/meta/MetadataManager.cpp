#include "MetadataManager.h"
#include "../db/Database.h"
#include "../db/ItemRepo.h"
#include <QFileInfo>
#include <QDir>
#include <windows.h>

namespace ArcMeta {

MetadataManager& MetadataManager::instance() {
    static MetadataManager inst;
    return inst;
}

MetadataManager::MetadataManager(QObject* parent) : QObject(parent) {}

void MetadataManager::initFromDatabase() {
    // 全局配置初始化
    Database::instance().init();
}

RuntimeMeta MetadataManager::getMeta(const std::wstring& path) {
    std::wstring vol = getVolumeSerialNumber(path);
    QSqlDatabase db = Database::instance().getThreadDatabase(vol);
    if (!db.isOpen()) return RuntimeMeta();

    QSqlQuery q(db);
    q.prepare("SELECT rating, color, tags, pinned, note, url FROM files WHERE path = ?");
    q.addBindValue(QString::fromStdWString(path));
    
    if (q.exec() && q.next()) {
        RuntimeMeta rm;
        rm.rating = q.value(0).toInt();
        rm.color = q.value(1).toString().toStdWString();
        QJsonDocument doc = QJsonDocument::fromJson(q.value(2).toByteArray());
        if (doc.isArray()) {
            for (const auto& v : doc.array()) rm.tags << v.toString();
        }
        rm.pinned = q.value(3).toBool();
        rm.note = q.value(4).toString().toStdWString();
        rm.url = q.value(5).toString().toStdWString();
        return rm;
    }
    return RuntimeMeta();
}

void MetadataManager::persistAsync(const std::wstring& path) {
    // 逻辑已整合至 ItemRepo
}

std::wstring MetadataManager::getVolumeSerialNumber(const std::wstring& path) {
    if (path.length() < 2 || path[1] != L':') return L"UNKNOWN";
    wchar_t root[4] = { path[0], L':', L'\', L'\0' };
    DWORD serial = 0;
    if (GetVolumeInformationW(root, nullptr, 0, &serial, nullptr, nullptr, nullptr, 0)) {
        wchar_t buf[16]; swprintf(buf, 16, L"%08X", serial); return buf;
    }
    return L"UNKNOWN";
}

// 其余辅助函数...

} // namespace ArcMeta
