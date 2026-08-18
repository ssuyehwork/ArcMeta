#pragma once

#include <QStringList>
#include <QImage>
#include <vector>

namespace QuarkMeta {

struct DuplicateItemInfo {
    QString folderId;
    QString path;
    QString filename;
    int width = 0;
    int height = 0;
    qint64 size = 0;
    QString tagHint;
    QImage thumbnail;
};

struct DuplicateConflictGroup {
    DuplicateItemInfo existingItem;
    DuplicateItemInfo newItem;
};

class DuplicateDetectorService {
public:
    // 后台比对新导入项与数据库已有项，返回重复冲突组列表
    static std::vector<DuplicateConflictGroup> detectDuplicates(const QStringList& newImportedPaths);
};

} // namespace QuarkMeta
