# 修复多处 Role 未声明及 FilterState 成员不匹配错误 —— Modification_Plan-24.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在项目物理拆分重构后，由于一些新提取的子面板及虚拟数据模型文件缺少核心契约头文件 `ModelContract.h` 的包含，导致所有 CommonRole 枚举项（如 `PathRole`、`IsDirRole` 等）报出“未声明的标识符”错误。此外，在 `FilterProxyModel.cpp` 中过滤逻辑使用了 `currentFilter.rating` 和 `colorTag` 成员，但实际上 `FilterState` 结构体中定义的是多值选择列表 `ratings` (QList<int>) 和 `colors` (QStringList)，从而引发了成员不存在的编译报错。

## 2. 问题定位
1. **缺失 ModelContract.h 包含**：
   - `src/ui/CategoryLibraryPanel.cpp` 引用了 `PathRole`、`IsCategoryRole` 等角色，但未包含 `ModelContract.h`。
   - `src/ui/DiskExplorerPanel.cpp` 引用了 `PathRole`、`IsDirRole` 等角色，但未包含 `ModelContract.h`。
   - `src/ui/models/ArcMetaVirtualDbModel.cpp` 引用了所有 CommonRole 角色，但未包含 `ModelContract.h`。
   - 解决方案：在上述 3 个源文件顶部添加正确的头文件包含（相对路径引入）。

2. **FilterState 成员不匹配**：
   - 在 `src/ui/models/FilterProxyModel.cpp` 过滤评分和设色时：
     - 原代码错误访问了 `currentFilter.rating`（不存在，实际应为 `currentFilter.ratings`）。
     - 原代码错误访问了 `currentFilter.colorTag`（不存在，实际应为 `currentFilter.colors`）。
   - 解决方案：使用 `currentFilter.ratings.contains(r)` 和 `currentFilter.colors.contains(c.toUpper())` 对多值列表进行过滤判定。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 解决““PathRole”: 未声明的标识符”、““IsDirRole”: 未声明的标识符”等未声明错误 | 在 CategoryLibraryPanel.cpp, DiskExplorerPanel.cpp, ArcMetaVirtualDbModel.cpp 顶部包含 ModelContract.h | ✅ |
| 2    | 解决“"rating": 不是 "ArcMeta::FilterState" 的成员”及“"colorTag": 不是 "ArcMeta::FilterState" 的成员”错误 | 将 FilterProxyModel.cpp 的过滤逻辑改为使用多值 ratings 与 colors 判断 | ✅ |

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 在 `src/ui/CategoryLibraryPanel.cpp` 中引入 `ModelContract.h`
```
<<<<<<< SEARCH
#include "CategoryLibraryPanel.h"
#include "UiHelper.h"
#include "../meta/CategoryRepo.h"
=======
#include "CategoryLibraryPanel.h"
#include "UiHelper.h"
#include "../core/ModelContract.h"
#include "../meta/CategoryRepo.h"
>>>>>>> REPLACE
```

### 4.2 在 `src/ui/DiskExplorerPanel.cpp` 中引入 `ModelContract.h`
```
<<<<<<< SEARCH
#include "DiskExplorerPanel.h"
#include "UiHelper.h"
#include "../util/ShellHelper.h"
=======
#include "DiskExplorerPanel.h"
#include "UiHelper.h"
#include "../core/ModelContract.h"
#include "../util/ShellHelper.h"
>>>>>>> REPLACE
```

### 4.3 在 `src/ui/models/ArcMetaVirtualDbModel.cpp` 中引入 `ModelContract.h`
```
<<<<<<< SEARCH
#include "ArcMetaVirtualDbModel.h"
#include "UiHelper.h"
=======
#include "ArcMetaVirtualDbModel.h"
#include "UiHelper.h"
#include "../../core/ModelContract.h"
>>>>>>> REPLACE
```

### 4.4 修正 `src/ui/models/FilterProxyModel.cpp` 中过滤条件判定
将单一值判断改为多值列表判定，与 `FilterState` 中的 `ratings` 和 `colors` 契约完全对齐。

```
<<<<<<< SEARCH
    // 评分过滤
    if (currentFilter.rating >= 0) {
        int r = sourceModel()->data(idx, RatingRole).toInt();
        if (r != currentFilter.rating) return false;
    }

    // 设色过滤
    if (!currentFilter.colorTag.isEmpty()) {
        QString c = sourceModel()->data(idx, ColorRole).toString();
        if (c.compare(currentFilter.colorTag, Qt::CaseInsensitive) != 0) return false;
    }
=======
    // 评分过滤
    if (!currentFilter.ratings.isEmpty()) {
        int r = sourceModel()->data(idx, RatingRole).toInt();
        if (!currentFilter.ratings.contains(r)) return false;
    }

    // 设色过滤
    if (!currentFilter.colors.isEmpty()) {
        QString c = sourceModel()->data(idx, ColorRole).toString();
        if (!currentFilter.colors.contains(c.toUpper())) return false;
    }
>>>>>>> REPLACE
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] `src/ui/CategoryLibraryPanel.cpp` (头文件包含)
- [ ] `src/ui/DiskExplorerPanel.cpp` (头文件包含)
- [ ] `src/ui/models/ArcMetaVirtualDbModel.cpp` (头文件包含)
- [ ] `src/ui/models/FilterProxyModel.cpp` (过滤逻辑判定修正)

**明确禁止越界修改的范围：**
- [ ] 分类与磁盘文件物理监控引擎——不修改
- [ ] UI 标题栏与视觉样式表现——不修改

## 6. 实现准则与预警【核心】
1. **精确路径引用**：在 `models/` 目录下的 `ArcMetaVirtualDbModel.cpp` 引用 `ModelContract.h` 需要两层上跳相对路径即 `../../core/ModelContract.h`；而在外层的 `CategoryLibraryPanel.cpp` 等文件则仅需一层上跳相对路径即 `../core/ModelContract.h`。必须保证路径完全准确。
2. **多值包含判断**：`colors` 中的空字符串表示“无色标”，此包含判断对空值也完全起效，不会破坏过滤空值逻辑。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 数据源契约与双轨路由 | 托管库上下文内元数据写入 SQLite 数据库，磁盘模式下写入离散缓存。 | ✅（本方案不修改物理落盘路径，仅修正类型访问） |
| 输入框清除按钮标准 | 每个输入框采用 setClearButtonEnabled(true) | ✅（本方案不修改任何输入框） |
| 标题栏与关闭按钮行为 | 关闭按钮采用 ErrorRed 悬停及高亮 | ✅（不修改任何界面控件） |

## 8. 待确认事项（可选）
无。
