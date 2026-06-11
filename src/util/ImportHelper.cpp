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
        // 1. 预统计总项数 (用于进度条范围)
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

        // 2. 核心递归逻辑
        auto processItem = [&](const QString& itemPath, int catId) {
            std::wstring wp = QDir::toNativeSeparators(itemPath).toStdWString();

            // 一站式注册：获取 FID/FRN、物理属性同步及视觉预热
            MetadataManager::instance().registerItem(wp);

            // 归类：建立 File ID 与分类的持久化关联
            if (catId > 0) {
                std::string fid = MetadataManager::instance().getFileIdSync(wp);
                if (!fid.empty()) {
                    CategoryRepo::addItemToCategory(catId, fid, wp);
                }
            }
        };

        std::function<void(const QString&, int)> activateAndCategorize = [&](const QString& p, int parentCatId) {
            if (!weakProgress) return;

            QFileInfo info(p);
            int currentCatId = parentCatId;
            bool isDirectory = info.isDir();

            // A. 如果是文件夹，自动创建分类节点
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
            }

            // B. 处理当前项
            if (!isDirectory) {
                processItem(p, currentCatId);
            } else {
                // 激活文件夹元数据，但不建立分类关联
                MetadataManager::instance().registerItem(QDir::toNativeSeparators(p).toStdWString());
            }

            // C. 精细化反馈：处理每个文件都更新 UI
            currentHandled++;
            QString fileName = info.fileName();
            QMetaObject::invokeMethod(weakProgress.data(), "updateProgress", Qt::QueuedConnection,
                Q_ARG(int, currentHandled), Q_ARG(int, totalItems), Q_ARG(QString, fileName));

            // D. 递归子项
            if (isDirectory) {
                QDir dir(p);
                QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
                for (const QFileInfo& subInfo : entries) {
                    activateAndCategorize(subInfo.absoluteFilePath(), currentCatId);
                }
            }
        };

        // 执行递归
        for (const QString& rootPath : paths) {
            activateAndCategorize(rootPath, targetCatId);
        }

        // 3. 完成后的 UI 刷新
        QMetaObject::invokeMethod(QCoreApplication::instance(), [weakProgress, weakParent, currentHandled]() {
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
