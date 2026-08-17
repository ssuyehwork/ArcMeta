# 系统架构规范与实施计划 (Architecture and Planning)

## 1. 内存模式下“批量重命名”逻辑架构规范 (In-Memory Batch Rename Specification)

### 1.1 核心设计理念
内存模式下的批量重命名必须遵循**事务隔离性与拓扑解耦**原则，彻底解决 Version-1 中因为顺序更新导致的数据覆盖与逻辑混乱。同时，重命名必须同步更新内存与数据库中的 `base_name`（显示名称）与 `ext`（后缀），确保界面（ContentPanel）实时无缝更新。

### 1.2 运行流水线
1. **变更快照 (Change Set Isolation)**：所有重命名请求先进入 `BatchRenameTransaction` 内存暂存区，禁止直接在主内存 Map 上原位修改（In-place Mutation）。
2. **三步冲突与依赖预检 (Pre-check & Dependency Graph)**：
   - **重名冲突校验**：检查新名称集合内部及与非变更实体的冲突。
   - **依赖拓扑排序 (Topology Sort)**：识别链式重命名（如 `A -> B, B -> C`）与循环重命名（如 `A -> B, B -> A`）。
   - **解耦处理**：链式变更按逆序执行；循环变更引入内存临时 UUID 进行三步交换。
3. **原子提交与物理更新 (Atomic Commit)**：拓扑校验通过后，物理磁盘改名，并一次性更新实体指针与双向索引表 (`Map<ID, Entity>` + `Map<Name, ID>`) 及数据库 `path` / `base_name` / `ext`。

---

## 2. 重新扫描与缩略图提取流水线 (Rescan & Thumbnail Pipeline)

### 2.1 重新扫描流水线 (Rescan Pipeline Architecture)
[用户触发“重新扫描该库”]
          ↓
[1. 文件系统/胶囊目录深度遍历 (Directory Walker)]
          ↓
[2. 更新内存/数据库文件索引表 (Metadata Indexer)]
          ↓
[3. 缩略图缺失检测器 (Thumbnail Missing Detector)]
          ↓
[4. 自动投递至后台生成队列 (Batch Thumbnail Worker Queue)]
          ↓
[5. 图像解码与缩放处理 (Decoder & Downsampler)]
          ↓
[6. 写入缓存区并通知 UI 刷新 (Cache Sync & Broadcast)]

### 2.2 胶囊文件夹 (Capsule Folder) 降级兼容策略
- **纯原图场景支持**：当扫描器检测到胶囊文件夹内仅存在图形图像原文件，缺少专属元数据或现有缩略图缓存时，不得将其判定为无效/坏胶囊。
- **自动触发 Fallback 机制**：系统自动启用降级提取协议（Fallback Pipeline），将该胶囊内所有支持的图像原文件列入待提取清单，全量生成标准缩略图（如 256x256 / 512x512 WebP/PNG 缓存）。

---

## 3. 磁盘导航模式父文件夹 SHA-256 缩略图隔离架构规范 (Disk Navigation SHA-256 Subdirectory Specification)

### 3.1 核心设计理念
为解决磁盘目录导航模式下单一 `.arcmeta/disk_thumbs/` 根目录文件数量过多导致的物理 I/O 检索性能衰减问题，系统采用**父文件夹路径 SHA-256 哈希隔离**策略。

### 3.2 存取与计算规范
1. **父文件夹哈希计算**：
   对于磁盘上的任意素材文件（如 `D:\photos\t 图片\1.jpg`），先提取其规范化的父文件夹路径（`D:/photos/t 图片`），计算其 SHA-256 哈希值 `FolderHash`（64 位 Hex 字符串）。
2. **专属存储桶目录**：
   系统在 `.arcmeta/disk_thumbs/` 下自动创建专属子目录：
   `.arcmeta/disk_thumbs/<FolderHash>/`
3. **文件缩略图 Key 生成**：
   单个文件的缩略图缓存文件名基于文件路径、大小、修改时间与尺寸生成 SHA-256 或唯一 Hash 字符串 `FileHash.png`。
4. **最终物理路径**：
   `.arcmeta/disk_thumbs/<FolderHash>/<FileHash>.png`

### 3.3 架构收益与约束
- **检索性能**：单子目录内文件数控制在同级素材数量（通常几百至几千），降低 NTFS/FAT32 文件系统 B+ 树查找深度，物理 I/O 复杂度维持在 $O(1)$。
- **安全与长路径规避**：哈希文件夹名去除了原始路径中的空格、中文及特殊字符，规避了 Windows 路径越界（MAX_PATH）及非法字符建文件夹失败的隐患。
- **模式隔离**：托管资源库模式（`.arc` 胶囊）继续保持胶囊内自治存储，磁盘导航模式采用父文件夹 SHA-256 隔离，实现双模式零干涉。
