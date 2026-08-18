# ArcMeta 调试日志 (`arcmeta_debug.log`) 全量写入点排查报告

## 1. 概述与写入机制说明

在当前版本的 ArcMeta 架构中，所有调试日志统一由 `ArcMeta::Logger` 模块进行管理并异步/同步写入到项目运行根目录下的 `arcmeta_debug.log` 文件中。

### 写入机制说明：
1. **统一重定向钩子 (`src/main.cpp`)**：
   - 在应用启动时（`main()` 函数第 102 行），通过 `qInstallMessageHandler(customMessageHandler)` 注册自定义消息处理器。
   - 所有由 Qt 标注输出函数（如 `qDebug()`, `qInfo()`, `qWarning()`, `qCritical()`, `qFatal()`）打印的信息均会被拦截，格式化为 `[DEBUG]`/`[WARN ]`/`[CRIT ]` 等统一前缀，并全量投递至 `ArcMeta::Logger::log(...)`。
2. **显式日志调用**：
   - 业务逻辑中直接调用 `ArcMeta::Logger::log(...)` 或 `Logger::log(...)` 的地方，将绕过 Qt 默认重定向，直接投递给后台日志写入线程。
3. **日志滚动与线程安全 (`src/ui/Logger.h`)**：
   - 日志系统采用后台线程 `LoggerWriterThread` 异步批量刷盘。
   - 单个日志文件大小超过 **4MB** 时，会自动执行滚动轮转（将当前日志重命名为 `arcmeta_debug.log.old` 并新建日志文件）。
   - 在程序即将退出时（`onApplicationAboutToQuit`），异步日志线程挂起停止，日志系统降级为同步直写模式，保障退出日志落盘不遗失。

---

## 2. 写入点全量明细清单（精准路径与行号）

本清单列出了当前版本 `src/` 目录下所有最终会写入 `arcmeta_debug.log` 的日志触发代码点，按模块分类排列：

---

### 2.1 系统主入口与日志核心 (`src/main.cpp`, `src/ui/Logger.h`)

| 文件路径 | 精准行号 | 日志类型 / 触发接口 | 触发场景与业务功能说明 |
| :--- | :--- | :--- | :--- |
| `src/main.cpp` | **Line 50** | `customMessageHandler()` | **Qt 消息全量重定向入口**：接收所有 `qDebug/qInfo/qWarning/qCritical/qFatal` 消息并转存至 `arcmeta_debug.log` |
| `src/main.cpp` | **Line 64** | `ArcMeta::Logger::stopAsyncLogger()` | 应用退出清场：挂起并停止异步日志线程 |
| `src/main.cpp` | **Line 99** | `rotateLogFiles("arcmeta_debug.log")` | 程序启动哨兵：检测日志大小，若超 4MB 自动轮转为 `.old` 备份 |
| `src/ui/Logger.h` | **Line 131** | `rotateLogFiles("arcmeta_debug.log")` | 降级直写模式下日志超限轮转检查 |
| `src/ui/Logger.h` | **Line 132** | `QFile file("arcmeta_debug.log")` | 降级直写模式下打开日志文件准备同步追加内容 |
| `src/ui/Logger.h` | **Line 145** | `new LoggerWriterThread(...)` | 首次打印日志时延迟创建异步写入线程 `LoggerWriterThread` |
| `src/ui/Logger.h` | **Line 163** | `Logger::log(...)` | 输出 `Async LoggerWriterThread is stopping...` 日志 |

---

### 2.2 核心调度与检索引擎 (`src/core/`)

| 文件路径 | 精准行号 | 调用的打印语句 / 级别 | 触发场景与业务功能说明 |
| :--- | :--- | :--- | :--- |
| `src/core/CoreController.cpp` | **Line 226** | `ArcMeta::Logger::log(...)` | 触发检索流程：记录搜索关键词、来源类型与限定路径 |
| `src/core/CoreController.cpp` | **Line 230** | `ArcMeta::Logger::log(...)` | 搜索关键词为空，记录跳过检索流程日志 |
| `src/core/CoreController.cpp` | **Line 238** | `ArcMeta::Logger::log(...)` | 搜索任务已启动，记录 `searchId` 并发射 `searchStarted` 信号 |
| `src/core/CoreController.cpp` | **Line 256** | `ArcMeta::Logger::log(...)` | 缓存阶段命中搜索结果，记录命中条数与传输日志 |
| `src/core/CoreController.cpp` | **Line 273** | `ArcMeta::Logger::log(...)` | 搜索任务正常结束，记录总命中项数与 `searchFinished` 信号发射 |
| `src/core/CoreController.cpp` | **Line 276** | `ArcMeta::Logger::log(...)` | 搜索任务被中途中止或作废警告 |
| `src/core/PhysicalDiskSearchExtractor.cpp` | **Line 26** | `ArcMeta::Logger::log(...)` | 物理磁盘 I/O 深度扫描进度汇报（每扫描 500 个文件汇报一次） |

