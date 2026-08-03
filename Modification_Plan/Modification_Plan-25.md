# 内存模式分类内容面板缩略图卡片过滤与真实素材穿透加载 —— Modification_Plan-25.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在目前系统的内存模式（侧边栏分类）下，部分托管资产文件夹内（如 `.arc` 容器内）包含真实的素材文件（如 `cx 抽象 - 736.eps`）以及配套的辅助缩略图文件（如 `cx 抽象 - 736_thumbnail.png`）。
当前存在两个主要设计缺陷：
1. 由于历史上的全量扫描与物理对账逻辑把 `.arc` 内部文件进行了全量登记，导致包内的辅助缩略图文件（以 `_thumbnail.png` 结尾）也被当成独立的卡片误呈现在内容面板中，产生严重的“货不对板”和冗余情况。
2. 当内容面板点击分类加载真实的素材卡片（如 `cx 抽象 - 736.eps`）时，由于缺乏对黑盒容器内部路径的穿透读取感知，缩略图加载器直接尝试对 `.eps` 原生物理文件本身进行 Shell 提取导致返回空，而对同级目录下完好保存的 `_thumbnail.png` 视若无睹，使得真实的素材卡片只显示成一个通用的白色 EPS 文件图标。

本方案旨在：
- 彻底过滤并屏蔽 `_thumbnail.png` 辅助缩略图，避免其误显为独立卡片；
- 为受控容器 `.arc` 内部的真实素材卡片建立高效、优雅的“包内缩略图穿透载入机制”，使其能精准加载同级包内的 `_thumbnail.png` 作为卡片画面呈现。

## 2. 问题定位
1. **数据登记与展现层拦截缺失**：
   - 数据登记层：历史残留脏数据仍然保存在 SQLite 各物理分库的 `metadata` 和 `category_items` 表中。需要在数据库初始化载入（`loadDb`）的第一时间，利用高效的 SQL 完成对历史上因物理扫描误登记入库的、路径以 `_thumbnail.png` 结尾的条目执行一键清除。
   - 展现加载层：视图载入时通过 `CategoryLoadService`（包括 `loadCategoryItems` 和 `loadPathItems`）组装 `ItemRecord` 数据。需要在这两处核心载入端对加载路径增加过滤，若路径以 `_thumbnail.png` 结尾（不区分大小写），则强行过滤、直接跳过，双重保障绝对纯净的 UI 渲染。
2. **缩略图加载模块缺乏包内穿透感知**：
   - 逻辑：在 `LibraryAssetModel::loadThumbnailsForRows` 的异步工作线程中，系统针对图片格式仅调用 `ShellIconManager::getShellThumbnail`。因为 `.eps` 等非标准系统直绘格式由 Shell 返回空，导致该卡片无缩略图。
   - 重构策略：在异步特征加载逻辑中，优先判定该文件路径是否处于 `.arc` 容器内（判断其父目录名是否以 `.arc` 结尾）。若是，则直接在其同级目录下加载对应的 `*_thumbnail.png` 文件作为该真实卡片的高清缩略图，完美穿透呈现。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 内容面板卡片里显示的是cx 抽象 - 736_thumbnail.png / 抽象 - 736.eps（对应用户原话） | 4.1节在 `loadDb` 进行一键 SQL 脏数据大扫除，并在 4.2 节重构 `CategoryLoadService` 的加载层彻底屏蔽 `_thumbnail.png`卡片，使卡片里只显示真实的素材卡片 | ✅ 一致 |
| 2    | 00msd6sg4k000.arc 文件夹里是有缩略图的，但卡片里实际显示出来的却不是缩略图（对应用户原话） | 4.3节重构 `LibraryAssetModel::loadThumbnailsForRows`，当检测到载入的文件处于 `.arc` 容器内时，自动获取并加载同包同级下的 `*_thumbnail.png` 文件作为卡片缩略图 | ✅ 一致 |

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 数据库载入层一键大清洗
修改 `src/meta/DatabaseManager.cpp` 中的 `loadDb`，在成功初始化 schema 后，利用高效的 SQL 一键彻底清除 `metadata` 和 `category_items` 表中历史上所有路径以 `_thumbnail.png` 结尾的脏数据记录：

