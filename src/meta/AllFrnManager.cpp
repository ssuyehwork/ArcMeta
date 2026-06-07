#include "AllFrnManager.h"
#include <QFile>
#include <QDataStream>
#include <shared_mutex>
#include <QDebug>
#include <QTimer>
#include <QCoreApplication>
#include <QApplication>
#include <QMetaObject>

namespace ArcMeta {

static std::shared_mutex s_frnMutex;
static QMap<QString, QString> s_frnCache;
static bool s_loaded = false;
static bool s_dirty = false;
static QTimer* s_saveTimer = nullptr;

// 2026-06-xx FRN 注册表二进制头
struct AllFrnHeader {
    char magic[4] = {'A', 'F', 'R', 'N'};
    uint32_t version = 3;
    uint32_t count = 0;
};

static void ensureLoaded() {
    if (s_loaded) return;
    
    // 2026-06-xx 物理加固：加载逻辑必须受锁保护，防止多线程重复加载
    QFile file("All_FRN_metadata.scch");
    if (file.exists() && file.open(QIODevice::ReadOnly)) {
        QDataStream ds(&file);
        ds.setVersion(QDataStream::Qt_6_0);
        AllFrnHeader header;
        if (file.read((char*)&header, sizeof(header)) == sizeof(header)) {
            if (memcmp(header.magic, "AFRN", 4) == 0) {
                ds >> s_frnCache;
            }
        }
        file.close();
    }
    s_loaded = true;
}

static void saveToDisk() {
    std::unique_lock<std::shared_mutex> lock(s_frnMutex);
    if (!s_dirty) return;

    qDebug() << "[AllFrnManager] 正在将 FRN 映射持久化到磁盘，项数:" << s_frnCache.size();
    QFile file("All_FRN_metadata.scch.tmp");
    if (file.open(QIODevice::WriteOnly)) {
        QDataStream ds(&file);
        ds.setVersion(QDataStream::Qt_6_0);
        AllFrnHeader header;
        header.count = (uint32_t)s_frnCache.size();
        file.write((char*)&header, sizeof(header));
        ds << s_frnCache;
        file.close();
        QFile::remove("All_FRN_metadata.scch");
        if (QFile::rename("All_FRN_metadata.scch.tmp", "All_FRN_metadata.scch")) {
            s_dirty = false;
            qDebug() << "[AllFrnManager] FRN 持久化成功";
        } else {
            qCritical() << "[AllFrnManager] FRN 持久化重命名失败!";
        }
    } else {
        qCritical() << "[AllFrnManager] 无法打开临时文件进行 FRN 持久化写入!";
    }
}

void AllFrnManager::registerFrn(const std::wstring& frn, const std::wstring& path) {
    if (frn.empty() || path.empty()) return;

    QString qFrn = QString::fromStdWString(frn);
    QString qPath = QString::fromStdWString(path);

    {
        std::unique_lock<std::shared_mutex> lock(s_frnMutex);
        ensureLoaded();
        if (s_frnCache.contains(qFrn) && s_frnCache[qFrn] == qPath) return;
        s_frnCache[qFrn] = qPath;
        s_dirty = true;
    }

    // 2026-06-xx 性能优化：引入异步防抖保存，彻底解决高频注册时的 IO 假死
    if (!s_saveTimer) {
        // 在主线程初始化计时器
        QMetaObject::invokeMethod(QCoreApplication::instance(), []() {
            if (!s_saveTimer) {
                s_saveTimer = new QTimer(QCoreApplication::instance());
                s_saveTimer->setSingleShot(true);
                s_saveTimer->setInterval(3000);
                QObject::connect(s_saveTimer, &QTimer::timeout, []() { saveToDisk(); });
                QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, []() { saveToDisk(); });
            }
            s_saveTimer->start();
        }, Qt::QueuedConnection);
    } else {
        QMetaObject::invokeMethod(s_saveTimer, "start", Qt::QueuedConnection);
    }
}

QMap<QString, QString> AllFrnManager::getAllFrns() {
    {
        std::shared_lock<std::shared_mutex> lock(s_frnMutex);
        if (s_loaded) return s_frnCache;
    }

    std::unique_lock<std::shared_mutex> lock(s_frnMutex);
    ensureLoaded();
    return s_frnCache;
}

void AllFrnManager::saveImmediately() {
    saveToDisk();
}

} // namespace ArcMeta
