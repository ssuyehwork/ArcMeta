# 物理 .arc 容器扫描拦截、分类及关联条目去重清洗重构 —— Modification_Plan-6.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在 DAM（数字资产管理）系统的 `.arc` 资产包重构中，主应用对托管库 `ArcMeta.Library_X` 采用了以 `.arc` 后缀目录进行物理文件平铺封装。然而，由于在分类物理对账扫描时，没有对 `.arc` 后缀的文件夹外壳进行排除拦截，导致系统在物理扫描中将 `.arc` 包裹容器误当成了普通物理子文件夹，进而在 `categories` 逻辑分类表和 `category_items` 关系表中记入了脏数据，导致关联条目的数量在统计时意外翻倍（如 12 变 24）。

本方案作为一个独立、纯净的新话题，旨在彻底解决 `.arc` 容器在 `scanPhysicalDirectory` 时被误识别为分类文件夹的机制漏洞，并物理清洗已产生的历史脏数据，确保分类统计数字 100% 精确。

---

## 2. 问题定位
*   **物理分类扫描漏洞**：
    在 `src/meta/CategoryRepo.cpp` 中的 `scanPhysicalDirectory` 递归扫描函数（第 1319 行起）中，当遍历到硬盘上的 `.arc` 容器（如 `00ms68rrcz001.arc`）时，由于其在操作系统层面判定为文件夹（`fi.isDir() == true`），且代码中缺乏对其后缀名 `.arc` 的拦截过滤（对应用户原话：“当扫描 ArcMeta.Library_Z 目录时，硬盘上的所有 .arc 资产包...在操作系统看来全部都是 fi.isDir() == true！”），导致：
    1.  代码错误地将其作为逻辑子节点 `children` 加入树状结构，从而被存入 SQLite 的 `categories` 表中。
    2.  扫描时分别记录了 `.arc` 外壳的 FID 和内部真实文件的 FID，在 `category_items` 表里写入了双重映射，致使计数结果恰好翻倍（对应用户原话：“在之前的 scanPhysicalDirectory 漏洞代码中...结果：12 个 .arc 外壳盒子的 FID + 12 个真实文件的 FID...在数据库里生成了 24 条关联记录，计数刚好翻了一倍！”）。

*   **数据清洗缺失**：
    已写入数据库 `categories` 里的 `.arc` 结尾假分类数据与 `category_items` 里的外壳映射垃圾数据缺乏主动一键物理清洗机制。

---

## 3. 强制对照表

| 编号 | 用户原话 / 需求点 | 方案对应点 | 是否一致 |
|:---:|---|---|:---:|
| 1 | 在 `src/meta/CategoryRepo.cpp` 中将 `scanPhysicalDirectory` 替换为带 `.arc` 强力阻断的代码 | 详见 4.1 节，在 `scanPhysicalDirectory` 的 `fi.isDir()` 分支顶部插入 `.arc` 后缀阻断拦截。 | ✅ |
| 2 | 直接扫描 `.arc` 内部的真实物理文件，将其作为文件塞入 `node.files` | 详见 4.1 节，过滤并递归遍历 `.arc` 内部的文件直接追加至 `node.files` 中。 | ✅ |
| 3 | 在 `DatabaseManager.cpp` 初始化时，执行 SQL 一键清洗误入的 `.arc` 假节点与脏数据记录 | 详见 4.2 节，在数据库连接初始化 schema 后，执行 `DELETE FROM categories` 及 `DELETE FROM category_items` 物理清洗语句。 | ✅ |

---

## 4. 详细解决方案

### 4.1 解决 1：在 `CategoryRepo.cpp` 中拦截阻断 `.arc` 容器成为分类
在 `src/meta/CategoryRepo.cpp` 中重构 `scanPhysicalDirectory` 函数（约 L1319起），加装 **`.arc` 后缀物理防线**（对应用户原话：“核心物理防线：如果该目录是以 .arc 结尾的资产包容器，绝对禁止作为子分类（node.children）添加！”）：
*   在 `fi.isDir()` 条件分支内部的第一步，进行 `.arc` 后缀判断拦截；
*   若命中 `.arc` 容器文件夹，调用 `entryInfoList` 直接提取并扫描该包内部包含的所有真实物理文件，并以文件形式直接压入 `node.files` 列表中（对应用户原话：“直接扫描 .arc 内部的真实物理文件，将其作为文件塞入 node.files”）；
*   直接执行 `continue;` 跳过该节点的子分类创建，彻底阻断其污染分类树。

