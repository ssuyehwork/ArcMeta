#include "CategoryDropProcessor.h"
#include "../meta/CategoryRepo.h"
#include "../meta/MetadataManager.h"
#include "../util/AssetImporter.h"
#include <QtConcurrent>
#include <QDebug>
#include <QCoreApplication>
#include <QEventLoop>
#include <QWidget>
#include <QDateTime>
#include <cmath>

namespace ArcMeta {

CategoryDropProcessor::CategoryDropProcessor(QObject* parent) : QObject(parent) {}

void CategoryDropProcessor::processDroppedPathsAsync(const QStringList& paths, int targetCategoryId) {
    // 异步后台运行：封装 QtConcurrent::run
    auto future = QtConcurrent::run([this, paths, targetCategoryId]() {
        bool success = true;
        int processedCount = 0;

        Category targetCat = CategoryRepo::getById(targetCategoryId);
        bool isTargetManagedLibraryRoot = (targetCat.parentId == 0 &&
            QString::fromStdWString(targetCat.name).startsWith("ArcMeta.Library_"));

        QStringList importPaths;
        std::vector<std::pair<std::string, std::wstring>> virtualAssocItems;

        qint64 startTime = QDateTime::currentMSecsSinceEpoch();
        qint64 lastEmitTime = 0;
        int total = paths.size();

        for (int i = 0; i < total; ++i) {
            const QString& srcPath = paths[i];
            std::wstring wPath = MetadataManager::normalizePath(srcPath.toStdWString());

            // 判断拖拽卡片是否是库内资产
            bool isManaged = MetadataManager::isInsideManagedLibrary(wPath);

            if (isManaged) {
                std::string assetId = MetadataManager::instance().getFolderIdSync(wPath);
                if (assetId.empty()) {
                    // Keep counting progress even if asset ID is empty
                } else if (isTargetManagedLibraryRoot) {
                    // 分支 A：跨盘迁移
                    QString targetLibraryPath = QString::fromStdWString(targetCat.physicalPath);
                    MetadataManager::instance().migrateCapsuleToLibrary(assetId, targetLibraryPath);
                    processedCount++;
                } else {
                    // 分支 B：存入收集容器，后续统一大事务落盘
                    virtualAssocItems.push_back({assetId, wPath});
                }
            } else {
                // 库外文件拖入，交由 AssetImporter 后续处理
                importPaths << srcPath;
            }

            // 每 200ms 推送当前进度
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

        // 大事务批量落盘
        if (!virtualAssocItems.empty()) {
            bool batchOk = CategoryRepo::addItemToCategoryBatch(targetCategoryId, virtualAssocItems);
            if (batchOk) {
                processedCount += static_cast<int>(virtualAssocItems.size());
            } else {
                success = false;
            }
        }

        // 如果存在需要打包导入的库外资产，目前 AssetImporter 包含 QProgressDialog 等 UI 操作，
        // 故必须通过 invokeMethod 回调主线程调用 AssetImporter 进行导入。
        if (!importPaths.isEmpty()) {
            QMetaObject::invokeMethod(this, [this, importPaths, targetCategoryId, success, processedCount]() {
                QWidget* parentWidget = qobject_cast<QWidget*>(parent());
                AssetImporter::importAssets(importPaths, targetCategoryId, parentWidget, [this, success, processedCount, importPaths]() {
                    emit processingFinished(success, processedCount + importPaths.size());
                });
            }, Qt::BlockingQueuedConnection);
        } else {
            emit processingFinished(success, processedCount);
        }
    });
    Q_UNUSED(future);
}

} // namespace ArcMeta
