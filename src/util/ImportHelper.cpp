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
        // 1. 预统计总项数
        int totalItems = 0;
        std::function<void(const QString&)> countTask = [&](const QString& p) {
            QDir dir(p);
            QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
            totalItems += entries.size();
            for (const QFileInfo& info : entries) {
                if (info.isDir()) countTask(info.absoluteFilePath());
            }
        };
        for (const auto& p : paths) {
            countTask(p);
            totalItems++;
        }

        QMetaObject::invokeMethod(weakProgress.data(), [weakProgress, totalItems]() {
            if (weakProgress) {
                weakProgress->setRange(0, totalItems);
                weakProgress->setValue(0);
            }
        });

        int currentHandled = 0;

        auto processItem = [&](const QString& itemPath, int catId) {
            std::wstring wp = QDir::toNativeSeparators(itemPath).toStdWString();
            MetadataManager::instance().registerItem(wp);
            if (catId > 0) {
                std::string fid = MetadataManager::instance().getFileIdSync(wp);
                if (!fid.empty()) {
                    CategoryRepo::addItemToCategory(catId, fid, wp);
                }
            }
        };

        // 递归逻辑修复：对所有文件夹均尝试创建分类节点，以保留完整层级
        std::function<void(const QString&, int)> activateAndCategorize = [&](const QString& p, int parentCatId) {
            if (!weakProgress) return;

            QFileInfo info(p);
            int currentCatId = parentCatId;
            bool isDirectory = info.isDir();

            if (isDirectory) {
                std::wstring name = info.fileName().toStdWString();
                int existingId = CategoryRepo::findCategoryId(parentCatId, name);
                if (existingId == 0) {
                    Category newCat;
                    newCat.name = name;
                    newCat.parentId = parentCatId;
                    newCat.color = getDefaultCategoryColor();
                    CategoryRepo::add(newCat);
                    currentCatId = newCat.id;
                } else {
                    currentCatId = existingId;
                }

                // 激活文件夹元数据
                MetadataManager::instance().registerItem(QDir::toNativeSeparators(p).toStdWString());
            }

            if (!isDirectory) {
                processItem(p, currentCatId);
            }

            // 精细化反馈
            currentHandled++;
            QString fileName = info.fileName();
            QMetaObject::invokeMethod(weakProgress.data(), "updateProgress", Qt::QueuedConnection,
                Q_ARG(int, currentHandled), Q_ARG(int, totalItems), Q_ARG(QString, fileName));

            if (isDirectory) {
                QDir dir(p);
                QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
                for (const QFileInfo& subInfo : entries) {
                    activateAndCategorize(subInfo.absoluteFilePath(), currentCatId);
                }
            }
        };

        for (const QString& rootPath : paths) {
            activateAndCategorize(rootPath, targetCatId);
        }

        // 3. 完成后的 UI 刷新与持久化
        QMetaObject::invokeMethod(QCoreApplication::instance(), [weakProgress, weakParent, currentHandled]() {
            // 物理加固：导入完成后立即强制物理落盘 (统一处理)
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
