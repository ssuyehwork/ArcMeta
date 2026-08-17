#include "AssetImporter.h" 
#include "ShellHelper.h" 
#include "../ui/Logger.h" 
#include "../ui/BatchProgressDialog.h" 
#include "../ui/ToolTipOverlay.h" 
#include "../meta/MetadataManager.h" 
#include "../meta/CategoryRepo.h" 
#include "../meta/DatabaseManager.h" 
#include "../ui/MediaColorExtractor.h" 
#include "../meta/CapsuleMediaExtractor.h"
#include "../meta/MediaExtractorPipeline.h"
#include <QDir> 
#include <QFileInfo> 
#include <QtConcurrent> 
#include <QMetaObject> 
#include <QCoreApplication> 
#include "FramelessDialog.h" 
#include <QDateTime> 
 
#ifdef Q_OS_WIN 
#include <windows.h> 
#include <objbase.h> 
#endif 
 
namespace ArcMeta { 

void AssetImporter::importAssets(const ImportContext& ctx) {
    if (ctx.sourcePaths.isEmpty()) {
        if (ctx.completionCallback) {
            ctx.completionCallback(false, 0, QStringList());
        }
        return;
    }

    (void)QtConcurrent::run([ctx]() {
#ifdef Q_OS_WIN
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
#endif

        int total = ctx.sourcePaths.size();
        int handled = 0;
        int successCount = 0;
        QStringList newlyImportedPaths;

        for (const QString& src : ctx.sourcePaths) {
            handled++;
            if (ctx.progressCallback) {
                ctx.progressCallback(handled, total);
            }

            // 获取目标资源库物理根目录
            QString managedRoot = ctx.targetPhysicalPath;
            if (managedRoot.isEmpty() && ctx.targetCategoryId > 0) {
                Category targetCat = CategoryRepo::getById(ctx.targetCategoryId);
                Category cur = targetCat;
                while (cur.parentId != 0) {
                    Category parent = CategoryRepo::getById(cur.parentId);
                    if (parent.id == 0) break;
                    cur = parent;
                }
                if (!cur.physicalPath.empty()) {
                    managedRoot = QString::fromStdWString(cur.physicalPath);
                }
            }
            if (managedRoot.isEmpty()) {
                QString drive = QFileInfo(src).absolutePath().left(3);
                if (drive.isEmpty()) {
                    drive = QCoreApplication::applicationDirPath().left(3);
                }
                if (drive.isEmpty()) {
                    drive = "C:/";
                }
                managedRoot = drive + "ArcMeta.Library_" + drive.at(0).toUpper();
            }

            if (!QDir().mkpath(managedRoot)) {
                continue;
            }

            QFileInfo srcInfo(src);
            bool ok = false;
            if (srcInfo.isFile()) {
                ok = importSingleFile(src, ctx.targetCategoryId, managedRoot, &newlyImportedPaths, ctx.allowMove);
            } else if (srcInfo.isDir()) {
                ok = importDirectoryRecursive(src, ctx.targetCategoryId, managedRoot, &newlyImportedPaths, ctx.allowMove);
            }
            if (ok) successCount++;
        }

        // 将文件路径批量投递至 MediaExtractorPipeline 异步队列中提取缩略图，避免同步阻塞
        if (!newlyImportedPaths.isEmpty()) {
            std::vector<std::wstring> stdPaths;
            for (const QString& p : newlyImportedPaths) {
                stdPaths.push_back(p.toStdWString());
            }
            MediaExtractorPipeline::instance().enqueueBatch(stdPaths);
        }

#ifdef Q_OS_WIN
        if (SUCCEEDED(hr)) CoUninitialize();
#endif

        QMetaObject::invokeMethod(QCoreApplication::instance(), [ctx, successCount, newlyImportedPaths]() {
            if (ctx.completionCallback) {
                ctx.completionCallback(true, successCount, newlyImportedPaths);
            }
        });
    });
}
 
void AssetImporter::importAssets(const QStringList& paths, 
                                 int targetCatId, 
                                 QWidget* parent, 
                                 std::function<void()> onComplete,
                                 bool allowMove) { 
    importAssets(paths, targetCatId, parent, [onComplete](const QStringList& newlyImportedPaths) {
        Q_UNUSED(newlyImportedPaths);
        if (onComplete) onComplete();
    }, allowMove);
}

void AssetImporter::importAssets(const QStringList& paths, 
                                 int targetCatId, 
                                 QWidget* parent, 
                                 std::function<void(const QStringList& newlyImportedPaths)> onComplete,
                                 bool allowMove) { 
    if (paths.isEmpty()) return; 
 
    BatchProgressDialog* progress = new BatchProgressDialog("正在导入资产包...", parent); 
    progress->show(); 
 
    struct ProgressContext { 
        std::atomic<bool> isCancelled{false}; 
    }; 
    auto pCtx = std::make_shared<ProgressContext>(); 
    QPointer<BatchProgressDialog> weakProgress(progress); 
 
    QObject::connect(progress, &BatchProgressDialog::rejected, [weakProgress, pCtx, parent]() { 
        if (!weakProgress) return; 
        if (!FramelessMessageBox::question(parent, "中断导入", "导入尚未完成。确定要停止当前导入吗？")) { 
            weakProgress->show(); 
            return; 
        } 
        pCtx->isCancelled = true; 
        weakProgress->deleteLater(); 
    }); 
 
    ImportContext ctx;
    ctx.sourcePaths = paths;
    ctx.targetCategoryId = targetCatId;
    ctx.allowMove = allowMove;

    ctx.progressCallback = [weakProgress, pCtx, paths](int handled, int total) {
        if (weakProgress && !pCtx->isCancelled) {
            QString currentFileName = "";
            if (handled - 1 >= 0 && handled - 1 < paths.size()) {
                currentFileName = QFileInfo(paths[handled - 1]).fileName();
            }
            QMetaObject::invokeMethod(weakProgress.data(), "updateProgress", Qt::QueuedConnection, 
                                     Q_ARG(int, handled), Q_ARG(int, total), Q_ARG(QString, currentFileName)); 
        }
    };

    ctx.completionCallback = [weakProgress, pCtx, onComplete](bool success, int successCount, const QStringList& newlyImportedPaths) {
        Q_UNUSED(success);
        Q_UNUSED(successCount);
        if (pCtx->isCancelled) return;
        if (weakProgress) {
            weakProgress->accept();
            weakProgress->deleteLater();
        }
        ToolTipOverlay::instance()->showText(QCursor::pos(), 
            QString("已成功导入 %1 个受控资产单元").arg(successCount), 2000, QColor("#2ecc71")); 
 
        if (onComplete) onComplete(newlyImportedPaths);
    };

    importAssets(ctx);
} 
 
bool AssetImporter::importSingleFile(const QString& srcPath, 
                                     int targetCatId, 
                                     const QString& managedRoot,
                                     QStringList* newlyImportedPaths,
                                     bool allowMove) { 
    QFileInfo srcInfo(srcPath); 
    if (!srcInfo.exists() || !srcInfo.isFile()) return false; 
 
    // 1. 生成 13 位唯一 Base36 胶囊 ID 
    QString fileId = ShellHelper::generateBase36Id(); 
 
    // 2. 建立物理容器 [ID].arc 
    QString containerDir = managedRoot + "/" + fileId + ".arc"; 
    if (!QDir().mkpath(containerDir)) return false; 
 
    // 3. 将真实资产放入物理容器中 
    QString fileName = srcInfo.fileName(); 
    QString destPath = containerDir + "/" + fileName; 
 
    QString srcDrive = QFileInfo(srcPath).absolutePath().left(3); 
    QString destDrive = QFileInfo(destPath).absolutePath().left(3); 
 
    bool copied = false; 
    if (allowMove && srcDrive.compare(destDrive, Qt::CaseInsensitive) == 0) { 
        copied = QFile::rename(srcPath, destPath); 
    } else { 
        copied = QFile::copy(srcPath, destPath); 
    } 
 
    if (!copied) { 
        QDir(containerDir).removeRecursively(); 
        return false; 
    } 
 
    // 4. 彻底切断同步缩略图物理提取，改为在 importAssets 外部统一后台异步队列提取
    if (newlyImportedPaths) {
        newlyImportedPaths->append(destPath);
    }
 
    // 🚀 【乐观生成并发登记】：直接提交 Base36 ID，若数据库触发极小概率碰撞，内部将自动自愈重试
    std::wstring wDestPath = QDir::toNativeSeparators(destPath).toStdWString(); 
    bool registered = MetadataManager::instance().registerAsset(fileId.toStdString(), wDestPath, targetCatId); 
    if (!registered) {
        // 若最终注册失败，清理物理胶囊容器
        QDir(containerDir).removeRecursively();
        return false;
    }
    return true;
} 
 
bool AssetImporter::importDirectoryRecursive(const QString& srcDir, 
                                             int parentCatId, 
                                             const QString& managedRoot,
                                             QStringList* newlyImportedPaths,
                                             bool allowMove) { 
    QFileInfo dirInfo(srcDir); 
    if (!dirInfo.exists() || !dirInfo.isDir()) return false; 
 
    if (dirInfo.fileName().endsWith(".arc", Qt::CaseInsensitive)) { 
        return false; // 跳过物理容器本身 
    } 
 
    Category cat; 
    // 🚨 安全下限防护：若 parentCatId < 0（如 -2），自动修正为 0（顶级分类），防止生成幽灵隐形分类
    cat.parentId = (parentCatId < 0) ? 0 : parentCatId; 
    cat.name = dirInfo.fileName().toStdWString(); 
    cat.color = CategoryRepo::getDefaultColor(); 
    if (!CategoryRepo::add(cat)) return false; 
 
    QDir dir(srcDir); 
    QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name); 
    for (const QFileInfo& entry : entries) { 
        if (entry.isFile()) { 
            importSingleFile(entry.absoluteFilePath(), cat.id, managedRoot, newlyImportedPaths, allowMove); 
        } else if (entry.isDir()) { 
            importDirectoryRecursive(entry.absoluteFilePath(), cat.id, managedRoot, newlyImportedPaths, allowMove); 
        } 
    } 
 
    return true; 
} 
 
} // namespace ArcMeta 
