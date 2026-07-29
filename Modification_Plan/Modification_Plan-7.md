# 侧边栏空白及根托管库拖拽释放归入未分类重构 —— Modification_Plan-7.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在 DAM（数字资产管理）系统的一等公民分类树重构中，主程序为托管库提供了直属的分类视图和树状渲染。然而在拖拽入库和分类绑定交互中，当用户拖动文件投放到分类面板空白处或根托管库时，缺少了清晰的、直观的默认逻辑分流。

本方案作为一个独立、纯净的新话题，旨在彻底解决这一交互缺失，确保侧边栏空白投递与根托管库投递时，默认、无损地路由归入 `CategoryRepo::UNCATEGORIZED_CAT_ID`（`-2` 未分类）。

---

## 2. 问题定位
*   **拖拽投递盲区**：
    在 `src/ui/CategoryPanel.cpp` 的 `pathsDropped` 监听回调中，系统针对拖拽动作释放点：
    1.  当拖动释放到树的空白空白区域时（判定为 `index.isValid() == false`），默认直接落入 `targetCatId = 0` 的非预期值（对应用户原话：“当 index.isValid() == false 时，代表拖到了侧边栏树的【空白处】！目标分类 ID 显式指定为 UNCATEGORIZED_CAT_ID (-2，未分类)”）。
    2.  当拖动释放到一等公民最顶层的根托管库分类节点时（判定为 `name.startsWith("ArcMeta.Library_")`），缺少专用的拦截，未能兜底路由至未分类。

这会导致这些投递动作无法归入预期的未分类数据库区间。

---

## 3. 强制对照表

| 编号 | 用户原话 / 需求点 | 方案对应点 | 是否一致 |
|:---:|---|---|:---:|
| 1 | 当 `index.isValid() == false` 时，代表拖到了侧边栏树的【空白处】，目标分类 ID 显式指定为 `UNCATEGORIZED_CAT_ID` | 详见 4.1 节，在 `index.isValid() == false` 分支将 `targetCatId` 赋值为 `CategoryRepo::UNCATEGORIZED_CAT_ID`。 | ✅ |
| 2 | 拖到根托管库上 (如 `ArcMeta.Library_D`) 同样默认归属于 `-2`（未分类） | 详见 4.1 节，添加对以 `ArcMeta.Library_` 开头且 `IdRole` 为 0 的节点判定，将 `targetCatId` 强制置为未分类 ID。 | ✅ |

---

## 4. 详细解决方案

### 4.1 解决：重构 `CategoryPanel.cpp` 中的 `pathsDropped` 投放分流逻辑
在 `src/ui/CategoryPanel.cpp` 中重构 `connect(m_categoryTree, &DropTreeView::pathsDropped, ...)` 的 Lambda 监听回调（约 L1035 起）：
*   将 `targetCatId` 的初始化值设为 `CategoryRepo::UNCATEGORIZED_CAT_ID`（其在 C++ 代码中为 `CategoryRepo::UNCATEGORIZED_CAT_ID` 的常数 `-2`）。
*   在 `index.isValid()` 的分支中：
    *   若目标项是根托管库分类（`index.data(IdRole).toInt() == 0 || name.startsWith("ArcMeta.Library_", Qt::CaseInsensitive)`），显式强设 `targetCatId = CategoryRepo::UNCATEGORIZED_CAT_ID;`（对应用户原话：“拖到根托管库上 (如 ArcMeta.Library_D)...targetCatId = UNCATEGORIZED_CAT_ID; // -2 未分类”）。
    *   若目标项是具体子分类且 `IdRole > 0`，设置 `targetCatId = index.data(IdRole).toInt();`。
*   在 `else` 分支（即 `index.isValid() == false` 侧边栏空白空白处）：
    *   显式设定 `targetCatId = CategoryRepo::UNCATEGORIZED_CAT_ID;`（对应用户原话：“当 index.isValid() == false 时，代表拖到了侧边栏树的【空白处】！目标分类 ID 显式指定为 UNCATEGORIZED_CAT_ID (-2，未分类)”）。

**重构后的核心代码实现**：
```cpp
    connect(m_categoryTree, &DropTreeView::pathsDropped, this, [this](const QStringList& paths, const QModelIndex& proxyIndex) {
        QModelIndex index = m_proxyModel->mapToSource(proxyIndex);
        int targetCatId = CategoryRepo::UNCATEGORIZED_CAT_ID; // 默认目标分类 ID 为 -2 (未分类)

        if (index.isValid()) {
            QString type = index.data(TypeRole).toString();
            QString name = index.data(NameRole).toString();

            // 1. 拖到回收站
            if (type == "trash") {
                if (ShellHelper::moveToTrash(paths)) {
                    m_categoryModel->refresh();
                    MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::FullRebuild);
                    ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#e74c3c;'>已成功移入回收站</b>", 1500, ErrorRed);
                }
                return;
            }

            // 2. 拖到具体的子分类上 (ID > 0)
            if (type == "category" && index.data(IdRole).toInt() > 0) {
                targetCatId = index.data(IdRole).toInt();
            } 
            // 3. 拖到根托管库上 (如 ArcMeta.Library_D)
            else if (index.data(IdRole).toInt() == 0 || name.startsWith("ArcMeta.Library_", Qt::CaseInsensitive)) {
                targetCatId = CategoryRepo::UNCATEGORIZED_CAT_ID; // -2 未分类
            }
        } else {
            // 🚨 4. 关键补充：当 index.isValid() == false 时，代表拖到了侧边栏树的【空白处】！
            // 目标分类 ID 显式指定为 UNCATEGORIZED_CAT_ID (-2，未分类)
            targetCatId = CategoryRepo::UNCATEGORIZED_CAT_ID;
        }

        if (!paths.isEmpty()) {
            // 抛出信号，通知 MainWindow 启动物理入库并绑定 targetCatId (-2 为未分类)
            emit pathsDroppedToCategory(paths, targetCatId);
        }
    });
```

---

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] `src/ui/CategoryPanel.cpp`（重构 `pathsDropped` 投放 Lambda，补齐空白投放和托管库顶层投放的未分类 `-2` 的兜底判定）

**明确禁止越界修改的范围：**
- [ ] 分类选择联动树的点击加载行为（`categorySelected`） —— 不修改
- [ ] 右键弹出菜单及底栏搜索框交互 —— 不修改

---

## 6. 实现准则与安全预警【核心】

1.  **标识符精准查找**：因为 `UNCATEGORIZED_CAT_ID` 静态常数定义在 `CategoryRepo` 命名空间或类内部，代码中必须完整书写为 `CategoryRepo::UNCATEGORIZED_CAT_ID` 以免编译失败。
2.  **避免变量污染**：`targetCatId` 仅作为拖放信号参数发射，不应该直接写入面板任何全局共享或缓存变量，保证多选拖入时各事务之间的原子隔离。

---

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 空白处释放归入未分类 | 拖到侧边栏空隙或不合法位置时，系统应兜底分流归入 `-2` 未分类，绝不上锁或丢弃 | ✅ 符合 |

---

## 8. 待确认事项（可选）
暂无。代码修复与 Lambda 路由逻辑均已定位冻结。