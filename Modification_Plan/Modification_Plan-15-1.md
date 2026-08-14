# 资产计数与展示中排除普通文件夹误伤 .arc 容器整构 —— Modification_Plan-15.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在项目“1个 .arc 包 = 1个受控资产单位”的核心架构体系下，`.arc` 容器物理上虽然是一个文件夹（如 `00ms8va9te001.arc`），但在内存数据库模式的语义中，它代表的是一个自包含的高内聚“资产”（等同于文件），绝不应该被普通文件夹过滤逻辑所忽略（对应用户原话：“.arc 容器虽然物理上是个文件夹，但在内存数据库模式的语义里，它就是"一个文件"（一个资产），不该被这条"排除文件夹"的判断误伤”）。
目前代码在元数据激活计数、分类项计数、启动全量重算以及系统分类路径获取时，均使用了 `isFolder` 属性无条件过滤和排除了文件夹。这导致虽然物理资产包已成功创建并归入磁盘，但前后台计数系统和内容展示面板无法正常抓取并显示它，造成了“导入成功但计数为 0、点击亦无数据”的逻辑漏洞（对应用户原话：“这正是导致这次"项目已创建、但计数为 0、点开也没数据"的直接原因”）。为打通资产识别的最后一公里，需要对这四大核心过滤点进行系统性整构。

## 2. 问题定位
通过全量代码深度对账，精确定位出 4 处无条件按 `isFolder` 过滤而误伤了 `.arc` 受控资产包的具体代码和逻辑：

1. **实时计数器漏增 (`src/meta/MetadataManager.cpp` 中的 `ensureActivated`)**：
   在向内存激活写入缓存时，若 `isFolder` 为真，则直接跳过 `s_totalCount` 和 `s_uncategorizedCount` 的自增。
2. **分类侧边栏计数漏计 (`src/meta/CategoryRepo.cpp` 中的 `getCounts`)**：
   在遍历内存快照并按分类汇聚计算侧边栏数字时，条件 `!meta.isFolder` 导致 `.arc` 资产包分类在侧边栏上恒显示为 `(0)`。
3. **全量重算审计过滤误伤 (`src/meta/CategoryRepo.cpp` 中的 `fullRecount`)**：
   在启动、盘点或重算时，条件 `meta.isFolder` 使得所有物理资产包在重新盘点审计中被全部清空过滤，直接清零数据库持久化状态。
4. **分类内容展现面板过滤不加载 (`src/meta/CategoryRepo.cpp` 中的 `getSystemCategoryPaths`)**：
   当用户点击“全部数据”或“未分类”时，逻辑 `if (meta.isFolder) return;` 导致内容展示区彻底屏蔽了该资产。

---

*答复用户专门核对事项*：`addItemToCategory` 底层仅执行 SQL 插入（`category_items` 表），其内部**没有任何 `isFolder` 过滤**，故资产向分类节点的物理绑定数据已经在表中正确落位，本病根正是导致“全部数据”和“具体托管库分类”两处计数均显示为 0、点开也空无一物的**唯一、共同且最根本的技术断点**。本方案将通过一次性重构解决这四大关联位置，实现彻底治愈。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 是 .arc 容器，即便物理上是文件夹，也要当资产计数 | 4.1 节，改造 `ensureActivated`，使 `.arc` 容器正常被实时计数器捕获 | ✅ |
| 2    | 只有真正的普通文件夹才继续排除在计数之外 | 4.1 节，普通子目录分类项依然完美被过滤和排除在计数之外 | ✅ |
| 3    | addItemToCategory 内部会不会也有类似按 isFolder 过滤的判断，导致两处计数卡在同一个病根上？ | 2 节“问题定位”末尾明确答复：`addItemToCategory` 内部无过滤，四大 `isFolder` 断点是导致两处计数卡死的唯一、共同且最根本的技术病根，本方案一并修复 | ✅ |

## 4. 详细解决方案
本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 重整元数据激活激活阶段的实时计数
在 `src/meta/MetadataManager.cpp` 的 `ensureActivated` 内，允许 `.arc` 容器通过 countsAsAsset 标识触发计数：

```
<<<<<<< SEARCH
        m_cache[nPath] = rm;
        if (!rm.isFolder) {
            CategoryRepo::s_totalCount.fetch_add(1);
            if (rm.tags.isEmpty()) {
                CategoryRepo::s_untaggedCount.fetch_add(1);
            } else {
                std::lock_guard<std::mutex> tagsLock(CategoryRepo::s_tagsMutex);
                for (const auto& t : rm.tags) {
                    if (!CategoryRepo::s_globalTagsSet.contains(t)) {
                        CategoryRepo::s_globalTagsSet.insert(t);
                        CategoryRepo::s_tagsCount.fetch_add(1);
                    }
                }
            }
            if (CategoryRepo::getItemCategoryIds(rm.fileId128).empty()) {
                CategoryRepo::s_uncategorizedCount.fetch_add(1);
            }
        }
=======
        m_cache[nPath] = rm;
        // 🚨 资产判定重构：.arc 容器在物理上虽然是文件夹，但在托管库语义中代表一个资产单元，必须作为资产进行计数
        bool countsAsAsset = !rm.isFolder || (nPath.size() >= 4 && nPath.substr(nPath.size() - 4) == L".arc");
        if (countsAsAsset) {
            CategoryRepo::s_totalCount.fetch_add(1);
            if (rm.tags.isEmpty()) {
                CategoryRepo::s_untaggedCount.fetch_add(1);
            } else {
                std::lock_guard<std::mutex> tagsLock(CategoryRepo::s_tagsMutex);
                for (const auto& t : rm.tags) {
                    if (!CategoryRepo::s_globalTagsSet.contains(t)) {
                        CategoryRepo::s_globalTagsSet.insert(t);
                        CategoryRepo::s_tagsCount.fetch_add(1);
                    }
                }
            }
            if (CategoryRepo::getItemCategoryIds(rm.fileId128).empty()) {
                CategoryRepo::s_uncategorizedCount.fetch_add(1);
            }
        }
>>>>>>> REPLACE
```

