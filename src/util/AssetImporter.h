#pragma once

#include <QStringList>
#include <QWidget>
#include <functional>

namespace ArcMeta {

/**
 * @brief 智能拖拽/导入分流器 (AssetImporter)
 */
class AssetImporter {
public:
    /**
     * @brief 执行智能分流导入与打包流程
     * @param paths 导入源路径列表
     * @param targetCatId 目标分类 ID (0 为根目录/未分类)
     * @param parent 父 QWidget
     * @param onComplete 导入完成后的刷新回调
     */
    static void importAssets(const QStringList& paths,
                             int targetCatId,
                             QWidget* parent = nullptr,
                             std::function<void()> onComplete = nullptr,
                             bool allowMove = false);

    static void importAssets(const QStringList& paths,
                             int targetCatId,
                             QWidget* parent,
                             std::function<void(const QStringList& newlyImportedPaths)> onComplete,
                             bool allowMove = false);

private:
    static bool importSingleFile(const QString& srcPath,
                                 int targetCatId,
                                 const QString& managedRoot,
                                 QStringList* newlyImportedPaths = nullptr,
                                 bool allowMove = false);

    static bool importDirectoryRecursive(const QString& srcDir,
                                         int parentCatId,
                                         const QString& managedRoot,
                                         QStringList* newlyImportedPaths = nullptr,
                                         bool allowMove = false);
};

} // namespace ArcMeta
