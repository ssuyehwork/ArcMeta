# 全应用误导性命名排查与架构解耦方案 —— Modification_Plan-18.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在项目快速演进和高并发双轨架构重构中，部分代码在职责、物理形态或位置变更后，其命名与物理设计未能及时同步演进，导致代码库中残留了大量语义含混、命名不符、甚至方向性倒置的“误导性命名”问题。根据用户的指令（对应用户原话：“请排查整款应用存在“误导性命名”的问题”），本方案对全应用代码资产进行了一次拉网式走查，系统总结并精确定位出 7 大核心误导性命名和不合理物理错位，并为下一步的执行者角色提供详尽、无歧义的架构整改和命名重构设计图纸。

## 2. 问题定位
经过对全应用代码的排查，我们定位出以下 7 处具有高度误导性的命名和物理组织问题：

1. **`UiHelper` (名不副实，上帝类误导)**：
   - **问题定位**：名义上是 `UiHelper`（样式/图像纯轻量渲染辅助类），实际却是一个集成了 Win32 COM 接口系统缩略图提取、CIE76 显著色板提取重型计算、多线程 QThreadPool 异步任务调度、以及静态共享图标缓存的高风险“多媒体提取与异步调度总线”。
2. **`file_id`/`fileId` 混淆 `.arc` 容器本质 (资产单位语义含混)**：
   - **问题定位**：托管库受控的每一个资产在物理上都是一个 `.arc` 文件夹容器（Folder），并非单文件（File）。然而，许多底层及 UI 成员变量名、SQL 主外键依旧写作 `file_id` 或 `fileId`。
3. **`CategoryRepo` 中的 `getFileIdsInCategory` 与 `getFileIdsRecursive` (方法名与内部实现矛盾)**：
   - **问题定位**：方法名为 `getFileIds...`，但其内部代码逻辑完全在处理并返回已重名后的 `folderId` 集合，与现有的 `folder_id` 物理语义彻底割裂，具有极大误导性。
4. **`DatabaseManager::getMemoryDb` (多盘符分库语义重合)**：
   - **问题定位**：该方法实际返回的是指定物理磁盘（Drive/Partition）分区的 SQLite 数据库连接。由于全局库 `global.db` 在内存中同样是 `:memory:`（通过 `m_globalDb.memDb` 连接），将分盘分库方法命名为 `getMemoryDb`，抹杀了其最核心的“分盘符/分区”业务属性。
5. **`IndexedEntry.h/.cpp` (重型 UI 核心 `ItemRecord` 与底层 MFT 扫描实体揉杂)**：
   - **问题定位**：文件名叫 `IndexedEntry`，但内部却同时塞入了全系统最核心的 UI 虚拟化展示条目 `ItemRecord`。这使得几乎所有 UI Panels 和加载服务（如 `ContentPanel.h`、`DiskScanService.h` 等）在需要使用 `ItemRecord` 时，不得不引入极其误导、本应被物理隔离的 `#include "../core/IndexedEntry.h"` 底层 MFT 扫描头文件。
6. **`DiskScanService` 与 `CategoryLoadService` 的物理路径错位 (核心业务逻辑位于 `src/ui`)**：
   - **问题定位**：这两个 Service 属于纯粹的高性能物理提取、解包与逻辑分流服务，100% 不依赖任何 QWidget 或视图渲染。但它们目前被物理存放在 `src/ui/` 目录下，极具误导性，混淆了 MVC 和双轨隔离边界。
7. **`CategoryRepo::syncPhysicalDirectoryCascade` (仓储层直接承接重型物理 I/O 驱动)**：
   - **问题定位**：`CategoryRepo` 作为逻辑分类持久化仓库，却承接了重型 I/O 的磁盘 DFS 扫描与对账工作，方法名和放置点与“逻辑数据库仓储”的单一职责不符。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 排查整款应用存在“误导性命名”的问题 (对应用户原话："请排查整款应用存在“误导性命名”的问题") | 4.1 节重定义 `UiHelper`、4.2 节对 `folderId` 执行语义完全重构、4.3 节对 `CategoryRepo` 接口纠偏 | ✅ 一致 |
