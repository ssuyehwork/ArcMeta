# Plan-133 实施方案：视口任务熔断机制与滚动事件节流优化

> **目标**：彻底解决“鼠标按住滚动条拖不动”以及“停下后因任务排队需等数秒才出图”的严重问题。

---

## 阶段一：消灭 `valueChanged` 高频直调（释放主线程事件循环）

### 修改文件：`src/ui/ContentPanel.cpp`

```diff
<<<<<<< SEARCH
    connect(m_gridView->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        if (!m_visibleTimer->isActive()) {
            m_visibleTimer->start();
        }
        refreshVisibleThumbnails();
    });
    connect(m_gridView->verticalScrollBar(), &QScrollBar::sliderReleased, this, [this]() {
        refreshVisibleThumbnails();
    });
=======
    connect(m_gridView->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        // 🚨 严禁在 valueChanged 中直调 refreshVisibleThumbnails！由 50ms 节流定时器独占控制
        if (!m_visibleTimer->isActive()) {
            m_visibleTimer->start(50);
        }
    });
    connect(m_gridView->verticalScrollBar(), &QScrollBar::sliderReleased, this, [this]() {
        refreshVisibleThumbnails();
    });
>>>>>>> REPLACE
```

---

## 阶段二：引入“代际版本号熔断机制”（消除几秒排队等待）

### 修改文件：`src/ui/models/LibraryAssetModel.h`

```diff
<<<<<<< SEARCH
    QSet<int> m_pendingUpdateRows;
};
=======
    QSet<int> m_pendingUpdateRows;
    std::atomic<uint64_t> m_currentGen{0};
};
>>>>>>> REPLACE
```

### 修改文件：`src/ui/models/LibraryAssetModel.cpp`

```diff
<<<<<<< SEARCH
void LibraryAssetModel::loadThumbnailsForRows(const QList<int>& rows) {
    // 内存模式：穿透 .arc 搜寻高清缩略图与宽高比
    std::vector<std::pair<QString, QString>> newQueue;
    for (int r : rows) {
=======
void LibraryAssetModel::loadThumbnailsForRows(const QList<int>& rows) {
    // 递增当前视口代际版本号，瞬间熔断上一批所有滑出屏幕的旧任务
    uint64_t thisGen = ++m_currentGen;

    std::vector<std::pair<QString, QString>> newQueue;
    for (int r : rows) {
>>>>>>> REPLACE
```

```diff
<<<<<<< SEARCH
        (void)QtConcurrent::run([weakThis, path]() {
            if (!weakThis) return;
=======
        (void)QtConcurrent::run([weakThis, path, thisGen]() {
            // 🚨 0毫秒熔断拦截：如果用户已经滑动到了新位置，旧任务直接自杀丢弃！
            if (!weakThis || weakThis->m_currentGen.load() != thisGen) return;
>>>>>>> REPLACE
```

```diff
<<<<<<< SEARCH
            // 主线程仅做 0.0001ms 纯内存赋值与局部卡片重绘
            QMetaObject::invokeMethod(weakThis.data(), [weakThis, path, icon, ar, hasThumb]() {
                if (weakThis) {
=======
            // 主线程仅做 0.0001ms 纯内存赋值与局部卡片重绘
            QMetaObject::invokeMethod(weakThis.data(), [weakThis, path, icon, ar, hasThumb, thisGen]() {
                // 再次熔断校验：确保只有当前有效视口的回调才更新 UI
                if (weakThis && weakThis->m_currentGen.load() == thisGen) {
>>>>>>> REPLACE
```

---

## 阶段三：同步对 `DiskItemModel` 引入同等熔断机制

### 修改文件：`src/ui/models/DiskItemModel.h` 与 `DiskItemModel.cpp`
将 `std::atomic<uint64_t> m_currentGen{0};` 同步加入 `DiskItemModel`，让磁盘模式与内存模式均享受任务即时熔断，告别排队延迟。
