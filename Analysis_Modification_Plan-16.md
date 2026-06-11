# 源码审计与重构方案：入库逻辑一体化 (Analysis_Modification_Plan-16)

## 1. 需求背景与审计结论
经审计，目前系统存在两套并行的入库逻辑：
*   **拖拽导入 (`CategoryPanel.cpp`)**：支持文件夹递归转分类，但采用“步进式反馈”（每 50 个文件刷新一次 UI），信息显示较简略。
*   **扫描入库 (`ContentPanel.cpp`)**：采用“精细化反馈”（逐个文件刷新），但缺乏文件夹转分类的自动建树功能。

**核心目标**：将两者逻辑合并，封装为独立的函数模块，统一采用精细化反馈，并实现功能一体化。

---

## 2. 需求理解与共识 (Understanding & Consensus)

### 2.1 逻辑封装 (Encapsulation)
*   **操作**：从 `CategoryPanel.cpp` 提取复杂的递归入库逻辑（含分类自动创建、Metadata 注册、FID 获取、数据库关联）。
*   **目标**：创建一个通用的 `ImportService` 或在现有架构中寻找合适位置（建议封装在 `src/core/ImportHelper`），提供统一的入库入口。

### 2.2 UI 反馈标准化 (Standardized Feedback)
*   **改变**：彻底废弃“每 50 个文件刷新一次”的限制。
*   **实现**：每一个文件处理时，必须调用 `BatchProgressDialog::updateProgress` 接口。
*   **格式**：强制拼接为 `[当前/总数] 百分比% - 文件名`（例如：`[42/100] 42.0% - photo.jpg`）。

### 2.3 功能一体化 (Integration)
*   **右键菜单升级**：`ContentPanel` 中的“扫描入库”不再使用旧代码，改为直接调用新封装的模块。
*   **能力对齐**：升级后的“扫描入库”将自动获得“递归文件夹转分类”的能力。
*   **代码清理**：删除 `ContentPanel.cpp` 中约 80 行的冗余递归代码。

---

## 3. 修改详细方案 (Modification Plan)

### 第一阶段：封装核心入库模块
1.  **新建 `src/util/ImportHelper.h/cpp`** (或在 `MetadataManager` 中扩展)：
    *   定义 `static void importPaths(const QStringList& paths, int targetCatId, BatchProgressDialog* progress, QWidget* parent)`。
    *   内置递归 Lambda `processRecursive`。
    *   **关键点**：在递归内部，每处理一个文件即调用 `updateProgress`。

### 第二阶段：重构侧边栏拖拽逻辑
1.  修改 `src/ui/CategoryPanel.cpp` 中的 `pathsDropped` 信号处理函数。
2.  移除原本的 `QtConcurrent::run` 内部的大段逻辑。
3.  改为一行调用：`ImportHelper::importPaths(paths, targetCatId, progress, this);`。

### 第三阶段：重构右键扫描逻辑
1.  修改 `src/ui/ContentPanel.cpp` 中的 `ActionAddToCategory` 分支。
2.  移除旧的递归逻辑。
3.  同样改为调用：`ImportHelper::importPaths({path}, 0, progress, this);`（默认入库到“未分类”或指定根节点）。

### 第四阶段：验证与性能观察
1.  **UI 压力测试**：验证在精细化反馈下，导入 1000+ 文件时界面的响应速度。
2.  **功能验证**：
    *   拖拽文件夹到分类树，验证分类层级是否正确创建。
    *   右键扫描文件夹，验证是否能自动识别子目录。
    *   确认进度条文字格式严格符合要求。

---

## 4. 预期效果
*   **代码质量**：消除冗余，逻辑收拢，提高可维护性。
*   **用户体验**：无论何种入库方式，用户都能获得一致且详尽的进度反馈。
*   **功能增强**：原有的“扫描入库”功能得到了质的提升。
