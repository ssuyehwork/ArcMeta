# 拖拽归类重复导入防呆重构 —— drag-drop-de-duplication.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在 ArcMeta 应用中，托管库（Library 模式）内部的拖拽归类操作应当仅作为逻辑分类关联。然而在实际运行时，同库拖拽行为被错误判定为跨库拖拽，导致了重复导入逻辑并弹出了查重窗口，严重破坏了 0 毫秒静默归类的流畅体验。本方案旨在修复底层同库/跨库判定，彻底解决此问题。

## 2. 问题定位
在 `src/core/CategoryDropProcessor.cpp` 的 `processDroppedPathsAsync` 异步处理函数中，同库与跨库的判定基于：
`isCrossLibrary = !srcPath.startsWith(targetLibraryPath, Qt::CaseInsensitive);`
其中 `srcPath` 是原始未经过标准化的路径（例如包含正斜杠 `/`），而 `targetLibraryPath` 是从数据库中读取出来并已经过系统级 `normalizePath` 转换为 Windows 原生反斜杠 `\` 的标准化物理路径。
由于斜杠方向（正斜杠 `/` 与反斜杠 `\`）不匹配，`startsWith` 匹配必定失败，进而导致同库拖拽一律被判定为“跨库”，错误触发了 `migrateCapsuleToLibrary` 并重新生成 ID 重复导入，最终弹出了查重对话框。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 0    | Step 1 中确认的"核心问题"：拖拽归类重复导入判定失效 | 本方案核心事件名：拖拽归类重复导入防呆重构 —— drag-drop-de-duplication.md | ✅ |
| 1    | 只要项目已经在系统内部（无论是“全部数据”、“未分类”还是某个自定义分类）（对应用户原话："只要项目已经在系统内部（无论是“全部数据”、“未分类”还是某个自定义分类）"） | 方案中对所有已在系统内部（托管库内）的资产均通过标准化路径对账正确识别（见 4.1） | ✅ |
| 2    | 拖拽到任何左侧分类，只能且必须是 0 毫秒静默归类，严禁弹出任何查重窗口、严禁走任何导入管线！（对应用户原话："拖拽到任何左侧分类，只能且必须是 0 毫秒静默归类，严禁弹出任何查重窗口、严禁走任何导入管线！"） | 方案对同库拖拽行为修正为 100% 走静默逻辑分类绑定分支，绝不触发查重窗口与导入管线（见 4.1） | ✅ |

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 修复同库/跨库路径标准化判定

在 `src/core/CategoryDropProcessor.cpp` 中，针对拖入的路径 `srcPath` 和目标库的根路径 `targetLibraryPath` 均进行完备的 `normalizePath` 标准化处理，对齐所有路径格式，以确保 `startsWith` 比较的绝对正确。

```
<<<<<<< SEARCH
                    Category cur = targetCat;
                    while (cur.id > 0 && cur.parentId != 0) {
                        cur = CategoryRepo::getById(cur.parentId);
                    }
                    QString targetLibraryPath = QString::fromStdWString(cur.physicalPath);

                    bool isCrossLibrary = false;
                    if (!targetLibraryPath.isEmpty()) {
                        isCrossLibrary = !srcPath.startsWith(targetLibraryPath, Qt::CaseInsensitive);
                    }
=======
                    Category cur = targetCat;
                    while (cur.id > 0 && cur.parentId != 0) {
                        cur = CategoryRepo::getById(cur.parentId);
                    }
                    QString targetLibraryPath = QString::fromStdWString(cur.physicalPath);

                    bool isCrossLibrary = false;
                    if (!targetLibraryPath.isEmpty()) {
                        // 预对齐：对目标库路径进行完备的物理标准化转换
                        std::wstring normTargetW = MetadataManager::normalizePath(targetLibraryPath.toStdWString());
                        QString normTarget = QString::fromStdWString(normTargetW);
                        
                        // 预对齐：使用已经过 normalizePath 标准化的 wPath 变量转换作为判定源
                        QString normSrc = QString::fromStdWString(wPath);
                        
                        isCrossLibrary = !normSrc.startsWith(normTarget, Qt::CaseInsensitive);
                    }
>>>>>>> REPLACE
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/core/CategoryDropProcessor.cpp`（具体修改行号位于 `processDroppedPathsAsync` 函数内的同/跨库判定处，约第 115-135 行左右）

**明确禁止越界修改的范围：**
- [ ] 其它逻辑分类批量绑定及 `CategoryRepo` 操作——不修改。
- [ ] `AssetImporter` 类物理复制与导入逻辑——不修改。
- [ ] `MetadataManager::isInsideManagedLibrary` 标准化检测函数主体——不修改。

## 6. 实现准则与预警【核心】
1. 必须使用 `MetadataManager::normalizePath` 对路径进行双向标准化，杜绝手写路径替换逻辑，避免斜杠不一致造成的 Bug。
2. 确保在 `isCrossLibrary` 的比较中直接复用已经过标准化转换的本地宽字符变量 `wPath`，消除重复进行 `normalizePath` 转换的 CPU 开销。
3. 确保判定无误，以使同库内拖动时 100% 走静默数据库批量写入分支，杜绝任何导入管线的唤起（对应用户原话：“严禁弹出任何查重窗口、严禁走任何导入管线！”）。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 内容面板数据源判定与强类型契约规范 | 判定数据源必须统一通过 ContentPanel::dataSourceType() 接口，且 inManagedContext() == true 时元数据 100% 写入统一 SQLite 数据库。 | ✅（本方案通过修正拖拽归类判定，完美维持了托管库内数据流向 SQLite 数据库的纯净契约，不侵入该规范要求） |
| 双轨路由物理隔离机制 | 托管库模式下数据完全由数据库中心表驱动。 | ✅（本方案修正后，确保库内拖拽完全走数据库静默关联记录，不产生多余的磁盘 I/O） |

## 8. 待确认事项（可选）
无。
