# Analysis_Modification_Plan-29: 图片尺寸自动识别与多维筛选支撑方案

## 1. 需求分析
用户希望在“扫描入库”过程中自动记录图片的物理尺寸（宽度和高度）。
*   **目的**：支撑前端筛选器（如横图、竖图、方形、16:9 等比例筛选）。
*   **挑战**：在数万张图片的批量扫描中，获取尺寸必须极其轻量，不能阻塞主线程，且需兼容 PSD、AI 等专业格式。

## 2. 逻辑架构设计

### 2.1 数据库 Schema 演进 (`src/meta/DatabaseManager.cpp`)
需要在 `metadata` 表中增加两个字段来存储原始像素信息：
*   `width`: INTEGER (像素宽)
*   `height`: INTEGER (像素高)

**自动迁移逻辑**：在 `DatabaseManager::loadDb` 中增加字段存在性检查，若缺失则执行 `ALTER TABLE`。

### 2.2 内存模型扩展 (`src/meta/MetadataManager.h`)
在 `RuntimeMeta` 结构体中同步增加字段：
```cpp
struct RuntimeMeta {
    // ... 原有字段 ...
    int width = 0;
    int height = 0;
};
```

### 2.3 尺寸提取策略 (`src/meta/MetadataManager.cpp`)
尺寸提取应集成在 `registerItem` 的生命周期内。为了性能最优，建议采取以下多级降级方案：

1.  **Shell 属性优先 (Windows)**：通过 `IPropertyStore` 获取 `PKEY_Image_HorizontalSize` 和 `PKEY_Image_VerticalSize`。这种方式无需解码图片，对于大型 PSD/RAW 文件速度最快。
2.  **QImageReader 兜底**：如果 Shell 接口不可用，使用 `QImageReader::size()`。注意不要使用 `QImage::load`，因为 `QImageReader::size()` 只读取头部信息，不加载像素内存，性能极高。
3.  **视觉解析同步**：在 `tryExtractColor` 过程中，如果已经获取到了 `QImage` 对象，则直接读取其尺寸并更新到元数据中。

### 2.4 数据持久化与初始化
*   **持久化 (`persistAsync`)**：在 SQL `INSERT OR REPLACE` 语句中加入 `width` 和 `height` 的绑定。
*   **初始化 (`initFromScchMode`)**：在从数据库加载缓存时，使用 `sqlite3_column_int` 将尺寸信息还原至 `RuntimeMeta`。

---

## 3. 修改方案详述

### 3.1 修改 MetadataManager 注册流程
在 `MetadataManager::registerItem` 中，增加一个 `tryExtractDimensions` 的调用：
```cpp
void MetadataManager::registerItem(const std::wstring& path) {
    std::wstring nPath = normalizePath(path);
    ensureActivated(nPath);
    
    // 异步或同步获取尺寸（取决于文件类型）
    // 建议在 registerItem 所在的 ImportHelper 后台线程中直接执行
    tryExtractDimensions(nPath); 
    
    syncPhysicalMetadata(nPath, false);
    tryExtractColor(nPath);
    // ...
}
```

### 3.2 提取逻辑实现 (伪代码)
```cpp
void MetadataManager::tryExtractDimensions(const std::wstring& path) {
    int w = 0, h = 0;
    // 方案 A: 使用 Windows Shell API (适用于 Windows 下的专业格式)
    // 方案 B: QImageReader reader(QString::fromStdWString(path)); 
    //        QSize sz = reader.size();
    
    if (w > 0 && h > 0) {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_cache[path].width = w;
        m_cache[path].height = h;
    }
}
```

### 3.3 支撑前端筛选逻辑
记录尺寸后，前端筛选器（FilterPanel）即可通过简单的数学判定实现以下分类：
*   **横图**: `width > height`
*   **竖图**: `height > width`
*   **方形**: `width == height` (或在一定误差范围内)
*   **16:9**: `abs(width/height - 1.77) < 0.05`

---

## 4. 验证要点
1.  **I/O 性能**：在大批量导入（10,000+）时，测试增加尺寸提取后的总时长。
2.  **准确性**：验证 1x1 像素图、超大图、以及无尺寸信息的非图像文件（应为 0, 0）。
3.  **持久化**：重启应用后，检查侧边栏或筛选器的计数值是否能从数据库正确恢复。