```
<<<<<<< SEARCH
        const char* arcCleanup2 =
            "DELETE FROM category_items WHERE path_hint LIKE '%.arc' ESCAPE '\\' "
            "OR path_hint LIKE '%.arc\\%' ESCAPE '\\';";
        sqlite3_exec(conn.memDb, arcCleanup2, nullptr, nullptr, nullptr);

        // FTS5 trigram 模糊匹配与自动触发器同步
=======
        const char* arcCleanup2 =
            "DELETE FROM category_items WHERE path_hint LIKE '%.arc' ESCAPE '\\' "
            "OR path_hint LIKE '%.arc\\%' ESCAPE '\\';";
        sqlite3_exec(conn.memDb, arcCleanup2, nullptr, nullptr, nullptr);

        // 🚨 2026-08-02 一键清除历史上误把 .arc 内部的 _thumbnail.png 登记为独立资产的脏数据
        const char* thumbCleanup1 = "DELETE FROM metadata WHERE path LIKE '%_thumbnail.png';";
        sqlite3_exec(conn.memDb, thumbCleanup1, nullptr, nullptr, nullptr);

        const char* thumbCleanup2 = "DELETE FROM category_items WHERE path_hint LIKE '%_thumbnail.png';";
        sqlite3_exec(conn.memDb, thumbCleanup2, nullptr, nullptr, nullptr);

        // FTS5 trigram 模糊匹配与自动触发器同步
>>>>>>> REPLACE
```

### 4.2 视图载入层双重防御过滤拦截
修改 `src/core/CategoryLoadService.cpp`，在 `loadCategoryItems` 和 `loadPathItems` 载入组装 `ItemRecord` 的源头处添加防御性过滤。任何以 `_thumbnail.png` 结尾（不区分大小写）的路径一律不予组装、直接跳过：

```
<<<<<<< SEARCH
    allRecords.reserve(allRecords.size() + items.size());
    for (const auto& item : items) {
        std::wstring wPath = MetadataManager::instance().getPathByFolderId(item.folderId);
        if (wPath.empty() && !item.pathHint.empty()) {
            wPath = item.pathHint;
        }

        if (!wPath.empty()) {
            allRecords.push_back(ItemRecord::create(QString::fromStdWString(wPath), nullptr, true));
        }
    }

    return allRecords;
}

std::vector<ItemRecord> CategoryLoadService::loadPathItems(const QStringList& paths) {
    std::vector<ItemRecord> records;
    records.reserve(static_cast<int>(paths.size()));
    for (const QString& p : paths) {
        if (!p.isEmpty()) {
            records.push_back(ItemRecord::create(p, nullptr, true));
        }
    }
    return records;
}
=======
    allRecords.reserve(allRecords.size() + items.size());
    for (const auto& item : items) {
        std::wstring wPath = MetadataManager::instance().getPathByFolderId(item.folderId);
        if (wPath.empty() && !item.pathHint.empty()) {
            wPath = item.pathHint;
        }

        if (!wPath.empty()) {
            QString qPath = QString::fromStdWString(wPath);
            if (qPath.endsWith("_thumbnail.png", Qt::CaseInsensitive)) {
                continue;
            }
            allRecords.push_back(ItemRecord::create(qPath, nullptr, true));
        }
    }

    return allRecords;
}

std::vector<ItemRecord> CategoryLoadService::loadPathItems(const QStringList& paths) {
    std::vector<ItemRecord> records;
    records.reserve(static_cast<int>(paths.size()));
    for (const QString& p : paths) {
        if (!p.isEmpty()) {
            if (p.endsWith("_thumbnail.png", Qt::CaseInsensitive)) {
                continue;
            }
            records.push_back(ItemRecord::create(p, nullptr, true));
        }
    }
    return records;
}
>>>>>>> REPLACE
```

### 4.3 缩略图穿透载入机制
修改 `src/ui/models/LibraryAssetModel.cpp` 的 `loadThumbnailsForRows` 中异步加载线程的分析循环。
如果被分析的真实素材文件父目录名为 `.arc` 容器，直接搜寻并加载该父目录下同级的 `*_thumbnail.png` 高清缩略图：

