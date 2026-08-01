# 全局误导性与非标准命名去误导化清理重构 —— Modification_Plan-19.md 
 
> 状态：已批准，执行中
 
--- 
 
## 1. 任务背景 
 
本方案承接自 `Modification_Plan-18.md`（旧编号），在确立了数据库同库同事务、物理包 semantic定位为 `.arc` 文件夹容器之后，为了**彻底根除并扩大排查全系统内因历史遗留而造成的非标准、具有强烈误导性的物理命名**，旨在进行一次高内聚、优雅的系统级整体去误导化重命名清理。 
 
--- 
 
## 2. 问题定位 
 
系统内散落的大量 `fileId`、`fid`、`fileFids`、`getFileIdSync` 等，在概念上会将人导向“物理文件（File）”，这与“物理托管单元本质上是文件夹容器（Folder）”这一黄金准则严重冲突，产生概念性误导。具体排查出的非标准堆砌节点如下： 
1. **结构体及局部变量**：`ItemMeta` / `ItemRecord` / `RuntimeMeta` 中的 `fileId128` / `fileId`； 
2. **底盘映射索引**：`m_fidToPath`、`m_fileNameToFids`、`m_folderNameToFids`、`m_extensionToFids`； 
3. **对外核心 API 接口**：`getPathByFid`、`getFileIdSync`、`getFileFidsByName`、`getFolderFidsByName`、`generateDeterministicSha256Id`、`generateFallbackFid`、`getVolumeFromFid`； 
4. **表结构表头**：分库数据库字段中由 Plan-18 全量更改后涉及的变量名配合。 
 
--- 
 
## 3. 强制对照表 
 
| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 | 
|------|---------------------|------------|----------| 
| 1    | "扩大范围排查哪些命名不标准导致误导性命名" | 扩大排查并全局清理 `fid` / `fileId` / `fileFids` 相关的 20 余处误导性成员、映射索引、对外函数名与临时变量，全部对齐升级为 `folderId` | ✅ | 
| 2    | "本方案承接自 Modification_Plan-18.md" | 任务背景中写明本方案承接自 `Modification_Plan-18.md`，旧文件永久作为历史讨论铁证，不作第二次修改，只自增新文件 | ✅ | 
 
--- 
 
## 4. 详细解决方案 
 
本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。 
 
### 4.1 修改点 A — `MetadataDefs.h` & `IndexedEntry.h` & `IndexedEntry.cpp`：物理成员彻底更名 
 
全量重命名结构体物理成员，名实相符： 
 
```diff 
// MetadataDefs.h 中更名： 
 struct ItemMeta { 
-    std::wstring fileId; 
+    std::wstring folderId; 
     // ... 
 }; 
 
 struct RuntimeMeta { 
     // ... 
-    std::string fileId128; 
+    std::string folderId; 
     // ... 
 }; 
``` 
 
```diff 
// IndexedEntry.h 中更名： 
 struct ItemRecord { 
-    std::string fileId; 
+    std::string folderId; 
     // ... 
 }; 
``` 
 
```diff 
// IndexedEntry.cpp 中更名： 
-    r.fileId = meta.fileId128; 
+    r.folderId = meta.folderId; 
``` 
 
### 4.2 修改点 B — `MetadataManager.h/.cpp`：底盘倒排映射索引与对位 API 大洗牌 
 
对所有的误导性倒排索引映射与核心对外函数名进行洗牌升级，名实相符： 
 
#### 1. 核心底盘成员变量洗牌： 
- `m_fidToPath` ➡️ **`m_folderIdToPath`** 
- `m_fileNameToFids` ➡️ **`m_assetNameToFolderIds`** 
- `m_folderNameToFids` ➡️ **`m_subFolderNameToFolderIds`** 
- `m_extensionToFids` ➡️ **`m_extensionToFolderIds`** 
 
#### 2. 核心对外方法洗牌： 
- `getFileIdSync` ➡️ **`getFolderIdSync`** 
- `getPathByFid` ➡️ **`getPathByFolderId`** 
- `getFileFidsByName` ➡️ **`getFolderIdsByName`** 
- `getFolderFidsByName` ➡️ **`getSubFolderIdsByName`** 
- `getFidsByExtension` ➡️ **`getFolderIdsByExtension`** 
- `generateDeterministicSha256Id` ➡️ **`generateDeterministicFolderId`** 
- `generateFallbackFid` ➡️ **`generateFallbackFolderId`** 
- `getVolumeFromFid` ➡️ **`getVolumeFromFolderId`** 
 
```diff 
// MetadataManager.h 核心接口更名： 
-    std::wstring getPathByFid(const std::string& fid); 
+    std::wstring getPathByFolderId(const std::string& folderId); 
 
-    std::string getFileIdSync(const std::wstring& path); 
+    std::string getFolderIdSync(const std::wstring& path); 
 
-    std::vector<std::string> getFolderIdsByName(const std::wstring& filename); 
+    std::vector<std::string> getFolderIdsByName(const std::wstring& filename); 
``` 
 
--- 
 
## 5. 修改边界声明【范围】 
 
**本次方案涉及范围：** 
- [ ] `src/meta/MetadataManager.h` & `MetadataManager.cpp`：重构对外 API 接口、清退所有的误导性 `fid` / `file` 索引命名。 
- [ ] `src/meta/MetadataDefs.h` & `src/core/IndexedEntry.h` & `IndexedEntry.cpp`：结构体成员变量名全量更正。 
- [ ] `src/meta/CategoryRepo.h` & `CategoryRepo.cpp`：同步更新，将对底层 folderId 与 getFolderIdSync 的调用变量名、接口参数重命名。 
 
**明确禁止越界修改的范围：** 
- [ ] 磁盘导航模式（`DiskNav`）相关逻辑 — 保持 100% 独立，不产生任何修改。 
 
--- 
 
## 6. 实现准则与预警【核心】 
 
1. **去误导彻底性**：此项改动不留任何概念混淆死角，所有的底层变量、映射和对外的接口全部同步洗牌升级为 folderId，消除多余误导。 
2. **高稳定不留死角**：此项重命名必须配合 Plan-18 分库重构一并进行，不能在半途中留存任何不一致的变量命名。 
 
--- 
 
## 7. Memories.md 合规检查 
 
| 组件 / 模式 | Memories.md 规范要求（具体内容） | 本方案是否符合 | 
|-------------|----------------------------------|----------------| 
| 双轨数据路由分流架构（第 1 节） | 托管库写入 SQLite，磁盘导航独占 `AmMetaJson` 读写至 cache 缓存，绝不污染物理文件夹 | ✅ 100% 符合 | 
| 数据源判定强类型契约（第 12 节） | 判定数据源必须统一通过 `isMirrorSource()` 或强类型进行识别 | ✅ 完全符合 | 
