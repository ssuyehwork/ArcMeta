#include "ScanController.h"
#include "../mft/MftReader.h"
#include <QtConcurrent>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QDir>
#include <windows.h>

namespace ArcMeta {

static QString getConfigPath() {
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(path);
    return path + "/arcmeta_scan_config.json";
}

void ScanConfig::load() {
    QFile file(getConfigPath());
    if (file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        QJsonObject obj = doc.object();

        auto loadSet = [&](const QString& key, QSet<QString>& set) {
            set.clear();
            QJsonArray arr = obj[key].toArray();
            for (const auto& v : arr) set.insert(v.toString());
        };

        loadSet("activeDrives", activeDrives);
        loadSet("defaultDrives", defaultDrives);
        loadSet("ignoredDrives", ignoredDrives);

        queryHistory.clear();
        QJsonArray qArr = obj["queryHistory"].toArray();
        for (const auto& v : qArr) queryHistory.append(v.toString());

        extHistory.clear();
        QJsonArray eArr = obj["extHistory"].toArray();
        for (const auto& v : eArr) extHistory.append(v.toString());

        if (obj.contains("viewMode")) viewMode = obj["viewMode"].toInt();
        if (obj.contains("iconSize")) iconSize = obj["iconSize"].toInt();
        if (obj.contains("sortColumn")) sortColumn = obj["sortColumn"].toInt();
        if (obj.contains("sortOrder")) sortOrder = obj["sortOrder"].toInt();
    }
}

void ScanConfig::save() {
    QFile file(getConfigPath());
    if (file.open(QIODevice::WriteOnly)) {
        QJsonObject obj;
        auto saveSet = [&](const QString& key, const QSet<QString>& set) {
            QJsonArray arr;
            for (const auto& v : set) arr.append(v);
            obj[key] = arr;
        };

        saveSet("activeDrives", activeDrives);
        saveSet("defaultDrives", defaultDrives);
        saveSet("ignoredDrives", ignoredDrives);

        QJsonArray qArr; for (const auto& v : queryHistory) qArr.append(v);
        obj["queryHistory"] = qArr;
        QJsonArray eArr; for (const auto& v : extHistory) eArr.append(v);
        obj["extHistory"] = eArr;

        obj["viewMode"] = viewMode;
        obj["iconSize"] = iconSize;
        obj["sortColumn"] = sortColumn;
        obj["sortOrder"] = sortOrder;

        file.write(QJsonDocument(obj).toJson());
    }
}

ScanController& ScanController::instance() {
    static ScanController inst;
    return inst;
}

ScanController::ScanController(QObject* parent) : QObject(parent) {
}

ScanController::~ScanController() {}

void ScanController::loadConfig() {
    m_config.load();
}

void ScanController::saveConfig() {
    m_config.save();
}

void ScanController::requestDriveProbe(bool force) {
    if (!force && !m_cachedDriveInfos.isEmpty()) {
        emit driveProbeFinished(m_cachedDriveInfos);
        return;
    }

    QtConcurrent::run([this]() {
        QVector<DriveInfo> drives;
        DWORD driveMask = GetLogicalDrives();
        for (int i = 0; i < 26; ++i) {
            if (driveMask & (1 << i)) {
                QString letter = QString(QChar('A' + i)) + QLatin1String(":");
                WCHAR volName[MAX_PATH + 1] = {0};
                WCHAR fsName[MAX_PATH + 1] = {0};
                QString driveRoot = letter + QLatin1String("\\");
                BOOL ok = GetVolumeInformationW(reinterpret_cast<const wchar_t*>(driveRoot.utf16()),
                                              volName, MAX_PATH + 1, NULL, NULL, NULL,
                                              fsName, MAX_PATH + 1);
                DriveInfo info;
                info.letter = letter;
                info.hasMedia = ok;
                if (ok) {
                    info.label = QString::fromWCharArray(volName);
                    info.isNtfs = QString::fromWCharArray(fsName).contains("NTFS", Qt::CaseInsensitive);
                } else {
                    info.isNtfs = false;
                }
                drives.append(info);
            }
        }

        QMetaObject::invokeMethod(this, [this, drives]() {
            m_cachedDriveInfos = drives;
            emit driveProbeFinished(drives);
        });
    });
}

void ScanController::requestScan(const QStringList& drives) {
    if (drives.isEmpty()) {
        emit scanFinished(true);
        return;
    }

    emit statusUpdated("正在扫描...", true);
    emit scanStarted();

    QtConcurrent::run([this, drives]() {
        MftReader::instance().buildIndex(drives);
        QMetaObject::invokeMethod(this, [this]() {
            emit statusUpdated("就绪", false);
            emit scanFinished(true);
        });
    });
}

void ScanController::updateActiveDrives(const QStringList& drives) {
    MftReader::instance().updateActiveDrives(drives);
}

} // namespace ArcMeta
