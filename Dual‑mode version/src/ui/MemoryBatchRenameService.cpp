#include "MemoryBatchRenameService.h"
#include "../meta/MetadataManager.h"
#include "../meta/CategoryRepo.h"
#include "../meta/FileOperationHelper.h"
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QUuid>
#include <QtConcurrent>
#include <set>
#include <map>

namespace ArcMeta {

bool BatchRenameTransaction::validateNameConflicts(const std::vector<RenamePair>& pairs, std::string& outError) {
    std::set<std::wstring> oldPathsSet;
    std::set<std::wstring> newPathsSet;

    for (const auto& pair : pairs) {
        if (pair.oldPath.empty() || pair.newPath.empty()) {
            outError = "Empty path encountered in rename pair.";
            return false;
        }
        oldPathsSet.insert(pair.oldPath);
    }

    for (const auto& pair : pairs) {
        if (pair.oldPath == pair.newPath) continue;

        // 校验新名称集合内部重复冲突
        if (newPathsSet.count(pair.newPath) > 0) {
            outError = "Duplicate target path detected in rename batch.";
            return false;
        }
        newPathsSet.insert(pair.newPath);

        // 校验与非变更实体的冲突（新路径已存在且不属于本次待变更旧路径集合）
        if (oldPathsSet.count(pair.newPath) == 0) {
            QString qNewPath = QString::fromStdWString(pair.newPath);
            if (QFile::exists(qNewPath) || QDir(qNewPath).exists()) {
                outError = "Target path already exists on disk.";
                return false;
            }
        }
    }
    return true;
}

std::vector<RenameExecutionStep> BatchRenameTransaction::buildTopologyExecutionPlan(const std::vector<RenamePair>& pairs) {
    std::vector<RenameExecutionStep> executionPlan;

    // 过滤无变更路径
    std::map<std::wstring, std::wstring> targetOf;
    for (const auto& pair : pairs) {
        if (pair.oldPath != pair.newPath) {
            targetOf[pair.oldPath] = pair.newPath;
        }
    }

    if (targetOf.empty()) return executionPlan;

    while (!targetOf.empty()) {
        std::set<std::wstring> remainingOlds;
        for (const auto& kv : targetOf) {
            remainingOlds.insert(kv.first);
        }

        std::wstring freeNode;
        bool foundFree = false;

        for (const auto& kv : targetOf) {
            if (remainingOlds.count(kv.second) == 0) {
                freeNode = kv.first;
                foundFree = true;
                break;
            }
        }

        if (foundFree) {
            std::wstring target = targetOf[freeNode];
            targetOf.erase(freeNode);

            RenameExecutionStep step;
            step.fromPath = freeNode;
            step.toPath = target;
            step.isTemporarySwap = false;
            executionPlan.push_back(step);
        } else {
            // 识别循环依赖：引入内存/磁盘临时 UUID 进行解耦三步交换
            auto it = targetOf.begin();
            std::wstring cycleStartNode = it->first;
            std::wstring finalTarget = it->second;

            QUuid uuid = QUuid::createUuid();
            std::wstring tempUuidStr = cycleStartNode + L".__tmp_" + uuid.toString(QUuid::WithoutBraces).toStdWString();

            RenameExecutionStep tempStep;
            tempStep.fromPath = cycleStartNode;
            tempStep.toPath = tempUuidStr;
            tempStep.isTemporarySwap = true;
            tempStep.tempUuid = tempUuidStr;
            executionPlan.push_back(tempStep);

            targetOf.erase(cycleStartNode);
            targetOf[tempUuidStr] = finalTarget;
        }
    }

    return executionPlan;
}

void MemoryBatchRenameService::execute(const std::vector<std::wstring>& originalPaths, 
                                       const std::vector<std::wstring>& newNames,
                                       std::function<void(int successCount)> callback) {
    if (originalPaths.empty() || originalPaths.size() != newNames.size()) {
        if (callback) callback(0);
        return;
    }

    // 🚨 将物理文件 I/O 与拓扑事务计算整体抛入后台线程，彻底释放 UI 主线程
    (void)QtConcurrent::run([originalPaths, newNames, callback]() {
        // Step 1: 构造规范化 RenamePair 集合
        std::vector<RenamePair> renamePairs;
        renamePairs.reserve(originalPaths.size());

        for (size_t i = 0; i < originalPaths.size(); ++i) {
            QString oldPathStr = QString::fromStdWString(originalPaths[i]);
            QFileInfo oldInfo(oldPathStr);
            QDir arcDir = oldInfo.absoluteDir();
            QString newMainPathStr = QDir::toNativeSeparators(arcDir.filePath(QString::fromStdWString(newNames[i])));

            std::wstring normOld = MetadataManager::normalizePath(oldInfo.absoluteFilePath().toStdWString());
            std::wstring normNew = MetadataManager::normalizePath(newMainPathStr.toStdWString());

            renamePairs.push_back({normOld, normNew});
        }

        // Step 2: 三步冲突校验 (Pre-check)
        std::string conflictErr;
        if (!BatchRenameTransaction::validateNameConflicts(renamePairs, conflictErr)) {
            qWarning() << "[MemoryBatchRename] Pre-check failed:" << QString::fromStdString(conflictErr);
            if (callback) {
                QMetaObject::invokeMethod(qApp, [callback]() { callback(0); }, Qt::QueuedConnection);
            }
            return;
        }

        // Step 3: 构建依赖图，获取拓扑排序执行序列 (Dependency Graph Topology Sort)
        std::vector<RenameExecutionStep> executionPlan = BatchRenameTransaction::buildTopologyExecutionPlan(renamePairs);

        // Step 4: 暂存执行拓扑序列，遇到异常自动回滚
        std::vector<RenameExecutionStep> executedSteps;
        bool hasError = false;

        for (const auto& step : executionPlan) {
            QString fromQPath = QString::fromStdWString(step.fromPath);
            QString toQPath = QString::fromStdWString(step.toPath);

            // A. 物理主资产文件重命名
            if (!FileOperationHelper::safeRename(fromQPath, toQPath)) {
                hasError = true;
                break;
            }

            // B. 同步处理胶囊文件夹内部缩略图 (*_thumbnail.png)
            QFileInfo fromInfo(fromQPath);
            QFileInfo toInfo(toQPath);
            QDir arcDir = fromInfo.absoluteDir();

            QString oldBaseName = fromInfo.completeBaseName();
            QString newBaseName = toInfo.completeBaseName();

            QString oldThumbAbsPath = arcDir.filePath(oldBaseName + "_thumbnail.png");
            QString newThumbAbsPath = arcDir.filePath(newBaseName + "_thumbnail.png");

            if (QFile::exists(oldThumbAbsPath) && oldThumbAbsPath != newThumbAbsPath) {
                FileOperationHelper::safeRename(oldThumbAbsPath, newThumbAbsPath);
            }

            executedSteps.push_back(step);
        }

        if (hasError) {
            // 出现异常，逆序回滚已完成的物理步骤
            qWarning() << "[MemoryBatchRename] Physical rename failed! Rolling back executed steps...";
            for (auto it = executedSteps.rbegin(); it != executedSteps.rend(); ++it) {
                QString fromQPath = QString::fromStdWString(it->toPath);
                QString toQPath = QString::fromStdWString(it->fromPath);
                FileOperationHelper::safeRename(fromQPath, toQPath);

                QFileInfo fromInfo(fromQPath);
                QFileInfo toInfo(toQPath);
                QDir arcDir = fromInfo.absoluteDir();

                QString oldThumb = arcDir.filePath(fromInfo.completeBaseName() + "_thumbnail.png");
                QString newThumb = arcDir.filePath(toInfo.completeBaseName() + "_thumbnail.png");
                if (QFile::exists(oldThumb)) {
                    FileOperationHelper::safeRename(oldThumb, newThumb);
                }
            }

            if (callback) {
                QMetaObject::invokeMethod(qApp, [callback]() { callback(0); }, Qt::QueuedConnection);
            }
            return;
        }

        // Step 5: 原子提交主内存与数据库同步
        std::vector<std::pair<std::wstring, std::wstring>> rawPairs;
        rawPairs.reserve(renamePairs.size());
        for (const auto& pair : renamePairs) {
            rawPairs.push_back({pair.oldPath, pair.newPath});
        }

        MetadataManager::instance().renameBatchAsync(rawPairs, callback);
    });
}

} // namespace ArcMeta