---

### 2.3 数据库管理与持久化模块 (`src/meta/DatabaseManager.cpp`, `MetadataManager.cpp`, `CategoryRepo.cpp`, `TagRepository.cpp`)

#### A. 数据库连接与架构升级 (`src/meta/DatabaseManager.cpp`)
| 文件路径 | 精准行号 | 打印语句 / 级别 | 触发场景与业务功能说明 |
| :--- | :--- | :--- | :--- |
| `src/meta/DatabaseManager.cpp` | **Line 141** | `qWarning()` | 内存 SQLite 数据库 (`:memory:`) 打开失败告警 |
| `src/meta/DatabaseManager.cpp` | **Line 145** | `qWarning()` | 磁盘 SQLite 数据库恢复/加载至内存数据库失败 |
| `src/meta/DatabaseManager.cpp` | **Line 251** | `qDebug()` | 主库基础 Schema 表结构初始化失败报错 |
| `src/meta/DatabaseManager.cpp` | **Line 283** | `qWarning()` | 全文检索 FTS 虚拟表架构初始化失败报错 |
| `src/meta/DatabaseManager.cpp` | **Line 321** | `qDebug()` | 旧版迁移：重命名 `metadata` 表的 `file_id` 字段为 `folder_id` |
| `src/meta/DatabaseManager.cpp` | **Line 341** | `qDebug()` | 旧版迁移：重命名 `category_items` 表的 `file_id` 字段为 `folder_id` |
| `src/meta/DatabaseManager.cpp` | **Line 346** | `qDebug()` | 旧版迁移：`metadata` 表新增 `width` 字段 |
| `src/meta/DatabaseManager.cpp` | **Line 350** | `qDebug()` | 旧版迁移：`metadata` 表新增 `height` 字段 |
| `src/meta/DatabaseManager.cpp` | **Line 354** | `qDebug()` | 旧版迁移：`metadata` 表新增 `ingestion_status` 字段 |
| `src/meta/DatabaseManager.cpp` | **Line 358** | `qDebug()` | 旧版迁移：`metadata` 表新增 `auto_color` 字段 |
| `src/meta/DatabaseManager.cpp` | **Line 362** | `qDebug()` | 旧版迁移：`metadata` 表新增 `added_at` 字段 |
| `src/meta/DatabaseManager.cpp` | **Line 380** | `qDebug()` | 旧版迁移：`metadata` 表新增 `sha256` 字段 |
| `src/meta/DatabaseManager.cpp` | **Line 402** | `qDebug()` | 旧版迁移：`metadata` 表新增 `base_name` 字段 |
| `src/meta/DatabaseManager.cpp` | **Line 406** | `qDebug()` | 旧版迁移：`metadata` 表新增 `ext` 字段 |
| `src/meta/DatabaseManager.cpp` | **Line 448** | `qDebug()` | 旧版迁移：存量数据 `base_name` 和 `ext` 回填计算完成 |
| `src/meta/DatabaseManager.cpp` | **Line 484** | `qDebug()` | 旧版迁移：`categories` 表新增 `category_kind` 字段并开始数据迁移 |
| `src/meta/DatabaseManager.cpp` | **Line 491** | `qDebug()` | 旧版迁移：`categories` 表 `category_kind` 字段迁移完成 |
| `src/meta/DatabaseManager.cpp` | **Line 508** | `qWarning()` | `saveDb` 保存数据库失败：连接句柄为空 |
| `src/meta/DatabaseManager.cpp` | **Line 526** | `qDebug()` | `saveDb` 成功将内存数据库同步持久化备份至磁盘 SQLite 文件 |
| `src/meta/DatabaseManager.cpp` | **Line 529** | `qWarning()` | `saveDb` 中途备份到硬盘失败警告 |
| `src/meta/DatabaseManager.cpp` | **Line 533** | `qWarning()` | `saveDb` 初始化备份句柄失败告警 |
| `src/meta/DatabaseManager.cpp` | **Line 570** | `qDebug()` | `flushAll` 刷盘跳过：当前无脏数据需要备份 |
| `src/meta/DatabaseManager.cpp` | **Line 574** | `qDebug()` | `flushAll` 开始执行所有脏数据库的强力落盘 |
| `src/meta/DatabaseManager.cpp` | **Line 594** | `qWarning()` | `flushAll` 全局库 (`global.db`) 备份落盘失败告警 |
| `src/meta/DatabaseManager.cpp` | **Line 604** | `qWarning()` | `flushAll` 某个分盘分库持久化备份失败告警 |
| `src/meta/DatabaseManager.cpp` | **Line 609** | `qDebug()` | `flushAll` 所有数据库成功落盘，脏标记清理完成 |
| `src/meta/DatabaseManager.cpp` | **Line 612** | `qWarning()` | `flushAll` 存在分库备份失败，保留脏标记等候下一次重试 |
| `src/meta/DatabaseManager.cpp` | **Line 638** | `qDebug()` | 物理盘分库数据库连接请求日志 |
| `src/meta/DatabaseManager.cpp` | **Line 655** | `qDebug()` | 移动存储盘符漂移检测与自动物理路由重对账连接日志 |

