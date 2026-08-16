# 实施方案：阶段四：资产关系管家引擎与 SSOT 治理 (CategoryBindingManager)

## 所属大纲章节
**1.1 全局数据与内存管理**（1.1.2 阶段四：SSOT 治理阶段 —— 资产关系集中化与补丁清理）

---

## 涉及代码文件
* `src/meta/CategoryBindingManager.h` （新增）
* `src/meta/CategoryBindingManager.cpp` （新增）
* `src/meta/CategoryRepo.h` （修改：移除交叉绑定 SQL 冗余补丁）
* `src/meta/CategoryRepo.cpp` （修改）
* `src/core/CategoryLoadService.cpp` （修改：清理 SCCH 绕过匹配路径的遗留补丁）

---

## 功能描述
此前系统在 `MetadataManager` 与 `CategoryRepo` 之间存在资产分类绑定的交叉同步与两套账补丁代码，增加了数据不一致风险与系统复杂度。
本方案建立**单一事实源（Single Source of Truth, SSOT）**：
1. **创建管家引擎**：新增 `CategoryBindingManager` 单例，统一托管所有资产与分类的绑定、解绑、移库与持久化 SQL 操作。
2. **清理冗余与僵尸代码**：清理 `MetadataManager` 与 `CategoryRepo` 中重复的绑定 SQL，并对齐/清除 `CategoryLoadService.cpp` 中绕过数据库匹配路径的 SCCH 遗留历史代码，确保数据流向单一清晰。

---

## 技术决策
1. **SSOT 职责收拢**：所有涉及到资产与 Category ID 的映射关系均必须通过 `CategoryBindingManager::instance()` 进行操作，严禁任何业务层直接对 `asset_categories` 表发起散装 SQL 操作。
2. **只读查询缓存**：`CategoryBindingManager` 在内存中维护分类到资产 ID 的倒排集合（`std::unordered_map<int, std::unordered_set<std::wstring>>`），提供 $O(1)$ 的关系查询性能。

---

## 强制性四项断层排查清单

1. **头文件核对**：
   * `src/meta/CategoryBindingManager.h` 必须包含 `<QObject>`, `<unordered_map>`, `<unordered_set>`, `<mutex>`, `<vector>`。

2. **成员核对**：
   * 声明 `CategoryBindingManager` 为单例类，包含 `bindAssetToCategory`、`unbindAssetFromCategory`、`getAssetsInCategory` 等专职 API。

3. **残留核对**：
   * 全全局搜索 `asset_categories` SQL 语句，将所有散落的 `INSERT INTO asset_categories` / `DELETE FROM asset_categories` 调用收拢合并至 `CategoryBindingManager` 中。

4. **断层核对（上下文连续性）**：
   * 对照 `CategoryLoadService.cpp` 中关于 SCCH 文件的加注逻辑，移除遗留的两套账补丁代码。

---

## 代码改动对照

### 新增文件: `src/meta/CategoryBindingManager.h`
```cpp
#pragma once
#include <QObject>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>

namespace ArcMeta {

class CategoryBindingManager : public QObject {
    Q_OBJECT
public:
    static CategoryBindingManager& instance();

    // 绑定资产到分类
    bool bindAssetToCategory(const std::wstring& path, int categoryId);
    // 从分类解绑资产
    bool unbindAssetFromCategory(const std::wstring& path, int categoryId);
    // 获取分类下的所有资产路径
    std::vector<std::wstring> getAssetsInCategory(int categoryId) const;

private:
    CategoryBindingManager(QObject* parent = nullptr) = default;

    mutable std::shared_mutex m_mutex;
    std::unordered_map<int, std::unordered_set<std::wstring>> m_categoryToAssets;
    std::unordered_map<std::wstring, std::unordered_set<int>> m_assetToCategories;
};

} // namespace ArcMeta
```

---

## 已知问题 / 待办
* 无。

---

## 涉及文件清单
1. `src/meta/CategoryBindingManager.h`（新增：资产分类关系 SSOT 管家头文件）
2. `src/meta/CategoryBindingManager.cpp`（新增：资产分类关系 SSOT 管家实现）
3. `src/meta/CategoryRepo.cpp`（修改：收拢关系 SQL 至 CategoryBindingManager）
4. `src/core/CategoryLoadService.cpp`（修改：清理 SCCH 遗留补丁代码）
