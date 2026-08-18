#include "CategoryDropProcessor.h" 
#include "../meta/CategoryRepo.h" 
#include "../meta/MetadataManager.h" 
#include "../util/AssetImporter.h"
#include "../meta/DuplicateDetectorService.h"
#include "../meta/CapsuleMediaExtractor.h"
#include "../ui/DuplicateConflictDialog.h"
#include <QtConcurrent> 
#include <QDebug> 
#include <QCoreApplication>
#include <QWidget>
#include <QDateTime>
#include <QApplication>
#include <cmath>
 
namespace QuarkMeta { 
 
CategoryDropProcessor::CategoryDropProcessor(QObject* parent) : QObject(parent) {} 
 
void CategoryDropProcessor::cancel() {
    m_isCancelled.store(true);
}

void CategoryDropProcessor::executeImportPipeline(const QStringList& paths, int targetCategoryId) {
    emit progressStarted();

    ImportContext ctx;
    ctx.sourcePaths = paths;
    ctx.targetCategoryId = targetCategoryId;
    ctx.progressCallback = [this](int current, int total) {
        emit progressUpdated(current, total, -1);
    };
    ctx.completionCallback = [this, paths, targetCategoryId](bool success, int count, const QStringList& newlyImportedPaths) {
        triggerDuplicateCheck(newlyImportedPaths, targetCategoryId);
        emit processingFinished(success, count, newlyImportedPaths);
    };

    AssetImporter::importAssets(ctx);
}

void CategoryDropProcessor::triggerDuplicateCheck(const QStringList& paths, int targetCategoryId) {
    (void)QtConcurrent::run([this, paths, targetCategoryId]() {
        auto conflicts = DuplicateDetectorService::detectDuplicates(paths);
        if (!conflicts.empty()) {
            QMetaObject::invokeMethod(this, [this, conflicts, targetCategoryId]() {
                QWidget* parentWidget = qobject_cast<QWidget*>(parent());
                if (!parentWidget) {
                    parentWidget = QApplication::activeWindow();
                }
                if (parentWidget) {
                    // 弹出 DuplicateConflictDialog
                    bool batchApplied = false;
                    DuplicateResolveAction batchAction = DuplicateResolveAction::UseExisting;

                    int totalCount = static_cast<int>(conflicts.size());
                    for (auto group : conflicts) {
                        DuplicateResolveAction chosenAction;
                        if (batchApplied) {
                            chosenAction = batchAction;
                        } else {
                            group.existingItem.thumbnail = CapsuleMediaExtractor::getCapsuleThumbnailReadOnly(group.existingItem.path);
                            // Ensure newItem has thumbnail loaded safely
                            // group.newItem.thumbnail = CapsuleMediaExtractor::getCapsuleThumbnailReadOnly(group.newItem.path);

                            DuplicateConflictDialog dlg(group, totalCount, parentWidget);
                            if (dlg.exec() == QDialog::Accepted) {
                                chosenAction = dlg.selectedAction();
                                if (dlg.applyToAll()) {
                                    batchApplied = true;
                                    batchAction = chosenAction;
                                }
                            } else {
                                continue;
                            }
                        }

                        if (chosenAction == DuplicateResolveAction::UseExisting) {
                            std::wstring newWPath = group.newItem.path.toStdWString();
                            std::string newFid = MetadataManager::instance().getFolderIdSync(newWPath);

                            // 1. 优先提取新文件所在的新建分类 ID（例如 "pin" 的 ID）
                            std::vector<int> boundCatIds = CategoryRepo::getItemCategoryIds(newFid, newWPath);
                            if (boundCatIds.empty() && targetCategoryId > 0) {
                                boundCatIds.push_back(targetCategoryId);
                            }

                            // 2. 将已存在文件关联至新分类
                            for (int cid : boundCatIds) {
                                if (cid > 0) {
                                    CategoryRepo::addItemToCategory(cid, group.existingItem.folderId.toStdString(), group.existingItem.path.toStdWString());
                                }
                            }

                            // 3. 删除重复的新文件
                            QFile::remove(group.newItem.path);
                            MetadataManager::instance().removeMetadataSync(newWPath);
                        }
                    }
                }
            });
        }
    });
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
                    Category cur = targetCat;
                    while (cur.id > 0 && cur.parentId != 0) {
                        cur = CategoryRepo::getById(cur.parentId);
                    }
                    QString targetLibraryPath = QString::fromStdWString(cur.physicalPath);

                    bool isCrossLibrary = false;
                    if (!targetLibraryPath.isEmpty()) {
                        // 预对齐：对目标库路径进行完备的物理标准化转换
                        std::wstring normTargetW = MetadataManager::normalizePath(targetLibraryPath.toStdWString());
                        QString normTarget = QString::fromStdWString(normTargetW);
                        
                        // 预对齐：使用已经过 normalizePath 标准化的 wPath 变量转换作为判定源
                        QString normSrc = QString::fromStdWString(wPath);
                        
                        isCrossLibrary = !normSrc.startsWith(normTarget, Qt::CaseInsensitive);
                    }

                    if (isCrossLibrary) {
                        std::string newAssetId = MetadataManager::instance().migrateCapsuleToLibrary(assetId, targetLibraryPath);
                        if (!newAssetId.empty()) {
                            std::wstring newPath = MetadataManager::instance().getPathByFolderId(newAssetId);
                            CategoryRepo::addItemToCategory(targetCategoryId, newAssetId, newPath);
                            processedCount++;
                        }
                    } else {
                        if (isTargetManagedLibraryRoot) {
                            processedCount++;
                        } else {
                            virtualAssocItems.push_back({assetId, wPath}); 
                        }
                    }
                }
            } else { 
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
                ImportContext ctx;
                ctx.sourcePaths = importPaths;
                ctx.targetCategoryId = targetCategoryId;
                ctx.progressCallback = [this, processedCount](int current, int total) {
                    emit progressUpdated(processedCount + current, processedCount + total, -1);
                };
                ctx.completionCallback = [this, success, processedCount](bool ok, int successCount, const QStringList& newlyImported) {
                    Q_UNUSED(ok);
                    emit processingFinished(success, processedCount + successCount, newlyImported);
                };
                AssetImporter::importAssets(ctx);
            }, Qt::BlockingQueuedConnection); 
        } else {
            emit processingFinished(success, processedCount, {}); 
        }
    }); 
    Q_UNUSED(future);
} 
 
} // namespace QuarkMeta 
