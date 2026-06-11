#pragma once

#include <QStringList>
#include <QObject>
#include <QPointer>
#include "../ui/BatchProgressDialog.h"

namespace ArcMeta {

/**
 * @brief 入库辅助类
 * 统一处理拖拽导入和扫描入库逻辑，提供精细化进度反馈。
 */
class ImportHelper {
public:
    /**
     * @brief 执行批量导入/入库操作
     * @param paths 路径列表
     * @param targetCatId 目标分类ID（0表示未分类）
     * @param progress 进度对话框指针
     * @param parentView 用于完成后的 UI 刷新通知
     */
    static void importPaths(const QStringList& paths, int targetCatId, BatchProgressDialog* progress, QWidget* parentView);

private:
    /**
     * @brief 获取默认分类颜色（与 CategoryPanel 保持一致）
     */
    static std::wstring getDefaultCategoryColor();
};

} // namespace ArcMeta
