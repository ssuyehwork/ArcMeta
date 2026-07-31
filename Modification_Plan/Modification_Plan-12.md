# 重构内存数据库模式唯一ID体系为 Base36 ID —— Modification_Plan-12.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在目前的应用架构中，内存数据库模式使用 Windows 系统级的 `fileId128`（即物理 FRN / 文件引用号）作为资产的唯一身份标识。在跨分区物理移动、外接移动硬盘插拔、非 NTFS 分区（如 FAT32 / exFAT）、以及多机对账同步等复杂多场景下，物理 `fileId128` 极易由于操作系统的底层变动而产生断层或冲突，给数据的绝对留存带来隐患。
为实现绝对持久且防飘移、系统级容灾的逻辑绑定，本方案将内存数据库模式的唯一 ID 体系从系统级 `fileId128` 全面替换为由 `ShellHelper::generateBase36Id()` 生成的 13 位 Base36 字符串（即受控容器的目录名，例如 `00ms73182x000`）。该改造仅影响内存数据库内部的身份标识机制，不改变磁盘模式的任何行为，确保磁盘模式 100% 的纯净性。

## 2. 问题定位
经过全量代码库的深度排查与检索，定位出引用 `fileId128`/`fid` 作为内存数据库模式主键或关联键的所有具体关键位置如下：

1. **`src/meta/MetadataManager.cpp`**
   - **`MetadataManager::getFileIdSync(const std::wstring& path)`**：此函数用于在登记或建立分类关联时，实时获取特定路径的唯一关联 ID。在资产被包装至 `.arc`（如 `00ms73182x000.arc`）之后，此处应优先提取并返回 13 位 Base36 ID。
   - **`MetadataManager::ensureActivated(const std::wstring& nPath)`**：用于将文件元数据激活并读入内存缓存 `m_cache`。在创建、导入、或首次对账载入缓存时，应当将对应的 `rm.fileId128` 优先设为 13 位 Base36 ID，使写盘和内存映射同步归一化为 Base36 ID。

2. **`src/core/IndexedEntry.cpp`**
   - **`ItemRecord::create(const QString& path, const RuntimeMeta* providedMeta)`**：在创建内容面板显示卡片对应的 `ItemRecord` 时，如果元数据缺失而触发了底层物理采样，其 `r.fileId` 应当优先使用 `getFileIdSync` 返回的 Base36 ID，确保前台 UI 渲染和逻辑与内存数据库完全接轨对齐。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | `category_items` 表：外键字段从 `fileId128` 改为 `00ms73182x000`（Base36 ID） | 4.1 节，改造 `getFileIdSync`，当路径属于受控包时，直接将关联表的外键写入为 Base36 ID | ✅ |
| 2    | `RuntimeMeta`：主键字段从 `fileId128` 改为 `00ms73182x000` | 4.1 节，改造 `ensureActivated`，使写入 SQLite `metadata` 主表与内存 `m_cache` 的 `fileId128` 字段为 Base36 ID | ✅ |
| 3    | `ItemRecord`：`fileId` 字段的取值来源从 `fileId128` 改为 `00ms73182x000` | 4.2 节，改造 `ItemRecord::create`，卡片的唯一标识绑定为通过 `getFileIdSync` 获取的 13 位 Base36 ID | ✅ |
| 4    | 所有以 `fid`/`fileId128` 为 key 的关联表、内存缓存结构，统一改为以 `00ms73182x000` 作为 key | 4.1、4.2 节，底层关联和反查主键经由 getFileIdSync 和 ensureActivated 自动归一化为以 Base36 ID 为 key | ✅ |
| 5    | ID 的生成时机：`00ms73182x000` 应在资产首次通过 `importSingleFile()` 打包生成 `.arc` 容器时确定一次，之后作为永久唯一 ID | 4.1 节，通过 extractBase36Id 算法，无论是该资产包文件夹自身还是包内原始文件，均能在首次导入后 100% 映射到相同的永久唯一 ID | ✅ |
| 6    | 不涉及磁盘模式：磁盘模式不使用、也不需要知道这个 ID | 4.1 节，在 extractBase36Id 判定中，非 `.arc` 下的物理磁盘路径不返回 Base36 ID，完美自愈回退到原系统 FRN 逻辑，磁盘模式无任何影响 | ✅ |

## 4. 详细解决方案
本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 改造 `src/meta/MetadataManager.cpp` 以支持永久 Base36 ID 提取与映射
在 `src/meta/MetadataManager.cpp` 中：
- 注入静态辅助方法 `extractBase36Id`。
- 修改 `getFileIdSync` 优先返回提取的 13 位 Base36 ID。
- 修改 `ensureActivated` 在内存预热和缓存建立时，优先用 13 位 Base36 ID 作为 `fileId128` 主键标识。

