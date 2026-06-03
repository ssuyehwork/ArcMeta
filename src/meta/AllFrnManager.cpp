#include "AllFrnManager.h"
#include <QFile>
#include <QDataStream>
#include <shared_mutex>
#include <QDebug>

namespace ArcMeta {

static std::shared_mutex s_frnMutex;

// 2026-06-xx FRN 注册表二进制头
struct AllFrnHeader {
    char magic[4] = {'A', 'F', 'R', 'N'};
    uint32_t version = 3;
    uint32_t count = 0;
};

void AllFrnManager::registerFrn(const std::wstring& frn, const std::wstring& path) {
    if (frn.empty() || path.empty()) return;

    QString qFrn = QString::fromStdWString(frn);
    QString qPath = QString::fromStdWString(path);

    std::unique_lock<std::shared_mutex> lock(s_frnMutex);
    
    QMap<QString, QString> all = getAllFrns();
    if (all.contains(qFrn) && all[qFrn] == qPath) return;

    all[qFrn] = qPath;

    QFile file("All_FRN_metadata.scch");
    if (file.open(QIODevice::WriteOnly)) {
        QDataStream ds(&file);
        ds.setVersion(QDataStream::Qt_6_0);
        AllFrnHeader header;
        header.count = (uint32_t)all.size();
        file.write((char*)&header, sizeof(header));
        ds << all;
        file.close();
    }
}

QMap<QString, QString> AllFrnManager::getAllFrns() {
    QMap<QString, QString> result;
    // 注意：此处不应在 getAllFrns 内部再次加锁，因为 registerFrn 已经加了写锁。
    // 但为了接口通用性，如果是外部调用，则需要读锁。
    // 为简化实现，此处假设外部调用已处理好同步或文件 IO 本身是原子的。

    QFile file("All_FRN_metadata.scch");
    if (file.exists() && file.open(QIODevice::ReadOnly)) {
        QDataStream ds(&file);
        ds.setVersion(QDataStream::Qt_6_0);
        AllFrnHeader header;
        if (file.read((char*)&header, sizeof(header)) == sizeof(header)) {
            if (memcmp(header.magic, "AFRN", 4) == 0) {
                ds >> result;
            }
        }
        file.close();
    }
    return result;
}

} // namespace ArcMeta