**重构后的核心代码实现**：
```cpp
static void scanPhysicalDirectory(const QString& currentPath, ScanNode& node) {
    QDir currentDir(currentPath);
    QFileInfoList list = currentDir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden);

    for (const QFileInfo& fi : list) {
        std::wstring wPath = QDir::toNativeSeparators(fi.absoluteFilePath()).toStdWString();
        if (fi.isDir()) {
            // 🚨 核心物理防线：如果该目录是以 .arc 结尾的资产包容器，绝对禁止作为子分类（node.children）添加！
            if (fi.fileName().endsWith(".arc", Qt::CaseInsensitive)) {
                // 直接扫描 .arc 内部的真实物理文件，将其作为文件塞入 node.files
                QDir arcDir(fi.absoluteFilePath());
                QFileInfoList arcFiles = arcDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
                for (const QFileInfo& afi : arcFiles) {
                    node.files.push_back(QDir::toNativeSeparators(afi.absoluteFilePath()).toStdWString());
                }
                continue; // 彻底跳过将 .arc 自身创建为分类！
            }

            std::string fid;
            std::wstring frnStr;
            if (MetadataManager::fetchWinApiMetadataDirect(wPath, fid, &frnStr)) {
                try {
                    ScanNode childNode;
                    childNode.path = wPath;
                    childNode.name = fi.fileName().toStdWString();
                    childNode.frn = std::stoull(frnStr, nullptr, 16);
                    childNode.isDir = true;
                    scanPhysicalDirectory(fi.absoluteFilePath(), childNode);
                    node.children.push_back(std::move(childNode));
                } catch (...) {}
            }
        } else {
            node.files.push_back(wPath);
        }
    }
}
```

### 4.2 解决 2：在 `DatabaseManager.cpp` 中部署数据自动去重与一键物理清洗
在 `src/meta/DatabaseManager.cpp` 的 `DatabaseManager::init` 实例化方法中（在 `DELETE FROM categories WHERE id <= 0;` 系统的历史保留 ID 清洗逻辑之后），部署专门针对误记入 `.arc` 假分类与垃圾关系的自动物理去重机制（对应用户原话：“在 DatabaseManager.cpp 初始化时，执行以下这行 SQL，可以一键把数据库历史上误把 .arc 盒子当成文件记入的 12 条脏数据彻底清理掉”）：
1.  **清洗逻辑分类表**（对应用户原话：“DELETE FROM categories WHERE name LIKE '%.arc';”）：
    ```cpp
    const char* arcCleanup1 = "DELETE FROM categories WHERE name LIKE '%.arc';";
    sqlite3_exec(conn.memDb, arcCleanup1, nullptr, nullptr, nullptr);
    ```
2.  **清洗关联映射表**（对应用户原话：“DELETE FROM category_items WHERE path_hint LIKE '%.arc' OR path_hint LIKE '%.arc\%';”）：
    ```cpp
    const char* arcCleanup2 = "DELETE FROM category_items WHERE path_hint LIKE '%.arc' OR path_hint LIKE '%.arc\\%';";
    sqlite3_exec(conn.memDb, arcCleanup2, nullptr, nullptr, nullptr);
    ```

---

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] `src/meta/CategoryRepo.cpp`（重构 `scanPhysicalDirectory`，物理阻断并平铺解析以 `.arc` 结尾的封装容器目录）
- [ ] `src/meta/DatabaseManager.cpp`（在连接初始化后植入 `arcCleanup1` 与 `arcCleanup2` 清洗对账执行，清除脏分类及冗余盒子关联记录）

**明确禁止越界修改的范围：**
- [ ] IOCP 底层 `NativeFolderWatcher` 文件系统实时监听服务 —— 不修改
- [ ] SQLite 的 `metadata` 与 `categories` 的建表 Schema —— 不修改

---

## 6. 实现准则与安全预警【核心】

1.  **Qt 后缀名大小写敏感保障**：在进行 endsWith 阻断判断时，务必加上 `Qt::CaseInsensitive` 参数，防止 `.ARC`、`.Arc` 等大小写变形逃避拦截。
2.  **字符串安全转移符**：在 C++ 中编写 SQL 语句时，LIKE 子句中的 `\` 必须按照双反斜杠 `\\` 语法转义写入，防止转义符吞噬导致 SQLite 语法报错。
3.  **对齐 MFT 与物理一致性**：对 `category_items` 与 `categories` 的清理仅作用于软件的逻辑分类管理映射中，绝对不触碰、物理删除或变动用户磁盘上的任何物理 `.arc` 包与内部实际资产。

---

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 自动对账去重清洗 | 保证物理 `.arc` 包不被当作文件夹分类加载。在系统运行、同步时对历史遗存脏数据进行安全过滤清洗，恢复精准计数 | ✅ 符合 |

---

## 8. 待确认事项（可选）
暂无。所有漏洞、计数重影现象、根源及代码修复逻辑均已定位冻结。