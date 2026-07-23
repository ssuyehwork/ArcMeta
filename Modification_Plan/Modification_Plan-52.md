# 列表视图星级资源与紧凑物理规格对齐重构 —— Modification_Plan-52.md

> 状态：已批准，执行中 / 已执行完成

## 1. 任务背景
在对用户提供的 `image.png` 进行了深度的物理对比与考古发现：在网格/自适应视图下，星星并非使用的是外部引入的大命名 SVG，而是原生的 `"star_filled"` 和 `"star"`；而且网格视图下的星星并非无间距紧贴，而是设置了负间距 `starSpacing = -4`。为了在列表视图中真正提供 1:1 绝对同频的质感和规格，需要把列表视图的图标资源、排版尺寸完全向网格视图的 18px 紧凑型物理规格进行精确对位。

## 2. 问题定位
- 渲染层：`src/ui/TreeItemDelegate.h` 的 `col == 2`（星级列）中星星绘制。
- 交互层：`src/ui/ContentPanel.cpp` 事件过滤器中的 `m_treeView` 鼠标位置命中检测。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 列表视图下显示的星级与星级之间间距并非 0 | 使用原生 `"star_filled"` / `"star"` 图标，改用紧凑间距 `starSpacing = -4px` 与 `banGap = 2px`、`banW = 12px`、`starSize = 18px` 完全对齐。 | ✅ |

## 4. 详细解决方案

### 4.1 渲染重构：`src/ui/TreeItemDelegate.h`
对 `col == 2` 分支的参数及逻辑进行标定：
- 图标大小：`banW = 12px`，`starSize = 18px`；
- 图标间距：`banGap = 2px`，`starSpacing = -4px`；
- 首星起点：`starsStartX = startX + banW + banGap` (14 偏移)；
- 资源更正：
  - 实心星使用 `"star_filled"`；
  - 空心星使用 `"star"`；
- 胶囊和循环位置计算：
  - `lastStarRect` 坐标计算中的间距使用 `starSpacing` (即 `starsStartX + 4 * (starSize + starSpacing)`)。
  - 星级循环绘制横坐标为 `starsStartX + i * (starSize + starSpacing)`。

### 4.2 交互重构：`src/ui/ContentPanel.cpp`
同步更新事件过滤器中 `m_treeView` 分支：
- 定义并使用完全相同的：`banW = 12; starSize = 18; banGap = 2; starSpacing = -4;`；
- 坐标起点：`starsStartX = startX + banW + banGap;`；
- 星级 Hitbox 范围横坐标设为 `starsStartX + i * (starSize + starSpacing)`。

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/ui/TreeItemDelegate.h`（星级绘制资源与物理紧凑规格修正）
- [ ] 模块/文件：`src/ui/ContentPanel.cpp`（事件过滤器星级点击 Hitbox 规格修正）

**明确禁止越界修改的范围：**
- [ ] 除列表视图星级绘制和交互对位外的其他逻辑与文件 —— 不修改

## 6. 实现准则与预警【核心】
1. **统一图标美感**：使用统一的原生 `"star_filled"` 和 `"star"` 矢量资源，使星星的圆润角和视觉质感与主视图实现真正的 1:1 对位，消除不同 SVG 的渲染级落差。
2. **防点击偏差**：确保事件拦截在数学上与渲染端的坐标计算公式百分之百一致，达到完美的交互操作体验。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 列表星级紧凑对齐 | 统一将列表模式星级提升至网格紧凑规格：禁止 12px，星星 18px，禁止到首星间距 2px，星间距 -4px，使用统一原生图标 star_filled 和 star。 | ✅ 符合 |

## 8. 待确认事项（可选）
*无。*
