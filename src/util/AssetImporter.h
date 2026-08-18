#pragma once

#include <QStringList>
#include <QWidget>
#include <functional>
#include <QObject>

namespace QuarkMeta {

struct ImportContext {
    QStringList sourcePaths;
    int targetCategoryId = 0;
    QString targetPhysicalPath;
    bool allowMove = false;
    std::function<void(int, int)> progressCallback;
    std::function<void(bool, int, const QStringList&)> completionCallback;
};

/**
 * @brief 智能拖拽/导入分流器 (AssetImporter)
 */
class AssetImporter : public QObject {
    Q_OBJECT
public:
    static void importAssets(const ImportContext& ctx);

    // 保持向后兼容的旧接口封装包装器
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

} // namespace QuarkMeta
