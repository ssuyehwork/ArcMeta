#include "OperationSnapshotEngine.h"
#include "../meta/MetadataManager.h"
#include "../meta/CategoryRepo.h"
#include "../ui/UndoToastOverlay.h"
#include "UndoManager.h"
#include "ActionCommand.h"
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
        auto stdIds = CategoryRepo::getItemCategoryIds(fid);
        snap.categoryIds.reserve(static_cast<int>(stdIds.size()));
        for (int cid : stdIds) {
            snap.categoryIds.append(cid);
        }
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

    // 3. 操作成功：如果外部传入了专用的 undoAction，
    // 在主线程中生成一个通用快照回滚 ActionCommand 并推送给 UndoManager，实现 100% 物理与虚拟并轨！
    if (undoAction) {
        class GeneralSnapshotUndoCommand : public ActionCommand {
        public:
            GeneralSnapshotUndoCommand(QVector<AssetItemSnapshot> before,
                                       std::function<bool(const QVector<AssetItemSnapshot>& beforeState)> undo)
                : m_before(before), m_undoFunc(undo) {}

            void execute() override {}
            void undo() override {
                if (m_undoFunc) {
                    m_undoFunc(m_before);
                }
            }
            void redo() override {}
            QString description() const override { return "快照撤销"; }
            bool affectsPath(const QString& path) const override {
                for (const auto& snap : m_before) {
                    if (snap.path == path) return true;
                }
                return false;
            }
        private:
            QVector<AssetItemSnapshot> m_before;
            std::function<bool(const QVector<AssetItemSnapshot>& beforeState)> m_undoFunc;
        };

        // 压入全局撤销栈，这样无论是按 Ctrl+Z 还是点击气泡，均能完美统一调用同一个 Command 恢复物理与逻辑状态
        UndoManager::instance().pushCommand(std::make_unique<GeneralSnapshotUndoCommand>(beforeState, undoAction));

        // 弹出反馈气泡，点击撤销会直接调用 UndoManager::instance().undo()
        UndoToastOverlay::instance()->showToast(
            parentWidget,
            successToastMsg,
            []() {
                // 回调闭包留空或传入 dummy 即可，因为在 4.1 节中 UndoToastOverlay 已经并轨至 UndoManager
            },
            5000
        );
    }

    return true;
}

} // namespace ArcMeta
