#pragma once
#include "ActionCommand.h"
#include "../meta/MetadataManager.h"
#include "../meta/CategoryRepo.h"
#include "../meta/DatabaseManager.h"
#include "sqlite3.h"
#include "../util/ShellHelper.h"
#include <QString>
#include <QVariant>
#include <QFileInfo>
#include <QDir>
#include <string>
#include <vector>
#include <utility>

#include <QtConcurrent>
#include <QCoreApplication>
#include "../meta/FileOperationHelper.h"
#include "../meta/CapsuleMediaExtractor.h"
#include "../ui/DiskBatchRenameService.h"

namespace QuarkMeta {

/**
 * @brief 重命名指令
 */
class RenameCommand : public ActionCommand {
public:
    RenameCommand(const QString& oldPath, const QString& newPath)
        : m_oldPath(oldPath), m_newPath(newPath) {}

    void execute() override {
        // 第一次 execute 由外部触发物理操作后调用 pushCommand，
        // 或者在 redo 时被调用。
    }

    void undo() override {
        ShellHelper::renameItem(m_newPath, m_oldPath);
    }

    void redo() override {
        ShellHelper::renameItem(m_oldPath, m_newPath);
    }

    QString description() const override { return "重命名"; }

    bool affectsPath(const QString& path) const override {
        return m_oldPath == path || m_newPath == path;
    }

private:
    QString m_oldPath;
    QString m_newPath;
};

/**
 * @brief 移动/剪切指令
 */
class MoveCommand : public ActionCommand {
public:
    MoveCommand(const QStringList& sourcePaths, const QString& oldDir, const QString& newDir)
        : m_oldDir(oldDir), m_newDir(newDir) {
        for (const QString& p : sourcePaths) {
            m_fileNames << QFileInfo(p).fileName();
        }
    }

    void execute() override {}

    void undo() override {
        QStringList currentPaths;
        for (const QString& name : m_fileNames) {
            currentPaths << QDir(m_newDir).filePath(name);
        }
        ShellHelper::copyOrMoveItems(currentPaths, m_oldDir, true);
    }

    void redo() override {
        QStringList currentPaths;
        for (const QString& name : m_fileNames) {
            currentPaths << QDir(m_oldDir).filePath(name);
        }
        ShellHelper::copyOrMoveItems(currentPaths, m_newDir, true);
    }

    QString description() const override { return "移动文件"; }

    bool affectsPath(const QString& path) const override {
        if (path.startsWith(m_oldDir) || path.startsWith(m_newDir)) return true;
        for (const QString& name : m_fileNames) {
            if (QDir(m_oldDir).filePath(name) == path || QDir(m_newDir).filePath(name) == path) return true;
        }
        return false;
    }

private:
    QStringList m_fileNames;
    QString m_oldDir;
    QString m_newDir;
};

/**
 * @brief 元数据变更指令 (星级、颜色)
 */
class MetadataCommand : public ActionCommand {
public:
    enum Type { Rating, Color };
    MetadataCommand(const QString& path, Type type, const QVariant& oldVal, const QVariant& newVal)
        : m_path(path), m_type(type), m_oldVal(oldVal), m_newVal(newVal) {}

    void execute() override {}

    void undo() override {
        applyValue(m_oldVal);
    }

    void redo() override {
        applyValue(m_newVal);
    }

    QString description() const override { return m_type == Rating ? "更改星级" : "更改颜色"; }

    bool affectsPath(const QString& path) const override {
        return m_path == path;
    }

private:
    void applyValue(const QVariant& val) {
        if (m_type == Rating) {
            MetadataManager::instance().setRating(m_path.toStdWString(), val.toInt());
        } else {
            MetadataManager::instance().setColor(m_path.toStdWString(), val.toString().toStdWString());
        }
    }

    QString m_path;
    Type m_type;
    QVariant m_oldVal;
    QVariant m_newVal;
};

/**
 * @brief 分类关联指令
 */
class CategorizeCommand : public ActionCommand {
public:
    CategorizeCommand(const QString& path, const std::string& fid, int categoryId, bool isAdd)
        : m_path(path), m_fid(fid), m_categoryId(categoryId), m_isAdd(isAdd) {}

    void execute() override {}

    void undo() override {
        if (m_isAdd) {
            CategoryRepo::removeItemFromCategory(m_categoryId, m_fid);
        } else {
            CategoryRepo::addItemToCategory(m_categoryId, m_fid, m_path.toStdWString());
        }
        MetadataManager::instance().notifyCategoryCountChanged();
    }

