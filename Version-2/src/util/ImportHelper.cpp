#include "ImportHelper.h"
#include "../ui/Logger.h"
#include "../ui/BatchProgressDialog.h"
#include "../ui/ToolTipOverlay.h"
#include "../meta/MetadataManager.h"
#include "../meta/CategoryRepo.h"
#include "../meta/DatabaseManager.h"
#include "ShellHelper.h"
#include <QDir>
#include <QFileInfo>
#include <QtConcurrent>
#include <QMetaObject>
#include <QCoreApplication>
#include "FramelessDialog.h"
#include <QMutex>
#include <QFuture>
#include <functional>

#ifdef Q_OS_WIN
#include <windows.h>
#include <objbase.h>
#endif

namespace ArcMeta {

void ImportHelper::importPaths(const QStringList& paths, 
                               const QString& targetPhysicalPath, 
                               QWidget* parent,
                               std::function<void()> onComplete) {
    if (paths.isEmpty()) return;

    BatchProgressDialog* progress = new BatchProgressDialog("正在迁移项目至资源库...", parent);
    progress->show();

    struct ImportContext {
        std::atomic<bool> isCancelled{false};
        QFuture<void> future;
    };
    auto context = std::make_shared<ImportContext>();
    QPointer<BatchProgressDialog> weakProgress(progress);

    QObject::connect(progress, &BatchProgressDialog::rejected, [weakProgress, context, parent]() {
        if (!weakProgress) return;
        if (!FramelessMessageBox::question(parent, "中断迁移", "迁移尚未完成。确定要停止当前迁移任务吗？")) {
            weakProgress->show();
            return;
        }
        context->isCancelled = true;
        if (context->future.isRunning()) context->future.waitForFinished();
        weakProgress->deleteLater();
    });

    // 捕获并保存 onComplete 刷新闭包
    context->future = QtConcurrent::run([paths, targetPhysicalPath, weakProgress, context, onComplete]() {
        // 物理移动后立即主动调用 syncAfterMove 进行元数据与统计对账
        
        int total = paths.size();
        int handled = 0;
        int successCount = 0;

        for (const QString& src : paths) {
            if (context->isCancelled) break;
            
            handled++;
            if (weakProgress) {
                QMetaObject::invokeMethod(weakProgress.data(), "updateProgress", Qt::QueuedConnection, 
                                         Q_ARG(int, handled), Q_ARG(int, total), Q_ARG(QString, QFileInfo(src).fileName()));
            }

            // 🚨 核心逻辑：如果目标位置是 ArcMeta.Library_[盘符] 资源库，为其创建 .arc 资产包！
            QString destPath;
            if (targetPhysicalPath.contains("ArcMeta.Library_", Qt::CaseInsensitive)) {
                // 生成 13 位唯一 ID，建 ID.arc/ 容器
                std::string assetId = MetadataManager::generateDeterministicFolderId(src.toStdWString());
                if (assetId.find("PATHURL:") == 0) assetId = assetId.substr(8);
                if (assetId.length() > 13) assetId = assetId.substr(0, 13);
                
                QString arcContainer = QDir(targetPhysicalPath).filePath(QString::fromStdString(assetId) + ".arc");
                if (!QDir().mkpath(arcContainer)) {
                    continue;
                }
                destPath = QDir(arcContainer).filePath(QFileInfo(src).fileName());
            } else {
                destPath = QDir(targetPhysicalPath).absoluteFilePath(QFileInfo(src).fileName());
            }

            // 执行物理移动
            bool moved = ShellHelper::copyOrMoveItems({src}, QFileInfo(destPath).absolutePath(), true);
            if (moved) {
                MetadataManager::instance().syncAfterMove(
                    QDir::toNativeSeparators(src).toStdWString(),
                    QDir::toNativeSeparators(destPath).toStdWString());
                successCount++;
            } else {
            }
        }

        QMetaObject::invokeMethod(QCoreApplication::instance(), [weakProgress, context, successCount, onComplete]() {
            if (context->isCancelled) return;
            if (weakProgress) {
                weakProgress->accept();
                weakProgress->deleteLater();
            }
            ToolTipOverlay::instance()->showText(QCursor::pos(), 
                QString("已成功迁移 %1 个条目").arg(successCount), 2000, QColor("#2ecc71"));

            // 物理搬运结束后，安全派发无感刷新指令 (对应用户原话："是无感刷新吗？")
            if (onComplete) {
                onComplete();
            }
        });
    });
}

} // namespace ArcMeta