### 4.2 重整分类侧边栏计数汇聚逻辑
在 `src/meta/CategoryRepo.cpp` 的 `getCounts` 中，将 `.arc` 容器计入分类总数计数中：

```
<<<<<<< SEARCH
    // 3. 遍历内存缓存，按 FID 去重并分发到各分类桶
    // 2026-07-xx 回滚：仅计算直接关联的 FID，取消自动向上递归汇总
    MetadataManager::instance().forEachCachedItem([&](const std::wstring&, const RuntimeMeta& meta) {
        // 2026-07-xx 物理对齐：只要在关联表中且非文件夹/回收站，即计入分类总数
        if (!meta.fileId128.empty() && !meta.isFolder && !meta.isTrash) {
            auto it = fidToCats.find(meta.fileId128);
            if (it != fidToCats.end()) {
                for (int catId : it->second) {
                    catToUniqueFids[catId].insert(meta.fileId128);
                }
            }
        }
    });
=======
    // 3. 遍历内存缓存，按 FID 去重并分发到各分类桶
    // 2026-07-xx 回滚：仅计算直接关联的 FID，取消自动向上递归汇总
    MetadataManager::instance().forEachCachedItem([&](const std::wstring& path, const RuntimeMeta& meta) {
        // 🚨 资产分类计数重构：只要在关联表中、非回收站、且非普通文件夹（保留 .arc 容器作为资产），即计入分类总数
        bool isArc = (path.size() >= 4 && path.substr(path.size() - 4) == L".arc");
        bool countsAsAsset = !meta.isFolder || isArc;
        if (!meta.fileId128.empty() && countsAsAsset && !meta.isTrash) {
            auto it = fidToCats.find(meta.fileId128);
            if (it != fidToCats.end()) {
                for (int catId : it->second) {
                    catToUniqueFids[catId].insert(meta.fileId128);
                }
            }
        }
    });
>>>>>>> REPLACE
```

### 4.3 重整全量重算及物理盘点过滤逻辑
在 `src/meta/CategoryRepo.cpp` 的 `fullRecount` 内，将 `.arc` 资产包免于重算过滤排除：

```
<<<<<<< SEARCH
    auto snapshot = MetadataManager::instance().getLightweightCacheSnapshot();
    for (const auto& meta : snapshot) {
        if (meta.fileId128.empty()) continue;
        if (meta.isFolder) continue;

        // 🚨 核心物理防火墙：如果是普通的磁盘导航模式下激活的库外普通项目，绝对禁止其污染侧边栏计数！
=======
    auto snapshot = MetadataManager::instance().getLightweightCacheSnapshot();
    for (const auto& meta : snapshot) {
        if (meta.fileId128.empty()) continue;
        
        bool isArc = (meta.path.size() >= 4 && meta.path.substr(meta.path.size() - 4) == L".arc");
        if (meta.isFolder && !isArc) continue;

        // 🚨 核心物理防火墙：如果是普通的磁盘导航模式下激活的库外普通项目，绝对禁止其污染侧边栏计数！
>>>>>>> REPLACE
```

### 4.4 重整系统分类内容面板展示加载逻辑
在 `src/meta/CategoryRepo.cpp` 的 `getSystemCategoryPaths` 内，解除对 `.arc` 容器路径的拦截：

```
<<<<<<< SEARCH
    MetadataManager::instance().forEachCachedItem([&](const std::wstring& path, const RuntimeMeta& meta) {
        // 核心红线：彻底排除文件夹
        if (meta.isFolder) return;
        
        bool match = false;
=======
    MetadataManager::instance().forEachCachedItem([&](const std::wstring& path, const RuntimeMeta& meta) {
        // 🚨 界面展现加载重构：普通物理文件夹予以排除，而 .arc 受控容器文件夹作为资产正常加载展现
        bool isArc = (path.size() >= 4 && path.substr(path.size() - 4) == L".arc");
        if (meta.isFolder && !isArc) return;
        
        bool match = false;
>>>>>>> REPLACE
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] `src/meta/MetadataManager.cpp`（重定义 ensureActivated 中的countsAsAsset）
- [ ] `src/meta/CategoryRepo.cpp`（重构 getCounts、fullRecount 和 getSystemCategoryPaths 以解除对 .arc 的误伤）

**明确禁止越界修改的范围：**
- [ ] 严禁修改任何 QML / 视图绘制代理（`GridItemDelegate`）或 MainWindow 底栏布局本身。

## 6. 实现准则与预警【核心】
1. **字符串比对容错**：在 C++ 中比对 `.arc` 后缀时，直接取 `nPath.substr(nPath.size() - 4)` 前保证 `size() >= 4`，坚决避免发生越界崩溃崩溃。
2. **各轨逻辑完全自愈**：通过对 `.arc` 的精准条件判定，既保证了物理磁盘导航模式下非托管的普通文件夹继续被 100% 过滤（不参与计数不污染数据库），又彻底激活了托管库模式下的包资产，高内聚且极其干净。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 双轨制数据路由分流 | 数据流绝对不交叉，磁盘模式不做特殊语义翻译不污染，托管库模式执行独立逻辑 | ✅ 符合。本方案通过极其轻量且高内聚的 `.arc` 判定，让双轨机制的分类及计数分流在逻辑上达到完美的科学归宿。 |

## 8. 待确认事项（可选）
- **无**。
