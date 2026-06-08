# Analysis_Modification_Plan-1: 修复重启后元数据加载失效及分类计数归零问题

## 1. 现象分析
根据用户提供的截图与调试日志（2026-06-07）：
- **图一（扫描后）**：系统通过 `getMeta` 直接访问文件路径，绕过了目录扫描限制，显示正常（全部数据：6）。
- **图二（重启后）**：日志显示 `分布式加载完成: 目录数= 37 文件数= 0`。
- **关键日志分析**：虽然系统找回了 37 个目录，但没有找回任何文件。

## 2. 逻辑架构瓶颈定位

通过对源代码的审计，定位到以下三个核心漏洞：

### 2.1 QDir 扫描的“视觉盲点”（主因）
- **位置**：`src/meta/MetadataManager.cpp` 中的 `initFromScchMode` 函数。
- **问题**：系统使用 `arcmetaDir.entryList({"*.scch"}, QDir::Files)` 来搜集元数据。
- **后果**：由于项目宪法规定 `.scch` 文件必须设为隐藏（`FILE_ATTRIBUTE_HIDDEN`），而 `QDir` 默认不扫描隐藏文件，导致 `entryList` 返回空列表。这是重启后文件计数归零的直接原因。

### 2.2 分布式发现协议的“登记漏洞”
- **位置**：`src/meta/MetadataManager.cpp` 中的 `getMeta` 函数。
- **问题**：当按需加载磁盘上的 `.scch` 到内存时，未同步调用 `registerArcmetaFrn`。
- **后果**：如果用户仅查看而未修改数据，该目录的 FRN 不会被记入 `All_FRN_metadata.scch`。重启后，系统因失去“地图索引”而无法找回这些目录。

### 2.3 初始化链条的“统计抢跑”
- **位置**：`src/meta/CategoryRepo.cpp` 与 `MetadataManager` 初始化时序。
- **问题**：`Category` 的全量重计（`fullRecount`）在元数据尚未完全载入内存时被高频触发，导致统计结果为 0。

## 3. 详细修改方案

### 方案 A：物理修复 initFromScchMode 的扫描标志
**修改要点：**
```cpp
// src/meta/MetadataManager.cpp
QDir arcmetaDir(QString::fromStdWString(resolvedPath) + "/.arcmeta");
// [物理加固] 必须显式包含 Hidden 标志，否则无法扫描到隐藏的 .scch 文件
QStringList scchFiles = arcmetaDir.entryList({"*.scch"}, QDir::Files | QDir::Hidden | QDir::System);
```

### 方案 B：补全 getMeta 的“自动登记”
**修改要点：**
在 `MetadataManager::getMeta` 成功加载数据后，增加对 `registerArcmetaFrn(parentDir)` 的调用。

### 方案 C：强化退出保护与时序对账
**修改要点：**
1. 在 `MainWindow::closeEvent` 中显式调用 `AllFrnManager::saveImmediately()`。
2. 确保 `initFromScchMode` 在发射全量刷新信号前，元数据缓存已处于“最终一致”状态。

## 4. 预期效果
- 重启程序后，`initFromScchMode` 能够正确识别隐藏的 `.scch` 文件。
- `m_cache` 正确填充，侧边栏计数恢复 (6)，与重启前状态完全一致。

---
**旁观者意见**：隐藏属性是系统为了减少干扰而引入的，但在分布式发现算法中却成了阻碍。通过在扫描标志位中补齐 `QDir::Hidden`，可以瞬间解决此“幽灵归零”问题。