#### B. 元数据管理 (`src/meta/MetadataManager.cpp`)
| 文件路径 | 精准行号 | 打印语句 / 级别 | 触发场景与业务功能说明 |
| :--- | :--- | :--- | :--- |
| `src/meta/MetadataManager.cpp` | **Line 843** | `qWarning()` | 计算并保存索引进度失败：无法获得目标文件夹对应的盘符分库 |
| `src/meta/MetadataManager.cpp` | **Line 885** | `qWarning()` | 写入文件夹索引进度 SQLite 错误告警 |
| `src/meta/MetadataManager.cpp` | **Line 2159** | `qWarning()` | 检测到路径偏移，从内存缓存清理旧条目以防止计数重复 |
| `src/meta/MetadataManager.cpp` | **Line 2471** | `qWarning()` | 异步持久化写入失败：无法取得目标路径内存库句柄 |
| `src/meta/MetadataManager.cpp` | **Line 2512** | `qWarning()` | 异步持久化写入 SQLite 内存库 `sqlite3_step` 失败告警 |
| `src/meta/MetadataManager.cpp` | **Line 2516** | `qWarning()` | 异步持久化写入 SQL `sqlite3_prepare_v2` 失败告警 |

#### C. 分类与标签仓储 (`src/meta/CategoryRepo.cpp`, `TagRepository.cpp`)
| 文件路径 | 精准行号 | 打印语句 / 级别 | 触发场景与业务功能说明 |
| :--- | :--- | :--- | :--- |
| `src/meta/CategoryRepo.cpp` | **Line 219** | `qCritical()` | 新增分类 SQL 语句 `sqlite3_step` 执行失败 |
| `src/meta/CategoryRepo.cpp` | **Line 223** | `qCritical()` | 新增分类 SQL 语句 `sqlite3_prepare_v2` 执行失败 |
| `src/meta/CategoryRepo.cpp` | **Line 771** | `qWarning()` | 按路径更新分类图标颜色属性执行失败警告 |
| `src/meta/TagRepository.cpp` | **Line 222** | `qWarning()` | 检测到分库中的旧版标签数据，自动发起向 `global.db` 的数据迁移 |
| `src/meta/TagRepository.cpp` | **Line 293** | `qWarning()` | 标签组数据成功迁移至全局主库 |
| `src/meta/TagRepository.cpp` | **Line 295** | `qWarning()` | 标签数据迁移事务提交失败告警 |

---

### 2.4 底层 MFT 卷扫描器 (`src/mft/MftReader.cpp`)

| 文件路径 | 精准行号 | 打印语句 / 级别 | 触发场景与业务功能说明 |
| :--- | :--- | :--- | :--- |
| `src/mft/MftReader.cpp` | **Line 49** | `qDebug()` | Windows MFT 权限提升：`OpenProcessToken` 失败 |
| `src/mft/MftReader.cpp` | **Line 54** | `qDebug()` | Windows MFT 权限提升：`LookupPrivilegeValue` 查找 LUID 失败 |
| `src/mft/MftReader.cpp` | **Line 63** | `qDebug()` | Windows MFT 权限提升：`AdjustTokenPrivileges` 调整特权失败 |
| `src/mft/MftReader.cpp` | **Line 69** | `qDebug()` | MFT 扫描提权失败（非管理员权限运行提示） |
| `src/mft/MftReader.cpp` | **Line 71** | `qDebug()` | MFT 扫描成功获取 `SeBackupPrivilege` 特权 |

---

### 2.5 视图交互与 UI 面板 (`src/ui/`)

