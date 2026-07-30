# 磁盘模式与托管分类模式全方位计数隔离机制重构 —— Modification_Plan-8.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在 DAM（数字资产管理）系统的一等公民分类重构中，软件已经建立了 **“托管分类模式”** 与 **“磁盘导航模式”** 明确的双轨机制。但是，在侧边栏树状统计计算的设计中，仍然存在着数据互渗与不该有的逻辑漏水关联。

本方案作为一个独立、纯净的新话题，旨在彻底解决库外（磁盘浏览模式）激活的元数据对侧边栏托管库分类统计的污染问题，确保“**两者相互不可有任何关联，各自执行各自的逻辑**”，彻底实现双轨数据计数的全方位强隔离。

---

## 2. 问题定位
*   **内存激活机制导致的数据泄露**：
    当用户处于普通的磁盘目录导航（磁盘）模式下，对某个物理文件进行设定星级、颜色、备注时（对应用户原话：“对某个项目设定星级、颜色、备注时”），该文件的元数据会写入离散 JSON 中，同时在内存中通过 `ensureActivated(nPath)` 进行共享激活。
*   **侧边栏计数统计缺乏库内判定阀**：
    在 `src/meta/CategoryRepo.cpp` 的 `CategoryRepo::fullRecount`（约第 970 行起）进行系统分类计数重新汇总时，是通过对 `MetadataManager::instance().getLightweightCacheSnapshot()` 进行全量循环统计。而由于该遍历过程中（L1050起）缺乏对“是否属于托管库内部”的物理检查，导致被用户在磁盘模式打标激活的任意库外物理文件在重算时都会被无差别累计进侧边栏中（对应用户原话：“侧边栏分类的计数是不是会发生变化？”），导致“全部数据”、“未分类”、“无标签”等系统逻辑分类的计数器产生污染与通货膨胀。

这严重损害了托管资产树统计的独立性与绝对精准性。

---

## 3. 强制对照表

| 编号 | 用户原话 / 需求点 | 方案对应点 | 是否一致 |
|:---:|---|---|:---:|
| 1 | 两者相互不可有任何关联，各自执行各自的逻辑 | 详见 4.1 节，在 `CategoryRepo::fullRecount` 统计中，彻底排除非托管库内的任何路径，确保物理浏览与逻辑分类各自独立。 | ✅ |
| 2 | 如果内容面板里显示的数据来源于目录导航（磁盘）情况下，对某个项目设定星级、颜色、备注时，侧边栏分类的计数是不是会发生变化？ | 详见 4.1 节，通过阻断库外数据参与侧边栏统计计数，彻底确保磁盘模式打星、设色、加备注时，侧边栏的计数稳如泰山、0% 发生变化。 | ✅ |

---

## 4. 详细解决方案

### 4.1 解决：在 `CategoryRepo::fullRecount` 汇总中加装库外数据物理阻断拦截
在 `src/meta/CategoryRepo.cpp` 中重构 `CategoryRepo::fullRecount` 的核对对账循环部分（约 L1050 起）（对应用户原话：“两者相互不可有任何关联，各自执行各自的逻辑”）：
*   在取得快照 `snapshot` 后的每一轮循环头部，对当前的 `meta.path` 进行托管库范围检测（调用 `MetadataManager::instance().isInsideManagedLibrary(meta.path)`）；
*   **物理防线**：如果判定其不处于当前电脑任何激活的托管库盘符（`ArcMeta.Library_[盘符]`）范围内（即 `isInsideManagedLibrary` 为 false），直接执行 `continue;` 彻底将该文件过滤拦截在系统计数之外；
*   确保 `total`、`untagged`、`recentlyVisited`、`uncategorized` 的汇总值仅代表已登记入库的文件。

**重构后的核心代码实现**：
```cpp
    // 2. 物理核对对账
    int total = 0;
    int tags = 0;
    int recentlyVisited = 0;
    int untagged = 0;
    int uncategorized = 0;
    int trash = 0;

    QSet<QString> uniqueTags;
    double now = static_cast<double>(QDateTime::currentMSecsSinceEpoch());

    auto snapshot = MetadataManager::instance().getLightweightCacheSnapshot();
    for (const auto& meta : snapshot) {
        if (meta.fileId128.empty()) continue;
        if (meta.isFolder) continue;

        // 🚨 核心物理防火墙：如果是普通的磁盘导航模式下激活的库外普通项目，绝对禁止其污染侧边栏计数！
        // 各自执行各自的逻辑，两者相互不产生任何关联。
        if (!MetadataManager::instance().isInsideManagedLibrary(meta.path)) {
            continue;
        }

        if (meta.isTrash) {
            trash++;
            continue;
        }

        total++;
        if (meta.tagsEmpty) {
            untagged++;
        } else {
            for (const QString& t : meta.tags) uniqueTags.insert(t);
        }

        if (meta.atime >= now - 86400000.0) {
            recentlyVisited++;
        }

        if (categorizedFids.find(meta.fileId128) == categorizedFids.end()) {
            uncategorized++;
        }
    }
```

---

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] `src/meta/CategoryRepo.cpp`（重构 `fullRecount` 遍历循环，在头部嵌入 `isInsideManagedLibrary` 阻断阀，排除任何磁盘模式库外项）

**明确禁止越界修改的范围：**
- [ ] SQLite 数据库底层读写机制 —— 不修改
- [ ] 侧边栏树形界面的渲染模型 `CategoryModel` 的初始化流程 —— 不修改

---

## 6. 实现准则与安全预警【核心】

1.  **路径类型对齐**：`meta.path` 在内存中是以 `std::wstring` 格式存储，传递给 `isInsideManagedLibrary` 前无需重复类型强转，完美避免类型错配引起的过滤泄露。
2.  **避免反复触发 recount**：本拦截阀运行于 `fullRecount` 高频遍历的循环体中，`isInsideManagedLibrary` 必须由 `MetadataManager` 进行内存级的常数复杂度快速哈希比对（通过 `m_libraryRoots`），不执行任何耗时磁盘 I/O 或数据库扫描，以保障极速、零卡顿的侧边栏刷新性能。

---

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 两者计数强物理隔离 | 普通磁盘物理打星打标与侧边栏系统逻辑分类计数强隔离。互不干涉、各自独立执行各自的逻辑 | ✅ 符合 |

---

## 8. 待确认事项（可选）
暂无。漏水与统计污染链路已被彻底物理切断并定位。