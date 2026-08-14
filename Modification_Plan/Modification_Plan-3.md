# 磁盘模式库外离散标记与双轨数据路由分流机制 —— Modification_Plan-3.md

> 状态：待批准执行

## 1. 任务背景
在 `Modification_Plan-2.md` 中，我们重构了 `.arc` 物理封装，并彻底物理根除了“创建自动导入”与外部 In-Place Watcher 机制，极大地简化和净化了软件架构。

本方案作为一个纯净、独立的新话题，旨在承接和解决“**如何在彻底弃用自动导入和外部监控（In-Place Watcher）后，依然让用户在普通的物理磁盘导航模式下（即免导入托管库的任何电脑物理文件夹内）实现对文件的星级打标、颜色分类及标签备注修改，并实现离散缓存秒开加载记忆**”这一高级特性。

---

## 2. 核心架构演进图谱

```text
[ 磁盘打标操作 (Alt + 0~5 或右键菜单) ]
                      │
            [ 路径位置智能路由分流 ]
                      │
           ┌──────────┴──────────┐
           ▼                     ▼
     【 托管库内 】        【 普通磁盘文件夹外 】
    (ArcMeta.Library_X)     (如 D:\Photos\my.jpg)
           │                     │
           ▼                     ▼
    写入 SQLite DB          写入 ArcMeta.cache/ 离散缓存
  (DatabaseManager)     (AmMetaJson :: D_Photos_hash.json)
```

---

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|:---:|---|---|:---:|
| 1 | 在磁盘模式下无需导入托管库，也能对电脑上任意物理文件夹里的文件进行打星、颜色标记、添加标签，且数据自动缓存存入 `ArcMeta.cache/*.json` | 详见 4.1、4.2、4.3 节，实现双轨落盘路由，库外元数据使用 `AmMetaJson` 纯净读写并秒开记忆。 | ✅ |

---

## 4. 详细重构解决方案

### 4.1 重构 1：解锁磁盘导航模式下的“打星、颜色、标签、备注”编辑权限
*   **解除 ContentPanel 的setData阻拦**：
    在 `src/ui/ContentPanel.cpp` 中的 `ArcMetaVirtualDbModel::setData`（约 L220 起）中，彻底物理删除（或注释掉）原本对非 `isInsideLibrary` 下 RatingRole/ColorRole/PinnedRole 抛出“编辑受阻”警告弹窗并返回 false 的拦截代码。允许数据编辑。

### 4.2 重构 2：在 `loadDirectory` 物理扫描时自动装载 `AmMetaJson` 离散标记缓存
*   在 `src/ui/ContentPanel.cpp` 的 `loadDirectory()` 的后台扫描 Lambda 函数 `scanDir`（约 L1320）中：
    1.  **加载缓存**：在递归开始处，针对当前物理扫描的文件夹路径 `p`，实例化 `AmMetaJson jsonCache(p.toStdWString());` 并调用 `jsonCache.load();`，拉取内存中已缓存的 items；
    2.  **动态命中**：在 `dir.entryInfoList` 循环遍历每一个物理文件（`info`）时，通过当前文件的逻辑名称 `info.fileName().toStdWString()` 到 `jsonCache.items()` 中进行检索。
    3.  **无缝还原**：如果命中，则将缓存的 `rating`、`color`、`pinned`、`note` 和 `tags` 分别赋值绑定到新生成的卡片实体 `ItemRecord` 的对应字段中。从而实现在没有导入数据库的情况下，磁盘扫描出来的瞬间也能有打标标记展示。

### 4.3 重构 3：在 `MetadataManager` 中实现双轨元数据读写落盘路由
*   在 `src/meta/MetadataManager.cpp` 中重构 `setRating` 和 `setColor`：
    1.  **如果是托管库内（`isInsideManagedLibrary(nPath)`）**：保持原样，通过 SQLite 数据库引擎进行落盘；
    2.  **如果是托管库外（普通磁盘路径）**：
        *   通过文件的物理全路径 `nPath` 解析出其物理所在的文件夹路径 `folderPath` 和逻辑文件名 `fileName`；
        *   实例化并加载当前文件夹对应的 JSON 缓存：`AmMetaJson jsonCache(folderPath); jsonCache.load();`；
        *   将新设定的星级值（`rating`）或颜色标签值（`color`）写入 `jsonCache.items()[fileName]`；
        *   调用 `jsonCache.save();` 实现离散物理落盘（生成于 `ArcMeta.cache/哈希.json`）；
        *   同时更新 `MetadataManager` 内部高频访问内存 `m_cache[nPath]` 以保持内存实时一致性；
        *   发射 `notifyUI(RefreshLevel::PathUpdate, ...)` 信号触发 UI 的局部无闪烁点亮重绘。

---

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] `src/ui/ContentPanel.cpp`（解封 `setData` 的库外阻拦，重写物理扫描 Lambda `scanDir` 自动装载 `AmMetaJson` 填充 ItemRecord）
- [ ] `src/meta/MetadataManager.cpp`（重写 `setRating` 与 `setColor` 实现双轨数据路由分流持久化）

**明确禁止越界修改的范围：**
- [ ] 磁盘模式底层 `MftReader` 与扫描缓存生命周期 —— 不修改
- [ ] 分类模式（Category Mode）数据库读写机制 —— 不修改

---

## 6. 实现准则与安全预警【核心】
1.  **避免物理文件污染**：`AmMetaJson` 必须在程序主目录的 `ArcMeta.cache/` 下使用文件夹路径哈希安全生成 JSON 文件，严禁将临时打标数据写在用户的物理文件夹里。
2.  **非阻塞异步 I/O 运行**：加载 `AmMetaJson` 的 load 过程处于后台 `QtConcurrent::run` 物理扫描线程中，保证大目录下大批量文件加载时不卡死主界面 GUI 线程。

---

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 双轨元数据分流 | 托管库内写入 SQLite 数据库，磁盘模式任意外部物理文件夹自动离散写入 `ArcMeta.cache/*.json` | ✅ 符合 |

---

## 8. 待确认事项（可选）
暂无。