| 2    | 纠正混淆文件与文件夹的命名 (对应用户原话："请排查整款应用存在“误导性命名”的问题") | 4.2 节将所有的残留 `fileId` 全部正名为 `folderId`，澄清 `.arc` 容器物理本质 | ✅ 一致 |
| 3    | 纠正重型实体与低层 MFT 绑定的头文件误导 (对应用户原话："请排查整款应用存在“误导性命名”的问题") | 4.5 节对 `ItemRecord` 与 `IndexedEntry` 进行物理拆分，创建独立文件 | ✅ 一致 |
| 4    | 纠正业务服务物理路径错位 (对应用户原话："请排查整款应用存在“误导性命名”的问题") | 4.6 节将纯逻辑/物理扫描服务从 `src/ui/` 迁移至 `src/core/` 并重编编译系统 | ✅ 一致 |

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 `UiHelper` 名不副实与重型组件提取
- **重构方案**：将 `UiHelper` 从单一的“全能类”进一步归口和命名规范化。
  - 保留 `UiHelper` 用于纯 UI 渲染相关的静态辅助逻辑（如 `StylePainter` 等）。
  - 将与多媒体、系统图标提取、COM 通信相关的静态逻辑彻底剥离并统一命名为 `ShellIconManager`，彻底消除由于 static 延迟加载导致的析构不安全和误导性命名。

### 4.2 `fileId`/`file_id` 残留完全正名为 `folderId`/`folder_id`
- **重构方案**：
  - 遍历底层 Ingestion 和 Metadata 读写中所有的 `fileId`/`fileId128` 临时命名，统一重命名为 `folderId`；
  - 保证在 C++ 数据结构（如 `ItemRecord`、`FolderMeta`、`ItemMeta` 等）及关联 SQL 语句中，所有的主外键列和 C++ 成员变量名高度内聚、完美对应，杜绝在同一行代码中混用 `fileId` 与 `folderId`。

### 4.3 `CategoryRepo` 接口命名纠偏
- **重构方案**：
  - 将 `CategoryRepo::getFileIdsInCategory` 重命名为 `CategoryRepo::getFolderIdsInCategory`；
  - 将 `CategoryRepo::getFileIdsRecursive` 重命名为 `CategoryRepo::getFolderIdsRecursive`；
  - 精准修正调用方（如 `CategoryPanel.cpp` 等）的调用。
  
  *Git merge diff 示例*：
  ```
  <<<<<<< SEARCH
      static std::vector<std::string> getFileIdsInCategory(int categoryId);
      static std::vector<std::string> getFileIdsRecursive(int categoryId);
  =======
      static std::vector<std::string> getFolderIdsInCategory(int categoryId);
      static std::vector<std::string> getFolderIdsRecursive(int categoryId);
  >>>>>>> REPLACE
  ```

### 4.4 `DatabaseManager::getMemoryDb` 语义精确化
- **重构方案**：
  - 将 `DatabaseManager::getMemoryDb` 重命名为 `DatabaseManager::getDriveDb`，使开发者一目了然其代表的是“特定驱动器/分区对应的数据库连接句柄”，与其最本质的业务属性对齐。
  - 修改 `TagRepository.cpp`、`DatabaseManager.cpp` 等中所有的调用链。

### 4.5 `ItemRecord` 与 `IndexedEntry` 物理拆分与独立头文件
- **重构方案**：
  - 将 `struct ItemRecord` 彻底从 `IndexedEntry.h / .cpp` 中剥离出来。
  - 新建 `src/core/ItemRecord.h` 与 `src/core/ItemRecord.cpp`，将 `ItemRecord` 完整迁移过去。
  - 原 `IndexedEntry.h` 仅保留纯粹用于 MFT 扫描的 `struct IndexedEntry` 的定义。
  - 替换所有 UI 文件中的 `#include "IndexedEntry.h"` 替换为极其干净、语义准确的 `#include "ItemRecord.h"`，阻断头文件依赖导致的语义误导。

