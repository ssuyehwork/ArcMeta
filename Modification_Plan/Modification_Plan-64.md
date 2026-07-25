# 右键主菜单直接呈现悬停白圈色块及存库修正方案 —— Modification_Plan-64.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在 `Modification_Plan-62.md` 中，我们设计了色块快捷栏，但将其作为二级子菜单（如“分类颜色”、“设定颜色标签”）进行了呈现。用户指出，这偏离了“直接显示在右键菜单上”的直觉化需求（即如图所示，直接呈现在右键主菜单底部或核心操作区域，不用展开二级子菜单）。本方案承接自 `Modification_Plan-62.md`，彻底纠正该设计偏差：通过 `QWidgetAction`，直接将 `ColorStripPicker` 水平色块条嵌入到分类右键主菜单、FolderButton 右键主菜单以及 ContentPanel 内容右键主菜单中，不再使用任何二级子菜单，完全直接展露！

## 2. 问题定位
- `CategoryPanel::showContextMenu` (即 `CategoryPanel.cpp`)：
  - 原本计划创建二级子菜单：`menu.addMenu("分类颜色")`。
  - **修正方案**：直接使用 `QWidgetAction` 将 `ColorStripPicker` 作为一行菜单项添加至右键主菜单体中（一般放置于删除分类下方、或单独作为分割线后的一行展示），完全符合用户截图所示的主菜单直接展露结构。
- `MainWindow::onFolderButtonContextMenu` (即 `MainWindow.cpp`)：
  - 同样直接在主菜单 `menu` 中，添加 `QWidgetAction`，直接展现色块。
- `ContentPanel::onCustomContextMenuRequested` (即 `ContentPanel.cpp`)：
  - 清理旧的“设定颜色标签”二级子菜单，通过 `QWidgetAction` 将 `ColorStripPicker` 嵌入主菜单中。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 直接将当前现有的标注色直接显示在右键菜单上 (对应用户原话) | 彻底摒弃二级子菜单的设计。直接在 `CategoryPanel` 侧边栏菜单、`MainWindow` 的 FolderButton 菜单以及 `ContentPanel` 内容右键菜单的主菜单层级中，使用 `QWidgetAction` 加载色块控件 `ColorStripPicker`，让用户一键可点，一目了然。 | ✅ |
| 2    | 鼠标悬停在某个色块上方时通过白圈突显色块 (对应用户原话) | `ColorStripPicker` 控件中，使用 `QPainter` 绘制圆形色块。当鼠标 Hover 到某一色块时，在色块外边缘绘制亮白色的外同心圆。 | ✅ |
| 3    | 选择某个色块后，将色值存入到 categories 表的 color 字段中的同时存入到 metadata 表 of color 字段中。 (对应用户原话) | 选中主菜单上的某个色块后：写入 `categories` 表对应项（若有），同时将绑定的物理路径写入 `metadata` 表对应项。两边字段同步更新并即时重绘刷新。 | ✅ |

## 4. 详细解决方案

### 4.1 水平色块自绘控件 `ColorStripPicker` 设计
（定义与实现方案继承自 Plan-62，声明于 `src/ui/ColorPicker.h` 并实现在 `src/ui/ColorPicker.cpp` 中）。

在主菜单上直接呈现时，我们可以将 `ColorStripPicker` 作为一个高度约为 `28px` 至 `32px`、水平铺满菜单宽度的控件：
- 共有 9 个色块，包含：“无颜色”(颜色名为空或 `#888780`、或者根据现有标注色进行对齐) 与 8 种标注色。
- 每个色块圆直径设为 `20px`。
- 色块之间的间隔设为 `8px`，左边距设为 `12px` 保持对齐美观。
- 鼠标悬停时（通过 `mouseMoveEvent` 物理计算当前鼠标落在哪个圆心半径 10 像素的圆形范围内），设置 `m_hoveredIndex` 并触发 `update()`，在 Paint 中于对应的色块外部绘制 `R = 12` 像素、画笔宽度为 `1.5px` 或 `1.8px` 的亮白色同心白圆圈。
- 鼠标点击时触发 `colorSelected(hexColor)` 信号。

### 4.2 重构 `CategoryPanel` 侧边栏分类右键菜单 (直接添加)
在 `src/ui/CategoryPanel.cpp` 的 `CategoryPanel::showContextMenu` 中：

```cpp
// 1. 删除旧的 actSetColor, actRandomColor，也不创建二级子菜单。
// 2. 在主菜单底部或指定位置（例如删除分类下方）添加分割线，然后直接插入色块条：
menu.addSeparator();

QWidgetAction* colorStripAction = new QWidgetAction(&menu);
ColorStripPicker* picker = new ColorStripPicker(colorStr, &menu);
colorStripAction->setDefaultWidget(picker);
menu.addAction(colorStripAction); // 直接作为菜单项插在主菜单

connect(picker, &ColorStripPicker::colorSelected, this, [this, id, &menu](const QString& hexColor) {
    auto all = CategoryRepo::getAll();
    for (auto& cat : all) {
        if (cat.id == id) {
            cat.color = hexColor.toUpper().toStdWString();
            CategoryRepo::update(cat);
            // 同步写入 metadata 表的 color 字段中
            if (!cat.physicalPath.empty()) {
                MetadataManager::instance().setColor(cat.physicalPath, cat.color, true);
            }
            break;
        }
    }
    m_categoryModel->refresh();
    menu.close(); // 选择色块后关闭整个主菜单
});
```

