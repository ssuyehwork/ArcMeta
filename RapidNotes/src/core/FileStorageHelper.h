#ifndef FILESTORAGEHELPER_H
#define FILESTORAGEHELPER_H

#include <QString>
#include <QStringList>
#include <QObject>
#include <QVariant>

class FramelessProgressDialog;
class QWidget;

class FileStorageHelper {
public:
    /**
     * @brief 处理导入逻辑 (支持拖拽和剪贴板触发)
     * @param paths 物理路径列表
     * @param targetCategoryId 目标父分类ID，默认为 -1 (根分类)
     * @return 成功导入的项目总数 (文件+分类)
     */
    static int processImport(const QStringList& paths, int targetCategoryId = -1, bool fromClipboard = false);

    /**
     * @brief 导出分类内容到本地目录
     */
    static void exportCategory(int catId, const QString& catName, QWidget* parent = nullptr);

    /**
     * @brief 递归导出分类结构（包括子分类）到本地目录
     * [任务1] 实现递归导出结构：主分类为父文件夹，子分类为子文件夹
     */
    static void exportCategoryRecursive(int catId, const QString& catName, QWidget* parent = nullptr);

    /**
     * @brief 导出分类为专属打包文件 (.rnp)
     * 2026-03-xx [NEW] 包含所有元数据：颜色、标签、星级、预设标签等
     */
    static void exportToPackage(int catId, const QString& catName, QWidget* parent = nullptr);

    /**
     * @brief 按照指定的过滤条件导出笔记及附件 (支持今日、昨日、收藏、未分类等)
     * 2026-03-22 [NEW]
     */
    static void exportByFilter(const QString& filterType, const QVariant& filterValue, const QString& exportName, QWidget* parent = nullptr);

    /**
     * @brief 导出整个数据库的分类树结构及其笔记数据
     * 2026-03-22 [NEW]
     */
    static void exportFullStructure(QWidget* parent = nullptr);

    /**
     * @brief 从专属打包文件 (.rnp) 导入
     * 2026-03-xx [NEW] 完美还原所有属性
     */
    static void importFromPackage(QWidget* parent = nullptr);

    static QString getStorageRoot();
    static QString getUniqueFilePath(const QString& dirPath, const QString& fileName);
    
    struct ItemStats {
        qint64 totalSize = 0;
        int totalCount = 0;
    };

    /**
     * @brief 统计路径列表的总大小和文件/文件夹总数
     */
    static ItemStats calculateItemsStats(const QStringList& paths);

private:
    /**
     * @brief 递归导入文件夹为分类结构
     */
    static int importFolderRecursive(const QString& folderPath, int parentCategoryId, 
                                   QList<int>& createdNoteIds, QList<int>& createdCatIds,
                                   FramelessProgressDialog* progress = nullptr, qint64* processedSize = nullptr, bool fromClipboard = false);
    
    /**
     * @brief 导入单个文件到指定分类
     */
    static bool storeFile(const QString& path, int categoryId, 
                         QList<int>& createdNoteIds,
                         FramelessProgressDialog* progress = nullptr, qint64* processedSize = nullptr, bool fromClipboard = false);

    /**
     * @brief 解析 CSV 文件并导入为笔记
     */
    static int parseCsvFile(const QString& csvPath, int catId, QList<int>* createdNoteIds = nullptr);
};

#endif // FILESTORAGEHELPER_H
