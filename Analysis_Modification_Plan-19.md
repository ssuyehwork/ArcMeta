# 逻辑架构分析与深度修改方案 (Analysis_Modification_Plan-19.md)

**审计日期**：2024-06-20  
**审计人员**：资深程序员旁观者 (Jules)

---

## 1. 重复定义排查与清理方案

### 1.1 统一图标缓存管理
-   **现状分析**：`src/mft/MftReader.h` 内部维护了 `m_icon_cache`，而 `src/ui/UiHelper.h` 维护了 `s_iconCache`。
-   **风险点**：内存冗余，且 `MftReader` 的缓存缺少 QColor 意识，无法适应 UI 的动态主题切换。
-   **详细修改方案**：
    1.  **物理删除**：移除 `MftReader.h` 中的 `QHash<QString, QIcon> m_icon_cache;` 及其相关 Getter/Setter。
    2.  **逻辑重定向**：在 `MftReader.cpp` 中原本调用 `m_icon_cache` 的位置，改为调用 `UiHelper::getFileIcon(path)`。
    3.  **单例增强**：确保 `UiHelper` 的缓存是线程安全的（使用 `QReadWriteLock`），因为 `MftReader` 可能在非 UI 线程请求图标。

### 1.2 全局 Role 枚举标准化
-   **现状分析**：`CategoryModel` 和 `ContentPanel` 的 Role 枚举值（PathRole 等）虽然名称一致，但物理数值（基础偏移）不同，导致在 `MainWindow` 的中转代码中需要大量 `static_cast` 或硬编码。
-   **详细修改方案**：
    1.  **新建定义文件**：在 `src/meta/MetadataDefs.h` 中新增 `enum class ArcMetaRole : int { ... }`。
    2.  **值分配**：统一从 `Qt::UserRole + 100` 开始编号，避免与 Qt 内部 Role 冲突。
    3.  **重构引用**：
        -   修改 `ContentPanel.h`：删除 `enum ItemRole`，直接使用 `ArcMetaRole`。
        -   修改 `CategoryModel.h`：删除 `enum Roles`，对齐至 `ArcMetaRole`。

---

## 2. 僵尸代码清理方案 (精简冗余，保留功能)

### 2.1 彻底移除 `LoadingWindow`
-   **现状分析**：该窗口已被用户要求禁用，代码处于“挂起”状态。
-   **详细修改方案**：
    1.  **删除物理文件**：`src/ui/LoadingWindow.h` 和 `src/ui/LoadingWindow.cpp`。
    2.  **清理项目文件**：从 `CMakeLists.txt` 或相关的 `.pri` 文件中移除这两个文件的编译条目。

### 2.2 废弃冗余的 `CacheManager` 类 (注意：非移除 .scch 功能)
-   **现状分析**：`ScanDialog` 的快照功能目前由 `src/mft/ScchCache.cpp` 稳定实现。`src/core/CacheManager.cpp` 是一个功能重复且在项目中**从未被实际调用**的冗余类。
-   **风险点**：同名魔数（SCCH）但实现逻辑不同的两个类并存，会严重误导后续开发，增加维护成本。
-   **详细修改方案**：
    1.  **代码瘦身**：物理删除 `src/core/CacheManager.h` 和 `src/core/CacheManager.cpp`。
    2.  **清理 UI 残留**：在 `src/ui/ScanDialog.h` 中删除未使用的 `std::unique_ptr<CacheManager> m_cacheManager;` 成员变量。
    3.  **注**：此操作仅为清理多余的代码副本，`ScanDialog` 依赖的 `MftReader` 及其 `ScchCache` 逻辑保持不动。

---

## 3. 逻辑架构合理性加固 (保持 MainWindow 与 ScanDialog 的独立性)

### 3.1 确立 SQLite 为唯一事实来源 (针对 MainWindow)
-   **现状分析**：`MainWindow` 启动时已成功实现“物理剥离”MFT 依赖，这种架构非常优秀，确保了主界面的轻量化。但 `CategoryRepo` 内部仍残留 JSON 双轨对账逻辑。
-   **详细修改方案**：
    1.  **架构单一化**：将 `CategoryRepo` 的主逻辑彻底锁定在 SQLite 上。
    2.  **异步备份**：JSON 仅作为离线导出使用，不再参与实时的“双向对账”，消除因程序崩溃导致的数据分歧风险。

### 3.2 异步任务流控 (Task Dispatcher)
-   **现状分析**：虽然 `ScanDialog` 关闭时会清理 MFT 内存，但散乱的 `QtConcurrent::run` 可能在后台仍有未结束的线程。
-   **详细修改方案**：
    1.  **引入管理器**：使用中心化的 `QThreadPool` 管理所有后台任务。
    2.  **优雅终止**：确保在 `ScanDialog` 关闭或程序退出时，所有正在进行的磁盘扫描或快照生成任务能被可靠地 Cancel 或等待完成。

---

## 4. 上下文冲突修复方案

### 4.1 状态锁 `m_isLoading` 的精细化
-   **现状分析**：单一布尔变量无法区分“正在加载快照数据”与“正在刷新 UI 视图”。
-   **详细修改方案**：定义多级状态枚举，确保在加载 `.scch` 快照期间，用户触发的排序请求能排队处理而非直接丢失。

### 4.2 强制路径转换协议
-   **现状分析**：`wstring` (数据库/Win32) 与 `QString` (Qt UI) 的频繁转换在长路径下可能存在编码落差。
-   **详细修改方案**：在 `UiHelper` 中统一路径格式化函数，强制要求所有跨层路径传递必须经过此标准化接口。

---

## 5. 总结

本方案在严格遵循**“MainWindow 纯数据库化”**与**“ScanDialog 按需加载快照”**这一优秀架构的前提下，提出了清理冗余代码副本（如 CacheManager）和加固异步流控的建议。实施后，代码库将更加纯净，功能逻辑将实现单一事实来源，进一步提升系统的健壮性。
