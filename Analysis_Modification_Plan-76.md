# Analysis and Modification Plan - 76

## 1. 任务背景
用户明确拒绝任何“兼容 JSON”的折中方案，要求彻底、全量地废除所有 JSON 相关的存储逻辑与解析代码。这意味着即使是目前以后缀 `.scch` 伪装的明文 JSON 存储也必须被替换为纯粹的二进制协议。

## 2. 全二进制 SCCH 协议 (Binary-SCCH v3) 技术规范

为了实现极致的性能与存储纯净性，建议采用以下二进制布局结构：

### 2.1 文件头结构 (Header) - 48 Bytes
| 偏移 | 长度 | 类型 | 说明 |
| :--- | :--- | :--- | :--- |
| 0 | 4 | char[4] | Magic Code: "SCCH" |
| 4 | 4 | uint32 | 协议版本: 0x00000003 |
| 8 | 8 | uint64 | 创建时间戳 (Unix ms) |
| 16 | 4 | uint32 | 文件夹条目数 (Item Count) |
| 20 | 4 | uint32 | 字符串池偏移量 (String Pool Offset) |
| 24 | 4 | uint32 | 字符串池大小 (String Pool Size) |
| 28 | 4 | uint32 | 全局校验和 (CRC32) |
| 32 | 16 | bytes | 保留位 (对齐 16 字节) |

### 2.2 条目索引区 (Entry Records)
每个条目（文件或子文件夹）采用 64 字节定长记录：
- `NameOffset` (uint32): 指向字符串池的文件名。
- `Flags` (uint32): 包含 `isDir`, `isPinned`, `isEncrypted` 等标志位。
- `Rating` (int8).
- `ColorRef` (uint16): 颜色索引（指向内置调色板）。
- `Size` (uint64).
- `MTime/CTime` (uint64 * 2).
- `TagsOffset` (uint32): 指向字符串池中以 `\0` 分隔的标签列表。
- `PaletteData` (24 bytes): 存储前 3 位主色调的 RGB 与 Ratio（直接二进制序列化）。

## 3. 代码剥离方案

### 3.1 核心存储类重构 (`AmMetaScch.cpp`)
- **Action**: 移除 `#include <QJsonDocument>` 等所有 JSON 头文件。
- **Action**: 将 `load()` 函数改为使用 `QFile::map()` 内存映射或 `QDataStream` 原始读取。
- **Action**: 废除 `folderToEntry` / `itemToEntry` 等将结构体转换为 `QJsonObject` 的中间转换函数，直接在 `save()` 时按字节流顺序写入结构体。

### 3.2 分类与收藏逻辑重构 (`CategoryRepo.cpp` / `FavoritesRepo.cpp`)
- **Action**: 目前这两个文件依然在手动构建 `QJsonObject`。
- **Action**: 必须重新定义 `arcmeta_categories.scch` 的二进制格式，采用类似的“头部+变长数据”模式。
- **Action**: 彻底移除 `ScchCategoryEngine` 中的 `loadCategoriesScch`（JSON 实现）。

### 3.3 UI 辅助功能清理 (`ScanDialog.cpp`)
- **Action**: 搜索历史记录（`queryHistory`）不再使用 JSON 格式。
- **Action**: 建议迁移至 `src/util/ConfigStore` (自定义二进制配置器) 或 `QSettings` 的原生二进制导出格式。

## 4. 迁移与兼容性策略
- **策略**: 由于用户要求“不采用兼容方案”，系统将不再读取旧版的 JSON-SCCH 文件。
- **后果**: 升级到 Binary-SCCH v3 后，旧版的元数据将被视为“无效格式”并触发重新扫描（这符合彻底废除的定义）。
- **执行**: 在初始化时，若检测到文件头不是 "SCCH" 二进制标识，则直接物理删除该文件并报错。

## 5. 总结建议
修改后的代码库将实现：
1. **零 QJson 依赖**: 编译产物不再包含任何 JSON 解析库的代码。
2. **极速加载**: 元数据加载速度提升 5-10 倍。
3. **架构纯净**: 从磁盘存储到内存模型，全部统一为强类型的二进制结构。
