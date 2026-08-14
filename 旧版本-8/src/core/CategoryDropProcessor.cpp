#include "CategoryDropProcessor.h" 
#include "../meta/CategoryRepo.h" 
#include "../meta/MetadataManager.h" 
#include "../util/AssetImporter.h"
#include <QtConcurrent> 
#include <QDebug> 
#include <QCoreApplication>
#include <QWidget>
#include <QDateTime>
#include <cmath>
 
namespace ArcMeta { 
 
CategoryDropProcessor::CategoryDropProcessor(QObject* parent) : QObject(parent) {} 
 
void CategoryDropProcessor::cancel() {
    m_isCancelled.store(true);
}

void CategoryDropProcessor::processDroppedPathsAsync(const QStringList& paths, int targetCategoryId) { 
    m_isCancelled.store(false);

    auto future = QtConcurrent::run([this, paths, targetCategoryId]() { 
        bool success = true; 
        int processedCount = 0; 
         
        Category targetCat = CategoryRepo::getById(targetCategoryId); 
        bool isTargetManagedLibraryRoot = (targetCat.parentId == 0 && targetCat.kind == CategoryKind::SystemLibrary); 

        QStringList importPaths; 
        std::vector<std::pair<std::string, std::wstring>> virtualAssocItems; 

        qint64 startTime = QDateTime::currentMSecsSinceEpoch();
        qint64 lastEmitTime = 0;
        int total = paths.size();

        for (int i = 0; i < total; ++i) { 
            if (m_isCancelled.load()) {
                success = false;
                break;
            }

            const QString& srcPath = paths[i];
            std::wstring wPath = MetadataManager::normalizePath(srcPath.toStdWString()); 
             
            bool isManaged = MetadataManager::isInsideManagedLibrary(wPath); 
 
            if (isManaged) { 
                std::string assetId = MetadataManager::instance().getFolderIdSync(wPath); 
                if (assetId.empty()) {
                    // 保持进度计数
                } else {
                    // 递归找到分类属于哪一个托管库根节点，以确定目标托管库物理根目录
                    Category cur = targetCat;
                    while (cur.id > 0 && cur.parentId != 0) {
                        cur = CategoryRepo::getById(cur.parentId);
                    }
                    QString targetLibraryPath = QString::fromStdWString(cur.physicalPath);

                    bool isCrossLibrary = false;
                    if (!targetLibraryPath.isEmpty()) {
                        isCrossLibrary = !srcPath.startsWith(targetLibraryPath, Qt::CaseInsensitive);
                    }

                    if (isCrossLibrary) {
                        // 跨托管库拖拽/粘贴：复制整套数据（元数据+胶囊包）并分配全新唯一ID，保证ID唯一性
                        std::string newAssetId = MetadataManager::instance().migrateCapsuleToLibrary(assetId, targetLibraryPath);
                        if (!newAssetId.empty()) {
                            std::wstring newPath = MetadataManager::instance().getPathByFolderId(newAssetId);
                            // 同时也应当把复制好的新资产关联到目标分类或目标库根分类中！
                            CategoryRepo::addItemToCategory(targetCategoryId, newAssetId, newPath);
                            processedCount++;
                        }
                    } else {
                        // 同一托管库内部的分类指派
                        if (isTargetManagedLibraryRoot) {
                            // 已经在当前托管库，不需要任何处理
                            processedCount++;
                        } else {
                            virtualAssocItems.push_back({assetId, wPath}); 
                        }
                    }
                }
            } else { 
                // 库外文件拖入，交由 AssetImporter 后续处理 
                importPaths << srcPath; 
            } 

            int processed = i + 1;
            qint64 now = QDateTime::currentMSecsSinceEpoch();
            qint64 elapsedMs = now - startTime;
            double rate = elapsedMs > 0 ? (double)processed / (elapsedMs / 1000.0) : 0.0;
            int remainingSeconds = -1;
            if (rate > 0.0) {
                remainingSeconds = static_cast<int>(std::round((total - processed) / rate));
            }
            if (now - lastEmitTime >= 200 || processed == total) {
                emit progressUpdated(processed, total, remainingSeconds);
                lastEmitTime = now;
            }
        } 
 
        // 数据库大事务批量落盘 
        if (!m_isCancelled.load() && !virtualAssocItems.empty()) { 
            bool batchOk = CategoryRepo::addItemToCategoryBatch(targetCategoryId, virtualAssocItems); 
            if (batchOk) { 
                processedCount += static_cast<int>(virtualAssocItems.size()); 
            } else { 
                success = false; 
            } 
        } 
 
        if (!m_isCancelled.load() && !importPaths.isEmpty()) { 
            QMetaObject::invokeMethod(this, [this, importPaths, targetCategoryId, success, processedCount]() { 
                QWidget* parentWidget = qobject_cast<QWidget*>(parent());
                AssetImporter::importAssets(importPaths, targetCategoryId, parentWidget, [this, success, processedCount](const QStringList& newlyImported) {
                    emit processingFinished(success, processedCount + newlyImported.size(), newlyImported);
                });
            }, Qt::BlockingQueuedConnection); 
        } else {
            emit processingFinished(success, processedCount, {}); 
        }
    }); 
    Q_UNUSED(future);
} 
 
} // namespace ArcMeta 
