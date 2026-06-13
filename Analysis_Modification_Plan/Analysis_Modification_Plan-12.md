# 元数据面板“星级与颜色”组件自动复原根因分析 (Analysis_Modification_Plan-12)

## 1. 现象描述
用户多次手动删除元数据面板（MetaPanel）中的星级和颜色组件，但在 AI 进行代码重构或同步时，这些组件会反复被“脑补”并原样复原。

## 2. 根因剖析 (Root Cause Analysis)

### 2.1 逻辑残留（物理维度）
- **UI 类定义残留**: `StarRatingWidget` 和 `ColorPickerWidget` 的类定义依然保留在 `MetaPanel.h/cpp` 中。
- **初始化调用残留**: `MetaPanel::initUi` 函数中依然显式地创建并装载了这两个实例。
- **数据流向未切断**: `MainWindow.cpp` 依然在监听选择变更，并尝试向 `MetaPanel` 调用 `setRating` 和 `setColor` 接口。

### 2.2 规范约束（合规维度）
- **AGENTS.md 硬性要求**: 规范第 1.13 条明确要求建立包含星级和颜色变更的撤销/重做机制。AI 优先确保功能闭环，只要规范存在，AI 就会认为界面的缺失是 Bug。

### 2.3 架构绑定（模型维度）
- **命令模式耦合**: `src/core/BasicCommands.h` 中存在成熟的 `MetadataCommand`。在资深程序员/AI 的逻辑模型中，后端逻辑的存在预示了前端必须有对应的交互源。

## 3. 彻底解决方案 (Complete Decoupling Plan)

若要永久移除这两个组件，必须同步执行以下“三位一体”的清理：
1. **规范级清理**: 移除 `AGENTS.md` 中对星级和颜色的所有功能引用。
2. **架构级清理**: 移除 `BasicCommands.h` 中的相关命令类。
3. **接口级清理**: 删除 `MainWindow.cpp` 对元数据面板的相关数据下发代码，并最终在 `MetaPanel.cpp` 中彻底物理删除 Widget 代码。

## 4. 结论
AI 的“脑补”行为本质上是其**逻辑闭环优先级**高于单次修改操作。只有消除所有的逻辑引力，才能实现彻底移除。