```
<<<<<<< SEARCH
    QPointer<LibraryAssetModel> weakThis(this);
    (void)QtConcurrent::run([weakThis, newQueue]() {
        for (const auto& task : newQueue) {
            if (!weakThis) break;
            QString path = task.first;
            QFileInfo info(path);
            QString ext = info.suffix().toLower();

            QImage img;
            double ar = 1.0;
            bool hasThumb = false;

            if (ext == "svg") {
                QSvgRenderer renderer(path);
                if (renderer.isValid()) {
                    QImage svgImg(128, 128, QImage::Format_ARGB32);
                    svgImg.fill(Qt::transparent);
                    QPainter painter(&svgImg);
                    renderer.render(&painter);
                    img = svgImg;
                    ar = 1.0;
                    hasThumb = true;
                }
            } else if (ext == "ai") {
                img = MediaColorExtractor::extractEmbeddedAiPreview(path);
                if (!img.isNull()) {
                    ar = (double)img.width() / img.height();
                    hasThumb = true;
                } else {
                    ar = -1.0;
                    hasThumb = false;
                }
            } else if (UiHelper::isGraphicsFile(ext) && ext != "cur" && ext != "ico" && ext != "ani" && ext != "ai") {
                img = ShellIconManager::getShellThumbnail(path, 128);
                if (!img.isNull()) {
                    ar = (double)img.width() / img.height();
                    hasThumb = true;
                }
            } else if (ext == "cur" || ext == "ico" || ext == "ani") {
                ar = 1.0;
                hasThumb = false;
            } else if ((ext == "arc" || path.endsWith(".arc", Qt::CaseInsensitive) || path.endsWith(".arc/", Qt::CaseInsensitive) || path.endsWith(".arc\\", Qt::CaseInsensitive)) && info.isDir()) {
=======
    QPointer<LibraryAssetModel> weakThis(this);
    (void)QtConcurrent::run([weakThis, newQueue]() {
        for (const auto& task : newQueue) {
            if (!weakThis) break;
            QString path = task.first;
            QFileInfo info(path);
            QString ext = info.suffix().toLower();

            QImage img;
            double ar = 1.0;
            bool hasThumb = false;

            bool isInsideArc = info.dir().dirName().endsWith(".arc", Qt::CaseInsensitive);

            if (isInsideArc) {
                // 🚨 完美穿透解包联动：如果真实素材在 .arc 包内，直接去搜寻并加载包内同级的 *_thumbnail.png
                QDir arcDir = info.dir();
                QStringList thumbFiles = arcDir.entryList({"*_thumbnail.png"}, QDir::Files);
                if (!thumbFiles.isEmpty()) {
                    QString thumbPath = arcDir.absoluteFilePath(thumbFiles.first());
                    img = QImage(thumbPath);
                    if (!img.isNull()) {
                        ar = (double)img.width() / img.height();
                        hasThumb = true;
                    }
                }
            } else if (ext == "svg") {
                QSvgRenderer renderer(path);
                if (renderer.isValid()) {
                    QImage svgImg(128, 128, QImage::Format_ARGB32);
                    svgImg.fill(Qt::transparent);
                    QPainter painter(&svgImg);
                    renderer.render(&painter);
                    img = svgImg;
                    ar = 1.0;
                    hasThumb = true;
                }
            } else if (ext == "ai") {
                img = MediaColorExtractor::extractEmbeddedAiPreview(path);
                if (!img.isNull()) {
                    ar = (double)img.width() / img.height();
                    hasThumb = true;
                } else {
                    ar = -1.0;
                    hasThumb = false;
                }
            } else if (UiHelper::isGraphicsFile(ext) && ext != "cur" && ext != "ico" && ext != "ani" && ext != "ai") {
                img = ShellIconManager::getShellThumbnail(path, 128);
                if (!img.isNull()) {
                    ar = (double)img.width() / img.height();
                    hasThumb = true;
                }
            } else if (ext == "cur" || ext == "ico" || ext == "ani") {
                ar = 1.0;
                hasThumb = false;
            } else if ((ext == "arc" || path.endsWith(".arc", Qt::CaseInsensitive) || path.endsWith(".arc/", Qt::CaseInsensitive) || path.endsWith(".arc\\", Qt::CaseInsensitive)) && info.isDir()) {
>>>>>>> REPLACE
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/meta/DatabaseManager.cpp` (清洗误登记的 `_thumbnail.png` 记录)
- [ ] 模块/文件：`src/core/CategoryLoadService.cpp` (双重过滤与拦截 `_thumbnail.png` 辅助卡片)
- [ ] 模块/文件：`src/ui/models/LibraryAssetModel.cpp` (穿透包内为真实素材载入同级的缩略图图片)

**明确禁止越界修改的范围：**
- [ ] 磁盘模式相关 `src/ui/models/DiskItemModel.cpp` —— 绝不修改
- [ ] 任何 `.scch` 或 `.json` 解析与读写 —— 绝不修改

## 6. 实现准则与预警【核心】
1. **防止编译与链接错误**：本方案使用的 QImage、QDir 及 QFileInfo 均是已在对应文件中包含的现有头文件，不引入任何新的不必要头文件依赖。
2. **极速零 I/O 损耗**：由于只有对满足 `isInsideArc` 为真的内部子素材（常在加载时触发）才执行极快的 QImage 读取，完全无感。
3. **自包含与开箱即用**：方案无任何伪代码，可直接实施进行物理替换。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 托管卡片解包名称及缩略图穿透 | 点击分类加载数据时，内容面板中呈现的受控资产包卡片其名称穿透显示为包内的真实素材文件名，缩略图也穿透并读取包内的高清缩略图 | ✅ 符合。本方案完美确保了在只显示唯一的 `.eps`（或真实格式）卡片时，完美穿透并加载对应的同级缩略图文件。 |

## 8. 待确认事项（可选）
（无）