    void redo() override {
        if (m_isAdd) {
            CategoryRepo::addItemToCategory(m_categoryId, m_fid, m_path.toStdWString());
        } else {
            CategoryRepo::removeItemFromCategory(m_categoryId, m_fid);
        }
        MetadataManager::instance().notifyCategoryCountChanged();
    }

    QString description() const override { return m_isAdd ? "添加分类" : "移除分类"; }

    bool affectsPath(const QString& path) const override {
        return m_path == path;
    }

private:
    QString m_path;
    std::string m_fid;
    int m_categoryId;
    bool m_isAdd;
};

/**
 * @brief 批量取消分类指令 (Plan-83)
 */
class BulkUncategorizeCommand : public ActionCommand {
public:
    BulkUncategorizeCommand(const QString& path, const std::string& fid, const std::vector<int>& oldCatIds)
        : m_path(path), m_fid(fid), m_oldCatIds(oldCatIds) {}

    void execute() override {}

    void undo() override {
        for (int catId : m_oldCatIds) {
            CategoryRepo::addItemToCategory(catId, m_fid, m_path.toStdWString());
        }
        MetadataManager::instance().notifyCategoryCountChanged();
    }

    void redo() override {
        CategoryRepo::removeAllCategories(m_fid);
        MetadataManager::instance().notifyCategoryCountChanged();
    }

    QString description() const override { return "回归未分类"; }

    bool affectsPath(const QString& path) const override {
        return m_path == path;
    }

private:
    QString m_path;
    std::string m_fid;
    std::vector<int> m_oldCatIds;
};

/**
 * @brief 2026-07-xx 按照用户要求 (1.19)：导入任务指令
 * 支持一键撤销整个导入任务产生的分类节点与关联
 */
class ImportCommand : public ActionCommand {
public:
    ImportCommand(const std::vector<int>& createdCatIds, 
                  const std::vector<std::pair<int, std::string>>& createdAssociations,
                  const std::vector<std::wstring>& registeredPaths)
        : m_createdCatIds(createdCatIds), 
          m_createdAssociations(createdAssociations),
          m_registeredPaths(registeredPaths) {}

    void execute() override {}

    void undo() override {
        // 1. 移除分类关联
        for (const auto& assoc : m_createdAssociations) {
            CategoryRepo::removeItemFromCategory(assoc.first, assoc.second);
        }
        // 2. 物理删除创建的分类节点 (逆序删除以处理父子关系)
        for (auto it = m_createdCatIds.rbegin(); it != m_createdCatIds.rend(); ++it) {
            // 此处直接操作 DB 物理删除，不走 CategoryRepo::remove 的移入回收站逻辑
            sqlite3* db = DatabaseManager::instance().getGlobalDb();
            if (db) {
                sqlite3_stmt* stmt;
                if (sqlite3_prepare_v2(db, "DELETE FROM categories WHERE id = ?", -1, &stmt, nullptr) == SQLITE_OK) {
                    sqlite3_bind_int(stmt, 1, *it);
                    sqlite3_step(stmt);
                    sqlite3_finalize(stmt);
                }
            }
        }
        // 3. 将新注册的项目标记回非受控状态 (Managed = false)
        for (const auto& wp : m_registeredPaths) {
            MetadataManager::instance().setManaged(wp, false, false);
        }

        // 4. 物理落盘并刷新 UI
        CategoryRepo::saveImmediately();
        MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::FullRebuild);
    }

    void redo() override {
        // 由于导入涉及物理扫描，REDO 逻辑较为复杂，暂时保留空实现或提示用户重新导入
    }

    QString description() const override { return "撤销导入任务"; }

    bool affectsPath(const QString& path) const override {
        std::wstring wp = QDir::toNativeSeparators(path).toStdWString();
        for (const auto& p : m_registeredPaths) if (p == wp) return true;
        return false;
    }

private:
    std::vector<int> m_createdCatIds;
    std::vector<std::pair<int, std::string>> m_createdAssociations;
    std::vector<std::wstring> m_registeredPaths;
};

/**
 * @brief 安全物理删除指令
 */
class SecureDeleteCommand : public ActionCommand {
public:
    explicit SecureDeleteCommand(const QStringList& paths) : m_targetPaths(paths) {}

    void execute() override {
        for (const auto& path : m_targetPaths) {
            QFileInfo info(path);
            if (info.isDir()) {
                QDir dir(path);
                dir.removeRecursively();
            } else {
                QFile::remove(path);
            }
            MetadataManager::instance().removeMetadataSync(path.toStdWString());
        }
    }

    void undo() override {
        // 物理删除不可撤销，此接口留空
    }

    void redo() override {
        execute();
    }

    QString description() const override { return "安全物理删除"; }

