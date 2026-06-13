# Analysis & Modification Plan - 扫描/导入流程颜色解析失效分析

## 1. 现象描述 (Symptom)
用户反映在执行“扫描入库”或“文件夹拖拽导入”时，文件虽然成功进入数据库，但元数据面板中的“色板”和“主色调”为空。手动在 UI 中点击文件时，颜色解析往往能恢复正常。

## 2. 核心原因分析 (Root Cause Analysis)

### 2.1 线程环境与 COM 限制 (核心原因)
*   **逻辑链路：** `ImportHelper` 通过 `QtConcurrent::run` 在后台线程执行递归扫描。
*   **技术细节：** 颜色提取的核心函数 `UiHelper::getShellThumbnail` 调用了 Windows Shell API (`IShellItemImageFactory`)。
*   **冲突点：** Windows Shell API 严格要求调用线程必须处于已初始化的 COM 环境中（通常是 STA 模式）。`QtConcurrent` 使用的是全局线程池，其子线程默认**未调用** `CoInitializeEx`。
*   **后果：** `pFactory->GetImage` 直接返回错误码，系统回退到 `QImage::load`。由于 `QImage` 不原生支持 PSD、AI、HEIC 等格式，导致这些文件的颜色提取静默失败。

### 2.2 批量操作下的 I/O 竞争
*   `ImportHelper` 在导入时开启了 `SqlTransaction`（大事务），旨在提升写入性能。
*   `MetadataManager::tryExtractColor` 在提取成功后会通过 `debouncePersist` 发起异步持久化请求。
*   在大规模导入（数千文件）时，大量小文件的颜色提取任务可能在后台线程池堆积，产生严重的磁盘 I/O 竞争，导致部分任务因超时或资源锁死而失败。

### 2.3 缓存早退机制
*   `MetadataManager::tryExtractColor` 第一行逻辑：`if (!instance().getMeta(nPath).color.empty()) return;`
*   如果文件曾被扫描过（即使当时提取失败留下了空标记或默认值），后续的自动扫描将不再重复尝试，除非用户手动触发。

## 3. 解决方案 (Proposed Solutions)

### 3.1 物理加固：后台线程 COM 初始化
在 `ImportHelper.cpp` 的后台线程入口处，显式引入 COM 初始化保护。
```cpp
// 建议在 ImportHelper 的 lambda 表达式开头增加
context->future = QtConcurrent::run([paths, targetCategoryId, weakProgress, context]() {
    #ifdef Q_OS_WIN
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    // 确保退出时释放
    struct ComUninit { ~ComUninit() { CoUninitialize(); } } _uninit;
    #endif
    // ... 原有逻辑 ...
});
```

### 3.2 提取策略优化
*   **分级提取：** 优先处理小尺寸图像。对于大文件，强制使用 `SIIGBF_THUMBNAILONLY` 标志以减少内存开销。
*   **重试机制：** 如果 `color` 为空但文件格式属于支持列表，允许在“空闲时间”进行二次扫描。

### 3.3 异步队列化
*   不应在导入主循环中同步执行 `tryExtractColor`。
*   建议引入一个 `ExtractionQueue`，将提取任务按优先级（当前可见项 > 目录预取项 > 后台导入项）进行排序执行，并限制并发数（如最多 2 个线程用于颜色提取），避免 I/O 锁死。

## 4. 结论 (Conclusion)
目前的失效并非算法错误，而是由于**后台线程缺乏必要的操作系统上下文（COM）**导致的。通过在任务线程中注入 `CoInitialize` 即可解决大部分专业格式文件的颜色提取问题。
