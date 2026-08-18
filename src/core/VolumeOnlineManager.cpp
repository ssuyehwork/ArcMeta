#include "VolumeOnlineManager.h"
#include <QDebug>

namespace QuarkMeta {

VolumeOnlineManager& VolumeOnlineManager::instance() {
    static VolumeOnlineManager inst;
    return inst;
}

VolumeOnlineManager::VolumeOnlineManager(QObject* parent)
    : QObject(parent) {
    // 初始收集在线盘符
    auto drives = QDir::drives();
    for (const QFileInfo& drive : drives) {
        QString path = drive.absolutePath();
        if (!path.isEmpty()) {
            QString letter = extractDriveLetter(path);
            if (!letter.isEmpty()) {
                m_onlineDrives.insert(letter.toUpper());
            }
        }
    }

    // 启动定时器轮询盘符状态 (2000ms)，感知热拔插
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &VolumeOnlineManager::checkVolumeState);
    m_timer->start(2000);
}

QSet<QString> VolumeOnlineManager::getOnlineDrives() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_onlineDrives;
}

QString VolumeOnlineManager::extractDriveLetter(const QString& str) {
    if (str.isEmpty()) return QString();

    // 针对 QuarkMeta.library_g 或 library_g 形态
    if (str.contains("library_", Qt::CaseInsensitive)) {
        int idx = str.indexOf("library_", 0, Qt::CaseInsensitive);
        QString sub = str.mid(idx + 8);
        if (!sub.isEmpty()) {
            return sub.left(1).toUpper();
        }
    }

    // 针对包含冒号的路径，如 "G:/..." 或 "G:"
    int colonIdx = str.indexOf(':');
    if (colonIdx > 0) {
        return str.mid(colonIdx - 1, 1).toUpper();
    }

    // 针对单字符，如 "G" 或 "g"
    if (str.length() == 1 && str.at(0).isLetter()) {
        return str.toUpper();
    }

    return QString();
}

bool VolumeOnlineManager::isLibraryOnline(const QString& libraryNameOrPath) const {
    QString letter = extractDriveLetter(libraryNameOrPath);
    if (letter.isEmpty()) return true; // 无法提取盘符的默认判定在线
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_onlineDrives.contains(letter.toUpper());
}

void VolumeOnlineManager::checkVolumeState() {
    auto drives = QDir::drives();
    QSet<QString> currentDrives;
    for (const QFileInfo& drive : drives) {
        QString path = drive.absolutePath();
        if (!path.isEmpty()) {
            QString letter = extractDriveLetter(path);
            if (!letter.isEmpty()) {
                currentDrives.insert(letter.toUpper());
            }
        }
    }

    QSet<QString> oldDrives;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        oldDrives = m_onlineDrives;
        m_onlineDrives = currentDrives;
    }

    // 拔盘广播
    for (const QString& letter : oldDrives) {
        if (!currentDrives.contains(letter)) {
            emit volumeStateChanged(letter, false);
        }
    }

    // 插盘广播
    for (const QString& letter : currentDrives) {
        if (!oldDrives.contains(letter)) {
            emit volumeStateChanged(letter, true);
        }
    }
}

} // namespace QuarkMeta
