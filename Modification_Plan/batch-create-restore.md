# 批量创建项目功能物理复原 —— batch-create-restore.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在当前的主干版本中，缺失了对“批量创建项目（BatchCreate）”这一核心功能的物理代码支持（包含对话框、自动持久化、规则行以及右键空白处触发菜单和快捷分发）。为了响应用户的最新指示，我们将从历史备份 `旧版本-7` 中将对应的核心逻辑进行 100% 物理级复原合入当前主干版本。

## 2. 问题定位与恢复策略
- **新建组件物理复原**：
  将 `旧版本-7/src/ui/CreateRuleRow.h` / `CreateRuleRow.cpp` 与 `旧版本-7/src/ui/BatchCreateDialog.h` / `BatchCreateDialog.cpp` 拷贝并导入到当前主干的 `src/ui/` 目录下。
- **构建脚本注册**：
  在 `CMakeLists.txt` 的 `src/ui` 源文件列表中，注册上述四个新文件。
- **动作路由与右键空白处菜单恢复**：
  1. 在 `src/ui/ContentPanel.h` 中，将 `ActionBatchCreate` 添加至 `ActionType` 动作枚举中。
  2. 在 `src/ui/ContentPanel.cpp` 中：
     - 包含 `"BatchCreateDialog.h"` 头文件。
     - 在右键空白菜单中，追加 `批量创建项目...` 选项。如果当前处于 Library 镜像源模式 (`isMirrorSource() == true`)，该菜单项设置为禁用并显示 ToolTip 隔离警告。
     - 在 `onCustomContextMenuRequested` 动作触发分发中，对 `ActionBatchCreate` 进行响应分发，弹出 `BatchCreateDialog`，成功后调用 `refresh()` 顺畅重刷视图模型。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 0    | Step 1 中确认的"核心问题"：复原“批量创建项目（BatchCreate）”功能至当前主干版本 | 本方案核心事件名：batch-create-restore.md | ✅ |
| 1    | 将“批量创建文件夹/文件”的代码逻辑，复原当前版本里 | 物理复制对应的4个核心 UI 文件并注册到 CMakeLists.txt，完成在 ContentPanel 中的右键菜单与快捷响应分发。 | ✅ |

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块和物理拷贝指令进行实施，不得做任何自由发挥或脑补改动。

### 4.1 物理拷贝文件指令
1. 物理拷贝 `旧版本-7/src/ui/CreateRuleRow.h` 写入为 `src/ui/CreateRuleRow.h`
2. 物理拷贝 `旧版本-7/src/ui/CreateRuleRow.cpp` 写入为 `src/ui/CreateRuleRow.cpp`
3. 物理拷贝 `旧版本-7/src/ui/BatchCreateDialog.h` 写入为 `src/ui/BatchCreateDialog.h`
4. 物理拷贝 `旧版本-7/src/ui/BatchCreateDialog.cpp` 写入为 `src/ui/BatchCreateDialog.cpp`

### 4.2 修改 `CMakeLists.txt`（注册源文件）

<<<<<<< SEARCH
    src/ui/RuleRow.cpp
    src/ui/RuleRow.h
    src/ui/SearchHistoryPanel.cpp
=======
    src/ui/RuleRow.cpp
    src/ui/RuleRow.h
    src/ui/CreateRuleRow.cpp
    src/ui/CreateRuleRow.h
    src/ui/BatchCreateDialog.cpp
    src/ui/BatchCreateDialog.h
    src/ui/SearchHistoryPanel.cpp
>>>>>>> REPLACE

### 4.3 修改 `src/ui/ContentPanel.h`（动作枚举追加）

<<<<<<< SEARCH
        ActionRescan,
        ActionRefresh,
        ActionCancelImport
    };
=======
        ActionRescan,
        ActionRefresh,
        ActionCancelImport,
        ActionBatchCreate
    };
>>>>>>> REPLACE

### 4.4 修改 `src/ui/ContentPanel.cpp`（头文件引入、菜单项构建、事件分支响应）

#### 4.4.1 引入头文件
<<<<<<< SEARCH
#include "ContentPanel.h"
#include "MainWindow.h"
#include "MetaPanel.h"
=======
#include "ContentPanel.h"
#include "MainWindow.h"
#include "MetaPanel.h"
#include "BatchCreateDialog.h"
>>>>>>> REPLACE

#### 4.4.2 在空白右键菜单追加“批量创建项目...”项
<<<<<<< SEARCH
        newMenu->addAction(UiHelper::getIcon("folder_filled", QColor("#EEEEEE")), "创建文件夹")->setData(ActionNewFolder); 
        newMenu->addAction(UiHelper::getIcon("text", QColor("#EEEEEE")), "创建 Markdown")->setData(ActionNewMd); 
        newMenu->addAction(UiHelper::getIcon("text", QColor("#EEEEEE")), "创建纯文本文件 (txt)")->setData(ActionNewTxt); 

        menu.addSeparator(); 
        QAction* actPaste = menu.addAction("粘贴"); 
