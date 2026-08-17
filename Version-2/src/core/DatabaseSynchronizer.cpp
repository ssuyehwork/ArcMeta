#include "DatabaseSynchronizer.h"
#include "../meta/CategoryRepo.h"
#include "../meta/DatabaseManager.h"
#include "../meta/MetadataManager.h"
#include "sqlite3.h"
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QtConcurrent>
#include <vector>
#include <string>
#include <functional>
#include <algorithm>

namespace ArcMeta {

struct ScanNode {
    std::wstring path;
    std::wstring name;
    uint64_t frn = 0;
    bool isDir = false;
    std::vector<ScanNode> children;
    std::vector<std::wstring> files;
};

static void scanPhysicalDirectory(const QString& currentPath, ScanNode& node) {
    QDir currentDir(currentPath);
    QFileInfoList list = currentDir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden);

    for (const QFileInfo& fi : list) {
        std::wstring wPath = QDir::toNativeSeparators(fi.absoluteFilePath()).toStdWString();
        if (fi.isDir()) {
            // 🚨 核心物理防线：如果该目录是以 .arc 结尾的资产包容器，绝对禁止作为子分类（node.children）添加！
            if (fi.fileName().endsWith(".arc", Qt::CaseInsensitive)) {
                // 直接扫描 .arc 内部的真实物理文件，将其作为文件塞入 node.files
                QDir arcDir(fi.absoluteFilePath());
                QFileInfoList arcFiles = arcDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
                for (const QFileInfo& afi : arcFiles) {
                    QString fn = afi.fileName();
                    if (fn.endsWith("_thumbnail.png", Qt::CaseInsensitive)) continue;
                    if (fn.compare("metadata.json", Qt::CaseInsensitive) == 0) continue;
                    node.files.push_back(QDir::toNativeSeparators(afi.absoluteFilePath()).toStdWString());
                }
                continue; // 彻底跳过将 .arc 自身创建为分类！
            }

            std::string fid;
            std::wstring frnStr;
            if (MetadataManager::fetchWinApiMetadataDirect(wPath, fid, &frnStr)) {
                try {
                    ScanNode childNode;
                    childNode.path = wPath;
                    childNode.name = fi.fileName().toStdWString();
                    childNode.frn = std::stoull(frnStr, nullptr, 16);
                    childNode.isDir = true;
                    scanPhysicalDirectory(fi.absoluteFilePath(), childNode);
                    node.children.push_back(std::move(childNode));
                } catch (...) {}
            }
        } else {
            node.files.push_back(wPath);
        }
    }
}

void DatabaseSynchronizer::syncPhysicalDirectoryCascade(const std::wstring& rootPath) {
    // 🚨 彻底根除全量物理对账逻辑：对账和盘点扫描程序已被完全裁撤，直接安全退避，100% 杜绝启动高负载
    Q_UNUSED(rootPath);
    qDebug() << "[Sync][CLEANUP] DatabaseSynchronizer::syncPhysicalDirectoryCascade has been completely removed.";
}

} // namespace ArcMeta
