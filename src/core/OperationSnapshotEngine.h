#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>
#include <memory>
#include <QWidget>

namespace QuarkMeta {

// 1. 支持的操作类型枚举（对应用户原话：“重命名 批量重命名 拖拽分类 删除 添加至收藏 归类到...”）
enum class SnapshotOperationType {
    Rename,            // 重命名
    BatchRename,       // 批量重命名
    DragCategorize,    // 拖拽分类
    DeleteToTrash,     // 移入回收站/删除
    ToggleFavorite,    // 添加/取消收藏
    AssignToCategory   // 归类到...
};

// 2. 单个资产项的状态快照原子数据
struct AssetItemSnapshot {
    QString path;                 // 绝对物理路径
    QString fileName;             // 文件名
    int primaryCategoryCatId = 0; // 主分类 ID
    QVector<int> categoryIds;     // 挂载的所有分类 ID 列表
    bool isPinned = false;        // 是否置顶/收藏
    int rating = 0;               // 星级
    QString color;                // 标记颜色
    QStringList tags;             // 标签列表
    QString note;                 // 备注
};

// 3. 一次操作的批量快照上下文
struct OperationSnapshotContext {
    SnapshotOperationType opType;
    QString description;                      // 操作描述（如 "成功重命名 5 个项目"）
    QVector<AssetItemSnapshot> beforeState;  // 操作前状态列表
    QVector<AssetItemSnapshot> afterState;   // 操作后状态列表
};

// 4. 操作快照与撤销引擎
class OperationSnapshotEngine {
public:
    static OperationSnapshotEngine& instance();

    // 从指定路径捕获当前内存/数据库中的元数据快照
    AssetItemSnapshot captureSingle(const QString& path);
    QVector<AssetItemSnapshot> captureBatch(const QStringList& paths);

    // 执行带快照捕获与 UndoToastOverlay 弹窗提醒的操作
    // 对应用户原话：“快照结合UndoToastOverlay”
    bool executeWithSnapshot(
        QWidget* parentWidget,
        SnapshotOperationType opType,
        const QStringList& targetPaths,
        const QString& successToastMsg,
        std::function<bool()> doAction,
        std::function<bool(const QVector<AssetItemSnapshot>& beforeState)> undoAction
    );

private:
    OperationSnapshotEngine() = default;
    ~OperationSnapshotEngine() = default;
    OperationSnapshotEngine(const OperationSnapshotEngine&) = delete;
    OperationSnapshotEngine& operator=(const OperationSnapshotEngine&) = delete;
};

} // namespace QuarkMeta
