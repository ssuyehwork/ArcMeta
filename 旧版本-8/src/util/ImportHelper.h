#pragma once

#include <QStringList>
#include <QWidget>

namespace ArcMeta {

/**
 * @brief 导入中枢模块 (ImportHelper)
 * 2026-07-xx 按照用户要求 (1.19)：归一化“收揽入库”与“拖拽导入”的行为逻辑。
 */
class ImportHelper {
public:
    /**
     * @brief 执行统一导入流程
     * @param paths 待导入的物理路径列表
     * @param targetCategoryId 目标父分类 ID (0 表示“我的分类”根目录)
     * @param parent UI 父窗口，用于显示进度框
     */
    static void importPaths(const QStringList& paths, int targetCategoryId = 0, QWidget* parent = nullptr);

    /**
     * @brief 物理准入校验并返回可导入的路径列表 (Plan-113)
     * 对于库外项目，自动执行同盘 Move；跨盘则拒绝并提示。
     */
    static QStringList validateAndMigrate(const QStringList& paths);
};

} // namespace ArcMeta
