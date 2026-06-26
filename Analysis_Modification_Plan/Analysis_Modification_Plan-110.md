# 选中项目主线程磁盘访问消除 —— Analysis_Modification_Plan-110.md

## 1. 任务背景

用户在内容面板点击选中某个项目时，`MainWindow.cpp:311` 处会构造 `QFileInfo info(path)` 并连续调用 8 次属性方法。这些调用在**主线程同步执行**，每次都会触发 Windows 文件系统 API（`GetFileAttributesW`、`GetFileInformationByHandle`），导致 UI 线程在 HDD / 网络路径 / USB 存储设备上出现可感知的卡顿或冻结。

与此同时，`ItemRecord`（`src/core/IndexedEntry.h` 第 14 行）在扫描阶段已经预取并缓存了全部所需字段（`size`、`mtime`、`ctime`、`atime`、`isDir`、`suffix`），但选中回调完全没有利用这些缓存，属于明确的设计遗漏。

---

## 2. 问题定位

### 2.1 有问题的代码段

**文件**：`src/ui/MainWindow.cpp`
**函数**：`connect(m_contentPanel, &ContentPanel::selectionChanged, ...)` 内部 lambda
**行号**：L311–L338

```
L311: QFileInfo info(path);
L315: info.fileName().isEmpty() ? path : info.fileName()
L316: info.isDir() ? "文件夹" : info.suffix().toUpper()
L317: info.isDir() ? "-" : QString::number(info.size()/1024) + " KB"
L318: info.birthTime().toString("yyyy-MM-dd")
L319: info.lastModified().toString("yyyy-MM-dd")
L320: info.lastRead().toString("yyyy-MM-dd")
L337: info.isDir() ? info.absoluteFilePath() : info.absolutePath()
```

### 2.2 根因

`QFileInfo` 是惰性求值对象——构造时不访问磁盘，但每次调用 `isDir()`、`size()`、`birthTime()` 等属性时，Qt 内部通过 `QFileSystemEngine::fillMetaData()` → `GetFileInformationByHandleEx` 同步查询磁盘。共 **8 次**文件系统调用全部在主线程中阻塞执行。

### 2.3 已有缓存字段对照

| `QFileInfo` 调用 | `ItemRecord` 对应字段（已有） | 数据类型 |
|---|---|---|
| `info.isDir()` | `record.isDir` | `bool` |
| `info.size()` | `record.size` | `long long`（字节）|
| `info.birthTime()` | `record.ctime` | `long long`（毫秒时间戳）|
| `info.lastModified()` | `record.mtime` | `long long`（毫秒时间戳）|
| `info.lastRead()` | `record.atime` | `long long`（毫秒时间戳）|
| `info.suffix()` | `record.suffix` | `QString` |
| `info.fileName()` | 可由 `record.path` 字符串截取，与 `data()` L147–151 同款逻辑 | — |
| `info.absoluteFilePath()` / `absolutePath()` | `record.path` 直接使用 | `QString` |

---

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|---|---|---|---|
| 1 | 选中项目出现延迟、缓慢 | 根因定位在 `MainWindow.cpp:311` 的主线程 `QFileInfo` 连续调用 | ✅ |
| 2 | ItemRecord 已有 size/mtime/ctime，为何还要实时读取 | 方案将 QFileInfo 全部替换为从 `idx.data(XxxRole)` 读取 | ✅ |
| 3 | 只修复选中延迟，不改变其他功能 | 方案仅修改 `MainWindow.cpp:311–338` 的 lambda 内部 | ✅ |
| 4 | ModelContract.h 无 SizeRole / MtimeRole 等专用 Role | 方案新增 6 个 Role | ✅ |

---

## 4. 详细解决方案

### 4.1 步骤一：`ModelContract.h` 新增 6 个 Role

在 `enum CommonRole` 的 `CountRole`（UserRole + 204）和 `RegistrationProgressRole`（UserRole + 205）之后追加：