    bool affectsPath(const QString& path) const override {
        for (const auto& p : m_targetPaths) {
            if (p == path) return true;
        }
        return false;
    }

private:
    QStringList m_targetPaths;
};

/**
 * @brief 安全加密指令
 */
class EncryptCommand : public ActionCommand {
public:
    EncryptCommand(const QStringList& paths, const std::string& pwd)
        : m_targetPaths(paths), m_pwd(pwd) {}

    void execute() override {
        // 后台加密由外部 QThreadPool 调用，此处指令记录该状态并支持后续扩展
    }

    void undo() override {
        // 解除加密
    }

    void redo() override {
        execute();
    }

    QString description() const override { return "安全加密保护"; }

    bool affectsPath(const QString& path) const override {
        for (const auto& p : m_targetPaths) {
            if (p == path || (p + ".amenc") == path) return true;
        }
        return false;
    }

private:
    QStringList m_targetPaths;
    std::string m_pwd;
};

/**
 * @brief 2026-06-xx 按照用户期望：增加批量重命名/移动/复制大事务的撤销与重做指令
 */
class BatchRenameCommand : public ActionCommand {
public:
    BatchRenameCommand(bool isCapsule,
                       DiskOperationMode mode,
                       const std::vector<std::wstring>& oldPaths,
                       const std::vector<std::wstring>& newPaths)
        : m_isCapsule(isCapsule), m_mode(mode), m_oldPaths(oldPaths), m_newPaths(newPaths) {}

    void execute() override {
        // 第一次 execute 已由 BatchRenameDialog 直接执行，无需重复操作
    }

    void undo() override {
        // 进行安全的局部变量值捕获，彻底避免 Lambda 后台线程运行期间 "this" 被释放析构带来的 Use-After-Free 悬空崩溃隐患
        std::vector<std::wstring> oldPaths = m_oldPaths;
        std::vector<std::wstring> newPaths = m_newPaths;
        bool isCapsule = m_isCapsule;
        DiskOperationMode mode = m_mode;

        (void)QtConcurrent::run([oldPaths, newPaths, isCapsule, mode]() {
            std::vector<std::pair<std::wstring, std::wstring>> rawPairs;
            for (size_t i = 0; i < oldPaths.size(); ++i) {
                QString oldPathStr = QString::fromStdWString(oldPaths[i]);
                QString newPathStr = QString::fromStdWString(newPaths[i]);

                if (isCapsule) {
                    QFileInfo newInfo(newPathStr);
                    QFileInfo oldInfo(oldPathStr);
                    QDir arcDir = newInfo.absoluteDir();
                    QString oldBaseName = oldInfo.completeBaseName();
                    QString newBaseName = newInfo.completeBaseName();
                    QString oldThumbPath = arcDir.filePath(oldBaseName + "_thumbnail.png");
                    QString newThumbPath = arcDir.filePath(newBaseName + "_thumbnail.png");

                    // 物理重命名主资产
                    if (FileOperationHelper::safeRename(newPathStr, oldPathStr)) {
                        if (QFile::exists(newThumbPath)) {
                            FileOperationHelper::safeRename(newThumbPath, oldThumbPath);
                        }
                        rawPairs.push_back({newPaths[i], oldPaths[i]});
                    }
                } else {
                    if (mode == DiskOperationMode::Copy) {
                        QFile::remove(newPathStr);
                        QString newThumbHashPath = CapsuleMediaExtractor::getDiskThumbCachePath(newPathStr);
                        if (QFile::exists(newThumbHashPath)) {
                            QFile::remove(newThumbHashPath);
                        }
                    } else {
                        bool ok = false;
                        if (mode == DiskOperationMode::Move) {
                            ok = FileOperationHelper::safeMove(newPathStr, oldPathStr);
                        } else {
                            ok = FileOperationHelper::safeRename(newPathStr, oldPathStr);
                        }

                        if (ok) {
                            QString oldThumbHashPath = CapsuleMediaExtractor::getDiskThumbCachePath(oldPathStr);
                            QString newThumbHashPath = CapsuleMediaExtractor::getDiskThumbCachePath(newPathStr);
                            if (QFile::exists(newThumbHashPath)) {
                                if (mode == DiskOperationMode::Move) {
                                    FileOperationHelper::safeMove(newThumbHashPath, oldThumbHashPath);
                                } else {
                                    FileOperationHelper::safeRename(newThumbHashPath, oldThumbHashPath);
                                }
                            }
                            rawPairs.push_back({newPaths[i], oldPaths[i]});
                        }
                    }
                }
            }

            if (!rawPairs.empty()) {
                MetadataManager::instance().renameBatchAsync(rawPairs);
            } else if (mode == DiskOperationMode::Copy) {
                // Copy 模式下虽然不修改 metadata，但删除文件后需要通知 UI 全局重建/刷新
                QMetaObject::invokeMethod(qApp, []() {
                    MetadataManager::instance().notifyFullUIRebuild();
                }, Qt::QueuedConnection);
            }
        });
    }

