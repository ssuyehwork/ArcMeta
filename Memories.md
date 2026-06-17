# 偏好记录 Memories.md

# 标题栏按钮及布局标准规范

## 1. 标题栏容器 (TitleBar)
- **高度 (Height)**: 物理高度固定为 `32px`。
- **布局边距 (ContentsMargins)**: `(8, 0, 5, 0)`
  - 左侧边距: `8px` (用于对齐应用名称标签)
  - 右侧对齐: `5px` (物理对齐右侧边缘)
- **全局间距 (Spacing)**: `8px` (应用名与按钮组之间的间距)。

## 2. 按钮组容器 (TitleBarButtons Container)
- **布局间距 (Spacing)**: 按钮与按钮之间的物理间距固定为 `4px`。
- **布局边距 (ContentsMargins)**: `(0, 0, 0, 0)`。

## 3. 按钮物理参数 (Button Parameters)
- **外框尺寸 (FixedSize)**: `24x24px`。
- **图标尺寸 (IconSize)**: `18x18px`。
- **圆角 (BorderRadius)**: `4px`。
- **背景样式**:
  - 默认状态: `transparent` (透明)。
  - 悬停状态 (Hover): `rgba(255, 255, 255, 0.1)` (关闭按钮除外)。
  - 按下状态 (Pressed): `rgba(255, 255, 255, 0.2)`。

## 4. 特殊按钮规范 (Special Buttons)
- **关闭按钮 (Close Button)**:
  - **全应用标准**: 所有界面（主窗口、面板、对话框、标签块）的关闭按钮必须保持视觉一致性。
  - **背景颜色**: 默认状态为 `transparent`。
  - **悬停状态 (Hover)**: `rgba(255, 255, 255, 0.1)` (强制标准)。
  - **按下状态 (Pressed)**: `rgba(255, 255, 255, 0.2)`。
  - **圆角**: `4px`。
- **置顶按钮 (Pin Button)**:
  - **激活颜色**: 选中状态下图标颜色切换为 `BrandOrange`。
- **同步按钮 (Sync Button)**:
  - **状态联动**: 存在待同步元数据时，图标强制显示为 `ErrorRed`；同步完成后恢复为 `TextMain`。

## 5. 交互行为
- 所有标题栏按钮必须开启 `Qt::WA_Hover` 属性以触发悬停事件。
- 必须安装 `m_hoverFilter` 事件过滤器以支持全局 ToolTip 悬浮提醒。
- 新建按钮 (+) 采用手动 `popup` 菜单模式，严禁使用 `setMenu` 以免破坏图标的绝对居中对齐。

// ===================|===================

# 6. 关于“清除”按钮
## 6.1 每个可编辑的输入框必须配置上“Qt 原生的 setClearButtonEnabled(true)”，而且只可采用“Qt 原生的 setClearButtonEnabled(true)”，杜绝脑补另创 

// ===================|===================

# 7. 元数据管理与搜索规范
## 7.1 隔离式多维关联索引
- **机制**：`MetadataManager` 通过 `m_fileNameToFids`（仅文件）、`m_folderNameToFids`（仅文件夹）及 `m_extensionToFids`（仅后缀，小写，不含点）三个隔离的倒排索引管理名称关联。
- **一致性**：在项目激活（`ensureActivated`）、重命名（`renameItem`）及删除（`removeMetadataSync`）时，必须同步维护上述三个索引映射。
- **去重**：注册索引时须执行 `std::find` 检查，防止同一 FID 在同一键下重复注。

## 7.2 “范围感知 (Scope-Aware)” 搜索
- **核心逻辑**：搜索行为必须实时对标 UI 顶部的 **蓝色提示线 (Focus Line)** 位置。
- **数据流**：搜索请求必须通过 `CoreController::performSearch` 转发，并携带当前的数据源范围参数（"category" 或 "nav"）。
- **过滤准则**：
    - **侧边栏模式**：限定在当前分类及其子类范围内（利用 `CategoryRepo::getItemsRecursive`）。
    - **目录导航模式**：限定在当前物理磁盘路径及其子目录范围内（通过路径前缀匹配）。
