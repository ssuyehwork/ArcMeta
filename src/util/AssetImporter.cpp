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
 
void AssetImporter::importAssets(const QStringList& paths, 
                                 int targetCatId, 
                                 QWidget* parent, 
                                 std::function<void()> onComplete) { 
    importAssets(paths, targetCatId, parent, [onComplete](const QStringList& /*imported*/) {
        if (onComplete) onComplete();
    });
}

void AssetImporter::importAssets(const QStringList& paths,
                                 int targetCatId,
                                 QWidget* /*parent*/,
                                 std::function<void(const QStringList&)> onComplete) {
    if (paths.isEmpty()) {
        if (onComplete) onComplete({});
        return;
    }
 
    struct ImportContext { 
        std::atomic<bool> isCancelled{false}; 
        QFuture<void> future; 
    }; 
    auto context = std::make_shared<ImportContext>(); 
 
    context->future = QtConcurrent::run([paths, targetCatId, context, onComplete]() {
#ifdef Q_OS_WIN 
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); 
#endif 
 
        int successCount = 0; 
        QStringList outImportedPaths;
 
        for (const QString& src : paths) { 
            if (context->isCancelled) break; 
 
            // 获取目标资源库物理根目录 
            QString managedRoot; 
            if (targetCatId > 0) { 
                Category targetCat = CategoryRepo::getById(targetCatId); 
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
                qWarning() << "[AssetImporter] 无法建立资源库根目录:" << managedRoot; 
                continue; 
            } 
 
            QFileInfo srcInfo(src); 
            bool ok = false; 
            if (srcInfo.isFile()) { 
                ok = importSingleFile(src, targetCatId, managedRoot, outImportedPaths);
            } else if (srcInfo.isDir()) { 
                ok = importDirectoryRecursive(src, targetCatId, managedRoot, outImportedPaths);
            } 
            if (ok) successCount++; 
        } 
 
#ifdef Q_OS_WIN 
        if (SUCCEEDED(hr)) CoUninitialize(); 
#endif 
 
        QMetaObject::invokeMethod(QCoreApplication::instance(), [context, successCount, onComplete, outImportedPaths]() {
            if (context->isCancelled) return; 
            ToolTipOverlay::instance()->showText(QCursor::pos(), 
                QString("已成功导入 %1 个受控资产单元").arg(successCount), 2000, QColor("#2ecc71")); 
 
            if (onComplete) onComplete(outImportedPaths);
        }); 
    }); 
}
 
bool AssetImporter::importSingleFile(const QString& srcPath, 
                                     int targetCatId, 
                                     const QString& managedRoot,
                                     QStringList& outImportedPaths) {
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
    if (srcDrive.compare(destDrive, Qt::CaseInsensitive) == 0) { 
        copied = QFile::rename(srcPath, destPath); 
    } else { 
        copied = QFile::copy(srcPath, destPath); 
    } 
 
    if (!copied) { 
        QDir(containerDir).removeRecursively(); 
        return false; 
    } 
 
    // 4. 生成容器内配套的预渲染缩略图 
    (void)CapsuleMediaExtractor::getCapsuleThumbnail(destPath, 512);
 
    // 🚨 重构核心：废除所有手写原始 SQL！统一转发给 MetadataManager 单一权威管线登记入库 
    std::wstring wDestPath = QDir::toNativeSeparators(destPath).toStdWString(); 
    if (MetadataManager::instance().registerAsset(fileId.toStdString(), wDestPath, targetCatId)) {
        outImportedPaths.append(destPath);
        return true;
    }
    return false;
} 
 
bool AssetImporter::importDirectoryRecursive(const QString& srcDir, 
                                             int parentCatId, 
                                             const QString& managedRoot,
                                             QStringList& outImportedPaths) {
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
            importSingleFile(entry.absoluteFilePath(), cat.id, managedRoot, outImportedPaths);
        } else if (entry.isDir()) { 
            importDirectoryRecursive(entry.absoluteFilePath(), cat.id, managedRoot, outImportedPaths);
        } 
    } 
 
    return true; 
} 
 
} // namespace ArcMeta 
