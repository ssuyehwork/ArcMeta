# Analysis_Modification_Plan-6.md - 深度架构审计与性能分析报告

## 1. 内存机制确认 (ScanDialog 与 侧边栏)

### 1.1 ScanDialog 的全量内存化
经审计 `ScanDialog.cpp` 与 `MftReader.cpp`，确认其采用了 **SoA (Structure of Arrays)** 架构：
- **机制**：启动时异步将所有已缓存盘符的 `.scch` 索引载入内存（`m_frns`, `m_sizes`, `m_string_pool` 等 std::vector）。
- **结论**：是的，它确实将所有盘符的元数据存入内存以实现瞬间搜索。
- **回收**：窗口关闭时通过 `vector::swap` 技巧物理回收内存，设计非常科学。

### 1.2 MainWindow 侧边栏的元数据汇聚
经审计 `CategoryRepo.cpp` 与 `MetadataManager.cpp`：
- **机制**：启动时全量加载所有监控目录下的 `metadata.scch`（用户定义的标签、评分等）。
- **原因**：这是为了支持“今天”、“未分类”等智能分类的**实时准确计数**。如果不全量载入，侧边栏数字刷新将面临巨大的磁盘 IO。

## 2. MainWindow 核心卡顿原因分析 (复诊报告)

即便在 `origin/main` 的更新版本中，主界面仍然卡顿假死，原因在于目前的优化只是“治标不治本”：

### 2.1 掩耳盗铃式的“数据屏蔽”
- **现状**：代码在 `FerrexVirtualDbModel::data()` 中将大小和日期硬编码返回为 `"-"`。
- **评价**：这仅是躲避了这两列的 `QFileInfo` 调用，属于**功能退化**。用户无法看到基本信息，且核心架构并未改变。

### 2.2 残留的致命同步 IO 调用
1. **IsEmptyRole (ContentPanel.cpp:197)**：`return info.isDir() && QDir(path).isEmpty();`
   - **后果**：渲染每一个文件夹项时，UI 线程都会同步扫描磁盘目录。这是“离谱假死”的直接来源。
2. **筛选逻辑性能自杀**：`FilterProxyModel::filterAcceptsRow` 依然在主线程内针对每一行数据重复构造 `QFileInfo` 来查询后缀和日期。

## 3. 终极改进方案：物理脱钩架构

主界面必须向 `ScanDialog` 的工业级标准看齐，执行“数据先行”重构：

1. **ItemRecord 全属性化**：
   ```cpp
   struct ItemRecord {
       QString path;
       bool isDir;
       bool isEmpty; // 后台扫描阶段预取
       long long size; // 后台扫描阶段预取
       long long mtime; // 后台扫描阶段预取
       QString extension;
       // ... 其他内存化字段
   };
   ```
2. **零 IO 渲染红线**：
   - 彻底废除 `data()` 函数中的 `QFileInfo` 调用。
   - 彻底废除 `filterAcceptsRow` 中的磁盘访问逻辑。
   - 所有的 UI 展示和过滤必须**仅读取内存中的 `ItemRecord` 数组**。

---
**旁观者审计结论**：更新后的版本并未解决“UI 渲染深度依赖磁盘 IO”这一根本矛盾。只有实现真正的 **SoA 数据驱动**，主界面才能获得与 `ScanDialog` 同样的丝滑感。
