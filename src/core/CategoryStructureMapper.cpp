#include "CategoryStructureMapper.h"
#include "../meta/CategoryRepo.h"
#include "../meta/MetadataManager.h"
#include "../meta/DatabaseManager.h"
#include <QFileInfo>
#include <QDir>
#include <functional>

namespace ArcMeta {

bool CategoryStructureMapper::ensureCategoryPath(const std::wstring& rootPath, int& outCatId) {
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    if (!db) return false;

    SqlTransaction trans(db);
    int rootCatId = 0;
    std::string rootFid;
    std::wstring rootFrnStr;

    if (MetadataManager::fetchWinApiMetadataDirect(rootPath, rootFid, &rootFrnStr)) {
        try {
            uint64_t frn = std::stoull(rootFrnStr, nullptr, 16);
            rootCatId = CategoryRepo::findByFrn(frn);
            if (rootCatId == 0) {
                QFileInfo info(QString::fromStdWString(rootPath));
                std::wstring parentPath = info.absolutePath().toStdWString();
                std::string parentFid;
                std::wstring parentFrnStr;
                int parentCatId = 0;
                if (MetadataManager::fetchWinApiMetadataDirect(parentPath, parentFid, &parentFrnStr)) {
                    uint64_t pFrn = std::stoull(parentFrnStr, nullptr, 16);
                    parentCatId = CategoryRepo::findByFrn(pFrn);
                }

                Category cat;
                if (info.fileName().startsWith("ArcMeta.Library_", Qt::CaseInsensitive)) {
                    cat.parentId = 0;
                } else {
                    cat.parentId = parentCatId;
                }
                cat.name = info.fileName().toStdWString();
                cat.physicalFrn = frn;
                cat.physicalPath = rootPath;
                cat.color = CategoryRepo::getDefaultColor();
                if (CategoryRepo::add(cat)) {
                    rootCatId = cat.id;
                }
            }
        } catch (...) {}
    }

    if (rootCatId > 0) {
        std::function<void(const QString&, int)> syncDir;
        syncDir = [&](const QString& currentPath, int parentCatId) {
            QDir currentDir(currentPath);
            QFileInfoList list = currentDir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden);

            for (const QFileInfo& fi : list) {
                std::wstring wPath = QDir::toNativeSeparators(fi.absoluteFilePath()).toStdWString();
                if (fi.isDir()) {
                    int existingId = CategoryRepo::findCategoryId(parentCatId, fi.fileName().toStdWString());
                    if (existingId == 0) {
                        std::string fid;
                        std::wstring frnStr;
                        if (MetadataManager::fetchWinApiMetadataDirect(wPath, fid, &frnStr)) {
                            try {
                                Category cat;
                                cat.parentId = parentCatId;
                                cat.name = fi.fileName().toStdWString();
                                cat.physicalFrn = std::stoull(frnStr, nullptr, 16);
                                cat.physicalPath = wPath;
                                cat.color = CategoryRepo::getDefaultColor();
                                if (CategoryRepo::add(cat)) {
                                    existingId = cat.id;
                                }
                            } catch (...) {}
                        }
                    }
                    if (existingId > 0) {
                        syncDir(fi.absoluteFilePath(), existingId);
                    }
                } else {
                    MetadataManager::instance().registerItem(wPath, true);
                    if (parentCatId > 0) {
                        std::string fid;
                        if (MetadataManager::fetchWinApiMetadataDirect(wPath, fid)) {
                            CategoryRepo::addItemToCategory(parentCatId, fid, wPath);
                        }
                    }
                }
            }
        };

        syncDir(QString::fromStdWString(rootPath), rootCatId);
    }

    bool ok = trans.commit();
    outCatId = rootCatId;
    return ok;
}

} // namespace ArcMeta
