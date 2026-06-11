#include "ImportHelper.h"
#include <QtConcurrent>
#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>
#include "../meta/MetadataManager.h"
#include "../meta/CategoryRepo.h"
#include "../ui/ToolTipOverlay.h"

namespace ArcMeta {

std::wstring ImportHelper::getDefaultCategoryColor() {
    return L"#555555";
}

void ImportHelper::importPaths(const QStringList& paths, int targetCatId, BatchProgressDialog* progress, QWidget* parentView) {
    if (paths.isEmpty() || !progress) return;

    QPointer<BatchProgressDialog> weakProgress(progress);
    QPointer<QWidget> weakParent(parentView);

    (void)QtConcurrent::run([paths, targetCatId, weakProgress, weakParent]() {
        // --- 1. 第一阶段：预统计与分类树预创建 ---
        int totalItems = 0;
        struct ImportNode {
            QString path;
            int parentCatId;
            bool isDirectory;
        };
        QList<ImportNode> allNodes;

        QMap<QString, int> pathToCatId;

        std::function<void(const QString&, int)> scanAndCreateTree = [&](const QString& p, int parentId) {
            if (weakProgress && weakProgress->isAborted()) return;

            QFileInfo info(p);
            bool isDir = info.isDir();
            int currentCatId = parentId;

            if (isDir) {
                std::wstring name = info.fileName().toStdWString();
                int existingId = CategoryRepo::findCategoryId(parentId, name);
                if (existingId == 0) {
                    Category newCat;
                    newCat.name = name;
                    newCat.parentId = parentId;
                    newCat.color = getDefaultCategoryColor();
                    CategoryRepo::add(newCat);
                    currentCatId = newCat.id;
                } else {
                    currentCatId = existingId;
                }
                pathToCatId[p] = currentCatId;
            }

            allNodes.append({p, currentCatId, isDir});
            totalItems++;

            if (isDir) {
                QDir dir(p);
                QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
                for (const QFileInfo& subInfo : entries) {
                    scanAndCreateTree(subInfo.absoluteFilePath(), currentCatId);
                    if (weakProgress && weakProgress->isAborted()) return;
                }
            }
        };

        for (const QString& rootPath : paths) {
            scanAndCreateTree(rootPath, targetCatId);
            if (weakProgress && weakProgress->isAborted()) break;
        }

        // 检查是否已终止
        if (weakProgress && weakProgress->isAborted()) {
            QMetaObject::invokeMethod(QCoreApplication::instance(), [weakProgress]() {
                if (weakProgress) {
                    weakProgress->reject();
                    weakProgress->deleteLater();
                }
            });
            return;
        }

        // --- 2. 第二阶段：强制刷新 UI 侧边栏 ---
        QMetaObject::invokeMethod(QCoreApplication::instance(), [weakProgress, totalItems]() {
            if (weakProgress) {
                weakProgress->setRange(0, totalItems);
                weakProgress->setValue(0);
                weakProgress->setStatus("已建立分类结构，正在准备导入数据...");
            }
            CategoryRepo::saveImmediately();
            MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::FullRebuild);
        }, Qt::BlockingQueuedConnection);

        // --- 3. 第三阶段：执行数据导入 (精细化反馈) ---
        int currentHandled = 0;
        for (const auto& node : allNodes) {
            if (!weakProgress || weakProgress->isAborted()) break;

            QFileInfo info(node.path);

            if (node.isDirectory) {
                MetadataManager::instance().registerItem(QDir::toNativeSeparators(node.path).toStdWString());
            } else {
                std::wstring wp = QDir::toNativeSeparators(node.path).toStdWString();
                MetadataManager::instance().registerItem(wp);

                if (node.parentCatId > 0) {
                    std::string fid = MetadataManager::instance().getFileIdSync(wp);
                    if (!fid.empty()) {
                        CategoryRepo::addItemToCategory(node.parentCatId, fid, wp);
                    }
                }
            }

            currentHandled++;
            QMetaObject::invokeMethod(weakProgress.data(), "updateProgress", Qt::QueuedConnection,
                Q_ARG(int, currentHandled), Q_ARG(int, totalItems), Q_ARG(QString, info.fileName()));
        }

        // --- 4. 第四阶段：完成处理 ---
        QMetaObject::invokeMethod(QCoreApplication::instance(), [weakProgress, weakParent, currentHandled]() {
            CategoryRepo::saveImmediately();
            bool wasAborted = weakProgress && weakProgress->isAborted();

            if (weakProgress) {
                if (wasAborted) weakProgress->reject();
                else weakProgress->accept();
                weakProgress->deleteLater();
            }

            MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::FullRebuild);

            if (weakParent) {
                QString msg = wasAborted ? QString("入库操作已中止（已处理 %1 项）").arg(currentHandled)
                                         : QString("已成功入库 %1 个项目").arg(currentHandled);
                QColor color = wasAborted ? QColor("#e74c3c") : QColor("#2ecc71");
                ToolTipOverlay::instance()->showText(QCursor::pos(), msg, 2000, color);
            }
        });
    });
}

} // namespace ArcMeta
