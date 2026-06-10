#pragma once
#include "ActionCommand.h"
#include "../meta/MetadataManager.h"
#include "../meta/CategoryRepo.h"
#include "../util/ShellHelper.h"
#include <QString>
#include <QVariant>
#include <QFileInfo>
#include <QDir>
#include <string>

namespace ArcMeta {

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

private:
    void applyValue(const QVariant& val) {
        if (m_type == Rating) {
            MetadataManager::instance().setRating(m_path.toStdWString(), val.toInt(), true);
        } else {
            MetadataManager::instance().setColor(m_path.toStdWString(), val.toString().toStdWString(), true);
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

private:
    QString m_path;
    std::string m_fid;
    int m_categoryId;
    bool m_isAdd;
};

} // namespace ArcMeta
