# .arc 资产包封装架构与侧边栏一等公民重构（不含自动导入） —— Modification_Plan-2.md

> 状态：待批准执行

## 1. 任务背景
在 DAM（数字资产管理）系统的高性能与清晰架构重构中，上一代方案 `Modification_Plan-1.md` 提出了 `.arc` 资产包物理封装、一等公民分类树状系统、添加日期、多媒体提取和“创建自动导入”的设想。

本方案承接自 `Modification_Plan-1.md` 的部分重构。根据用户最新作出的决策：
* 仅保留对 `ArcMeta.Library_[盘符]`（资产包物理封装与侧边栏一等公民树模型）的重构方案。
* 彻底弃用“创建自动导入”功能与外部临时监控（In-Place Watcher）。
由于 `AGENTS.md` 约束已创建的 `Modification_Plan-N.md` 在创建后禁止第二次修改。因此，在本轮决策中，将“弃用并彻底根除自动导入与外部临时监控”这一话题记录在 `Modification_Plan-2.md` 中，而遗漏的关于“磁盘模式下库外离散标记分流机制”这一纯净新话题将直接作为 `Modification_Plan-3.md` 新建。

---

## 2. 核心架构演进图谱

```text
[ 物理磁盘层 (D:\ArcMeta.Library_D\) ] ──> 极简平铺，绝对不深度嵌套
  ├── m8crzbs3jb7dj.arc/ ───> 原始文件.psd + _thumbnail.png (256x256)
  └── k9x2p1q4r8v5z.arc/ ───> 照片.jpg + _thumbnail.png (256x256)

[ 侧边栏分类树 (彻底废除“我的分类”外壳) ]
  ├── ArcMeta.Library_D (一等公民，parentId = 0)
  │   └── 文件夹 A (次等分类，parentId = Library_D)
```

---

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|:---:|---|---|:---:|
| 1 | 仅对 `ArcMeta.Library_[盘符]` 资产包重构。 | 详见 4.1、4.2 节重构方案。 | ✅ |
| 2 | 彻底弃用并物理物理根除“创建自动导入”功能（不使用外部临时监控 In-Place Watcher）。 | 详见 4.3 节彻底物理根除方案。 | ✅ |

---

## 4. 详细重构解决方案

### 4.1 重构：`.arc` 物理资产包封装规范与一等公民树模型
*   **物理封装机制**：
    所有托管库根目录 `[盘符]:/ArcMeta.Library_[盘符]/` 采用 `.arc` 后缀目录进行物理文件平铺封装。
*   **13 位 Base36 ID 产生器**：
    在 `src/util/ShellHelper.h` 封装 13 位唯一 ID 产生器。
*   **一等公民设定**：
    在 `CategoryModel` 和 `CategoryPanel` 中，将 `ArcMeta.Library_[盘符]` 提拔为 `parentId = 0` 的直属根分类节点挂载在分类树最顶层。
*   **内容面板过滤**：
    在 `ContentPanel` 中的 `loadCategory` 视图与 `scanDir` 物理扫描中拦截过滤 `.arc` 后缀文件夹，使用户完全感知不到 `.arc` 容器。

### 4.2 智能拖拽/导入分流器（`AssetImporter`）
*   当用户向内容面板拖拽单文件、散落的多文件或者整个物理文件夹时，分流进入 `AssetImporter`。
*   **单文件导入**：在库下建立 `m8crzbs3jb7dj.arc` 文件夹并移动，提取缩略图。
*   **文件夹导入**：在逻辑树中递归新建此文件夹逻辑树，逻辑关联资产包。

### 4.3 物理毁灭：彻底净化根除“创建自动导入”与 In-Place Watcher
*   **彻底根除 `CustomFolderImportDialog` 对话框**：
    物理删除该弹窗声明与定义，彻底断开一切对其 `include` 和调用。
*   **彻底根除主窗口中的自动导入 UI 入口与处理**：
    删除 `onDriveBarContextMenu` 和 `onFolderButtonContextMenu` 中的“新建自动导入”菜单项和槽绑定，并在主窗口中移除 `showNewAutoImportDialog()` 声明与实现。
*   **彻底根除配置文件和 App 启动对自定义监控路径的监听点火**：
    在 `MainWindow::updateCustomFolderButtons()` 和 `CoreController` 的初始化逻辑中，彻底删除对 `DriveBar/CustomMonitoredFolders` 配置的循环读取、`addWatch()` 以及对 `AutoImportManager::instance().handleRecursiveIngestion()` 的异步调用，彻底切断外部监控队列。

---

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] `src/ui/MainWindow.h` / `src/ui/MainWindow.cpp`（删除自动导入 Dialog 和相关主入口槽）
- [ ] `src/core/CoreController.cpp`（完全清理启动时对 custom monitored folder 路径的读取与点火）
- [ ] `src/core/AutoImportManager.h` / `src/core/AutoImportManager.cpp`（物理清除服务于外部临时文件夹的所有重对账及导入队列处理逻辑）

**明确禁止越界修改的范围：**
- [ ] IOCP 底层 `NativeFolderWatcher` 框架驱动 —— 不修改

---

## 6. 实现准则与安全预警【核心】
1.  **编译依赖保障**：彻底清除 `CustomFolderImportDialog` 后，需检查所有文件的 `include` 或强转，防止编译失败。
2.  **避免二次干扰**：移除外部高防抖队列，确保不与 `NativeFolderWatcher` 产生冲突。

---

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 自动导入彻底根除 | 彻底无死角清理“创建自动导入” QAction 菜单项及 CustomFolderImportDialog 界面 | ✅ 符合 |

---

## 8. 待确认事项（可选）
暂无。