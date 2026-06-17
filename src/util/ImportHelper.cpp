#include "ImportHelper.h"
#include "../ui/Logger.h"
#include "../ui/BatchProgressDialog.h"
#include "../ui/ToolTipOverlay.h"
#include "../meta/MetadataManager.h"
#include "../meta/CategoryRepo.h"
#include "../meta/DatabaseManager.h"
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

void ImportHelper::importPaths(const QStringList& paths, int targetCategoryId, QWidget* parent) {
    if (paths.isEmpty()) return;

    BatchProgressDialog* progress = new BatchProgressDialog("正在处理项目导入...", parent);
    progress->show();

    // 2026-07-xx 建立导入任务的上下文
    struct ImportContext {
        std::atomic<bool> isCancelled{false};
        QFuture<void> future;
    };
    auto context = std::make_shared<ImportContext>();
    QPointer<BatchProgressDialog> weakProgress(progress);

    // 处理用户关闭进度框的操作 (中断保护)
    QObject::connect(progress, &BatchProgressDialog::rejected, [weakProgress, context, parent]() {
        if (!weakProgress) return;

        // 2026-07-xx 按照用户要求：弹出确认停止
        QString msg = "导入尚未完成。确定要停止当前导入任务吗？已处理的数据将保留。";
        // 使用 FramelessMessageBox::question 替代，映射按钮逻辑
        if (!FramelessMessageBox::question(parent, "中断导入", msg)) {
            weakProgress->show(); // 恢复显示
            return;
        }

        context->isCancelled = true;
        
        // 2026-07-xx 物理加固：等待后台线程安全停止，杜绝竞态导致的数据库损坏
        if (context->future.isRunning()) {
            context->future.waitForFinished();
        }

        // 2026-07-xx 按照用户要求：中断后必须强制触发刷新，使已处理的数据在 UI 上可见
        // 采用 semantic 通知，MetadataManager::notifyUI(FullRebuild) 会自动处理相关逻辑
        MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::FullRebuild);

        ToolTipOverlay::instance()->showText(QCursor::pos(), "导入已中断，进度已保留", 2000, QColor("#FF8C00"));
        weakProgress->deleteLater();
    });

    context->future = QtConcurrent::run([paths, targetCategoryId, weakProgress, context]() {
        #ifdef Q_OS_WIN
        CoInitializeEx(NULL, COINIT_APARTMENTTHREADED); // 赋予后台线程 Shell 调用能力
        #endif

        // A. 预统计阶段
        int totalItems = 0;
        std::function<void(const QString&)> countTask = [&](const QString& p) {
            if (context->isCancelled) return;
            QDir dir(p);
            QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
            totalItems += entries.size();
            for (const QFileInfo& info : entries) {
                if (info.isDir()) countTask(info.absoluteFilePath());
            }
        };
        for(const auto& p : paths) { countTask(p); totalItems++; }

        if (weakProgress) {
            QMetaObject::invokeMethod(weakProgress.data(), [weakProgress, totalItems]() {
                if (weakProgress) {
                    weakProgress->setRange(0, totalItems);
                    weakProgress->setValue(0);
                }
            });
        }

        int currentHandled = 0;
        int batchCounter = 0;

        // 2026-07-xx 按照用户要求 (1.19)：在大循环外开启显式事务，将寻道风暴降至最低
        sqlite3* db = DatabaseManager::instance().getGlobalDb();
        SqlTransaction* currentTrans = new SqlTransaction(db);

        // B. 递归导入逻辑
        std::function<void(const QString&, int)> processPath = [&](const QString& p, int parentId) {
            if (context->isCancelled) return;

            QFileInfo info(p);
            bool isDir = info.isDir();
            std::wstring wp = QDir::toNativeSeparators(p).toStdWString();
            QString fileName = info.fileName();

            qDebug() << "[ImportHelper] Processing path:" << p << "isDir:" << isDir << "parentId:" << parentId;

            // 1. 详细进度反馈
            currentHandled++;
            if (weakProgress) {
                QMetaObject::invokeMethod(weakProgress.data(), "updateProgress", Qt::QueuedConnection, 
                                         Q_ARG(int, currentHandled), Q_ARG(int, totalItems), Q_ARG(QString, fileName));
            }

            int currentCatId = parentId;

            // 2. 文件夹转分类镜像
            if (isDir) {
                std::wstring name = fileName.toStdWString();
                int existingId = CategoryRepo::findCategoryId(parentId, name);
                qDebug() << "[ImportHelper] Directory check:" << fileName << "Existing ID:" << existingId;
                
                if (existingId == 0) {
                    Category newCat;
                    newCat.name = name;
                    newCat.parentId = parentId;
                    newCat.color = CategoryRepo::getDefaultColor();
                    if (CategoryRepo::add(newCat)) {
                        currentCatId = newCat.id;
                        qDebug() << "[ImportHelper] Created new category:" << fileName << "ID:" << currentCatId;
                    } else {
                        qDebug() << "[ImportHelper] FAILED to create category:" << fileName;
                    }
                } else {
                    currentCatId = existingId;
                }
                // 激活文件夹元数据
                MetadataManager::instance().registerItem(wp);
            } else {
                // 3. 文件处理
                MetadataManager::instance().registerItem(wp);

                if (currentCatId > 0) {
                    std::string fid = MetadataManager::instance().getFileIdSync(wp);
                    if (!fid.empty()) {
                        CategoryRepo::addItemToCategory(currentCatId, fid, wp);
                    }
                }
            }

            // 4. 500项大事务分批提交
            if (++batchCounter >= 500) {
                currentTrans->commit();
                delete currentTrans;
                
                CategoryRepo::saveImmediately();
                currentTrans = new SqlTransaction(db);
                batchCounter = 0;
            }

            // 递归
            if (isDir) {
                QDir dir(p);
                QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
                for (const QFileInfo& subInfo : entries) {
                    processPath(subInfo.absoluteFilePath(), currentCatId);
                }
            }
        };

        for (const QString& root : paths) {
            processPath(root, targetCategoryId);
        }

        // 提交最后一批并释放事务
        currentTrans->commit();
        delete currentTrans;

        // C. 导出阶段 (Plan-58: Scoped DB 物理落地)
        if (!context->isCancelled && !paths.isEmpty()) {
            QString rootPath = paths.first(); // 简化逻辑：仅对第一个导入根目录尝试导出
            std::wstring wRoot = QDir::toNativeSeparators(rootPath).toStdWString();
            std::string fid;
            MetadataManager::fetchWinApiMetadataDirect(wRoot, fid);
            if (!fid.empty()) {
                QString dbPath = rootPath + "/.arcmeta/" + QString::fromStdString(fid) + ".db";
                QDir().mkpath(rootPath + "/.arcmeta");

                // 从内存拉取该目录下所有已加载记录
                std::vector<ItemRecord> exportRecords;
                MetadataManager::instance().forEachCachedItem([&](const std::wstring& p, const RuntimeMeta& rm) {
                    QString qp = QString::fromStdWString(p);
                    if (qp.startsWith(rootPath)) {
                        ItemRecord r;
                        r.fileId = QString::fromStdString(rm.fileId128);
                        r.path = qp;
                        r.isDir = rm.isFolder;
                        r.rating = rm.rating;
                        r.color = QString::fromStdWString(rm.color);
                        r.tags = rm.tags;
                        r.note = QString::fromStdWString(rm.note);
                        r.url = QString::fromStdWString(rm.url);
                        r.ctime = rm.ctime;
                        r.mtime = rm.mtime;
                        r.atime = rm.atime;
                        r.size = rm.fileSize;
                        r.width = rm.width;
                        r.height = rm.height;
                        r.isTrash = rm.isTrash;
                        r.isInvalid = rm.isInvalid;
                        r.originalPath = QString::fromStdWString(rm.originalPath);
                        r.palettes.clear();
                        for (const auto& pe : rm.palettes) r.palettes.push_back({pe.color, pe.ratio});
                        exportRecords.push_back(r);
                    }
                });

                if (!exportRecords.empty()) {
                    DatabaseManager::instance().exportToScopedDb(dbPath.toStdWString(), exportRecords, QDateTime::currentMSecsSinceEpoch());
                    ArcMeta::Logger::log(QString("[Import] 已完成 Scoped DB 物理落地: %1").arg(dbPath));
                }
            }
        }

        // D. 完成阶段
        QMetaObject::invokeMethod(QCoreApplication::instance(), [weakProgress, context, currentHandled]() {
            if (context->isCancelled) return;

            if (weakProgress) {
                weakProgress->accept();
                weakProgress->deleteLater();
            }
            CategoryRepo::saveImmediately();
            MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::FullRebuild);
            
            ToolTipOverlay::instance()->showText(QCursor::pos(), 
                QString("已成功导入 %1 个项目并生成镜像").arg(currentHandled), 2000, QColor("#2ecc71"));
        });

        #ifdef Q_OS_WIN
        CoUninitialize();
        #endif
    });
}

} // namespace ArcMeta
