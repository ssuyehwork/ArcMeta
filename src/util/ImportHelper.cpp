#include "ImportHelper.h"
#include "../ui/BatchProgressDialog.h"
#include "../ui/ToolTipOverlay.h"
#include "../meta/MetadataManager.h"
#include "../meta/CategoryRepo.h"
#include "../meta/DatabaseManager.h"
#include "../core/UndoManager.h"
#include "../core/BasicCommands.h"
#include <QDir>
#include <QFileInfo>
#include <QtConcurrent>
#include <QMetaObject>
#include <QCoreApplication>
#include <QMessageBox>
#include <functional>

namespace ArcMeta {

void ImportHelper::importPaths(const QStringList& paths, int targetCategoryId, QWidget* parent) {
    if (paths.isEmpty()) return;

    BatchProgressDialog* progress = new BatchProgressDialog("正在处理项目导入...", parent);
    progress->show();

    // 2026-07-xx 按照用户要求 (1.19)：建立导入任务的快照数据
    struct ImportContext {
        std::vector<int> createdCatIds;
        std::vector<std::pair<int, std::string>> createdAssociations;
        std::vector<std::wstring> registeredPaths;
        std::atomic<bool> isCancelled{false};
    };
    auto context = std::make_shared<ImportContext>();
    QPointer<BatchProgressDialog> weakProgress(progress);

    // 处理用户关闭进度框的操作 (中断保护)
    QObject::connect(progress, &BatchProgressDialog::rejected, [weakProgress, context, parent]() {
        if (!weakProgress) return;

        // 2026-07-xx 按照用户要求：弹出二次确认
        QString msg = "导入尚未完成。您是希望【保留当前进度】并退出，还是【一键撤销】已录入的数据？";
        QMessageBox msgBox(QMessageBox::Question, "中断导入", msg, QMessageBox::NoButton, parent);
        QPushButton* btnKeep = msgBox.addButton("保留进度并退出", QMessageBox::AcceptRole);
        QPushButton* btnUndo = msgBox.addButton("一键撤销并退出", QMessageBox::DestructiveRole);
        msgBox.addButton("继续导入", QMessageBox::RejectRole);

        int ret = msgBox.exec();
        if (ret == QMessageBox::RejectRole) {
            weakProgress->show(); // 恢复显示
            return;
        }

        context->isCancelled = true;
        if (msgBox.clickedButton() == btnUndo) {
            // 执行撤销
            ImportCommand cmd(context->createdCatIds, context->createdAssociations, context->registeredPaths);
            cmd.undo();
            ToolTipOverlay::instance()->showText(QCursor::pos(), "导入已取消并执行一键撤销", 2000, QColor("#E81123"));
        } else {
            ToolTipOverlay::instance()->showText(QCursor::pos(), "导入已中断，进度已保留", 2000, QColor("#FF8C00"));
        }
        weakProgress->deleteLater();
    });

    (void)QtConcurrent::run([paths, targetCategoryId, weakProgress, context]() {
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

        // B. 递归导入逻辑
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

            // 2. 文件夹转分类镜像
            if (isDir) {
                std::wstring name = fileName.toStdWString();
                int existingId = CategoryRepo::findCategoryId(parentId, name);
                if (existingId == 0) {
                    Category newCat;
                    newCat.name = name;
                    newCat.parentId = parentId;
                    newCat.color = L"#555555"; // 统一默认深灰
                    if (CategoryRepo::add(newCat)) {
                        currentCatId = newCat.id;
                        context->createdCatIds.push_back(currentCatId);
                    }
                } else {
                    currentCatId = existingId;
                }
                // 激活文件夹元数据
                MetadataManager::instance().registerItem(wp);
            } else {
                // 3. 文件处理
                MetadataManager::instance().registerItem(wp);
                context->registeredPaths.push_back(wp);

                if (currentCatId > 0) {
                    std::string fid = MetadataManager::instance().getFileIdSync(wp);
                    if (!fid.empty()) {
                        if (CategoryRepo::addItemToCategory(currentCatId, fid, wp)) {
                            context->createdAssociations.push_back({currentCatId, fid});
                        }
                    }
                }
            }

            // 4. 500项大事务分批提交 (模拟，实际 CategoryRepo 已在 addItemToCategory 内部使用了事务)
            // 但此处我们可以在此处强制 flush 物理磁盘
            if (++batchCounter >= 500) {
                CategoryRepo::saveImmediately();
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

        // C. 完成阶段
        QMetaObject::invokeMethod(QCoreApplication::instance(), [weakProgress, context, currentHandled]() {
            if (context->isCancelled) return;

            // 记录到撤销栈
            auto importCmd = std::make_unique<ImportCommand>(context->createdCatIds,
                                                            context->createdAssociations,
                                                            context->registeredPaths);
            UndoManager::instance().pushCommand(std::move(importCmd));

            if (weakProgress) {
                weakProgress->accept();
                weakProgress->deleteLater();
            }
            CategoryRepo::saveImmediately();
            MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::FullRebuild);

            ToolTipOverlay::instance()->showText(QCursor::pos(),
                QString("已成功导入 %1 个项目并生成镜像").arg(currentHandled), 2000, QColor("#2ecc71"));
        });
    });
}

} // namespace ArcMeta
