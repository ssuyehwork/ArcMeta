#include "OperationSnapshotEngine.h"
#include "../meta/MetadataManager.h"
#include "../meta/CategoryRepo.h"
#include "../ui/UndoToastOverlay.h"
#include <QFileInfo>

namespace ArcMeta {

OperationSnapshotEngine& OperationSnapshotEngine::instance() {
    static OperationSnapshotEngine inst;
    return inst;
}

AssetItemSnapshot OperationSnapshotEngine::captureSingle(const QString& path) {
    AssetItemSnapshot snap;
    snap.path = path;
    snap.fileName = QFileInfo(path).fileName();

    std::wstring wpath = path.toStdWString();
    std::string fid = MetadataManager::instance().getFolderIdSync(wpath);

    if (!fid.empty()) {
        snap.categoryIds = QVector<int>::fromStdVector(CategoryRepo::getItemCategoryIds(fid));
        // 读取收藏/置顶与元数据属性
        auto meta = MetadataManager::instance().getMeta(wpath);
        snap.isPinned = meta.pinned;
        snap.rating = meta.rating;
        snap.color = QString::fromStdWString(meta.manualColor);
        snap.tags = meta.tags;
        snap.note = QString::fromStdWString(meta.note);
    }
    return snap;
}

QVector<AssetItemSnapshot> OperationSnapshotEngine::captureBatch(const QStringList& paths) {
    QVector<AssetItemSnapshot> list;
    list.reserve(paths.size());
    for (const auto& p : paths) {
        list.append(captureSingle(p));
    }
    return list;
}

bool OperationSnapshotEngine::executeWithSnapshot(
    QWidget* parentWidget,
    SnapshotOperationType opType,
    const QStringList& targetPaths,
    const QString& successToastMsg,
    std::function<bool()> doAction,
    std::function<bool(const QVector<AssetItemSnapshot>& beforeState)> undoAction)
{
    Q_UNUSED(opType);
    if (!doAction) return false;

    // 1. 操作前：自动捕获受影响资产的状态快照 (Before State)
    QVector<AssetItemSnapshot> beforeState = captureBatch(targetPaths);

    // 2. 执行主体写操作
    bool ok = doAction();
    if (!ok) return false;

    // 3. 操作成功：结合 UndoToastOverlay 进行撤销反馈弹出
    // 对应用户原话：“快照结合UndoToastOverlay”
    if (undoAction) {
        UndoToastOverlay::instance()->showToast(
            parentWidget,
            successToastMsg,
            [undoAction, beforeState]() {
                // 点击“撤销”按钮时，传入捕获的物理快照回滚
                undoAction(beforeState);
            },
            5000
        );
    }

    return true;
}

} // namespace ArcMeta