### 4.3 重构 `MainWindow` FolderButton 右键菜单 (直接添加)
在 `src/ui/MainWindow.cpp` 的 `MainWindow::onFolderButtonContextMenu` 中：

```cpp
// 1. 彻底删除原有的 actSetColor, actRandomColor 分支及二级 iconMenu 子菜单中的冗余操作
// 2. 直接在主菜单中增加色块条：
menu.addSeparator();

QWidgetAction* colorStripAction = new QWidgetAction(&menu);
ColorStripPicker* picker = new ColorStripPicker(colorStr, &menu);
colorStripAction->setDefaultWidget(picker);
menu.addAction(colorStripAction);

connect(picker, &ColorStripPicker::colorSelected, this, [btn, path, &menu](const QString& hexColor) {
    AppConfig::instance().setValue(QString("DriveBar/FolderColor_%1").arg(path), hexColor.toUpper());
    AppConfig::instance().sync();

    // 同步到 categories 表与 metadata 表
    std::wstring normPath = MetadataManager::normalizePath(path.toStdWString());
    MetadataManager::instance().setColor(normPath, hexColor.toUpper().toStdWString(), true);

    btn->update();
    menu.close(); // 选择后直接关闭主菜单
});
```

### 4.4 重构 `ContentPanel` 右键菜单 (直接添加)
在 `src/ui/ContentPanel.cpp` 的 `ContentPanel::onCustomContextMenuRequested` 中：

```cpp
// 1. 彻底删除旧的设定颜色标签二级子菜单 colorMenu
// 2. 直接在主菜单中嵌入色块：
menu.addSeparator();

QWidgetAction* colorStripAction = new QWidgetAction(&menu);
ColorStripPicker* picker = new ColorStripPicker(currentColorStr, &menu);
colorStripAction->setDefaultWidget(picker);
menu.addAction(colorStripAction);

connect(picker, &ColorStripPicker::colorSelected, this, [this, view, &menu](const QString& hexColor) {
    auto indexes = view->selectionModel()->selectedIndexes();
    for (const auto& idx : indexes) {
        if (idx.column() == 0) {
            QString itemPath = idx.data(PathRole).toString();
            // A. 同步存入 metadata 表的 color 字段中
            m_proxyModel->setData(idx, hexColor, ColorRole);

            // B. 同步存入 categories 表的 color 字段中 (如果是被绑定的文件夹)
            std::wstring normPath = MetadataManager::normalizePath(itemPath.toStdWString());
            CategoryRepo::updateCategoryColorByPath(normPath, hexColor.toUpper().toStdWString());

            // 重新刷新图标以实现同步
            QIcon coloredIcon;
            QString ext = QFileInfo(itemPath).suffix().toLower();
            if (UiHelper::isGraphicsFile(ext)) {
                QImage img = UiHelper::getShellThumbnail(itemPath, this->m_zoomLevel);
                if (!img.isNull()) coloredIcon = QIcon(QPixmap::fromImage(img));
            }
            if (coloredIcon.isNull()) {
                coloredIcon = UiHelper::getFileIcon(itemPath, this->m_zoomLevel);
            }
            m_proxyModel->setData(idx, coloredIcon, Qt::DecorationRole);
        }
    }
    menu.close(); // 选择色块后直接关闭主菜单
});
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/ui/CategoryPanel.h` (清除旧响应方法定义)
- [ ] 模块/文件：`src/ui/CategoryPanel.cpp` (分类主菜单上通过 QWidgetAction 直接加载色块条并执行同步更新)
- [ ] 模块/文件：`src/ui/MainWindow.h` (移除旧成员声明)
- [ ] 模块/文件：`src/ui/MainWindow.cpp` (FolderButton 主菜单层级通过 QWidgetAction 直接呈现色块条并执行同步更新)
- [ ] 模块/文件：`src/ui/ContentPanel.cpp` (内容面板右键主菜单直接嵌入色块条，重构同步存库交互)
- [ ] 模块/文件：`src/ui/ColorPicker.h` (声明 `ColorStripPicker` 水平色块栏组件，支持 Hover 判定)
- [ ] 模块/文件：`src/ui/ColorPicker.cpp` (实现 `ColorStripPicker` 水平色块自绘、白圈 Hover、ToolTip 和鼠标事件)

**明确禁止越界修改的范围：**
- [ ] 模块/文件：`src/core/BasicCommands.h` —— 不修改
- [ ] 模块/文件：`src/meta/DatabaseManager.cpp` —— 不修改底层 Schema
- [ ] 模块/文件：`src/mft/MftReader.cpp` —— 不修改物理扫描层

## 6. 实现准则与预警【核心】
1. **直接展露**：ColorStripPicker 作为主右键菜单体的 QWidgetAction 直接添加，不再嵌套二级子菜单。确保它的背景色（PaintEvent 中）跟右键菜单本身的暗色系完全协调一致（如 `#1E1E1E` 或采用 `palette().color(QPalette::Window)`）。
2. **选择后菜单关闭**：选择色块发射信号后，必须确保通过 `menu.close()` 将主菜单关闭，提供跟原生 Action 点击关闭菜单完全一致的用户交互体验。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 双向同步模式 | 普通未入库项目不受修改颜色影响，更改颜色只对入库绑定有效的文件夹分类有效。 | ✅ 符合。代码中加入了绑定路径非空判定，以及 `updateCategoryColorByPath` 健壮同步，完全符合规约。 |

## 8. 待确认事项（可选）
（无）