#### A. 左侧分类面板 (`src/ui/CategoryPanel.cpp`)
| 文件路径 | 精准行号 | 打印语句 / 级别 | 触发场景与业务功能说明 |
| :--- | :--- | :--- | :--- |
| `src/ui/CategoryPanel.cpp` | **Line 67** | `Logger::log(...)` | `~CategoryPanel` 析构函数触发，进入内部更新并断开信号 |
| `src/ui/CategoryPanel.cpp` | **Line 1173** | `qWarning()` | 重置分类文件夹路径时 `QFile::rename` 物理更名失败 |
| `src/ui/CategoryPanel.cpp` | **Line 1324** | `Logger::log(...)` | `modelAboutToBeReset`: 记录 Model 重置前的行数 |
| `src/ui/CategoryPanel.cpp` | **Line 1349** | `Logger::log(...)` | `modelAboutToBeReset`: 保存树控件展开节点的状态 (ID & 名称) |
| `src/ui/CategoryPanel.cpp` | **Line 1352** | `Logger::log(...)` | `modelAboutToBeReset`: 无真实分类数据，跳过保存状态 |
| `src/ui/CategoryPanel.cpp` | **Line 1357** | `Logger::log(...)` | `modelAboutToBeReset`: 标记内部更新标志 `m_isInternalUpdating = true` |
| `src/ui/CategoryPanel.cpp` | **Line 1361** | `Logger::log(...)` | `modelReset`: 记录 Model 重置完成后的行数 |
| `src/ui/CategoryPanel.cpp` | **Line 1374** | `Logger::log(...)` | `modelReset`: 开始恢复之前保存的分类展开状态 |
| `src/ui/CategoryPanel.cpp` | **Line 1384** | `Logger::log(...)` | `modelReset`: 展开状态恢复完毕，重置 `m_isInternalUpdating = false` |
| `src/ui/CategoryPanel.cpp` | **Line 1532** | `Logger::log(...)` | 处于恢复状态或内部更新中，忽略保存展开状态的请求 |
| `src/ui/CategoryPanel.cpp` | **Line 1539** | `Logger::log(...)` | 分类模型为空，忽略保存展开状态的请求 |
| `src/ui/CategoryPanel.cpp` | **Line 1548** | `Logger::log(...)` | 节点仅包含占位符，忽略保存展开状态请求 |
| `src/ui/CategoryPanel.cpp` | **Line 1570** | `Logger::log(...)` | 成功将展开状态写入 Settings 并绑定至动态属性 |
| `src/ui/CategoryPanel.cpp` | **Line 1581** | `Logger::log(...)` | 从 Settings 中成功加载树节点的展开状态配置 |

#### B. 主内容展示面板 (`src/ui/ContentPanel.cpp`)
| 文件路径 | 精准行号 | 打印语句 / 级别 | 触发场景与业务功能说明 |
| :--- | :--- | :--- | :--- |
| `src/ui/ContentPanel.cpp` | **Line 870** | `ArcMeta::Logger::log(...)` | 缩放网格尺寸调节日志记录 `m_zoomLevel` |
| `src/ui/ContentPanel.cpp` | **Line 2836** | `ArcMeta::Logger::log(...)` | 启动物理递归/单级扫描文件夹日志 |
| `src/ui/ContentPanel.cpp` | **Line 2893** | `ArcMeta::Logger::log(...)` | 物理目录扫描成功完成并更新至 UI |
| `src/ui/ContentPanel.cpp` | **Line 2896** | `ArcMeta::Logger::log(...)` | 拦截并丢弃过期的目录扫描回调结果 |
| `src/ui/ContentPanel.cpp` | **Line 2924** | `ArcMeta::Logger::log(...)` | 视图内本地过滤搜索词更新记录 |
| `src/ui/ContentPanel.cpp` | **Line 3117** | `ArcMeta::Logger::log(...)` | 分类节点下的资产列表加载完成日志 |
| `src/ui/ContentPanel.cpp` | **Line 3119** | `ArcMeta::Logger::log(...)` | 拦截并丢弃过期的分类加载回调 |
| `src/ui/ContentPanel.cpp` | **Line 3134** | `ArcMeta::Logger::log(...)` | `loadPaths` 收到空路径，同步清空展示列表 |
| `src/ui/ContentPanel.cpp` | **Line 3180** | `ArcMeta::Logger::log(...)` | 指定路径列表同步加载完成 |
| `src/ui/ContentPanel.cpp` | **Line 3182** | `ArcMeta::Logger::log(...)` | 拦截并丢弃过期的路径列表加载回调 |
| `src/ui/ContentPanel.cpp` | **Line 3193** | `ArcMeta::Logger::log(...)` | `appendPaths` 拦截过期的异步追加请求 |
| `src/ui/ContentPanel.cpp` | **Line 3215** | `ArcMeta::Logger::log(...)` | `appendPaths` 成功追加新资产路径记录至 UI 视图模型 |
| `src/ui/ContentPanel.cpp` | **Line 3217** | `ArcMeta::Logger::log(...)` | `appendPaths` 在回调阶段拦截到作废请求 |

