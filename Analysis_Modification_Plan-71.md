# 代码逻辑分析报告 - ContentPanel 递归显示缓存机制

## 1. 问题背景
用户询问在 `ContentPanel` 中按下“显示子文件夹中的项目”执行递归操作后，系统是否进行了缓存。

## 2. 逻辑架构分析

### 2.1 文件列表加载逻辑 (ContentPanel::loadDirectory)
- **物理扫描**：当 `recursive` 参数为 `true` 时，程序通过 `QThreadPool` 启动后台线程执行 `scanDir`。
- **实时性**：`scanDir` 直接调用 `QDir::entryInfoList` 遍历磁盘物理目录。
- **非持久化**：扫描结果存储在 `FerrexVirtualDbModel` 的内存向量 `m_allRecords` 中。
- **清除机制**：每次加载前会调用 `m_model->clear()`，意味着之前的路径列表被丢弃。
- **结论**：**文件路径列表本身没有缓存**，每次开启递归都是对磁盘的重新物理读取。

### 2.2 元数据管理逻辑 (MetadataManager & FerrexVirtualDbModel)
- **视图层缓存**：`FerrexVirtualDbModel` 内部持有 `m_metaCache` (QCache, 1000条)，缓存已加载项的属性（星级、标签等）。
- **全局内存缓存**：`MetadataManager` 维护 `m_cache` (std::unordered_map)，存储从数据库或 `.am_meta.json` 加载的运行时元数据。
- **结论**：**文件属性数据存在多级内存缓存**。

### 2.3 资源缓存逻辑
- **图标缓存**：`m_iconCache` (500条) 存储图标和缩略图，避免重复执行系统 shell 转换。
- **宽高比缓存**：`m_aspectRatios` 存储图片比例。

### 2.4 数据库交互 (ItemRepo)
- 在递归扫描过程中，数据库仅作为元数据持久化的“冷库”，**不参与**文件列表扫描结果的存储或缓存。

## 3. 总结答复
按下递归按钮后，**文件路径列表是实时扫描的（无缓存）**，而**文件的属性、图标和缩略图则存在高效的内存缓存**。这种设计确保了列表的准确性（与磁盘同步）和交互的流畅性。
