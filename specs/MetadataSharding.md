# MetadataSharding 实施方案

## 所属大纲章节
大纲章节：1.1 全局数据与内存管理 - 1.1.3 阶段一底座止血具体技术实现规范 (MetadataManager 分片容器架构)

## 涉及代码文件
- `src/meta/MetadataManager.h`
- `src/meta/MetadataManager.cpp`

## 功能描述
将 `MetadataManager` 内存缓存结构由单一全局整块 RCU 内存快照（`std::shared_ptr<const std::unordered_map<std::wstring, RuntimeMeta>> m_snapshot`）重构升级为 **256 分片并发哈希容器（256-Sharded Concurrent Map）**。

本次重构的目标：
1. 彻底废除写操作（如 `setRating`、`setTags`、`setColor` 等）触发的 `make_shared<unordered_map>(*oldMap)` 2GB 内存全量深拷贝。
2. 将更新粒度缩小至单分片（Shard）的 `std::unique_lock` 局部排他锁，时间复杂度降至 $O(1)$，分配内存为 $0$。
3. `forEachCachedItem()` 改为逐分片获取 `shared_lock` 的**弱一致性遍历**，满足 `StatisticsService` 等消费者的最终一致性要求。
4. 严格遵守全局跨类锁顺序（第一顺位 `DatabaseManager per-drive` 锁，第二顺位 `MetadataManager shard` 锁），避免死锁。

## 技术决策

### 1. 分片容器数据结构定义
定义 `MetaShard` 结构体：
```cpp
struct MetaShard {
    mutable std::shared_mutex mutex;
    std::unordered_map<std::wstring, RuntimeMeta> items;
};
```
在 `MetadataManager` 私有成员中声明：
```cpp
static constexpr size_t NUM_SHARDS = 256;
std::array<MetaShard, NUM_SHARDS> m_shards;
```
分片定位哈希函数：
```cpp
inline size_t getShardIndex(const std::wstring& path) const {
    return std::hash<std::wstring>{}(path) % NUM_SHARDS;
}
```

### 2. 读写与并发规则
- **单项读取 (`getMeta`)**：计算 `shardIndex` $\rightarrow$ 获取 `m_shards[shardIndex].mutex` 的 `std::shared_lock` $\rightarrow$ 查找并返回 `RuntimeMeta`。
- **单项写入 (`setRating`, `setColor` 等)**：计算 `shardIndex` $\rightarrow$ 获取 `m_shards[shardIndex].mutex` 的 `std::unique_lock` $\rightarrow$ 原地修改 `m_shards[shardIndex].items[path]`。
- **遍历 (`forEachCachedItem`)**：按分片索引 `0..255` 顺次获取 `shared_lock`，读完单个分片立即释放锁再进入下一个分片，提供弱一致性遍历承诺。

### 3. 断层检查四项排查结论

1. **头文件核对**：
   - `std::array` 需要 `#include <array>`。`MetadataManager.h` 目前未包含 `<array>`，必须在 `MetadataManager.h` 的 include 区域添加 `#include <array>`。
   - `std::shared_mutex` 已在 `MetadataManager.h` 包含。

2. **成员核对**：
   - 移除私有成员 `std::shared_ptr<const std::unordered_map<std::wstring, RuntimeMeta>> m_snapshot;`。
   - 新增私有结构体 `MetaShard` 及其私有成员 `std::array<MetaShard, 256> m_shards;`。
   - 新增辅助方法 `size_t getShardIndex(const std::wstring& path) const;`。

3. **残留核对**：
   - 全项目范围对 `m_snapshot` 进行搜索，所有调用点均集中在 `MetadataManager.cpp` 及 `MetadataManager.h` 内的 `forEachCachedItem`。
   - 必须逐一替换所有 `atomic_load(&m_snapshot)` 和 `atomic_store(&m_snapshot)` 调用，彻底清除 RCU 深拷贝残留逻辑。

4. **上下文核对**：
   - 修改前/修改后对照代码块均直接取自当前最新 `MetadataManager.h` 和 `MetadataManager.cpp` 的实测源码，逐字精确匹配。

## 详细代码修改方案

### 修改 1：`src/meta/MetadataManager.h` — 包含头文件与分片结构声明

* **文件路径**：`src/meta/MetadataManager.h`
* **精确定位**：Include 区域与 `MetadataManager` 私有成员区

**修改前 (old_str)**：
```cpp
#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>
#include <string>
#include <atomic>
#include <deque>
#include <mutex>
#include <memory>
```

**修改后 (new_str)**：
```cpp
#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>
#include <string>
#include <atomic>
#include <deque>
#include <mutex>
#include <memory>
#include <array>
```

---

* **精确定位**：`MetadataManager.h` 中 `forEachCachedItem` 函数实现