```
// 伪代码说明，禁止直接作为可执行代码使用
FileSizeRole = Qt::UserRole + 206,  // 文件大小（long long，字节），文件夹为 0
MtimeRole    = Qt::UserRole + 207,  // 修改时间（long long，毫秒时间戳）
CtimeRole    = Qt::UserRole + 208,  // 创建时间（long long，毫秒时间戳）
AtimeRole    = Qt::UserRole + 209,  // 访问时间（long long，毫秒时间戳）
IsDirRole    = Qt::UserRole + 210,  // 是否为文件夹（bool）
SuffixRole   = Qt::UserRole + 211,  // 文件后缀（QString，小写）
```

> ⚠️ 数值从 +206 紧接现有最大值 +205，实现前须 grep 确认 +206~+211 区间无其他私有 Role 占用。

### 4.2 步骤二：`ContentPanel.cpp` 的 `data()` 补充处理

在 `data()` 函数中现有 `else if` 链（约 L180–L200 区域），在 `AspectRatioRole` 分支之前插入：

```
// 伪代码说明
else if (role == IsDirRole) {
    return record.isDir;
} else if (role == FileSizeRole) {
    return record.size;                  // long long 原始字节数
} else if (role == MtimeRole) {
    return record.mtime;                 // long long 毫秒时间戳
} else if (role == CtimeRole) {
    return record.ctime;                 // long long 毫秒时间戳
} else if (role == AtimeRole) {
    return record.atime;                 // long long 毫秒时间戳
} else if (role == SuffixRole) {
    return record.suffix;                // QString 小写后缀
}
```

`isCategory == true` 的分支（L116–L139）对未处理 Role 已 `return QVariant()`，行为安全，不需额外处理。

### 4.3 步骤三：`MainWindow.cpp` L311–L338 替换为读 Role

将：

```
// 现有代码（伪代码示意）
QFileInfo info(path);
m_metaPanel->updateInfo(
    info.fileName(), info.isDir() ? "文件夹" : ...,
    info.size(), info.birthTime(), info.lastModified(), info.lastRead(),
    info.absoluteFilePath(), idx.data(EncryptedRole).toBool()
);
QString category = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
m_metaPanel->setCategory(category);
```

替换为（伪代码说明，全部读内存，零磁盘访问）：

```
// 1. 从 Role 读原始值（全部内存操作）
bool      isDir  = idx.data(IsDirRole).toBool();
long long size   = idx.data(FileSizeRole).toLongLong();
long long ctime  = idx.data(CtimeRole).toLongLong();
long long mtime  = idx.data(MtimeRole).toLongLong();
long long atime  = idx.data(AtimeRole).toLongLong();
QString   suffix = idx.data(SuffixRole).toString();

// 2. 文件名：纯字符串截取，与 data() column0 同款逻辑，零 I/O
int lastSlash = std::max(path.lastIndexOf('\\'), path.lastIndexOf('/'));
QString fileName = (lastSlash == -1) ? path : path.mid(lastSlash + 1);
if (fileName.isEmpty()) fileName = path;

// 3. 格式化（纯内存运算）
QString typeStr = isDir ? "文件夹" : suffix.toUpper() + " 文件";
QString sizeStr = isDir ? "-" :
    (size < 1024         ? QString::number(size) + " B"  :
     size < 1024*1024    ? QString::number(size/1024.0,'f',1) + " KB" :
                           QString::number(size/(1024.0*1024),'f',1) + " MB");
QString ctimeStr = (ctime > 0) ? QDateTime::fromMSecsSinceEpoch(ctime).toString("yyyy-MM-dd") : "-";
QString mtimeStr = (mtime > 0) ? QDateTime::fromMSecsSinceEpoch(mtime).toString("yyyy-MM-dd") : "-";
QString atimeStr = (atime > 0) ? QDateTime::fromMSecsSinceEpoch(atime).toString("yyyy-MM-dd") : "-";

// 4. 更新 UI（与原接口签名完全一致）
m_metaPanel->updateInfo(
    fileName, typeStr, sizeStr,
    ctimeStr, mtimeStr, atimeStr,
    path,                              // absoluteFilePath → 直接用 path
    idx.data(EncryptedRole).toBool()
);

// 5. category 行修复
int lastSep = std::max(path.lastIndexOf('\\'), path.lastIndexOf('/'));
QString category = isDir ? path : (lastSep > 0 ? path.left(lastSep) : path);
m_metaPanel->setCategory(category);
```

