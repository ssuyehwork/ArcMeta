# 磁盘栏适配编译错误修复方案 —— Analysis_Modification_Plan-71.md

## 1. 任务背景
在根据 Plan-70 整合磁盘栏“设为默认”功能时，用户遇到了 `buildIndex` 成员不存在以及 `MftReader` 标识符未定义的编译错误。本方案旨在提供精准的接口对标与依赖补全说明。

## 2. 问题定位
- **错误 A**：`"buildIndex": 不是 "ArcMeta::CoreController" 的成员`
    - **根因**：当前版本 `CoreController` 的扫描触发接口已统一命名为 `startScan(const QString& drive)`。
- **错误 B**：`“MftReader”: 不是类或命名空间名称`
    - **根因**：`src/ui/MainWindow.cpp` 缺失 `#include "mft/MftReader.h"` 头文件引用。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | `buildIndex` 报错 | 将调用替换为 `CoreController::instance().startScan(letter)` | ✅ |
| 2    | `MftReader` 报错 | 在文件顶部补充 `#include "mft/MftReader.h"` | ✅ |

## 4. 详细解决方案

### 4.1 接口名称对标
在 `src/ui/MainWindow.cpp` 涉及启动扫描的逻辑中，使用以下代码替换旧版 API：
```cpp
// 错误写法
// CoreController::instance().buildIndex(letter); 

// 正确写法
CoreController::instance().startScan(letter);
```

### 4.2 头文件依赖补全
在 `src/ui/MainWindow.cpp` 顶部包含区补充：
```cpp
#include "mft/MftReader.h"
#include "core/CoreController.h"
```

## 5. 修改边界声明【红线】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/ui/MainWindow.cpp` (仅限头文件补全与接口名替换)

**明确禁止越界修改的范围：**
- [ ] 禁止修改 `CoreController` 或 `MftReader` 的底层逻辑。

## 6. 实现准则与预警【核心】
1. **命名匹配**：必须核实 `CoreController.h` 中的方法签名。当前生产版本确认为 `startScan`。
2. **作用域预警**：调用 `startScan` 时，传入的参数应为盘符字符串（如 "C:"），不建议携带尾部反斜杠。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|-------------|----------------------|----------------|
| 编译稳定性 | 必须确保头文件引用完整 | ✅ |
| 架构分层 | UI 层通过 Controller 访问底层服务 | ✅ |

## 8. 待确认事项
- 无。
