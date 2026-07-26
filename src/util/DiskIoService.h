#pragma once
#include <QStringList>
#include <QPointer>
#include <QtConcurrent>
#include <QDir>
#include <QCoreApplication>
#include <QFileInfo>
#include <QFile>
#include <functional>
#include "SecureFileEraser.h"
#include "../meta/MetadataManager.h"
#include "../core/UndoManager.h"

namespace ArcMeta {

/**
 * @brief DiskIoService: 专门接管磁盘物理文件系统 I/O 操作的服务委托，将复杂的物理磁盘删改及后续数据库状态更新从 UI 中彻底剥离。
 */
class DiskIoService {
public:
    /**
     * @brief 异步执行物理删除或安全抹除操作，支持 QPointer 弱引用保护生命周期
     * @param targetPaths 待删除项的绝对路径列表
     * @param isSecure 是否进行深层覆写安全抹除
     * @param progressCallback 进度更新回调 (0~100)
     * @param completionCallback 完成后的回调
     */
    template<typename T>
    static void asyncDeletePaths(
        const QStringList& targetPaths,
        bool isSecure,
        QPointer<T> context,
        std::function<void(int)> progressCallback,
        std::function<void()> completionCallback) 
    {
        (void)QtConcurrent::run([targetPaths, isSecure, context, progressCallback, completionCallback]() {
            int count = 0;
            for (const QString& p : targetPaths) {
                if (!context) return;
                std::wstring wp = QDir::toNativeSeparators(p).toStdWString();
                
                bool physicalOk = false;
                if (isSecure) {
                    physicalOk = SecureFileEraser::shredFile(p);
                } else {
                    // 普通彻底删除：递归
                    std::function<bool(const QString&)> recursiveRemove;
                    recursiveRemove = [&](const QString& target) -> bool {
                        QFileInfo info(target);
                        if (info.isDir()) {
                            QDir dir(target);
                            for (const QString& entry : dir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
                                recursiveRemove(target + "/" + entry);
                            }
                            return QDir().rmdir(target);
                        } else {
                            return QFile::remove(target);
                        }
                    };
                    physicalOk = recursiveRemove(p);
                }

                if (physicalOk) {
                    MetadataManager::instance().deletePermanently(wp);
                    UndoManager::instance().removeCommandsForPath(p);
                }

                count++;
                if (progressCallback) {
                    int percent = (int)((float)count / targetPaths.size() * 100);
                    QMetaObject::invokeMethod(QCoreApplication::instance(), [progressCallback, percent]() {
                        progressCallback(percent);
                    });
                }
            }

            if (completionCallback) {
                QMetaObject::invokeMethod(QCoreApplication::instance(), [context, completionCallback]() {
                    if (context) {
                        completionCallback();
                    }
                });
            }
        });
    }
};

} // namespace ArcMeta
