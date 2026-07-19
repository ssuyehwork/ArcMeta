#include "PhysicalDiskSearchExtractor.h"
#include <QDirIterator>
#include "../meta/MetadataManager.h"
#include "Logger.h"

namespace ArcMeta {

int PhysicalDiskSearchExtractor::performDiskSearch(
    const QString& parentPath,
    const QString& keyword,
    const std::atomic<bool>& abortFlag,
    const std::atomic<int>& currentSearchId,
    int searchId,
    std::unordered_set<std::wstring>& seenPaths,
    std::function<void(const QStringList&)> batchCallback
) {
    QDirIterator it(parentPath, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    QStringList batch;
    int scanCount = 0;
    int foundCount = 0;

    while (it.hasNext()) {
        if (abortFlag || currentSearchId != searchId) break;
        scanCount++;
        if (scanCount % 2000 == 0) {
             ArcMeta::Logger::log(QString("[Core] I/O 扫描进度: 已检查 %1 个项目 [%2]").arg(scanCount).arg(searchId));
        }

        QString fullPath = it.next();
        QString fileName = it.fileName();

        if (fileName.contains(keyword, Qt::CaseInsensitive)) {
            std::wstring wPath = MetadataManager::normalizePath(fullPath.toStdWString());
            if (seenPaths.find(wPath) == seenPaths.end()) {
                batch << fullPath;
                seenPaths.insert(wPath);
                foundCount++;

                if (batch.size() >= 50) {
                    batchCallback(batch);
                    batch.clear();
                }
            }
        }
    }

    if (!batch.isEmpty() && !abortFlag && currentSearchId == searchId) {
        batchCallback(batch);
    }

    return foundCount;
}

} // namespace ArcMeta
