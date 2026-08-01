# 补全新增 Delegate 与 View 文件的 ModelContract 头文件引入 —— Modification_Plan-25.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
本方案承接自 Modification_Plan-24.md。在执行方案 24 后，由于 `TreeItemDelegate.h`、`CategoryDelegate.h`、`DropListView.cpp` 以及 `DropJustifiedView.cpp` 等其他相关视图/代理文件在使用 CommonRole 枚举项（如 `PathRole`、`IsDirRole` 等）时未包含 `#include "../core/ModelContract.h"`，导致这些文件在某些编译单元内报出“未声明的标识符”错误。本方案旨在补全这些头文件引入。

## 2. 问题定位
经过全量头文件包含关系依赖性检查，确认以下文件虽然引用了 `PathRole`、`IsDirRole`、`RatingRole` 等 `CommonRole` 中的枚举，但是并未显式或隐式包含 `src/core/ModelContract.h`。在被未包含 `ModelContract.h` 的单元引用时会抛出未声明标识符的编译错误：
- `src/ui/TreeItemDelegate.h` (引用了 `IsLockedRole`、`ManagedRole`、`TypeRole` 等)
- `src/ui/CategoryDelegate.h` (引用了 `ColorRole` 等)
- `src/ui/ThumbnailDelegate.h` (定义接口并使用 Qt::UserRole 等，在其 cpp 中引用角色)
- `src/ui/ThumbnailDelegate.cpp` (引用了这些变量)
- `src/ui/DropListView.cpp` (引用了 `PathRole`)
- `src/ui/DropJustifiedView.cpp` (引用了 `PathRole` 等，或需要做相同动作)
- `src/ui/CategoryPanel.cpp` (虽然目前没有直接报错，但通过添加 `#include "../core/ModelContract.h"` 能达到防御性高健壮编译安全)

解决方案：在这些文件顶部直接显式添加 `#include "../core/ModelContract.h"` 或 `#include "../../core/ModelContract.h"`。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 解决 Delegate 和 Drop 视图中 “PathRole”、“IsDirRole” 等未声明标识符错误 | 在 TreeItemDelegate.h, CategoryDelegate.h, ThumbnailDelegate.h, ThumbnailDelegate.cpp, DropListView.cpp, DropJustifiedView.cpp 中显式引入 ModelContract.h | ✅ |

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 在 `src/ui/TreeItemDelegate.h` 中包含 `ModelContract.h`
```
<<<<<<< SEARCH
#include "ContentPanel.h"
#include "../meta/MetadataManager.h"
=======
#include "ContentPanel.h"
#include "../core/ModelContract.h"
#include "../meta/MetadataManager.h"
>>>>>>> REPLACE
```

### 4.2 在 `src/ui/CategoryDelegate.h` 中包含 `ModelContract.h`
```
<<<<<<< SEARCH
#include "CategoryModel.h"
#include "CategoryFilterProxyModel.h"
=======
#include "CategoryModel.h"
#include "../core/ModelContract.h"
#include "CategoryFilterProxyModel.h"
>>>>>>> REPLACE
```

### 4.3 在 `src/ui/ThumbnailDelegate.h` 中包含 `ModelContract.h`
```
<<<<<<< SEARCH
#pragma once

#include <QStyledItemDelegate>

namespace ArcMeta {
=======
#pragma once

#include <QStyledItemDelegate>
#include "../core/ModelContract.h"

namespace ArcMeta {
>>>>>>> REPLACE
```

### 4.4 在 `src/ui/ThumbnailDelegate.cpp` 中包含 `ModelContract.h`
```
<<<<<<< SEARCH
#include "ThumbnailDelegate.h"
#include "ContentPanel.h"
=======
#include "ThumbnailDelegate.h"
#include "../core/ModelContract.h"
#include "ContentPanel.h"
>>>>>>> REPLACE
```

### 4.5 在 `src/ui/DropListView.cpp` 中包含 `ModelContract.h`
```
<<<<<<< SEARCH
#include "DropListView.h"
#include "ContentPanel.h"
=======
#include "DropListView.h"
#include "../core/ModelContract.h"
#include "ContentPanel.h"
>>>>>>> REPLACE
```

### 4.6 在 `src/ui/DropJustifiedView.cpp` 中包含 `ModelContract.h`
```
<<<<<<< SEARCH
#include "DropJustifiedView.h"
#include "ContentPanel.h"
=======
#include "DropJustifiedView.h"
#include "../core/ModelContract.h"
#include "ContentPanel.h"
>>>>>>> REPLACE
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] `src/ui/TreeItemDelegate.h`
- [ ] `src/ui/CategoryDelegate.h`
- [ ] `src/ui/ThumbnailDelegate.h`
- [ ] `src/ui/ThumbnailDelegate.cpp`
- [ ] `src/ui/DropListView.cpp`
- [ ] `src/ui/DropJustifiedView.cpp`

**明确禁止越界修改的范围：**
- [ ] 各个 Delegate 与视图的渲染逻辑——不修改
- [ ] 分类与磁盘数据的监控与读取机制——不修改

## 6. 实现准则与预警【核心】
1. **精确路径引用**：在 `src/ui/` 目录下对 `src/core/` 目录中头文件的引用统一为 `"../core/ModelContract.h"`。
2. **严防重复引用**：使用带有 `#pragma once` 的头文件，能够天然防止重复定义，但在每一处需要使用枚举符号的编译文件中都显式包含是个极佳的 C++ 习惯。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 双轨标记与模式隔离 | 托管库上下文内，元数据写入 SQLite 数据库；库外普通磁盘模式下写入离散缓存。 | ✅（不修改打标和隔离逻辑） |
| 输入框清除功能 | 每个输入框必须配置原生 setClearButtonEnabled(true) | ✅（不涉及任何输入框） |
| 标题栏与关闭按钮行为 | 所有界面关闭按钮采用 ErrorRed 悬停及高亮 | ✅（不修改任何标题栏和 UI 界面） |

## 8. 待确认事项（可选）
无。
