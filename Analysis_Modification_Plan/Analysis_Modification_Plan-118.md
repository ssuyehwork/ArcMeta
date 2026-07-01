# Analysis_Modification_Plan-118: 托管库侧边栏自动同步与冗余清理方案

## 1. 任务背景
当前 ArcMeta 系统的物理托管文件夹（`ArcMeta.Library_X`）与侧边栏“我的分类”逻辑层级处于割裂状态。为了实现“物理驱动逻辑”的自动化管理目标，需建立托管库的自动感知、双向重命名同步机制，并彻底移除已过时的“未分类”冗余逻辑。

## 2. 问题定位
- **感知缺失**：`AutoImportManager` 目前仅负责文件入库，未对磁盘根目录的托管文件夹创建执行“逻辑分类自动同步”。
- **同步断裂**：`CategoryModel` 的重命名操作仅限于数据库字段更新，未联动磁盘 IO；`UsnWatcher` 捕获的重命名信号未反馈给 `CategoryRepo` 进行逻辑名更新。
- **冗余残留**：`CategoryModel`、`CategoryPanel` 和 `CategoryRepo` 中仍保留 ID 为 `-2` 的“未分类”逻辑，导致 UI 复杂且架构不纯粹。
- **绑定薄弱**：`categories` 表缺少物理唯一标识符（FID）字段，无法在文件夹改名后精准找回对应逻辑分类。

## 3. 强制对照表
| 用户原话 | 对应实现细节 |
| :--- | :--- |
| “当检测到物理磁盘根目录下创建了符合 ArcMeta.Library_X 命名的文件夹时，系统应自动在侧边栏“我的分类”中同步创建一个同名的逻辑分类。” | 在 `AutoImportManager` 捕获文件夹创建信号，判定为托管库后调用 `CategoryRepo::add`。 |
| “托管库物理文件夹作为“主分类”常驻侧边栏，支持“快速访问”固定。” | 托管库分类在 `CategoryModel` 构建时作为顶层节点，且 `pinned` 属性默认开启或允许用户操作。 |
| “逻辑 -> 物理：侧边栏改名自动触发磁盘 Rename。” | 重构 `CategoryModel::setData`，检测到分类改名时同步执行物理 `QDir::rename`。 |
| “物理 -> 逻辑：USN 感知到库内文件夹改名... 确认 IO 成功后再更新逻辑分类名。” | `UsnWatcher` 捕获 `USN_REASON_RENAME_NEW_NAME` 信号，通过 FID 匹配 `CategoryRepo` 中的记录并更新名称。 |
| “彻底移除“未分类”相关代码。” | 清理 `CategoryModel`（ID -2）、`CategoryRepo` 及右键菜单中的“回归未分类”逻辑。 |

## 4. 详细解决方案

### 4.1 数据库结构升级 (DatabaseManager)
- 在 `DatabaseManager.cpp` 的 `categories` 建表语句中新增 `folder_fid TEXT` 字段。
- 增加字段自动迁移逻辑，确保旧版数据库平滑升级。

### 4.2 物理驱动逻辑：自动感应创建 (AutoImportManager)
- 在 `AutoImportManager::onEntryAdded` 中，识别到 ParentFrn 为磁盘根目录且名称匹配 `ArcMeta.Library_X` 的文件夹时：
  1. 提取该文件夹的 128 位 FID。
  2. 调用 `CategoryRepo::add` 创建逻辑分类，并将 `folder_fid` 与该 FID 绑定。
  3. 标记该分类为 `pinned` 以符合常驻需求。

### 4.3 彻底去冗余 (Cleanup)
- **CategoryModel.cpp**: 移除 `addSystemItem("未分类", ...)`。
- **CategoryRepo.cpp/h**: 移除 `UNCATEGORIZED_CAT_ID` 及相关逻辑。
- **ContentPanel.cpp**: 移除右键菜单中的“回归未分类”操作（对应的 `actToUncat`）。

### 4.4 双向同步：逻辑 $\rightarrow$ 物理 (CategoryModel)
- 修改 `CategoryModel::setData`:
  1. 获取该分类绑定的 `folder_fid`。
  2. 若 FID 存在，通过 `MetadataManager` 获取物理路径。
  3. 执行物理重命名：`QDir().rename(oldPath, newPath)`。
  4. 只有磁盘操作成功后，才执行 `CategoryRepo::update`。

### 4.5 双向同步：物理 $\rightarrow$ 逻辑 (UsnWatcher & CategoryRepo)
- **UsnWatcher.cpp**: 捕获 `USN_REASON_RENAME_NEW_NAME` 信号。
- **CategoryRepo.cpp**: 新增 `updateNameByFid(fid, newName)`。
- **联动逻辑**: 当库内文件夹（通过 FID 在 `categories` 表中存在记录）发生物理改名，USN 捕获后调用 `updateNameByFid`，随后触发侧边栏 UI 异步刷新。

## 5. 修改边界
- `src/meta/DatabaseManager.cpp`: Schema 升级。
- `src/meta/CategoryRepo.h/cpp`: 增加 FID 支持及清理冗余。
- `src/core/AutoImportManager.cpp`: 增加文件夹感应逻辑。
- `src/mft/UsnWatcher.cpp`: 增强 RENAME 信号转发。
- `src/ui/CategoryModel.cpp`: UI 逻辑清理与 IO 联动。
- `src/ui/ContentPanel.cpp`: 菜单清理。

## 6. 实现准则与预警
- **成功即同步**：物理改名必须在 IO 确认成功后再更新数据库。若由于权限或占用导致物理重命名失败，UI 侧应回滚或提示，保持状态一致。
- **FID 唯一性**：严禁多个逻辑分类绑定同一个物理 FID。
- **性能预警**：USN 信号处理需保持轻量，UI 刷新应通过 `CategoryPanel` 的防抖机制执行。

## 7. Memories.md 合规检查
- [x] Jules 语言规范：全程使用中文。
- [x] 考古规范：1:1 还原旧版本重命名习惯（保留物理路径完整性）。
- [x] 去冗余规范：彻底废除 `pending_imports` 后，本方案进一步清理 `categories` 表中的系统保留 ID。
- [x] 视觉规范：改名同步后的 UI 刷新需确保不发生全量闪烁。
