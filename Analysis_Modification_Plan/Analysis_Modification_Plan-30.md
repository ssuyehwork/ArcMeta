# Analysis_Modification_Plan-30: 链接、备注及文件大小多维筛选方案

## 1. 需求背景
用户希望增强筛选面板（FilterPanel）的功能，支持以下三个新维度：
1.  **链接过滤**：筛选“有链接”或“无链接”的文件。
2.  **备注过滤**：筛选“有备注”或“无备注”的文件。
3.  **大小过滤**：支持通过输入数字范围（KB/MB）筛选文件大小。

## 2. 逻辑架构设计

### 2.1 内存模型扩展 (`src/core/IndexedEntry.h`)
为了确保筛选器在 `FilterProxyModel` 中能以 O(1) 复杂度运行，必须在 `ItemRecord` 中缓存链接和备注状态：
```cpp
struct ItemRecord {
    // ... 原有字段 ...
    QString url;
    QString note;
};
```
在 `FerrexVirtualDbModel` 加载数据时，需同步从 `MetadataManager` 中读取这两项内容。

### 2.2 筛选状态定义 (`src/ui/FilterPanel.h`)
扩展 `FilterState` 结构体，增加新的筛选谓词：
```cpp
struct FilterState {
    // ... 原有字段 ...
    enum Presence { All, Yes, No };
    Presence linkPresence = All;
    Presence notePresence = All;
    
    long long minSize = -1; // 字节单位，-1 表示不限制
    long long maxSize = -1;
};
```

### 2.3 筛选算法实现 (`src/ui/ContentPanel.cpp`)
在 `FilterProxyModel::filterAcceptsRow` 中增加三段逻辑判定：

1.  **链接判定**：
    *   若为 `Yes`：`!record.url.isEmpty()`
    *   若为 `No`：`record.url.isEmpty()`
2.  **备注判定**：
    *   若为 `Yes`：`!record.note.isEmpty()`
    *   若为 `No`：`record.note.isEmpty()`
3.  **大小判定**：
    *   `if (minSize != -1 && record.size < minSize) return false;`
    *   `if (maxSize != -1 && record.size > maxSize) return false;`

## 3. UI 交互方案 (`src/ui/FilterPanel.cpp`)

### 3.1 状态切换组件 (Link & Note)
*   **形式**：采用“分段选择器”或一组单选按钮。
*   **布局**：在“筛选”面板上方或作为独立 Group 插入。
*   **选项**：全部、有链接、无链接；全部、有备注、无备注。

### 3.2 文件大小范围输入组件
*   **组成**：
    *   两个 `QLineEdit`：分别对应“最小”和“最大”。
    *   `QComboBox`：单位切换（KB, MB, GB）。
    *   `QIntValidator`：确保只能输入数字。
*   **单位换算逻辑**：当用户在 UI 输入 `1` 并选择 `MB` 时，程序内部将其转换为 `1048576` 字节参与筛选。

## 4. 具体修改建议

### 4.1 数据加载同步
修改 `ContentPanel::loadDirectory` 和 `loadCategory` 中的 Lambda 逻辑，在构建 `ItemRecord` 时，将 `meta.url` 和 `meta.note` 同步注入。

### 4.2 筛选器响应
在 `FilterPanel` 中为 LineEdit 增加 `textChanged` 信号监听，但需引入 300ms 的防抖机制（Debounce），避免用户输入数字过程中 UI 频繁重绘导致输入卡顿。

---

## 5. 验证要点
1.  **边界值测试**：测试 0 字节文件在“无限制”和“0-10 KB”范围下的表现。
2.  **组合筛选**：验证“有备注”且“大小 > 5MB”的交叉筛选是否准确。
3.  **性能验证**：在 5 万级数据目录下，频繁改动大小范围筛选，观察界面响应延迟。
