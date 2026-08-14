# 批量重命名弹窗阻塞改 ToolTip 异步提示方案 —— Modification_Plan-49.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在目前的设计中，批量重命名成功后采用的是传统的模态弹窗通知，这是一个典型、不合理的阻碍性架构，且会导致代码运行和用户交互流程的阻塞（对应用户原话：“关于‘批量重命名’，这个弹窗提示属于傻逼架构，而且会导致代码运行阻塞”）。
为了提供流畅的、非阻塞性、高内聚的异步交互反馈，本方案致力于将该模态阻碍性弹窗，彻底替换为全局的、扁平化的非阻塞式 `ToolTipOverlay` 气泡通知（对应用户原话：“应该采用ToolTipOverlay提示即可”）。

## 2. 问题定位
* **阻塞性交互根因**：`src/ui/BatchRenameDialog.cpp` 里的 427 行，执行了：
  ```cpp
  FramelessMessageBox::information(this, "操作完成", QString("成功处理 %1 个项目").arg(successCount));
  ```
  这调起了强模态对话框，阻断了事件流动。
* **解决策略**：将其完全替换为 `ToolTipOverlay::instance()->showText(...)`（或 `showTip`）。为了保证气泡在对话框淡出关闭后依然在合适位置（如鼠标当前位置）优雅、独立地展示 2000 毫秒（2 秒）后自动渐隐淡出，由于 `ToolTipOverlay` 是全局单例且不从属本对话框生命周期，该改动安全、优雅、绝对不阻塞代码运行。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 模态弹窗导致代码运行阻塞（对应用户原话：“这个弹窗提示属于傻逼架构，而且会导致代码运行阻塞”） | 物理彻底移除 `FramelessMessageBox::information` 模态弹窗调用，解除强交互事件流动阻塞 | ✅ 一致 |
| 2    | 采用 ToolTipOverlay 提示即可（对应用户原话：“应该采用ToolTipOverlay提示即可”） | 使用全局独立单例 `ToolTipOverlay::instance()->showText` 进行优雅、非阻塞式异步提示 | ✅ 一致 |

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 重构批量重命名完成提示，移出阻碍性模态，无缝切入 `ToolTipOverlay` 异步定位气泡

在 `src/ui/BatchRenameDialog.cpp` 中执行替换：

```
<<<<<<< SEARCH
    FramelessMessageBox::information(this, "操作完成", QString("成功处理 %1 个项目").arg(successCount));
    accept();
}
=======
    // 🚀【异步非阻塞】：彻底清除模态阻断弹窗，转用扁平、独立的 ToolTipOverlay 在当前鼠标位置提示 2 秒
    ToolTipOverlay::instance()->showText(QCursor::pos(), QString("成功处理 %1 个项目").arg(successCount), 2000);
    accept();
}
>>>>>>> REPLACE
```

引入头文件依赖：
```
<<<<<<< SEARCH
#include "RuleRow.h"
#include <QFileInfo>
=======
#include "RuleRow.h"
#include "ToolTipOverlay.h"
#include <QFileInfo>
>>>>>>> REPLACE
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] `src/ui/BatchRenameDialog.cpp` （彻底替换掉模态提示框为 `ToolTipOverlay` 调用并引入依赖头）

**明确禁止越界修改的范围：**
- [ ] 批量重命名本身的逻辑内核（如 `DiskBatchRenameService`）及对话框内其它预设、参数绑定判定。

## 6. 实现准则与预警【核心】
1. **重命名零漏写头文件**：必须引入 `ToolTipOverlay.h` 依赖。
2. **生命周期安全性**：`ToolTipOverlay` 为全局常驻单例，因此在 `accept()` 对话框淡出、关闭并自我析构后，气泡依然能在全局正常渲染并自如执行渐隐褪色，无任何空指针（Dangling Pointer）异常风险。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 全局 Tip 渲染 | 严禁使用任何形式的“Windows 系统默认 Tip 样式”！所有的 ToolTip 逻辑必须通过 ToolTipOverlay 渲染。且保持扁平 (Flat)，严禁添加阴影特效。 | ✅ 符合（完全遵照 Memories.md 与规范要求，直接引入 `ToolTipOverlay` 实例并将其显示时间设置为 2 秒优雅淡出，完全契合 UI 扁平化无阴影规范） |

## 8. 待确认事项（可选）
*无*
