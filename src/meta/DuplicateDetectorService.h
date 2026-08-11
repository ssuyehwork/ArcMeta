#pragma once

#include <QStringList>
#include <QImage>
#include <vector>

namespace ArcMeta {

struct DuplicateItemInfo {
    QString folderId;
    QString path;
    QString filename;
    int width = 0;
    int height = 0;
    qint64 size = 0;
    QString tagHint;
    QImage thumbnail;
    std::string sha256; // 新增
};

struct DuplicateConflictGroup {
    DuplicateItemInfo existingItem;
    DuplicateItemInfo newItem;
};

class DuplicateDetectorService {
public:
    // 后台比对新导入项与数据库已有项，返回重复冲突组列表
    static std::vector<DuplicateConflictGroup> detectDuplicates(const QStringList& newImportedPaths);

    static std::string calculateFastHash(const QString& filePath);
    static std::string calculateFullSha256(const QString& filePath);
};

} // namespace ArcMeta