=======
        newMenu->addAction(UiHelper::getIcon("folder_filled", QColor("#EEEEEE")), "创建文件夹")->setData(ActionNewFolder); 
        newMenu->addAction(UiHelper::getIcon("text", QColor("#EEEEEE")), "创建 Markdown")->setData(ActionNewMd); 
        newMenu->addAction(UiHelper::getIcon("text", QColor("#EEEEEE")), "创建纯文本文件 (txt)")->setData(ActionNewTxt); 

        menu.addSeparator(); 

        QAction* actBatchCreate = menu.addAction(UiHelper::getIcon("add", QColor("#EEEEEE")), "批量创建项目...");
        actBatchCreate->setData(ActionBatchCreate);
        // 6.1 磁盘目录模式独占
        if (isMirrorSource()) {
            actBatchCreate->setEnabled(false);
            actBatchCreate->setToolTip("批量创建仅支持在物理磁盘模式下使用");
        }

        menu.addSeparator(); 
        QAction* actPaste = menu.addAction("粘贴"); 
>>>>>>> REPLACE

#### 4.4.3 捕捉动作并弹出对话框
<<<<<<< SEARCH
        case ActionCancelImport: {
            auto indexes = view->selectionModel()->selectedIndexes();
            QStringList targetPaths;
            for (const auto& idx : indexes) {
                if (idx.column() == 0) {
                    QString p = idx.data(PathRole).toString();
                    if (!p.isEmpty()) targetPaths << p;
                }
            }
            if (targetPaths.isEmpty() && !path.isEmpty()) targetPaths << path;

            if (!targetPaths.isEmpty()) {
                DiskIoService::instance().cancelImportAndEraseMetadata(targetPaths);
                ToolTipOverlay::instance()->showText(QCursor::pos(), "已提交异步取消导入和清除指令", 2000, Style::SuccessGreen);
            }
            break;
        }
        default:
            break;
    }
}
=======
        case ActionCancelImport: {
            auto indexes = view->selectionModel()->selectedIndexes();
            QStringList targetPaths;
            for (const auto& idx : indexes) {
                if (idx.column() == 0) {
                    QString p = idx.data(PathRole).toString();
                    if (!p.isEmpty()) targetPaths << p;
                }
            }
            if (targetPaths.isEmpty() && !path.isEmpty()) targetPaths << path;

            if (!targetPaths.isEmpty()) {
                DiskIoService::instance().cancelImportAndEraseMetadata(targetPaths);
                ToolTipOverlay::instance()->showText(QCursor::pos(), "已提交异步取消导入和清除指令", 2000, Style::SuccessGreen);
            }
            break;
        }
        case ActionBatchCreate: {
            BatchCreateDialog dlg(m_currentPath, this);
            if (dlg.exec() == QDialog::Accepted) {
                refresh();
            }
            break;
        }
        default:
            break;
    }
}
>>>>>>> REPLACE

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 物理复制：`src/ui/CreateRuleRow.h`, `src/ui/CreateRuleRow.cpp`, `src/ui/BatchCreateDialog.h`, `src/ui/BatchCreateDialog.cpp`
- [ ] 注册管理：`CMakeLists.txt`
- [ ] 右键联动：`src/ui/ContentPanel.h`, `src/ui/ContentPanel.cpp`

**明确禁止越界修改的范围：**
- [ ] 严禁触碰 `ContentPanel` 中的拖拽接收、视图列表选择逻辑与加密加解密业务流。

## 6. 实现准则与预警【核心】
1. **编译警告防范**：引入的 `CreateRuleRow` 和 `BatchCreateDialog` 在重载或实现析构函数、多路 QComboBox / QSpinBox 的槽联动中，涉及的所有声明变量和形参必须 100% 引用到，确保绝不留下任何 `-Wunused-variable` 或 `-Wunused-parameter` 编译器警告。
2. **防重名覆盖机制**：由于物理创建需要通过 `QDir().mkpath` 或 `QFile::open` 真正写入物理磁盘，因此在开始循环创建前，必须严格运行方案中约定的重名自动追加 (1), (2)... 防覆盖安全防线，保持绝对安全的幂等性。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 一键清除按钮 | 每个可编辑的输入框必须配置上“Qt 原生的 setClearButtonEnabled(true)”，而且只可采用“Qt 原生的 setClearButtonEnabled(true)”，杜绝脑补另创。本方案的后缀配置输入框没有另创清除，符合规范。 | ✅ |
| 窗口关闭按钮恒常红色 | 关闭按钮悬停、普通态等背景均为 ErrorRed。本方案复用 FramelessDialog，完全继承该标准。 | ✅ |
