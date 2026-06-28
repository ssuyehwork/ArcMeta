# 右键菜单“收揽入库/重新扫描”判定逻辑修正 —— implementation_plan-2.md

## 1. 任务背景
用户反馈在物理目录导航时，位于托管库（如 `ArcMeta.Library_Z`）之外的项目，其右键菜单依然错误地显示为“重新扫描”（对应用户原话：“处于‘ArcMeta.Library_盘符’文件夹之外……菜单选项应该显示为‘收揽入库’而不该是‘重新扫描’”）。这导致了用户交互逻辑的混乱，需要修正判定逻辑。

## 2. 问题定位
- **文件**：`src/ui/ContentPanel.cpp`
- **函数**：`onCustomContextMenuRequested(const QPoint& pos)`
- **代码行号**：约 1120 行
- **根因分析**：
  当前逻辑直接读取了 `ManagedRole`（对应 `hasUserOperations()`）。由于该标志位在文件曾被标记颜色或曾入库后即为 true，即便文件物理位置已不在托管库内，该标志位依然保持，导致菜单项判定错误。
  必须将判定依据从“元数据是否存在”改为“物理位置是否合规”。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 标记为①的项目处于“ArcMeta.Library_盘符”文件夹之外 | 使用 `AutoImportManager::isPathInManagedLibrary` 进行物理路径判定 | ✅  |
| 2    | 右键菜单选项应该显示为“收揽入库” | 当物理路径在库外时，菜单文本显示为“收揽入库” | ✅  |
| 3    | 而不该是“重新扫描” | 仅当物理路径在库内时，菜单文本才显示为“重新扫描” | ✅  |

## 4. 详细解决方案

### 4.1 UI 考古 (Code Archaeology)
- **寻找同类案例**：在 `src/ui/ThumbnailDelegate.cpp` 第 132 行和 150 行中，系统在绘制状态图标（对勾/进度环）时，已经使用了正确的判定方式：
  ```cpp
  if (inManagedLib && ingStatus == 1 && AutoImportManager::isPathInManagedLibrary(path.toStdWString())) { ... }
  ```
- **对齐实现**：`ContentPanel` 的右键菜单逻辑应与之对齐，使用 `AutoImportManager` 提供的物理准入接口。

### 4.2 逻辑修正
在 `src/ui/ContentPanel.cpp` 的 `onCustomContextMenuRequested` 函数中，修改菜单文本生成逻辑：

```cpp
// 1. 获取当前项的标准化物理路径
QString itemPath = currentIndex.data(PathRole).toString();

// 2. 判定是否位于托管库物理范围内（对应用户原话：“处于‘ArcMeta.Library_盘符’文件夹之外”）
bool inManagedLibrary = AutoImportManager::isPathInManagedLibrary(itemPath.toStdWString());

// 3. 根据物理位置切换菜单文本
QString scanText = inManagedLibrary ? "重新扫描" : "收揽入库";
menu.addAction(UiHelper::getIcon("add", QColor("#FF8C00"), 18), scanText)->setData(ActionAddToCategory);
```

## 5. 修改边界声明【红线】

**本次方案涉及范围：**
- [ ] `src/ui/ContentPanel.cpp`：仅修改 `onCustomContextMenuRequested` 中关于 `scanText` 的赋值逻辑。

**明确禁止越界修改的范围：**
- [ ] 禁止修改 `MetadataManager` 中的 `ManagedRole` 逻辑。
- [ ] 禁止修改 `ActionAddToCategory` 的执行流程。

## 6. 实现准则与预警
1. **头文件依赖**：确保 `ContentPanel.cpp` 已包含 `#include "../core/AutoImportManager.h"`（经审计已包含）。
2. **性能预警**：`isPathInManagedLibrary` 涉及磁盘序列号获取与字符串比对，在右键点击瞬间触发无性能压力，但在循环中需谨慎。此处为单次右键触发，合规。
3. **上下文一致性**：必须确保 `path` 变量在判定前已通过 `data(PathRole)` 正确获取。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|-------------|----------------------|----------------|
| 托管库判定   | 托管项目需显示状态标识，入库态显示绿色对勾 | ✅ (本方案修正了与此视觉状态对齐的右键菜单文本逻辑) |
| 右键菜单样式 | 采用 `UiHelper::applyMenuStyle` | ✅ (现有代码已遵循，本方案仅改动文本) |

## 8. 待确认事项
- 无。逻辑指向明确，符合 Plan-113 架构红线。
