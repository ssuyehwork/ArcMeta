#include "AssetImporter.h"
#include "ShellHelper.h"
#include "../ui/Logger.h"
#include "../ui/BatchProgressDialog.h"
#include "../ui/ToolTipOverlay.h"
#include "../meta/MetadataManager.h"
#include "../meta/CategoryRepo.h"
#include "../meta/DatabaseManager.h"
#include "../ui/WindowsShellThumbnailProvider.h"
#include <QDir>
#include <QFileInfo>
#include <QtConcurrent>
#include <QMetaObject>
#include <QCoreApplication>
#include "FramelessDialog.h"
#include <QDateTime>

namespace ArcMeta {

void AssetImporter::importAssets(const QStringList& paths,
                                 int targetCatId,
                                 QWidget* parent,
                                 std::function<void()> onComplete) {
    if (paths.isEmpty()) return;

    BatchProgressDialog* progress = new BatchProgressDialog("正在导入资产包...", parent);
    progress->show();

    struct ImportContext {
        std::atomic<bool> isCancelled{false};
        QFuture<void> future;
    };
    auto context = std::make_shared<ImportContext>();
    QPointer<BatchProgressDialog> weakProgress(progress);

    QObject::connect(progress, &BatchProgressDialog::rejected, [weakProgress, context, parent]() {
        if (!weakProgress) return;
        if (!FramelessMessageBox::question(parent, "中断导入", "导入尚未完成。确定要停止当前导入吗？")) {
            weakProgress->show();
            return;
        }
        context->isCancelled = true;
        if (context->future.isRunning()) context->future.waitForFinished();
        weakProgress->deleteLater();
    });

    context->future = QtConcurrent::run([paths, targetCatId, weakProgress, context, onComplete]() {
        int total = paths.size();
        int handled = 0;

        for (const QString& src : paths) {
            if (context->isCancelled) break;

            handled++;
            if (weakProgress) {
                QMetaObject::invokeMethod(weakProgress.data(), "updateProgress", Qt::QueuedConnection,
                                         Q_ARG(int, handled), Q_ARG(int, total), Q_ARG(QString, QFileInfo(src).fileName()));
            }

            // 1. 获取目标盘符托管库路径 [盘符]:/ArcMeta.Library_[盘符]/
            QString drive = QFileInfo(src).absolutePath().left(3);
            if (drive.isEmpty()) drive = "D:/";
            QString managedRoot = drive + "ArcMeta.Library_" + drive.at(0).toUpper();
            QDir().mkpath(managedRoot);

            QFileInfo srcInfo(src);
            if (srcInfo.isFile()) {
                importSingleFile(src, targetCatId, managedRoot);
            } else if (srcInfo.isDir()) {
                importDirectoryRecursive(src, targetCatId, managedRoot);
            }
        }

        QMetaObject::invokeMethod(QCoreApplication::instance(), [weakProgress, context, handled, onComplete]() {
            if (context->isCancelled) return;
            if (weakProgress) {
                weakProgress->accept();
                weakProgress->deleteLater();
            }
            ToolTipOverlay::instance()->showText(QCursor::pos(),
                QString("已完成 %1 个项目的物理分流导入").arg(handled), 2000, QColor("#2ecc71"));

            if (onComplete) {
                onComplete();
            }
        });
    });
}

bool AssetImporter::importSingleFile(const QString& srcPath,
                                     int targetCatId,
                                     const QString& managedRoot) {
    QFileInfo srcInfo(srcPath);
    if (!srcInfo.exists() || !srcInfo.isFile()) return false;

    // 1. 产生 13 位唯一 Base36 ID
    QString fileId = ShellHelper::generateBase36Id();

    // 2. 建立 [ID].arc 文件夹容器
    QString containerDir = managedRoot + "/" + fileId + ".arc";
    if (!QDir().mkpath(containerDir)) return false;

    // 3. 将文件复制/移动进容器 (支持跨盘物理搬运)
    QString fileName = srcInfo.fileName();
    QString destPath = containerDir + "/" + fileName;

    bool moved = false;
    if (QFile::rename(srcPath, destPath)) {
        moved = true;
    } else {
        if (QFile::copy(srcPath, destPath)) {
            QFile::remove(srcPath);
            moved = true;
        }
    }

    if (!moved) {
        QDir(containerDir).removeRecursively();
        return false;
    }

    // 4. 提取 256x256 高清预渲染缩略图 [baseName]_thumbnail.png
    QImage thumb = WindowsShellThumbnailProvider::getShellThumbnail(destPath, 256);
    if (!thumb.isNull()) {
        QString baseName = QFileInfo(fileName).completeBaseName();
        thumb.save(containerDir + "/" + baseName + "_thumbnail.png", "PNG");
    }

    // 5. 写入数据库，标记其 added_at 为当前时间戳
    std::wstring wDestPath = QDir::toNativeSeparators(destPath).toStdWString();
    MetadataManager::instance().ensureActivated(wDestPath);

    // 更新 added_at 为当前毫秒时间戳
    long long nowMsecs = QDateTime::currentMSecsSinceEpoch();
    MetadataManager::instance().setAddedAt(wDestPath, nowMsecs, false);

    // 6. 分类归纳
    // 如果 targetCatId > 0，绑定它
    if (targetCatId > 0) {
        CategoryRepo::addItemToCategory(targetCatId, fileId.toStdString(), wDestPath);
    }

    return true;
}

bool AssetImporter::importDirectoryRecursive(const QString& srcDir,
                                             int parentCatId,
                                             const QString& managedRoot) {
    QFileInfo dirInfo(srcDir);
    if (!dirInfo.exists() || !dirInfo.isDir()) return false;

    // 🚨 核心防线：.arc 容器已经是最终打包好的资产单元，绝不能被当作普通子目录再次创建分类/递归打包
    if (dirInfo.fileName().endsWith(".arc", Qt::CaseInsensitive)) {
        return false; // 视为已导入资产，跳过，不重复打包、不创建同名分类
    }

    // 1. 在 categories 树中递归新建逻辑分类
    Category cat;
    cat.parentId = parentCatId;
    cat.name = dirInfo.fileName().toStdWString();
    cat.color = CategoryRepo::getDefaultColor();
    if (!CategoryRepo::add(cat)) return false;

    // 2. 递归导入文件夹里的所有实体文件和子目录
    QDir dir(srcDir);
    QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name);
    for (const QFileInfo& entry : entries) {
        if (entry.isFile()) {
            importSingleFile(entry.absoluteFilePath(), cat.id, managedRoot);
        } else if (entry.isDir()) {
            importDirectoryRecursive(entry.absoluteFilePath(), cat.id, managedRoot);
        }
    }

    return true;
}

} // namespace ArcMeta
