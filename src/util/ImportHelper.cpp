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
        // 目标：先在侧边栏建立物理分类，然后立即刷新 UI
        int totalItems = 0;
        struct ImportNode {
            QString path;
            int parentCatId;
            bool isDirectory;
        };
        QList<ImportNode> allNodes;

        // 用于存储路径与创建出的分类 ID 的映射，确保子项能找到父分类
        QMap<QString, int> pathToCatId;

        std::function<void(const QString&, int, bool)> scanAndCreateTree = [&](const QString& p, int parentId, bool isRoot) {
            QFileInfo info(p);
            bool isDir = info.isDir();
            int currentCatId = parentId;

            if (isDir) {
                // 如果是文件夹，无论如何都尝试创建分类
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
                    scanAndCreateTree(subInfo.absoluteFilePath(), currentCatId, false);
                }
            }
        };

        // 执行扫描与预创建
        for (const QString& rootPath : paths) {
            scanAndCreateTree(rootPath, targetCatId, true);
        }

        // --- 2. 第二阶段：强制刷新 UI 侧边栏 ---
        // 按照用户要求：先在侧边栏创建分类，然后刷新，之后才开始导入
        QMetaObject::invokeMethod(QCoreApplication::instance(), [weakProgress, totalItems]() {
            if (weakProgress) {
                weakProgress->setRange(0, totalItems);
                weakProgress->setValue(0);
                weakProgress->setStatus("已建立分类结构，正在准备导入数据...");
            }
            // 物理落盘并通知 UI 全量刷新（此时分类树已在 DB 中）
            CategoryRepo::saveImmediately();
            MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::FullRebuild);
        }, Qt::BlockingQueuedConnection);

        // --- 3. 第三阶段：执行数据导入 (精细化反馈) ---
        int currentHandled = 0;
        for (const auto& node : allNodes) {
            if (!weakProgress) break;

            QFileInfo info(node.path);

            if (node.isDirectory) {
                // 激活文件夹元数据
                MetadataManager::instance().registerItem(QDir::toNativeSeparators(node.path).toStdWString());
            } else {
                // 激活项目并关联到分类
                std::wstring wp = QDir::toNativeSeparators(node.path).toStdWString();
                MetadataManager::instance().registerItem(wp);

                if (node.parentCatId > 0) {
                    std::string fid = MetadataManager::instance().getFileIdSync(wp);
                    if (!fid.empty()) {
                        CategoryRepo::addItemToCategory(node.parentCatId, fid, wp);
                    }
                }
            }

            // 精细化进度反馈
            currentHandled++;
            QMetaObject::invokeMethod(weakProgress.data(), "updateProgress", Qt::QueuedConnection,
                Q_ARG(int, currentHandled), Q_ARG(int, totalItems), Q_ARG(QString, info.fileName()));
        }

        // --- 4. 第四阶段：完成处理 ---
        QMetaObject::invokeMethod(QCoreApplication::instance(), [weakProgress, weakParent, currentHandled]() {
            CategoryRepo::saveImmediately();
            if (weakProgress) {
                weakProgress->accept();
                weakProgress->deleteLater();
            }
            MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::FullRebuild);
            if (weakParent) {
                ToolTipOverlay::instance()->showText(QCursor::pos(),
                    QString("已成功入库 %1 个项目").arg(currentHandled), 2000, QColor("#2ecc71"));
            }
        });
    });
}

} // namespace ArcMeta
