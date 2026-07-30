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
     * @param targetPhysicalPath 目标物理路径 (如果是非空，说明是迁入物理库路径)
     * @param isMove 是否是剪切/移动操作
     * @param parent 父 QWidget
     * @param onComplete 导入完成后的刷新回调
     */
    static void importAssets(const QStringList& paths,
                             int targetCatId,
                             const QString& targetPhysicalPath = "",
                             bool isMove = false,
                             QWidget* parent = nullptr,
                             std::function<void()> onComplete = nullptr);

private:
    static bool importSingleFile(const QString& srcPath,
                                 int targetCatId,
                                 const QString& managedRoot);

    static bool importDirectoryRecursive(const QString& srcDir,
                                         int parentCatId,
                                         const QString& managedRoot);
};

} // namespace ArcMeta
