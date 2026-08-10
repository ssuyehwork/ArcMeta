# 操作状态快照与UndoToastOverlay集成模块化设计 —— 操作状态快照与UndoToastOverlay集成模块化设计.md

## 1. 任务背景
在桌面资产管理系统中，用户频繁进行资源整理与元数据编辑操作。为了提升操作容错性与交互体验，系统需要针对关键写操作（包括重命名、批量重命名、拖拽分类、删除/移入回收站、添加至收藏、归类到...）提供可靠的“操作前/后状态快照捕获”与“一键撤销回滚”能力。

现有的 `UndoToastOverlay` 控件已具备 Snackbar 风格的撤销提示 UI 展现，但缺乏统一、高内聚的状态快照引擎（Operation Snapshot Engine）。目前各个视图与操作动作各自编写离散的状态备份与撤销回调，导致逻辑分散且难以复用。

本方案旨在设计并独立模块化 `OperationSnapshotEngine`，统一抽象资产状态快照，并将其与 `UndoToastOverlay` 及 `UndoManager` 深度集成，打造开箱即用的通用撤销底层架构。

---

## 2. 问题定位
1. **快照逻辑离散与未模块化**：针对“重命名（对应用户原话：“重命名”）”、“批量重命名（对应用户原话：“批量重命名”）”、“拖拽分类（对应用户原话：“拖拽分类”）”、“删除（对应用户原话：“删除”）”、“添加至收藏（对应用户原话：“添加至收藏”）”、“归类到...（对应用户原话：“归类到...”）”等操作，没有统一的状态快照数据模型。
2. **与 UI 浮窗交互未标准化**：操作触发后，各 UI 板块各自处理 `UndoToastOverlay` 的弹出与闭包构建，缺少统一的操作执行与撤销生命周期包装器。
3. **扩展性受限**：后续若新增其他批量/单项编辑功能，需要重新手动编写撤销与快照逻辑，违反单一职责原则（SRP）与防重复造轮子准则。

---

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 主要是应用在这些场景：重命名、批量重命名、拖拽分类、删除、添加至收藏、归类到...（对应用户原话：“主要是应用在类似这些场景 重命名 批量重命名 拖拽分类 删除 添加至收藏 归类到...”） | 在 4.1 节抽象支持这 6 类场景的通用 `AssetItemSnapshot` 与 `OperationType` 快照数据结构 | ✅ |
| 2    | 快照结合UndoToastOverlay（对应用户原话：“快照结合UndoToastOverlay”） | 在 4.2 节设计 `OperationSnapshotEngine::executeAndShowToast()` 接口，自动捕获快照并触发 `UndoToastOverlay` 弹出 | ✅ |
| 3    | 期望独立模块化，便于后续复用（对应用户原话：“我期望的是将其独立模块化，因为后续还有很多功能需要使用到快照”） | 在 4.1 节新建 `src/core/OperationSnapshotEngine.h` 及 `.cpp` 独立模块，解耦 UI 与状态捕获 | ✅ |

---

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 新建独立快照引擎模块 (`src/core/OperationSnapshotEngine.h` / `.cpp`)

#### [NEW] [OperationSnapshotEngine.h](file:///G:/C++/ArcMeta/ArcMeta/src/core/OperationSnapshotEngine.h)
```cpp
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>
#include <memory>
#include <QWidget>

namespace ArcMeta {

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

} // namespace ArcMeta
```

#### [NEW] [OperationSnapshotEngine.cpp](file:///G:/C++/ArcMeta/ArcMeta/src/core/OperationSnapshotEngine.cpp)
```cpp
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
        auto meta = MetadataManager::instance().getMetadataSync(wpath);
        snap.isPinned = meta.isPinned;
        snap.rating = meta.rating;
        snap.color = QString::fromStdWString(meta.colorStr);
        snap.tags = meta.tags;
        snap.note = QString::fromStdWString(meta.userNote);
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
```

---

### 4.2 将新模块加入编译系统 (`CMakeLists.txt`)

#### [MODIFY] [CMakeLists.txt](file:///G:/C++/ArcMeta/ArcMeta/CMakeLists.txt)
```diff
--- CMakeLists.txt
+++ CMakeLists.txt
@@ -45,6 +45,8 @@
     src/core/AutoImportManager.cpp
     src/core/SyncStatusService.cpp
+    src/core/OperationSnapshotEngine.h
+    src/core/OperationSnapshotEngine.cpp
     src/meta/MetadataManager.cpp
     src/meta/DatabaseManager.cpp
```

---

### 4.3 核心场景接入范例说明

