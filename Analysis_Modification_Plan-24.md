# FERREX 代码逻辑审计：小学生水平 vs 工业级架构 (Analysis_Modification_Plan-24.md)

作为资深程序员旁观者，我不仅关注“能不能运行”，更关注“逻辑健壮性”。在对 `src` 文件夹进行深度审计后，我发现以下代码逻辑属于“小学生作业水平”，在百万级数据或高并发环境下会迅速崩溃。

---

## 1. 逻辑重灾区：MetadataManager 的“自杀式”加载

在 `MetadataManager::getMeta` 中，逻辑如下：
```cpp
RuntimeMeta MetadataManager::getMeta(const std::wstring& path) {
    // 1. 查内存缓存 (正确)
    // 2. 内存没命中 -> 立即创建 AmMetaJson(parentDir) 并在主线程执行 amJson.load() (小学生水平)
}
```
### 为什么是“小学生水平”？
*   **现象**：如果你在主界面滚动 1000 个从未加载过的文件，`MetadataManager` 就会在 UI 线程发起 **1000 次磁盘打开文件并解析 JSON** 的操作。
*   **后果**：这比普通的 I/O 更可怕，JSON 的解析开销（CPU）加上磁盘寻道（I/O）会直接让界面从卡顿变成崩溃。
*   **工业级方案**：**异步预读 + 批量加载**。当用户进入某个文件夹时，后台线程应一次性加载该目录下 `.am_meta.json` 的所有条目。

---

## 2. 统计计数的“土法炼钢”：ContentPanel 的循环计数

在 `ContentPanel::search` 中，统计逻辑如下：
```cpp
ScanStats stats;
for (int i = 0; i < proxy->rowCount(); ++i) { // 遍历一万行
    // 对每一行取数据、分拆标签、累加计数 (小学生水平)
}
emit directoryStatsReady(stats);
```
### 为什么是“小学生水平”？
*   **现象**：用户每搜一个字，程序就傻傻地把所有搜索结果遍历一遍。
*   **后果**：随着数据量增加，搜索后的响应延迟会呈指数级增长。
*   **工业级方案**：**数据库内聚合**。直接发送一条 SQL 语句由 SQLite 内部完成聚合。

---

## 3. 文件夹状态的“偷懒”检查：entryList() 滥用

在 `ContentPanel.cpp` 和 `NavPanel.cpp` 中：
```cpp
bool hasSubDirs = !dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty(); // 小学生水平
```
### 为什么是“小学生水平”？
*   **现象**：为了判断一个文件夹是否有子目录，程序竟然去完整读取该目录下的所有项并生成一个字符串列表。
*   **后果**：如果该目录下有海量文件，系统会为了画一个“小箭头”而严重卡顿。
*   **工业级方案**：使用 `QDirIterator` 并设置只读第一个条目即停止。

---

## 4. 品牌硬编码：散落各处的 "ArcMeta" 字符串

在整个 `src/ui` 目录下，`QSettings` 等多处硬编码了机构名称：
```cpp
QSettings settings("ArcMeta团队", "ArcMeta"); // 小学生水平
```
### 为什么是“小学生水平”？
*   **现象**：品牌信息散落在代码各处，而不是统一管理。
*   **后果**：品牌更名时，修改极其困难且容易漏改。
*   **工业级方案**：**单点配置中心**。在应用全局层面统一设置组织名称。

---

## 总结

**目前的 `src` 逻辑架构处在“能跑通但极脆”的状态。** 小学生水平的程序员关注“功能的堆砌”，而工业级程序员关注“性能的边界”和“架构的解耦”。

---
*分析员：Jules (资深程序员旁观者)*
*日期：2026-05-11*
