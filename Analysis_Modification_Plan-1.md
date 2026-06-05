# 侧边栏分类逻辑架构分析与修改方案 (CategoryRepo.cpp)

## 1. 逻辑架构分析

当前版本的 `CategoryRepo`（位于 `src/meta/CategoryRepo.cpp`）已全面转向 **Binary-SCCH v3** 架构，彻底废弃了 SQLite 数据库。其核心逻辑架构如下：

### 1.1 存储机制
- **持久化文件**：使用全局唯一的 `arcmeta_categories.scch` 文件。
- **二进制序列化**：通过 `QDataStream` 实现。文件头包含 Magic Code `CATS` 和版本号（Version 3）。
- **读写策略**：采用“全量加载-全量保存”模式。每次 `add`、`update` 或 `remove` 操作都会触发 `loadAll` 加载完整数据到内存，修改后再通过 `saveAll` 重写整个文件。

### 1.2 数据模型
- **Category (分类实体)**：存储分类的元属性，如 ID、父 ID、名称、颜色、预设标签、排序权重以及加密状态。
- **CategoryItemRecord (关联记录)**：存储文件与分类的映射关系。关键在于使用 `fileId128`（物理指纹）而非物理路径作为关联键，以应对文件移动。

### 1.3 核心逻辑与耦合
- **物理校验逻辑**：在 `getFileIdsInCategory` 和 `getCounts` 中，系统不仅检查关联记录，还通过 `MetadataManager` 进行物理存在性校验。只有在内存缓存中存在且物理路径有效的项目才会被计入显示和统计。
- **系统分类生成**：如“今日数据”、“未分类”等，并非持久化记录，而是基于 `MetadataManager` 内存镜像的实时扫描计算。

---

## 2. 预期偏差原因分析 (为何“不达预期”)

根据代码分析，侧边栏表现异常（如计数不准、项显示不全）的根源可能在于以下几点：

1.  **物理校验的副作用**：
    `getFileIdsInCategory` 强依赖于 `MetadataManager::instance().getPathByFid`。如果 `MetadataManager` 的内存缓存未完成预热，或者物理磁盘上的文件被移动/重命名但 USN 日志尚未同步更新，分类中的项会因“物理不可达”而在 UI 上消失，即使关联记录在 `.scch` 中是正确的。
2.  **全量读写的性能瓶颈**：
    随着用户关联的文件增加，`CategoryItemRecord` 的数量会迅速膨胀。每次修改分类（甚至是修改一个颜色）都要全量序列化数万条关联记录，会导致 UI 明显的卡顿感。
3.  **File ID 的不稳定性**：
    `fileId128` 的生成依赖于磁盘卷序列号和 FRN（File Reference Number）。在跨驱动器移动或某些特殊文件系统下，如果 ID 生成逻辑不一致，会导致分类关联失效。
4.  **内存镜像不同步**：
    `CategoryRepo` 逻辑分散在 `src/db/` 和 `src/meta/` 两个目录下，虽然目前看来 `src/meta/` 是主力，但旧代码残留可能导致逻辑混淆。

---

## 3. 详细修改方案

### 3.0 统一代码基座 (优先级：最高)
- **方案**：彻底移除 `src/db/` 目录及其下的所有实现。
- **深度分析**：
    - `src/db/CategoryRepo.cpp` 使用的是 **JSON 序列化** 逻辑，虽然文件后缀也叫 `.scch`，但其内部是通过 `QJsonDocument` 读写的。
    - `src/meta/CategoryRepo.cpp` 使用的是 **QDataStream 二进制** 逻辑，这是项目当前标准的 **Binary-SCCH v3**。
    - **混淆风险**：两个目录下存在完全同名的命名空间 `ArcMeta` 和类名 `CategoryRepo`。如果构建系统（如 CMake）配置不慎，可能会导致链接到错误的二进制实现，从而引发数据损坏（尝试用 JSON 解析二进制文件，或反之）。
    - **结论**：`src/db/` 是架构演进中的“死代码”残留，必须在下一次物理清理中彻底物理删除。

### 3.1 分布式 FRN 登记与物理追踪 (逻辑确认)
- **分析结论**：目前的拖拽（CategoryPanel）与深度扫描（ContentPanel）逻辑已经正确集成了 FRN 登记机制。
- **工作流确认**：
    1.  **触发点**：当文件夹被拖入分类或执行右键“扫描数据”时。
    2.  **文件生成**：系统会在目标目录递归生成 `metadata.scch`。
    3.  **登记逻辑**：在保存 `metadata.scch` 后，代码会通过 `fetchWinApiMetadataDirect` 提取该文件的 NTFS FRN（物理身份），并立即调用 `AllFrnManager::registerFrn`。
    4.  **持久化**：FRN 与物理路径的映射关系会被同步写入全局的 `All_FRN_metadata.scch` 索引。
- **优化建议**：目前登记逻辑散落在 `CategoryPanel` 和 `ContentPanel` 的不同函数中，建议将其封装到 `MetadataManager::syncPhysicalMetadata` 的统一出口，实现“保存即登记”的硬核逻辑一致性。

### 3.2 引入增量缓存与延迟写入
- **方案**：在 `CategoryRepo` 内部维护一个静态单例缓存（如 `std::vector` 或 `QMultiMap`），在程序启动时加载一次。
- **目标**：将 `add/update/remove` 操作改为先更新内存，再通过 `debounce`（防抖）定时器异步触发 `saveAll`，避免频繁的 IO 操作。

### 3.2 优化物理校验策略
- **方案**：将“物理校验”与“数据查询”解耦。
- **细节**：
    - 在 UI 层显示时，先展示所有已记录的项（即使物理路径暂不可知）。
    - 引入“幽灵项”状态：对于已记录但物理路径未匹配的项，在 UI 上以灰度显示，而非直接剔除。
    - 在 `MetadataManager` 完成后台扫描后再触发分类树的局部刷新。

### 3.3 系统分类逻辑重构
- **方案**：优化 `getSystemCounts` 性能。
- **细节**：不再在每次刷新 UI 时遍历全量缓存。在 `MetadataManager` 发生 `metaChanged` 信号时，只针对变更项增量更新系统分类计数。

### 3.4 清理冗余代码
- **方案**：彻底移除 `src/db/` 目录下的所有 `CategoryRepo` 及其相关文件，确保全局只有 `src/meta/` 下的 SCCH 逻辑在运行，消除二义性。

### 3.5 强化 File ID 鲁棒性
- **方案**：在 `CategoryItemRecord` 中增加 `path_hint`（路径提示）。
- **细节**：当 `fileId128` 匹配失败时，尝试通过路径提示进行找回，如果文件确实在该位置且生成了新 ID，则自动修复关联记录。