#### 场景 1：重命名与批量重命名（对应用户原话：“重命名 批量重命名”）
在 `ContentPanel::onBatchRename` 或单项重命名逻辑中：
```cpp
OperationSnapshotEngine::instance().executeWithSnapshot(
    this,
    SnapshotOperationType::BatchRename,
    selectedPaths,
    QString("成功重命名 %1 个项目").arg(selectedPaths.size()),
    [this, renameRules]() {
        return performBatchRenameInternal(renameRules);
    },
    [this](const QVector<AssetItemSnapshot>& beforeSnapshots) {
        // 基于快照还原原始路径与文件名
        return restoreOriginalNamesFromSnapshots(beforeSnapshots);
    }
);
```

#### 场景 2：拖拽分类与归类到...（对应用户原话：“拖拽分类 归类到...”）
在 `CategoryPanel` / `ContentPanel` 分类分配逻辑中：
```cpp
OperationSnapshotEngine::instance().executeWithSnapshot(
    this,
    SnapshotOperationType::AssignToCategory,
    paths,
    QString("已将 %1 个项目归类到指定分类").arg(paths.size()),
    [targetCatId, paths]() {
        return CategoryRepo::addItemsToCategory(targetCatId, paths);
    },
    [paths](const QVector<AssetItemSnapshot>& beforeSnapshots) {
        // 从快照中还原每个资产在归类前的原始分类映射集合 (categoryIds)
        for (const auto& snap : beforeSnapshots) {
            CategoryRepo::setItemCategories(snap.path, snap.categoryIds);
        }
        return true;
    }
);
```

#### 场景 3：删除/移入回收站（对应用户原话：“删除”）
在 `ContentPanel::deleteSelectedItems` 中：
```cpp
OperationSnapshotEngine::instance().executeWithSnapshot(
    this,
    SnapshotOperationType::DeleteToTrash,
    paths,
    QString("已将 %1 个项目移入回收站").arg(paths.size()),
    [this, paths]() {
        return moveToTrashInternal(paths);
    },
    [this](const QVector<AssetItemSnapshot>& beforeSnapshots) {
        // 从回收站中恢复并还原分类/星级元数据
        return restoreFromTrashWithSnapshots(beforeSnapshots);
    }
);
```

#### 场景 4：添加至收藏（对应用户原话：“添加至收藏”）
在 `MetaPanel` 或右键菜单中：
```cpp
OperationSnapshotEngine::instance().executeWithSnapshot(
    this,
    SnapshotOperationType::ToggleFavorite,
    paths,
    "已添加至收藏",
    [paths]() {
        return setFavoriteStatusBatch(paths, true);
    },
    [paths](const QVector<AssetItemSnapshot>& beforeSnapshots) {
        for (const auto& snap : beforeSnapshots) {
            setFavoriteStatusSingle(snap.path, snap.isPinned);
        }
        return true;
    }
);
```

---

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [NEW] `src/core/OperationSnapshotEngine.h`（快照数据结构与引擎头文件）
- [NEW] `src/core/OperationSnapshotEngine.cpp`（快照捕获与 UndoToast 联动实现）
- [MODIFY] `CMakeLists.txt`（加入编译源文件列表）
- [MODIFY] `src/ui/ContentPanel.cpp`（重命名、删除、归类动作接入快照引擎）
- [MODIFY] `src/ui/CategoryPanel.cpp`（拖拽分类动作接入快照引擎）

**明确禁止越界修改的范围：**
- `DatabaseManager.cpp` SQLite 基础表结构与连接初始化——不修改
- `UndoToastOverlay.cpp` 的 UI 样式与绘制逻辑——不修改
- `ThumbnailDelegate.cpp` 卡片渲染逻辑——不修改

---

## 6. 实现准则与预警【核心】

1. **头文件与依赖隔离**：`OperationSnapshotEngine` 位于 `src/core/`，可调用 `src/meta/` 的元数据与分类 Repo，UI 层包含 `OperationSnapshotEngine.h` 调用统一接口，绝不产生循环依赖。
2. **闭包捕获安全与生命周期预警**：在传入 `undoAction` 闭包时，快照数据 `beforeState` 必须按值捕获（Value Capture），禁止引用捕获局部变量，确保 5000ms 后点击“撤销”按钮时快照内存依然合法有效。
3. **物理文件恢复防冲突**：对于重命名和移入回收站恢复，还原时需检查目标物理路径是否已被占用，若已被占用应自动处理后缀防重名，防止覆盖已有文件。

---

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| UI 控件风格 | 撤销反馈浮窗需保持既有 `UndoToastOverlay` 黑色圆角 Snackbar 样式与透明度淡入淡出动画 | ✅ |
| 单一职责 (SRP) | 视图层（ContentPanel / CategoryPanel）仅分发事件，状态捕获与撤销逻辑剥离交由 `OperationSnapshotEngine` 业务类处理 | ✅ |
| 一键清除按钮 | 搜索/输入框一键清除使用 Qt 原生 `setClearButtonEnabled(true)`，本方案不新增输入框，不涉及 | ✅ |

---

## 8. 待确认事项
无。方案设计全面自包含，待用户授权批准后即可交由执行者角色实施物理代码新增与修改。
