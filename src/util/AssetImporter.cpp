#include "AssetImporter.h"
#include "ShellHelper.h"
#include "../ui/Logger.h"
#include "../ui/BatchProgressDialog.h"
#include "../ui/ToolTipOverlay.h"
#include "../meta/MetadataManager.h"
#include "../meta/CategoryRepo.h"
#include "../meta/DatabaseManager.h"
#include "../ui/WindowsShellThumbnailProvider.h"
#include "../ui/MediaColorExtractor.h"
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
#ifdef Q_OS_WIN
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
#endif

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

            // 1. 获取目标盘符资源库路径 [盘符]:/ArcMeta.Library_[盘符]/
            // 优先由 targetCatId 反查它所属的顶层库路径；否则退化为用源文件盘符兜底
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
                if (drive.isEmpty()) drive = "D:/";
                managedRoot = drive + "ArcMeta.Library_" + drive.at(0).toUpper();
            }
            
            if (!QDir().mkpath(managedRoot)) {
                qWarning() << "[AssetImporter] 无法建立资源库根目录:" << managedRoot << " 导入源:" << src;
                continue;
            }

            QFileInfo srcInfo(src);
            bool ok = false;
            if (srcInfo.isFile()) {
                ok = importSingleFile(src, targetCatId, managedRoot);
                if (!ok) {
                    qWarning() << "[AssetImporter] importSingleFile 导入失败！源文件:" << src;
                }
            } else if (srcInfo.isDir()) {
                ok = importDirectoryRecursive(src, targetCatId, managedRoot);
                if (!ok) {
                    qWarning() << "[AssetImporter] importDirectoryRecursive 导入失败！源文件夹:" << src;
                }
            }
            if (ok) {
                successCount++;
            }
        }

#ifdef Q_OS_WIN
        if (SUCCEEDED(hr)) {
            CoUninitialize();
        }
#endif

        QMetaObject::invokeMethod(QCoreApplication::instance(), [weakProgress, context, successCount, onComplete]() {
            if (context->isCancelled) return;
            if (weakProgress) {
                weakProgress->accept();
                weakProgress->deleteLater();
            }
            ToolTipOverlay::instance()->showText(QCursor::pos(),
                QString("已成功导入 %1 个受控资产单元").arg(successCount), 2000, QColor("#2ecc71"));

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

    // 🚨 导入剪切流程自适应整构：根据源与目标是否在同盘，实施高效且合理的逻辑分流
    QString srcDrive = QFileInfo(srcPath).absolutePath().left(3);
    QString destDrive = QFileInfo(destPath).absolutePath().left(3);

    bool copied = false;
    if (srcDrive.compare(destDrive, Qt::CaseInsensitive) == 0) {
        // 同盘：直接 rename 指针原子重定向，耗时仅 1ms，源文件原地自然消失
        copied = QFile::rename(srcPath, destPath);
    } else {
        // 跨盘：rename 在跨物理卷时必然失败，fallback 退化为物理数据复制搬运
        copied = QFile::copy(srcPath, destPath);
    }

    if (!copied) {
        QDir(containerDir).removeRecursively();
        return false;
    }

    // 4. 提取 256x256 高清预渲染缩略图 [baseName]_thumbnail.png
    // 🚨 极致自包含整构：选用直接字节解码提取器，彻底解除对 Windows Shell 命名空间异步索引的时序依赖
    QImage thumb = MediaColorExtractor::getImageForAnalysis(destPath, 256);
    if (!thumb.isNull()) {
        QString baseName = QFileInfo(fileName).completeBaseName();
        QString thumbPath = containerDir + "/" + baseName + "_thumbnail.png";
        bool saveOk = thumb.save(thumbPath, "PNG");
        qDebug() << "[AssetImporter] 缩略图生成成功，保存" << (saveOk ? "成功" : "失败") << "：" << thumbPath;
    } else {
        qWarning() << "[AssetImporter] 缩略图生成失败，getImageForAnalysis 返回空图：" << destPath;
    }

    // 5. 写入数据库：将整个 .arc 资产包文件夹作为唯一的受控资产单位进行激活和登记！ 
    std::wstring wContainerPath = QDir::toNativeSeparators(containerDir).toStdWString(); 
    sqlite3* db = DatabaseManager::instance().getDbForPath(wContainerPath); 
    if (!db) return false; 
 
    std::string actualFolderId = fileId.toStdString(); // folder_id 为 13 位 Base36 包 ID 
 
    // 🚨 优雅大事务重构：资产表(metadata)与分类关联表(category_items) 100% 存放在同一个物理 SQLite 分库中，同温同事务落盘 
    SqlTransaction trans(db); 
 
    long long nowMsecs = QDateTime::currentMSecsSinceEpoch(); 
    // a. 写入元数据资产表 (绑定 folder_id) 
    sqlite3_stmt* stmtMeta = nullptr; 
    const char* sqlMeta = "INSERT OR REPLACE INTO metadata (folder_id, path, is_folder, added_at) VALUES (?, ?, ?, ?)"; 
    if (sqlite3_prepare_v2(db, sqlMeta, -1, &stmtMeta, nullptr) == SQLITE_OK) { 
        sqlite3_bind_text(stmtMeta, 1, actualFolderId.c_str(), -1, SQLITE_TRANSIENT); 
        sqlite3_bind_text16(stmtMeta, 2, wContainerPath.c_str(), -1, SQLITE_TRANSIENT); 
        sqlite3_bind_int(stmtMeta, 3, 1); // 文件夹资产 
        sqlite3_bind_int64(stmtMeta, 4, nowMsecs); 
        sqlite3_step(stmtMeta); 
        sqlite3_finalize(stmtMeta); 
    } 
 
    // b. 写入分类关联表 
    // 修改方案：ArcMeta.Library_[盘符] 是物理仓库入口，不是一个可以被 category_items 绑定的 
    // 逻辑分类。只有用户真正手动选择了具体分类（targetCatId > 0）时，才写入 category_items； 
    // 否则完全不写这张表，让该资产在"未分类"这个统计口径下正确地被识别为"没有任何分类关联" 
    if (targetCatId > 0) { 
        sqlite3_stmt* stmtItems = nullptr; 
        const char* sqlItems = "INSERT OR REPLACE INTO category_items (category_id, folder_id, path_hint, added_at) VALUES (?, ?, ?, ?)"; 
        if (sqlite3_prepare_v2(db, sqlItems, -1, &stmtItems, nullptr) == SQLITE_OK) { 
            sqlite3_bind_int(stmtItems, 1, targetCatId); 
            sqlite3_bind_text(stmtItems, 2, actualFolderId.c_str(), -1, SQLITE_TRANSIENT); 
            sqlite3_bind_text16(stmtItems, 3, wContainerPath.c_str(), -1, SQLITE_TRANSIENT); 
            sqlite3_bind_double(stmtItems, 4, static_cast<double>(nowMsecs)); 
            sqlite3_step(stmtItems); 
            sqlite3_finalize(stmtItems); 
        } 
    } 
 
    if (!trans.commit()) { 
        qWarning() << "[AssetImporter] 100% 同盘单连接事务提交失败！路径:" << destPath; 
        return false; 
    } 
 
    // c. 激活内存缓存 
    MetadataManager::instance().registerItem(wContainerPath, true); 
 
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