    void redo() override {
        // 进行安全的局部变量值捕获，彻底避免 Lambda 后台线程运行期间 "this" 被释放析构带来的 Use-After-Free 悬空崩溃隐患
        std::vector<std::wstring> oldPaths = m_oldPaths;
        std::vector<std::wstring> newPaths = m_newPaths;
        bool isCapsule = m_isCapsule;
        DiskOperationMode mode = m_mode;

        (void)QtConcurrent::run([oldPaths, newPaths, isCapsule, mode]() {
            std::vector<std::pair<std::wstring, std::wstring>> rawPairs;
            for (size_t i = 0; i < oldPaths.size(); ++i) {
                QString oldPathStr = QString::fromStdWString(oldPaths[i]);
                QString newPathStr = QString::fromStdWString(newPaths[i]);

                if (isCapsule) {
                    QFileInfo newInfo(newPathStr);
                    QFileInfo oldInfo(oldPathStr);
                    QDir arcDir = oldInfo.absoluteDir();
                    QString oldBaseName = oldInfo.completeBaseName();
                    QString newBaseName = newInfo.completeBaseName();
                    QString oldThumbPath = arcDir.filePath(oldBaseName + "_thumbnail.png");
                    QString newThumbPath = arcDir.filePath(newBaseName + "_thumbnail.png");

                    if (FileOperationHelper::safeRename(oldPathStr, newPathStr)) {
                        if (QFile::exists(oldThumbPath)) {
                            FileOperationHelper::safeRename(oldThumbPath, newThumbPath);
                        }
                        rawPairs.push_back({oldPaths[i], newPaths[i]});
                    }
                } else {
                    if (mode == DiskOperationMode::Copy) {
                        if (QFile::copy(oldPathStr, newPathStr)) {
                            QString oldThumbHashPath = CapsuleMediaExtractor::getDiskThumbCachePath(oldPathStr);
                            QString newThumbHashPath = CapsuleMediaExtractor::getDiskThumbCachePath(newPathStr);
                            if (QFile::exists(oldThumbHashPath)) {
                                QFile::copy(oldThumbHashPath, newThumbHashPath);
                            }
                        }
                    } else {
                        bool ok = false;
                        if (mode == DiskOperationMode::Move) {
                            ok = FileOperationHelper::safeMove(oldPathStr, newPathStr);
                        } else {
                            ok = FileOperationHelper::safeRename(oldPathStr, newPathStr);
                        }

                        if (ok) {
                            QString oldThumbHashPath = CapsuleMediaExtractor::getDiskThumbCachePath(oldPathStr);
                            QString newThumbHashPath = CapsuleMediaExtractor::getDiskThumbCachePath(newPathStr);
                            if (QFile::exists(oldThumbHashPath)) {
                                if (mode == DiskOperationMode::Move) {
                                    FileOperationHelper::safeMove(oldThumbHashPath, newThumbHashPath);
                                } else {
                                    FileOperationHelper::safeRename(oldThumbHashPath, newThumbHashPath);
                                }
                            }
                            rawPairs.push_back({oldPaths[i], newPaths[i]});
                        }
                    }
                }
            }

            if (!rawPairs.empty()) {
                MetadataManager::instance().renameBatchAsync(rawPairs);
            } else if (mode == DiskOperationMode::Copy) {
                // Copy 模式重新生成物理文件后也需要通知 UI 刷新
                QMetaObject::invokeMethod(qApp, []() {
                    MetadataManager::instance().notifyFullUIRebuild();
                }, Qt::QueuedConnection);
            }
        });
    }

    QString description() const override {
        if (m_isCapsule) return "批量重命名 (胶囊)";
        switch (m_mode) {
            case DiskOperationMode::Rename: return "批量重命名 (磁盘)";
            case DiskOperationMode::Move: return "批量移动";
            case DiskOperationMode::Copy: return "批量复制";
        }
        return "批量操作";
    }

    bool affectsPath(const QString& path) const override {
        std::wstring p = QDir::toNativeSeparators(path).toStdWString();
        for (const auto& op : m_oldPaths) {
            if (op == p) return true;
        }
        for (const auto& np : m_newPaths) {
            if (np == p) return true;
        }
        return false;
    }

private:
    bool m_isCapsule;
    DiskOperationMode m_mode;
    std::vector<std::wstring> m_oldPaths;
    std::vector<std::wstring> m_newPaths;
};

} // namespace QuarkMeta
