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

QFuture<void> ImportHelper::importPaths(const QStringList& paths, int targetCategoryId, QWidget* parent, bool showProgress) {
    if (paths.isEmpty()) return {};

    BatchProgressDialog* progress = nullptr;
    if (showProgress) {
        progress = new BatchProgressDialog("正在处理项目导入...", parent);
        progress->show();
    }

    // 2026-07-xx 建立导入任务的上下文
    struct ImportContext {
        std::atomic<bool> isCancelled{false};
        QFuture<void> future;
    };
    auto context = std::make_shared<ImportContext>();
    QPointer<BatchProgressDialog> weakProgress(progress);

    // 处理用户关闭进度框的操作 (中断保护)
    if (progress) QObject::connect(progress, &BatchProgressDialog::rejected, [weakProgress, context, parent]() {
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

        // 2026-07-xx 按照 Plan-85：重构为两阶段并发导入模型
        struct ImportItem { QString path; int parentId; bool isDir; };
        std::vector<ImportItem> scanQueue;
        
        // 阶段 1：极速递归扫描 (Scanner)
        std::function<void(const QString&, int)> scanner = [&](const QString& p, int parentId) {
            if (context->isCancelled) return;
            QFileInfo info(p);
            bool isDir = info.isDir();
            scanQueue.push_back({p, parentId, isDir});
            if (isDir) {
                QDir dir(p);
                QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
                for (const auto& sub : entries) scanner(sub.absoluteFilePath(), -1); // 暂时标记 parent 为 -1，由 Processor 修正
            }
        };
        // 注意：由于文件夹转分类具有强时序依赖，此处先采用混合同步模式
        
        sqlite3* db = DatabaseManager::instance().getGlobalDb();
        SqlTransaction* currentTrans = new SqlTransaction(db);

        // B. 导入执行逻辑
        std::function<void(const QString&, int)> processPath = [&](const QString& p, int parentId) {
            if (context->isCancelled) return;

            QFileInfo info(p);
            bool isDir = info.isDir();
            std::wstring wp = QDir::toNativeSeparators(p).toStdWString();
            QString fileName = info.fileName();

            // 1. 详细进度反馈
            currentHandled++;
            if (weakProgress) {
                QMetaObject::invokeMethod(weakProgress.data(), "updateProgress", Qt::QueuedConnection, 
                                         Q_ARG(int, currentHandled), Q_ARG(int, totalItems), Q_ARG(QString, fileName));
            }

            int currentCatId = parentId;

            // 2. 文件夹处理
            if (isDir) {
                std::wstring name = fileName.toStdWString();
                int existingId = CategoryRepo::findCategoryId(parentId, name);
                
                if (existingId == 0) {
                    Category newCat;
                    newCat.name = name;
                    newCat.parentId = parentId;
                    newCat.color = CategoryRepo::getDefaultColor();
                    if (CategoryRepo::add(newCat)) {
                        currentCatId = newCat.id;
                    }
                } else {
                    currentCatId = existingId;
                }
                MetadataManager::instance().registerItem(wp);
            } else {
                // 3. 文件处理 (2026-07-xx 并发优化点：registerItem 内部已包含视觉预热)
                MetadataManager::instance().registerItem(wp);

                if (currentCatId > 0) {
                    std::string fid = MetadataManager::instance().getFileIdSync(wp);
                    if (!fid.empty()) {
                        CategoryRepo::addItemToCategory(currentCatId, fid, wp);
                    }
                }
            }

            // 4. 分批事务提交
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

        currentTrans->commit();
        delete currentTrans;

        // C. 完成阶段
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

    return context->future;
}

} // namespace ArcMeta
