# Analysis and Modification Plan - 79

## 1. 深度清算：残留架构与逻辑分析

经过对 `src` 目录的全面扫描，虽然物理上的 SQLite 数据库文件和 JSON 解析库调用已被移除，但代码库中仍存在明显的“心理残留”与“命名惯性”，这些残留会干扰后续的维护并误导架构理解。

### 1.1 命名语义残余 (Semantic Residue)
- **UI 模型命名**: `src/ui/ContentPanel.h` 和 `.cpp` 中依然定义了 `FerrexVirtualDbModel` 类，且类注释为“虚拟化数据库模型”。
- **变量命名惯性**:
    - `src/ui/CategoryPanel.cpp` 第 1125 行：变量名 `parentIdInDb`。
    - `src/ui/ContentPanel.cpp` 中多处注释提到“数据库搜索”。
- **UI 文本提示**: `src/core/CoreController.cpp` 及 `ContentPanel.cpp` 的 log 输出仍在使用 `(SCCH模式)` 与旧逻辑混用的描述。

### 1.2 物理文件残余 (Physical Artifacts)
- **分布式 JSON 镜像**: 源码目录下及各个子文件夹中仍存在大量的 `.am_meta.json` 文件。这些文件曾是“旧版本-1”的核心，但在当前的全二进制 SCCH 架构下已完全失去作用，且容易导致用户混淆。
- **全局映射残留**: 根目录下的 `All_FRN_am_meta.json` 依然存在，应由 `AllFrnManager` 的二进制版本彻底取代。

### 1.3 逻辑注释残余 (Dead Code/Comments)
- `MetadataManager.cpp`: 包含大量“2026-06-xx 彻底废除...逻辑”的注释。这些注释虽然起到了说明作用，但也让代码显得冗余。
- `MetadataManager.h`: 保留了 `removeFidsFromLog`, `addToSyncLog`, `saveSyncLog` 等空函数，这些是为了兼容 UI 调用而留下的“盲肠”。

---

## 2. 详细修改建议方案

### 2.1 语义重构（去除 "Db" 烙印）
- **类名更名**: 将 `FerrexVirtualDbModel` 统一重命名为 `FerrexVirtualScchModel`。
- **变量清洗**:
    - `parentIdInDb` -> `parentCategoryId`
    - `db_dist` -> `scch_dist` (在颜色计算逻辑中)
- **UI 字符串对齐**: 将所有 log 打印中的 "Db" 字样替换为 "SCCH" 或 "Binary Engine"。

### 2.2 物理环境清理（清空 JSON）
- **执行全局删除命令**: 建议执行 `find . -name ".am_meta.json" -delete` 彻底移除源码中的旧格式文件。
- **移除全局映射**: 删除项目根目录下的 `All_FRN_am_meta.json`，确保 `AllFrnManager` 仅依赖 `all_frns.scch` 或内存重建。

### 2.3 逻辑接口精简
- **移除盲肠函数**:
    - 在 `MetadataManager.h` 中彻底删除 `hasPendingSync`, `getPendingSyncDirs`, `removeFidsFromLog`, `addToSyncLog`, `saveSyncLog` 的声明。
    - 在 `ContentPanel.cpp` 和 `MainWindow.cpp` 中搜索并移除对上述空函数的调用（如果存在）。
- **清理过渡注释**: 移除那些标记“已废除”但不再具有参考价值的代码块，保持代码的工业级纯净度。

### 2.4 根目录存储策略对齐
- 根据 **Plan-78** 的结论，驱动器根目录的元数据应由“全局二进制快照 (Global Snapshot)”接管，而非旧版的 `am_meta.json`。
- 建议在 `MetadataManager::persistAsync` 中正式补全针对根目录的二进制写入逻辑。

## 3. 结论
目前的系统已在核心逻辑上完成了从 DB/JSON 到 SCCH 的跃迁，但要达到“资深程序员”级别的代码质量，必须进行上述的语义与物理清理。这不仅是代码风格的问题，更是确保“全二进制化”架构不被后续误操作破坏的关键。
