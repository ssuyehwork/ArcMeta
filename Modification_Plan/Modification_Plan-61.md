# 文件夹与映射分类颜色双向实时同步机制修复 —— Modification_Plan-61.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在当前版本中，原本设计完美的物理文件夹与其对应的数据库映射分类之间的双向颜色标签同步链路，在之前的迭代中因逻辑疏漏发生了断裂。当用户从内容面板对已入库的文件夹设定颜色标签时，侧边栏映射分类的展示颜色并不会同频更新；而从侧边栏分类设定颜色（或随机颜色）后，对应已入库文件夹在内容区渲染的彩色胶囊也未能自动同步，导致体验断层。本方案旨在完全恢复这两条自上而下及自下而上的双向同步逻辑。

## 2. 问题定位
- **断裂点 A（自下而上失效）**：  
  在 `MetadataManager::setColor(path, color)` 中，当用户通过内容面板的右键菜单修改某个物理文件夹的颜色标签时，虽然设置并持久化了该文件夹的 `manualColor`，但**完全没有**更新 `categories` 分类表里对应映射该路径的分类记录的 `color`。
- **断裂点 B（自上而下失效）**：  
  在 `CategoryPanel::onSetColor()` 及 `CategoryPanel::onRandomColor()` 中，当用户通过侧边栏分类树修改分类颜色时，虽然将新颜色写入了 `CategoryRepo` 的数据库分类定义表，但**完全没有**同步更新内存缓存 `MetadataManager` 中 `cat.physicalPath` 文件夹的 `manualColor` 并通知界面重绘。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 对文件夹进行设定某个颜色标签后，它会同步到相应的映射分类颜色 | 在 `MetadataManager::setColor` 写入文件夹颜色后，自动调用数据库更新接口同步映射分类颜色 | ✅ |
| 2    | 无论是从侧边栏分类设定颜色或从内容面板对文件夹进行设定颜色，两者是相互同步的 | 实现了侧边栏分类设定颜色与物理文件夹 `manualColor` 数据打标的双向同频更新（对应用户原话：“两者是相互同步的”） | ✅ |
| 3    | 此功能仅限与已入库的文件夹/分类 | 在双向更新代码中置入路径非空与受控入库前置检验，不影响非入库的外部项目 | ✅ |

## 4. 详细解决方案

### 4.1 自下而上：物理文件夹颜色变化 → 同步映射分类
修改 `src/meta/MetadataManager.cpp` 中的 `MetadataManager::setColor` 接口。
如果是文件夹且新颜色发生改变，查询是否存在绑定了该 `nPath` 的已入库分类记录：
- 我们可以直接调用 `CategoryRepo::updateCategoryColorByPath(nPath, color)`。
- 该接口在 SQL 层面执行 `UPDATE categories SET color = ? WHERE physical_path = ?`。
- 考虑到前端分类树有缓存更新机制，如果成功同步更新了分类记录，则通过 `notifyUI(RefreshLevel::FullRebuild)` 触发全量重建以刷新侧边栏分类树，实现无缝视觉同频。

### 4.2 自上而下：侧边栏分类颜色变化 → 同步映射文件夹
修改 `src/ui/CategoryPanel.cpp` 中的 `CategoryPanel::onSetColor` 及 `CategoryPanel::onRandomColor`。
当对分类设定新颜色后：
- 在调用 `CategoryRepo::update(cat)` 后，检查 `cat.physicalPath` 是否非空。
- 若非空（代表该分类已映射至已入库的物理托管文件夹），则调用 `MetadataManager::instance().setColor(cat.physicalPath, cat.color, true)` 同步更新缓存及物理元数据。

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/meta/MetadataManager.cpp` (修改 `setColor` 函数)
- [ ] 模块/文件：`src/ui/CategoryPanel.cpp` (修改 `onSetColor` 及 `onRandomColor` 函数)

**明确禁止越界修改的范围：**
- [ ] 其他非颜色或非入库项目的元数据编辑逻辑——不修改
- [ ] 侧边栏分类的其他增删改查动作——不修改

## 6. 实现准则与预警【核心】
1. **防死循环机制**：由于 `onSetColor` 会调用 `MetadataManager::setColor`，而 `MetadataManager::setColor` 内部又会调用 `updateCategoryColorByPath`，我们必须增加属性查重阻尼。只有在新颜色不等于旧颜色时才触发同步更新与 UI 信号发射，从而在数据一致后即刻终结递归链。
2. **多线程并发安全**：对 `CategoryRepo` 的数据库写操作有 `WriteGuard` 加锁保护，在跨线程回调时保持对 SQLite 的事务完整性。
3. **入库边界把控**：同步判定仅在 `!cat.physicalPath.empty()` 且为文件夹类型时执行，普通独立文件的染色不向分类树溢出。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| UI 刷新控制 | 全量重建信号使用 `__RELOAD_ALL__`。局部刷新通过 `RefreshLevel::PathUpdate` 单独更新路径，减少渲染开销。 | 符合。侧边栏同步成功后发射全量重建，文件夹更改只定向更新。 |
| 线程与锁 | 数据库的所有修改由 `WriteGuard` 统一调度。唯一元数据访问需经过 `m_mutex` 读写锁机制，保证线程安全。 | 符合。本方案不创建任何裸线程，完全利用已有线程池与加锁设施。 |

## 8. 待确认事项（可选）
（无。本方案针对该双向同步链的逻辑极其内聚且纯粹。）
