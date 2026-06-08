#include "MetadataManager.h"
#include "../db/Database.h"
#include "../db/ItemRepo.h"
#include <QFileInfo>
#include <QDir>
#include <windows.h>
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QSqlDatabase>

namespace ArcMeta {
MetadataManager& MetadataManager::instance() { static MetadataManager inst; return inst; }
MetadataManager::MetadataManager(QObject* parent) : QObject(parent) {}
void MetadataManager::initFromDatabase() { Database::instance().init(); }

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
        if (doc.isArray()) { for (const auto& v : doc.array()) rm.tags << v.toString(); }
        rm.pinned = q.value(3).toBool();
        rm.note = q.value(4).toString().toStdWString();
        rm.url = q.value(5).toString().toStdWString();
        return rm;
    }
    return RuntimeMeta();
}

void MetadataManager::persistAsync(const std::wstring& path) { Q_UNUSED(path); }
std::wstring MetadataManager::getVolumeSerialNumber(const std::wstring& path) {
    if (path.length() < 2 || path[1] != L':') return L"UNKNOWN";
    wchar_t root[4] = { path[0], L':', L'\\', L'\0' };
    DWORD serial = 0;
    if (GetVolumeInformationW(root, nullptr, 0, &serial, nullptr, nullptr, nullptr, 0)) {
        wchar_t buf[16]; swprintf(buf, 16, L"%08X", serial); return buf;
    }
    return L"UNKNOWN";
}

QStringList MetadataManager::searchInCache(const QString& k) { Q_UNUSED(k); return {}; }

void MetadataManager::setRating(const std::wstring& p, int r) {
    std::wstring vol = getVolumeSerialNumber(p);
    QSqlDatabase db = Database::instance().getThreadDatabase(vol);
    if (!db.isOpen()) return;
    QSqlQuery q(db); q.prepare("UPDATE files SET rating = ? WHERE path = ?");
    q.addBindValue(r); q.addBindValue(QString::fromStdWString(p));
    if (q.exec()) Database::instance().markDirty(vol);
}

void MetadataManager::setColor(const std::wstring& p, const std::wstring& c) {
    std::wstring vol = getVolumeSerialNumber(p);
    QSqlDatabase db = Database::instance().getThreadDatabase(vol);
    if (!db.isOpen()) return;
    QSqlQuery q(db); q.prepare("UPDATE files SET color = ? WHERE path = ?");
    q.addBindValue(QString::fromStdWString(c)); q.addBindValue(QString::fromStdWString(p));
    if (q.exec()) Database::instance().markDirty(vol);
}

void MetadataManager::setPinned(const std::wstring& p, bool pin) {
    std::wstring vol = getVolumeSerialNumber(p);
    QSqlDatabase db = Database::instance().getThreadDatabase(vol);
    if (!db.isOpen()) return;
    QSqlQuery q(db); q.prepare("UPDATE files SET pinned = ? WHERE path = ?");
    q.addBindValue(pin ? 1 : 0); q.addBindValue(QString::fromStdWString(p));
    if (q.exec()) Database::instance().markDirty(vol);
}

void MetadataManager::setTags(const std::wstring& p, const QStringList& t) {
    std::wstring vol = getVolumeSerialNumber(p);
    QSqlDatabase db = Database::instance().getThreadDatabase(vol);
    if (!db.isOpen()) return;
    QSqlQuery q(db); q.prepare("UPDATE files SET tags = ? WHERE path = ?");
    q.addBindValue(QJsonDocument(QJsonArray::fromStringList(t)).toJson(QJsonDocument::Compact));
    q.addBindValue(QString::fromStdWString(p));
    if (q.exec()) Database::instance().markDirty(vol);
}

void MetadataManager::setNote(const std::wstring& p, const std::wstring& n) {
    std::wstring vol = getVolumeSerialNumber(p);
    QSqlDatabase db = Database::instance().getThreadDatabase(vol);
    if (!db.isOpen()) return;
    QSqlQuery q(db); q.prepare("UPDATE files SET note = ? WHERE path = ?");
    q.addBindValue(QString::fromStdWString(n)); q.addBindValue(QString::fromStdWString(p));
    if (q.exec()) Database::instance().markDirty(vol);
}

void MetadataManager::setURL(const std::wstring& p, const std::wstring& u) {
    std::wstring vol = getVolumeSerialNumber(p);
    QSqlDatabase db = Database::instance().getThreadDatabase(vol);
    if (!db.isOpen()) return;
    QSqlQuery q(db); q.prepare("UPDATE files SET url = ? WHERE path = ?");
    q.addBindValue(QString::fromStdWString(u)); q.addBindValue(QString::fromStdWString(p));
    if (q.exec()) Database::instance().markDirty(vol);
}

void MetadataManager::setEncrypted(const std::wstring& p, bool e) { Q_UNUSED(p); Q_UNUSED(e); }
void MetadataManager::setPalettes(const std::wstring& p, const QVector<QPair<QColor, float>>& pal) { Q_UNUSED(p); Q_UNUSED(pal); }
QVector<QColor> MetadataManager::getPalettes(const std::wstring& p) { Q_UNUSED(p); return {}; }
void MetadataManager::renameItem(const std::wstring& o, const std::wstring& n) { ItemRepo::updatePath(getVolumeSerialNumber(o), L"", n, L""); }
void MetadataManager::removeMetadataSync(const std::wstring& p) { ItemRepo::physicalRemove(p); }
void MetadataManager::syncPhysicalMetadata(const std::wstring& p) { Q_UNUSED(p); }
std::string MetadataManager::getFileIdSync(const std::wstring& p) { Q_UNUSED(p); return ""; }
bool MetadataManager::hasPendingSync() const { return false; }
QStringList MetadataManager::getPendingSyncDirs() { return {}; }
void MetadataManager::removeFidsFromLog(const QStringList& f) { Q_UNUSED(f); }
void MetadataManager::addToSyncLog(const std::wstring& d) { Q_UNUSED(d); }
bool MetadataManager::fetchWinApiMetadataDirect(const std::wstring& p, std::string& fid, std::wstring* frn, long long* s, std::wstring* t, long long* ct, long long* mt, long long* at) {
    Q_UNUSED(p); Q_UNUSED(fid); Q_UNUSED(frn); Q_UNUSED(s); Q_UNUSED(t); Q_UNUSED(ct); Q_UNUSED(mt); Q_UNUSED(at); return false;
}
}
