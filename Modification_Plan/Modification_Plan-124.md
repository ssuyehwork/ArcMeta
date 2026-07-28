# 修改方案：重构 HasThumbnailRole 判定以彻底根除卡片内系统默认图标的冗余边框拉伸 (Modification_Plan-124.md)

## 1. 深度根因分析与排查扩展

### 1.1 问题描述
即便我们在 `WindowsShellThumbnailProvider` 中使用了 `SIIGBF_THUMBNAILONLY` 屏蔽了原生大图标的白边，部分没有真正物理缩略图的图形文件（如 `.ai`、`.psd` 等，在没有安装对应预览插件时），在其卡片内部依然呈现出大范围、被拉伸放大的系统级自带外边框/圆角背景。

### 1.2 深层成因定位
在 `src/ui/ContentPanel.cpp` 中的数据模型实现 `ArcMetaVirtualDbModel::data` 里，针对 `HasThumbnailRole` （是否拥有物理缩略图）的判定存在以下盲目逻辑：
```cpp
    } else if (role == HasThumbnailRole) {
        // 只要是图形或视频格式，均预设为 true，强制 Delegate 进入填满模式，消除抖动
        if (UiHelper::isGraphicsFile(record.suffix)) return true;
        if (record.width > 0 && record.height > 0) return true;
        return m_aspectRatios.contains(QDir::toNativeSeparators(path));
    }
```
1. **强制预设 `true` 的弊端**：
   只要文件属于图形/视频后缀，无论真实的缩略图加载是否成功，`HasThumbnailRole` 都被硬编码直接返回 `true`。
2. **Delegate 对 Fallback 图标的错误拉伸**：
   在缩略图加载失败时（即 `getShellThumbnail` 返回空图像），模型会将该卡片的内存缩略图缓存 `m_iconCache` 指向 `UiHelper::getFileIcon(path, 128)` 得到的降级系统默认大图标。
   然而，当渲染代理 `ThumbnailDelegate::paint` 或 `TreeItemDelegate::paint` 查询 `HasThumbnailRole` 时，由于其返回的是 `true`，代理会认为**它已经拥有了一个有效缩略图**，从而在 `CardPainterHelper::drawCardCover` 中强行将这个降级系统默认大图标提取为 Pixmap，并执行 **100% 全尺寸的拉伸平滑填充**。
   这就导致系统默认文件图标内部自带的任何浅色底色、微弱框线、或品牌自带圆角外边框（如 Adobe 官方图标的外围圆角边框）被无限放大拉伸，填满了整个卡片区域，从而形成了用户在暗色主题下所能看到的极不美观的“冗余内边框”。

### 1.3 根治解决方案
我们需要重构 `HasThumbnailRole` 的判定逻辑，**只有当该文件的物理缩略图被真正成功提取、生成、且其宽高比已无损登记于 `m_aspectRatios` 中时**，才认为该文件拥有有效的缩略图。
- 当处于“正在加载”或“加载失败降级”状态时，`HasThumbnailRole` 将返回 `false`。
- 如果返回 `false`，渲染代理 `ThumbnailDelegate` 及 `TreeItemDelegate` 将会自动以 **65% 比例居中悬浮、不拉伸** 的方式优雅呈现该文件的系统默认图标，这就完全避免了默认图标的外围背景和边框被强制拉伸至 100% 产生的冗余边框视觉。

---

## 2. 修改边界声明【范围】

本方案涉及一个核心文件的物理代码重构，具体的修改边界如下：

### 物理文件修改清单：
1. `src/ui/ContentPanel.cpp`
   - 重构 `ArcMetaVirtualDbModel::data` 中关于 `HasThumbnailRole` 的数据返回判断，剔除硬编码直接返回 `true` 的宽松模式。

---

## 3. 详细物理改动细节

### 3.1 `src/ui/ContentPanel.cpp`
- **定位代码位置**：`ArcMetaVirtualDbModel::data` 方法中的 `HasThumbnailRole` 角色分支（第221行左右）
- **代码变动内容**：
```cpp
<<<<<<< SEARCH
    } else if (role == HasThumbnailRole) {
        // 2026-xx-xx 按照 Plan-114：优化 HasThumbnailRole 判定逻辑
        // 只要是图形或视频格式，均预设为 true，强制 Delegate 进入填满模式，消除抖动
        if (UiHelper::isGraphicsFile(record.suffix)) return true;
        if (record.width > 0 && record.height > 0) return true;
        return m_aspectRatios.contains(QDir::toNativeSeparators(path));
    } else if (role == Qt::DecorationRole && index.column() == 0) {
=======
    } else if (role == HasThumbnailRole) {
        // 2026-07-29 极致重构：只有当真正拥有成功加载并计算的缩略图（已记录宽高比）时，才返回 true。
        // 避免在未加载或加载失败降级时，由于宽松返回 true 导致 Delegate 强行将 fallback 系统默认图标当作缩略图执行 100% 拉伸绘制产生冗余内边框。
        return m_aspectRatios.contains(QDir::toNativeSeparators(path));
    } else if (role == Qt::DecorationRole && index.column() == 0) {
>>>>>>> REPLACE
```