### 4.6 `DiskScanService` 与 `CategoryLoadService` 物理路径重构
- **重构方案**：
  - 将这两个纯核心逻辑、无 UI 渲染的隔离服务文件从 `src/ui/` 目录下物理移动至 `src/core/` 目录中。
  - 物理迁移路径：
    - `src/ui/DiskScanService.h` -> `src/core/DiskScanService.h`
    - `src/ui/DiskScanService.cpp` -> `src/core/DiskScanService.cpp`
    - `src/ui/CategoryLoadService.h` -> `src/core/CategoryLoadService.h`
    - `src/ui/CategoryLoadService.cpp` -> `src/core/CategoryLoadService.cpp`
  - 更新项目构建配置 `CMakeLists.txt` 中的源文件配置，确保编译顺利通过。

### 4.7 `CategoryRepo::syncPhysicalDirectoryCascade` 架构内聚重构
- **重构方案**：
  - 将 `syncPhysicalDirectoryCascade` 从 `CategoryRepo` 中剥离出来，下沉并委托给更符合职责的 `CoreController` 的后台同步服务中，或者封装至独立的 `DatabaseSynchronizer` 服务中，在 Repo 层只保留纯粹的 CRUD 式 DML 仓储层，使命名和物理归口保持高内聚。

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/meta/CategoryRepo.h` / `src/meta/CategoryRepo.cpp` (方法命名纠偏)
- [ ] 模块/文件：`src/meta/DatabaseManager.h` / `src/meta/DatabaseManager.cpp` (getMemoryDb 重命名为 getDriveDb)
- [ ] 模块/文件：`src/core/IndexedEntry.h` / `src/core/IndexedEntry.cpp` (拆分出 `ItemRecord.h/.cpp`)
- [ ] 模块/文件：物理移动并重构 `src/ui/DiskScanService.*` 与 `src/ui/CategoryLoadService.*` 至 `src/core/`
- [ ] 模块/文件：`CMakeLists.txt` (更新编译文件路径)

**明确禁止越界修改的范围：**
- [ ] 底层 USN 日志流及 MFT 扫描核心读取：`src/mft/MftReader.h/.cpp` —— 不修改
- [ ] 加密模块：`src/crypto/EncryptionManager.h/.cpp` —— 不修改

## 6. 实现准则与预警【核心】
1. **防止编译链阻断**：由于 `ItemRecord` 的物理文件被拆分，所有的 UI Panel 和 View 都会涉及头文件更改。执行者在物理迁移时必须使用全局扫描，将所有 `#include "IndexedEntry.h"` 精准替换为 `#include "ItemRecord.h"`。
2. **保持向后兼容性**：在 `AmMetaJson` 读写磁盘 JSON 缓存时，必须继续映射 `"file_id_128"`，以保持与旧版本磁盘元数据的完美兼容。但在内存中其变量一律使用 `folderId`。
3. **构建系统更新**：物理移动文件到 `src/core/` 后，执行者必须立即更新主构建脚本（`CMakeLists.txt`），删除原 `src/ui/...` 节点并添加 `src/core/...` 节点，并立即执行编译验证，防止产生“找不到源文件”的编译错误。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|--------------------------------------------|----------------|
| 双轨标记落盘路由 | 托管库写入 SQLite 数据库，库外磁盘导航写入 `ArcMeta.cache` JSON 离散缓存。 | ✅ 符合。本重构彻底澄清了 `fileId` 与 `folderId` 的命名混淆，确保落盘逻辑更加清晰无误。 |
| 统一数据来源判断复用 | 全应用必须统一且复用 `isMirrorSource()` 判别视图数据来源属性。 | ✅ 符合。本次仅纠正误导性命名，不修改 `isMirrorSource()` 语义逻辑。 |
| 缩略图平滑加载规范 | 异步加载期间 `data()` 返回空图标 `QIcon()`，杜绝闪烁。 | ✅ 符合。物理拆分 `ItemRecord` 时，保持其作为数据模型的轻量属性，不影响 Delegate 加载。 |

## 8. 待确认事项（可选）
- **无**。