**修改前 (old_str)**：
```cpp
    template<typename Func>
    void forEachCachedItem(Func&& fn) const {
        auto currentSnapshot = std::atomic_load(&m_snapshot);
        if (!currentSnapshot) return;
        for (auto it = currentSnapshot->begin(); it != currentSnapshot->end(); ++it) {
            fn(it->first, it->second);
        }
    }
```

**修改后 (new_str)**：
```cpp
    template<typename Func>
    void forEachCachedItem(Func&& fn) const {
        // [1.1.4 规范] 256分片弱一致性遍历：逐分片获取 shared_lock 读取，读毕即释
        for (size_t i = 0; i < NUM_SHARDS; ++i) {
            std::shared_lock<std::shared_mutex> lock(m_shards[i].mutex);
            for (const auto& pair : m_shards[i].items) {
                fn(pair.first, pair.second);
            }
        }
    }
```

---

* **精确定位**：`MetadataManager.h` 私有成员区 `m_snapshot` 声明

**修改前 (old_str)**：
```cpp
    // [RCU 内存快照设计]：将缓存升级为原子共享智能指针快照，实现 Lock-Free 共享读取
    std::shared_ptr<const std::unordered_map<std::wstring, RuntimeMeta>> m_snapshot;
    std::unordered_map<std::string, std::wstring> m_folderIdToPath;
```

**修改后 (new_str)**：
```cpp
    // 256 分片并发哈希容器：替代全量深拷贝 COW 快照
    struct MetaShard {
        mutable std::shared_mutex mutex;
        std::unordered_map<std::wstring, RuntimeMeta> items;
    };
    static constexpr size_t NUM_SHARDS = 256;
    std::array<MetaShard, NUM_SHARDS> m_shards;

    inline size_t getShardIndex(const std::wstring& path) const {
        return std::hash<std::wstring>{}(normalizePath(path)) % NUM_SHARDS;
    }

    std::unordered_map<std::string, std::wstring> m_folderIdToPath;
```

---

### 修改 2：`src/meta/MetadataManager.cpp` — 接口重构实现

* **文件路径**：`src/meta/MetadataManager.cpp`
* **精确定位**：`getMeta` 函数实现

**修改前 (old_str)**：
```cpp
RuntimeMeta MetadataManager::getMeta(const std::wstring& path) {
    std::wstring nPath = normalizePath(path);
    auto currentSnapshot = std::atomic_load(&m_snapshot);
    if (currentSnapshot) {
        auto it = currentSnapshot->find(nPath);
        if (it != currentSnapshot->end()) {
            return it->second;
        }
    }
    return RuntimeMeta();
}
```

**修改后 (new_str)**：
```cpp
RuntimeMeta MetadataManager::getMeta(const std::wstring& path) {
    std::wstring nPath = normalizePath(path);
    size_t idx = getShardIndex(nPath);
    std::shared_lock<std::shared_mutex> lock(m_shards[idx].mutex);
    auto it = m_shards[idx].items.find(nPath);
    if (it != m_shards[idx].items.end()) {
        return it->second;
    }
    return RuntimeMeta();
}
```

---

* **精确定位**：`setRating` 函数实现

**修改前 (old_str)**：
```cpp
void MetadataManager::setRating(const std::wstring& path, int rating, bool notify) {
    std::wstring nPath = normalizePath(path);
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        auto currentSnapshot = std::atomic_load(&m_snapshot);
        auto newMap = std::make_shared<std::unordered_map<std::wstring, RuntimeMeta>>(*currentSnapshot);
        (*newMap)[nPath].rating = rating;
        std::atomic_store(&m_snapshot, std::shared_ptr<const std::unordered_map<std::wstring, RuntimeMeta>>(newMap));
    }
    if (notify) {
        notifyUI(RefreshLevel::PathUpdate, QString::fromStdWString(nPath));
    }
}
```

**修改后 (new_str)**：
```cpp
void MetadataManager::setRating(const std::wstring& path, int rating, bool notify) {
    std::wstring nPath = normalizePath(path);
    size_t idx = getShardIndex(nPath);
    {
        std::unique_lock<std::shared_mutex> lock(m_shards[idx].mutex);
        m_shards[idx].items[nPath].rating = rating;
    }
    if (notify) {
        notifyUI(RefreshLevel::PathUpdate, QString::fromStdWString(nPath));
    }
}
```

## 已知问题 / 待办
1. 阶段二将建立基于分片事件通知的内存倒排索引（`TagIndex` / `CategoryIndex`），在此之前标签与分类查询使用分片迭代。
2. 本次变更完成后需运行 unit test 进行并发安全验证。

## 涉及文件清单
1. `src/meta/MetadataManager.h`（修改：升级成员结构，替换为 256 分片容器）
2. `src/meta/MetadataManager.cpp`（修改：重构读写函数，移除 COW 内存深拷贝）
