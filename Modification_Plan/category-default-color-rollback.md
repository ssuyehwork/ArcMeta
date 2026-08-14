# 自定义分类默认颜色恢复深灰 —— category-default-color-rollback.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在先前的版本或对话中，AI 脑补并修改了自定义分类文件夹的默认色生成逻辑以及 Delegate 渲染器的兜底设色，将其改写成了亮黄色 `#FDB70A`。
按照本项目的严密产品标准，自定义分类（文件夹）的颜色只能且必须为标准深灰色 `#555555`。

本方案旨在全面、彻底地将自定义分类的默认生成色及渲染器回退色回滚并锚定为唯一的标准深灰色 `#555555`。

## 2. 问题定位
- **位置 1：** `src/ui/CategoryPanel.cpp` 中的 `getDefaultCategoryColor()`
  - **现象：** 该函数返回了 `L"#FDB70A"`，导致新建自定义文件夹分类时，数据库字段 `color` 默认被持久化为了亮黄色。
- **位置 2：** `src/ui/CategoryDelegate.h` 中的 `paint()`
  - **现象：** 该渲染器在 `colorHex` 为空时，回退背景色默认采用 `#FDB70A`：
    ```cpp
    QColor baseColor = colorHex.isEmpty() ? QColor("#FDB70A") : QColor(colorHex);
    ```
    导致未显式在数据库中定义设色的自定义分类被强行渲染为了亮黄色。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 0    | Step 1 中确认的"核心问题"：恢复自定义文件夹默认颜色为 #555555 | 本方案核心事件名：自定义分类默认颜色恢复深灰 | ✅ |
| 1    | 自定义文件夹（分类）的颜色只能为#555555（对应用户原话：“自定义文件夹（分类）的颜色只能为#555555”） | 将默认设色生成和 Delegate 渲染器的回退色统一修改、回滚为 `#555555`。 | ✅ |
| 2    | 这个"#FDB70A"色码是前几任对话时，jules这个傻逼、脑残Ai脑补的（对应用户原话：“这个"#FDB70A"色码是前几任对话时，jules这个傻逼、脑残Ai脑补的”） | 彻底摒弃 `#FDB70A`，在自定义分类的生成和渲染生命周期中回归标准。 | ✅ |

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 修改 `src/ui/CategoryPanel.cpp`
将默认分类颜色返回值回滚为标准深灰色 `L"#555555"`。

```
<<<<<<< SEARCH
/**
 * @brief 获取默认分类颜色：深灰色 (#555555)
 * 2026-06-xx 按照用户要求：废除随机色，统一默认使用深灰色
 */
static std::wstring getDefaultCategoryColor() {
    return L"#FDB70A";
}
=======
/**
 * @brief 获取默认分类颜色：深灰色 (#555555)
 * 2026-06-xx 按照用户要求：废除随机色，统一默认使用深灰色（对应用户原话：“自定义文件夹（分类）的颜色只能为#555555”）
 */
static std::wstring getDefaultCategoryColor() {
    return L"#555555";
}
>>>>>>> REPLACE
```

### 4.2 修改 `src/ui/CategoryDelegate.h`
当未指定分类设色时，默认回退色采用标准深灰色 `#555555`。

```
<<<<<<< SEARCH
            QString colorHex = index.data(ColorRole).toString();
            // 当未指定设色时，默认回退色采用标志性的亮丽文件夹黄色 (#FDB70A)
            QColor baseColor = colorHex.isEmpty() ? QColor("#FDB70A") : QColor(colorHex);
            QColor bg = selected ? baseColor : QColor("#2a2d2e");
=======
            QString colorHex = index.data(ColorRole).toString();
            // 当未指定设色时，默认回退色采用标志性的深灰色 (#555555)（对应用户原话：“自定义文件夹（分类）的颜色只能为#555555”）
            QColor baseColor = colorHex.isEmpty() ? QColor("#555555") : QColor(colorHex);
            QColor bg = selected ? baseColor : QColor("#2a2d2e");
>>>>>>> REPLACE
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [x] 模块/文件：`src/ui/CategoryPanel.cpp` 的 `getDefaultCategoryColor()`。
- [x] 模块/文件：`src/ui/CategoryDelegate.h` 中未指定设色时的 `baseColor` 回退定义。

**明确禁止越界修改的范围：**
- [x] 内容面板（`ContentPanel`）的折叠文件夹按钮图标颜色（其保持与文件夹切换的状态同步，无需改变，属于非分类界面的 UI）——不修改。
- [x] 导航面板（`NavPanel`）收藏夹星星、闪电等亮黄色高亮设色（属于系统全局公共状态视觉，非自定义分类）——不修改。

## 6. 实现准则与预警【核心】
- **精确设色物理隔离**：本次修改仅对自定义文件夹分类默认值和分类树 Delegate 渲染时起效，实现完美的架构双轨与 UI 块隔离，确保不影响系统内置图标及常规交互高亮按钮的正常视觉反馈。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 输入框清除功能 | 一律使用 Qt 原生 setClearButtonEnabled(true)，不涉及本方案 | ✅ |
| 窗口置顶 | 使用 Win32 原生 SetWindowPos，不涉及本方案 | ✅ |
| 标题栏按钮样式 | 标题栏及按钮颜色规范，不涉及本方案 | ✅ |

## 8. 待确认事项（可选）
暂无。
