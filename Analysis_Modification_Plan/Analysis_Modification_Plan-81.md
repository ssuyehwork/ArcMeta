# 分类搜索递归性缺失与搜索范围感知优化 —— Analysis_Modification_Plan-81.md

## 1. 任务背景
在 ArcMeta 中，分类（Category）具有层级结构。目前当用户选中一个父分类并执行搜索时，系统仅在选中的该级分类中检索项目，无法自动穿透并检索其所有子分类下的内容。这导致搜索结果严重不完整，违反了“所见即所搜”的直觉。

## 2. 问题定位

### 2.1 根因分析
1.  **查询逻辑单一**：在 `CoreController::performSearch` 及其关联的 DB 查询层中，针对 `DataSource == "category"` 的处理，仅通过单一的 `category_id = ?` 进行 SQL 过滤。
2.  **缺乏递归解析**：系统未在搜索发起前，根据当前选中 ID 递归解析出其所有子分类的 ID 集合。

### 2.2 涉及文件
- `src/core/CoreController.cpp`：搜索调度中枢。
- `src/meta/CategoryRepo.h/cpp`：需提供获取子树 ID 列表的接口。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 无法自动递归检索其子分类下的内容 | 在搜索前执行分类树递归解析，生成 ID 集合 | ✅ |
| 2    | 导致搜索结果不完整 | 将 ID 集合应用至 SQL `IN` 子句中 | ✅ |

## 4. 详细解决方案

### 4.1 提供递归 ID 查询接口
在 `CategoryRepo` 中实现获取指定分类及其所有后代分类 ID 的静态函数。由于分类树在内存中维护，该操作应极其轻量。

```cpp
// src/meta/CategoryRepo.h
static std::vector<int> getSubtreeIds(int rootId);

// src/meta/CategoryRepo.cpp
std::vector<int> CategoryRepo::getSubtreeIds(int rootId) {
    std::vector<int> results = { rootId };
    auto all = getAll(); // 获取全量内存缓存
    
    std::function<void(int)> collect = [&](int pid) {
        for (const auto& cat : all) {
            if (cat.parentId == pid) {
                results.push_back(cat.id);
                collect(cat.id);
            }
        }
    };
    collect(rootId);
    return results;
}
```

### 4.2 优化搜索调度逻辑
修改 `CoreController::performSearch`，当数据源为分类且 `categoryId > 0` 时，动态扩充 ID 范围。

```cpp
// src/core/CoreController.cpp (伪代码)
void CoreController::performSearch(const QString& keyword, const QString& source, int catId, ...) {
    // ... 
    if (source == "category" && catId > 0) {
        std::vector<int> targetIds = CategoryRepo::getSubtreeIds(catId);
        // 将 targetIds 传递给后端 SQL 构建器
        // 后端构建 "SELECT ... FROM item_categories WHERE category_id IN (id1, id2, ...)"
    }
    // ...
}
```

## 5. 修改边界声明【红线】

**本次方案涉及范围：**
- [ ] `CategoryRepo` 的内存查询扩展。
- [ ] `CoreController` 的搜索参数预处理。

**明确禁止越界修改的范围：**
- [ ] 禁止修改磁盘路径模式（nav）下的搜索逻辑。
- [ ] 禁止修改数据库索引结构。

## 6. 实现准则与预警【核心】

1.  **性能预警**：若分类层级极深（如超过 50 层），递归算法需防范栈溢出，但在本项目业务场景下，分类树通常规模较小，内存递归是安全的。
2.  **SQL 参数化**：在将 ID 集合转为 `IN` 子句时，必须确保 SQL 构建的安全性，防止拼接注入。
3.  **结果去重**：一个项目可能同时属于父分类和子分类（虽然不推荐），查询结果需在 SQL 层级通过 `DISTINCT` 或 `GROUP BY` 进行去重。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|-------------|----------------------|----------------|
| 架构红线 | 功能必须与蓝色提示线对齐 | ✅ 方案补全了对齐后的逻辑完整性 |
| 搜索行为 | 必须通过 CoreController 发起 | ✅ 方案直接修改该控制器 |

## 8. 待确认事项
- 是否需要增加 UI 开关供用户选择“仅搜索当前级”或“递归搜索”？（本方案默认按用户期望改为递归搜索，不增加复杂度）。