#### C. 主窗口与拖拽交互 (`src/ui/MainWindow.cpp`, `DropListView.cpp`, `FormatDecoders.cpp`, `LoadingWindow.cpp`)
| 文件路径 | 精准行号 | 打印语句 / 级别 | 触发场景与业务功能说明 |
| :--- | :--- | :--- | :--- |
| `src/ui/MainWindow.cpp` | **Line 1813** | `ArcMeta::Logger::log(...)` | 统一导航调度器跳转记录 (URL & 历史记录标记) |
| `src/ui/MainWindow.cpp` | **Line 2318** | `qWarning()` | 检测到磁盘监控盘符/目录已物理失效，自动从 DriveBar 清退 |
| `src/ui/DropListView.cpp` | **Line 58** | `Logger::log(...)` | 列表视图开始拖拽选中的文件资产 |
| `src/ui/DropListView.cpp` | **Line 69** | `Logger::log(...)` | 从 View Model 中提取选中项的 `PathRole` 绝对路径 |
| `src/ui/DropListView.cpp` | **Line 78** | `Logger::log(...)` | 最终注入拖拽 MimeData 的物理路径清单日志 |
| `src/ui/FormatDecoders.cpp` | **Line 100** | `qWarning()` | MemoryGuard 保护：TIFF 图像估算超内存安全上限拒绝分配 |
| `src/ui/FormatDecoders.cpp` | **Line 316** | `qWarning()` | EPS 解码器：文件打不开告警 |
| `src/ui/FormatDecoders.cpp` | **Line 322** | `qWarning()` | EPS 解码器：文件头小于 30 字节非法文件告警 |
| `src/ui/FormatDecoders.cpp` | **Line 388** | `qWarning()` | EPS 解码器：提取内嵌位图预览图失败告警 |
| `src/ui/LoadingWindow.cpp` | **Line 57** | `qWarning()` | SVG 加载窗口：SVG 资源渲染失败 |
| `src/ui/LoadingWindow.cpp` | **Line 60** | `qWarning()` | SVG 加载窗口：`refresh` 图标缺失告警 |

---

### 2.6 工具与导入助手 (`src/util/`)

| 文件路径 | 精准行号 | 打印语句 / 级别 | 触发场景与业务功能说明 |
| :--- | :--- | :--- | :--- |
| `src/util/AssetImporter.cpp` | **Line 77** | `qWarning()` | 建立资产托管库根目录失败告警 |
| `src/util/ImportHelper.cpp` | **Line 80** | `qWarning()` | 建立 `.arc` 资产包文件容器失败告警 |
| `src/util/ImportHelper.cpp` | **Line 96** | `qWarning()` | 复制或移动文件资产项失败告警 |
| `src/util/ShellHelper.cpp` | **Line 188** | `qWarning()` | 检测到移动硬盘盘符漂移，进行物理纠偏重命名数据库 |
| `src/util/ShellHelper.cpp` | **Line 198** | `qWarning()` | 重命名冲突：先将已有目标数据库重命名为 `.invalid` |
| `src/util/ShellHelper.cpp` | **Line 203** | `qWarning()` | 数据库文件物理重命名成功 |
| `src/util/ShellHelper.cpp` | **Line 206** | `qWarning()` | 数据库文件物理重命名失败 |
| `src/util/ShellHelper.cpp` | **Line 225** | `qWarning()` | 自动对账：纠偏重命名数据库文件 |
| `src/util/ShellHelper.cpp` | **Line 227** | `qWarning()` | 数据库文件纠偏重命名失败，降级原样加载 |
| `src/util/ShellHelper.cpp` | **Line 244** | `qWarning()` | 冲突处理：将多余/冗余数据库标注为 `.invalid` |

---

## 3. 统计汇总

- **总触发入口数量**：84 处
- **涉及源文件数量**：15 个文件
- **日志输出机制构成**：
  1. **标准 `qDebug() / qWarning() / qCritical()` 输出**（经 `customMessageHandler` 钩子统一收集并转存）：63 处
  2. **显式 `Logger::log()` / `ArcMeta::Logger::log()` 调用**：21 处
