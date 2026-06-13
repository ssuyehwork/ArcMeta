# Analysis_Modification_Plan-28: 颜色解析精度优化、线程安全加固及重试机制补全

## 1. 现状问题深度剖析

### 1.1 颜色提取算法精度问题 (ArcMeta vs Eagle)
根据反馈，ArcMeta 在处理复杂插画（如古筝图）时存在显著偏差：
*   **低饱和度过采样**：背景的大面积白色/浅灰色占据了过多的色块比例，导致视觉冗余。
*   **暗部细节丢失**：画面中极具特征的黑色墨点、笔触完全未被提取。
*   **算法根因**：当前 `UiHelper::extractPalette` 在计算像素权重时，虽有 `rankWeight` 机制，但对黑色（极低亮度）给予了过低的固定权重（0.01），且对低饱和度区域的降权力度不足，导致高频但视觉权重低的背景色占主导。

### 1.2 批量扫描时的“失忆”现象 (COM 初始化缺失)
*   **环境冲突**：`ImportHelper` 在后台线程 (`QtConcurrent::run`) 执行，而 `getShellThumbnail` 调用的 Windows Shell API (如 `IShellItemImageFactory`) 严格依赖 **COM (Component Object Model)** 环境。
*   **后果**：由于后台线程未调用 `CoInitializeEx`，Shell 接口会频繁返回 `E_FAIL` 或 `null`。系统误以为文件损坏或无预览图，导致入库图片大面积缺失主色调。

### 1.3 脆弱的重试机制
*   **逻辑缺陷**：`MetadataManager::processVisualRetryQueue` 中的第 826-828 行逻辑过于激进——“只要尝试过一次，无论成功与否都从队列移除”。在文件被锁定或 COM 暂时失效的情况下，这导致了颜色的永久性丢失。

---

## 2. 逻辑架构修改方案

### 2.1 优化颜色采样权重策略 (`src/ui/UiHelper.h`)
引入“感知显著性”加权模型，调整 `extractPalette` 内部逻辑：
*   **暗部提权**：将极暗色（黑色）的权重从固定 `0.01` 提升，或者根据其在局部区域的对比度进行动态补偿。
*   **背景动态降权**：通过 HSL 空间更精确地识别“近似白色背景”，对其进行更强力度的惩罚。
*   **相似色合并阈值微调**：略微收紧合并阈值，增加色彩提取的层次感（如灰阶梯度）。

### 2.2 线程上下文加固 (`src/util/ImportHelper.cpp`)
在后台导入线程的入口处增加 COM 初始化保护：
```cpp
// 在 ImportHelper::importPaths 的 lambda 内部
context->future = QtConcurrent::run([paths, targetCategoryId, weakProgress, context]() {
    #ifdef Q_OS_WIN
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED); // 赋予后台线程 Shell 调用能力
    #endif

    // ... 原有逻辑 ...

    #ifdef Q_OS_WIN
    CoUninitialize();
    #endif
});
```

### 2.3 容错重试机制改良 (`src/meta/MetadataManager.cpp`)
重构 `processVisualRetryQueue` 的移除策略：
*   **基于结果移除**：仅当 `setItemVisualMetadata` 成功执行或判定文件确实非图像（无法提取）时，才从 `m_visualRetryQueue` 移除。
*   **增加错误状态判断**：如果是因为 Shell 接口返回空导致的失败，保留在队列中并在下次心跳（5秒后）再次尝试。

---

## 3. 具体实施建议

### 3.1 颜色算法调整
在 `UiHelper::extractPalette` 中，建议将权重计算逻辑修改为：
1.  **Vibrancy (鲜艳度)** 保持高权重。
2.  **Contrast (对比度)**：对于亮度 L < 0.1 的像素（黑色），赋予一个基于“视觉对比”的基础权重（例如 0.5），而非降权至 0.01。
3.  **背景识别**：如果 L > 0.95 且 S < 0.05，权重降至 0.001。

### 3.2 性能平衡
*   采样尺寸建议维持在 256px 或 200px 缩放图，以确保性能。
*   K-Means 聚类次数无需变动，核心在于前置的像素权重分桶。

### 3.3 补救信号
*   在 `ImportHelper` 完成后，如果发现有大量失败项留在 `m_visualRetryQueue`，可在日志或 UI 状态栏给予隐晦提示，或者在闲时静默重试。

---

## 4. 验证要点
1.  **对比验证**：使用古筝插画重新运行提取逻辑，检查黑色和暖棕色的占比是否提升，白色是否不再霸屏。
2.  **并发压力测试**：在导入 5000+ 图片时，观察 `getShellThumbnail` 的成功率。
3.  **持久化验证**：确保通过重试机制提取出的颜色能正确写入 SQLite 数据库。