> 修改完成后，`QFileInfo info(path)` 这行可完全删除，不再有任何 QFileInfo 调用。

---

## 5. 修改边界声明【红线】

**本次方案涉及范围：**
- [ ] `src/core/ModelContract.h`：新增 6 个 Role 枚举值（+206 ~ +211）
- [ ] `src/ui/ContentPanel.cpp`：`FerrexVirtualDbModel::data()` 函数中追加 6 个 Role 的 else-if 分支
- [ ] `src/ui/MainWindow.cpp`：L311–L338 的 lambda 中，删除 `QFileInfo info(path)` 及其所有属性调用，改为读 Role

**明确禁止越界修改的范围：**
- [ ] `loadDirectory` / `createItemRecord` 扫描逻辑——不修改
- [ ] `MetaPanel::updateInfo` 接口签名——不修改
- [ ] 分类模式 `loadCategory` 的任何逻辑——不修改
- [ ] `DecorationRole` 缩略图生成逻辑——不修改

---

## 6. 实现准则与预警【核心】

### 6.1 头文件依赖

| 修改文件 | 需要的 #include | 说明 |
|---|---|---|
| `ModelContract.h` | `<Qt>`（已有）| 新 Role 同文件，无需新增 include |
| `ContentPanel.cpp` | 已包含 `ModelContract.h` | 新 Role 定义来自同文件，无需新增 |
| `MainWindow.cpp` | 已包含 `ModelContract.h` | 新 Role 来自同文件，无需新增 |
| `MainWindow.cpp` | `<QDateTime>` | 用于 `QDateTime::fromMSecsSinceEpoch()`，实现前须确认是否已 include |

### 6.2 Role 数值冲突预警（必做）

实现前必须执行以下搜索，确认 +206~+211 区间未被占用：

```
grep -rn "UserRole + 20[6-9]\|UserRole + 21[0-1]" src/
```

若发现冲突，从已有最大值 + 1 顺延。

### 6.3 `record.ctime` 语义确认（必做）

`ctime` 在 Windows 下语义为"创建时间"（对应 `FILETIME ftCreationTime`），与 `info.birthTime()` 等价。但**必须在 `fetchWinApiMetadataDirect` 函数中核实** `r.ctime` 的赋值来源，防止语义错误导致创建时间显示错误。

### 6.4 `atime / ctime / mtime == 0` 防护

当时间戳为 0 时，不显示 "1970-01-01"，改为显示 "-"。方案中已加入 `(xxx > 0) ?` 的判断，实现时须对齐此逻辑。

### 6.5 `isCategory` 条目防护

`ModelContract.h` 中的分类条目（`isCategory == true`）在 `data()` 中对未处理 Role 返回 `QVariant()`，`toLongLong()` / `toBool()` 的默认值均为 0 / false，UI 显示无异常，无需额外处理。

---

## 7. Memories.md 合规检查

本次修改不涉及新 UI 组件，主要为模型层 Role 新增与 MainWindow 逻辑修正。

| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|---|---|---|
| ModelContract.h Role 新增 | 无现有规范 | 遵循现有 XxxRole 命名风格；建议用户补充规范 |
| `data()` 函数修改 | 无现有规范 | ✅ 仅追加 else-if 分支，不破坏现有逻辑 |
| 主线程 I/O 约束 | Memories.md 提及"避免主线程 I/O" | ✅ 本方案正是消除主线程 I/O 的修复 |

---

## 8. 待确认事项

1. **`record.ctime` 赋值来源**：请在 `fetchWinApiMetadataDirect` 中确认 `r.ctime` 对应 `ftCreationTime`（创建时间），而非 Unix 语义的状态变更时间。

2. **`atime == 0` 展示策略**：方案中 `atime == 0` 时显示 "-"，请确认是否接受此行为。

3. **`QDateTime` 头文件**：请在 `MainWindow.cpp` 顶部确认是否已有 `#include <QDateTime>`，若无则须补充。
