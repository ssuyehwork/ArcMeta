# Analysis_Modification_Plan-126.md

## 1. 任务背景
在 ArcMeta 系统中，为了实现极致的性能与数据一致性，需将托管库（`ArcMeta.Library_`）的监控机制由“双轨制”收拢为“单一 USN 主轨”。同时，确立托管库分支在侧边栏分类树中与物理磁盘目录的 **1:1 绝对镜像**关系，废除一切非物理驱动的逻辑脑补。

## 2. 问题定位
*   **冗余监控**：`CoreController.cpp` 中仍存在 `NativeFolderWatcher` (IOCP) 的初始化，造成了重复的信号处理和资源浪费。
*   **过滤开销**：`AutoImportManager.cpp` 当前采用全量字符串路径比对，在处理全卷 USN 信号时性能压力较大。
*   **同步滞后**：目前的分类创建（`handleRecursiveIngestion`）主要由启动对账触发，缺乏基于 USN 实时变动的“增量镜像同步”。
*   **职责偏移**：部分 UI 逻辑（如收揽入库）可能尝试直接触发表登记，违反了“位移驱动入库”的单向链条。

## 3. 强制对照表

| 序号 | 需求原文（用户指令） | 详细解决方案 | 是否对齐 |
| :--- | :--- | :--- | :--- |
| 1 | “使用 USN Journal 监控整个卷，但在解析阶段只保留路径属于 ArcMeta.Library_[盘符] 文件夹及其子目录的事件” | 在 `AutoImportManager` 中实现基于 FRN 链条或高效路径前缀的全局拦截。 | ✅ |
| 2 | “我要的就是结构上的 1:1 复刻 (Mirroring)” | `CategoryRepo` 增加物理关联逻辑，确保托管库分支的分类 ID 严格对应物理 FRN，名称与层级动态同步。 | ✅ |
| 3 | “既然业务逻辑和生命周期管理上，它确实是专门围绕 “ArcMeta.Library_盘符” 文件夹设计的...完全可以胜任” | 确立“前哨（Native Watcher）+ 底线（USN Journal）”的协作（注：用户后续方针调整为单一 USN 主轨，已在下方体现）。 | ✅ |
| 4 | “既然业务逻辑是局部的...改变方针：使用 USN Journal 监控整个卷...其他文件夹的事件全部丢弃” | 彻底废除 NativeFolderWatcher，转向单一 USN 驱动。 | ✅ |

## 4. 详细解决方案

### 4.1 监控链路收拢（彻底去冗余）
*   **清理入口**：修改 `src/core/CoreController.cpp`，移除 `NativeFolderWatcher::instance().addWatch()` 的所有调用循环。
*   **停止服务**：确保 `NativeFolderWatcher` 不再被实例化或初始化，彻底释放 IOCP 线程池资源。

### 4.2 高效全卷过滤算法（USN 降噪）
*   **逻辑层级过滤**：在 `AutoImportManager::onEntryAdded` 和 `onEntryUpdated` 中：
    1.  **快速排除**：利用 `MftReader` 缓存，获取当前变动项的 `ParentFileReferenceNumber`。
    2.  **树级索引判定**：检查该 Parent FRN 是否属于已记录的托管库 FRN 树分支。
    3.  **静默丢弃**：凡是不在托管库路径范围内的 USN 记录，在进入数据库事务前立即丢弃，确保系统不会被系统分区或其他盘符的琐碎变动淹没。

### 4.3 1:1 物理镜像同步逻辑（镜像铁律实现）
*   **增量分类创建**：当监听到文件夹创建（`USN_REASON_FILE_CREATE`）且路径位于托管库内时：
    - 立即调用 `CategoryRepo::add`，物理 FRN 映射至该分类，名称直接复刻物理文件夹名。
*   **原子重命名/位移**：当监听到文件夹重命名或移动时：
    - 根据物理 FRN 定位分类，同步更新其 `name` 和 `parentId`。
    - 严禁逻辑层产生“幽灵分类”，物理目录消失，对应的镜像分类必须同步逻辑删除（或标记失效）。

### 4.4 反向触发入库闭环（数据一致性红线）
*   **收揽功能物理化**：修改 UI 侧的“迁移/入库”逻辑，仅负责执行物理位移操作（`QFile::rename`）。
*   **USN 响应式登记**：文件的元数据登记（`MetadataManager::registerItem`）**只能**由 `AutoImportManager` 在捕获到 USN 物理变动信号后发起。

## 5. 修改边界
*   `src/core/CoreController.cpp`：移除 Native Watcher 初始化逻辑。
*   `src/core/AutoImportManager.cpp`：实现基于 FRN 链的全卷 USN 过滤与镜像分类实时同步。
*   `src/meta/CategoryRepo.cpp`：增强物理 FRN 与逻辑分类的关联 API。
*   `src/ui/ContentPanel.cpp` 等：清理手动触发入库的冗余 UI 调用。

## 6. 实现准则与预警
*   **最高准则**：严禁逻辑层“脑补”任何物理磁盘不存在的关系。
*   **性能预警**：全卷监控期间，对系统盘大量琐碎变动的过滤必须在 `MftReader` 内存层级完成，严禁频繁查询磁盘。
*   **锁安全**：USN 驱动的分类创建涉及数据库写锁，需确保不会与 UI 渲染线程产生死锁。

## 7. Memories.md 合规检查
*   [x] 唯一数据入库红线：符合。
*   [x] 托管库镜像铁律：符合。
*   [x] 1:1 复刻优先原则：符合。
*   [x] 彻底废除 NativeFolderWatcher：符合。
*   [x] 托管库监控单一主轨规约：符合。