```
<<<<<<< SEARCH
std::string MetadataManager::getFileIdSync(const std::wstring& path) {
    std::string fid;
    if (!fetchWinApiMetadataDirect(path, fid, nullptr)) fid = MetadataManager::generateDeterministicSha256Id(path);
    return fid;
}
=======
// 🚨 内存数据库模式唯一ID体系重构：路径级 Base36 ID 静态提取解析器
static std::string extractBase36Id(const std::wstring& path) {
    // 查找 ".arc" 容器扩展名在路径中的位置
    size_t pos = path.find(L".arc");
    if (pos == std::wstring::npos) return "";

    // 向上查找紧邻 ".arc" 前方的路径分隔符以界定容器名称
    size_t lastSep = path.rfind(L'\\', pos);
    if (lastSep == std::wstring::npos) {
        lastSep = path.rfind(L'/', pos);
    }

    size_t start = (lastSep == std::wstring::npos) ? 0 : lastSep + 1;
    std::wstring folderName = path.substr(start, pos - start);

    // 托管资产容器文件夹名格式恒为 13 位 Base36 字符串 (如 00ms73182x000)
    if (folderName.length() == 13) {
        return std::string(folderName.begin(), folderName.end());
    }
    return "";
}

std::string MetadataManager::getFileIdSync(const std::wstring& path) {
    // 1. 如果处于受控托管库中，直接提取 13 位 Base36 ID，终结系统级 FRN 物理依赖
    std::string base36 = extractBase36Id(path);
    if (!base36.empty()) {
        return base36;
    }

    // 2. 磁盘模式（非托管路径）不使用 Base36 ID，自愈退避至原本的系统级物理 FRN
    std::string fid;
    if (!fetchWinApiMetadataDirect(path, fid, nullptr)) fid = MetadataManager::generateDeterministicSha256Id(path);
    return fid;
}
>>>>>>> REPLACE
```

```
<<<<<<< SEARCH
    if (success) {
        // 3. 写锁写入缓存
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        if (m_cache.count(nPath)) return; // 二次检查防止竞态

        // 共享元数据逻辑 (FID 关联)
        if (!rm.fileId128.empty() && m_fidToPath.count(rm.fileId128)) {
=======
    if (success) {
        // 3. 写锁写入缓存
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        if (m_cache.count(nPath)) return; // 二次检查防止竞态

        // 🚨 内存数据库模式唯一ID体系重构：激活写入内存缓存前，将主键统一覆盖为 13 位 Base36 ID
        std::string base36 = extractBase36Id(nPath);
        if (!base36.empty()) {
            rm.fileId128 = base36;
        }

        // 共享元数据逻辑 (FID 关联)
        if (!rm.fileId128.empty() && m_fidToPath.count(rm.fileId128)) {
>>>>>>> REPLACE
```

### 4.2 改造 `src/core/IndexedEntry.cpp` 以对齐 ItemRecord 卡片 ID 的提取来源
在 `src/core/IndexedEntry.cpp` 中，修改 `ItemRecord::create` 中缺少缓存触发底层采样时的 ID 提取方式，改由 `getFileIdSync` 统一输出，以确保非 NTFS 或首次加载时卡片 ID 自动收拢到 Base36 ID。

```
<<<<<<< SEARCH
    // Plan-124: 只有在内存缓存缺失物理时间戳时，才触发 fetchWinApiMetadataDirect
    if (meta.fileId128.empty() || (meta.ctime == 0 && meta.mtime == 0)) {
        std::string fid;
        long long size = 0, ctime = 0, mtime = 0, atime = 0;
        MetadataManager::fetchWinApiMetadataDirect(wPath, fid, nullptr, &size, nullptr, &ctime, &mtime, &atime);
        r.size = size;
        r.ctime = ctime;
        r.mtime = mtime;
        r.atime = atime;
        r.fileId = fid;
        r.isDir = QFileInfo(nPath).isDir();
    } else {
=======
    // Plan-124: 只有在内存缓存缺失物理时间戳时，才触发 fetchWinApiMetadataDirect
    if (meta.fileId128.empty() || (meta.ctime == 0 && meta.mtime == 0)) {
        std::string fid;
        long long size = 0, ctime = 0, mtime = 0, atime = 0;
        MetadataManager::fetchWinApiMetadataDirect(wPath, fid, nullptr, &size, nullptr, &ctime, &mtime, &atime);
        r.size = size;
        r.ctime = ctime;
        r.mtime = mtime;
        r.atime = atime;

        // 🚨 内存数据库模式唯一ID体系重构：调用 getFileIdSync 优先解析和提取 Base36 ID
        r.fileId = QString::fromStdString(MetadataManager::instance().getFileIdSync(wPath));

        r.isDir = QFileInfo(nPath).isDir();
    } else {
>>>>>>> REPLACE
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] `src/meta/MetadataManager.cpp`（实现 Base36 路径提取算法，重写 getFileIdSync 与 ensureActivated）
- [ ] `src/core/IndexedEntry.cpp`（物理同步卡片物理采样时 fileId 为 Base36 ID）

**明确禁止越界修改的范围：**
- [ ] `src/meta/DatabaseManager.cpp` 基础数据库建表结构与字段名称——不修改，直接兼容 `TEXT PRIMARY KEY` 写入。
- [ ] `src/util/AssetImporter.cpp` 中由 `importSingleFile` 生成 `.arc` 容器名（原本即为 13 位 Base36 ID）——不修改，只对其在写入数据库时的关联 ID 解析。

## 6. 实现准则与预警【核心】
1. **防空指针崩溃**：在提取 Base36 ID 时，必须通过精确的 `.arc` 定位 and 13 字符长度检验，杜绝因对非托管大文件名误删导致 ID 错配。
2. **磁盘模式 100% 纯净**：非受控盘符路径（不含 `.arc`）直接由 `extractBase36Id` 拦截，确保磁盘模式无感退避原物理 FRN 机制。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 双轨制路由分流 | 各自独立，内存数据库模式解析容器，磁盘模式不做特殊翻译不污染文件 | ✅ 符合。本重构直接让内存数据库全面摆脱对 Windows 磁盘底层物理 FRN 的依赖，达成内存模式纯净运行。 |

## 8. 待确认事项（可选）
- **无**。
