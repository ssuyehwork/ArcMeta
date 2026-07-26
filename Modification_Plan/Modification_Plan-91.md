# 解决全局所有按钮点击后显示点状聚焦虚线框问题 —— Modification_Plan-91.md

> 状态：已批准，已执行完成

## 1. 任务背景
在全站范围内，当用户点击任意按钮（如顶栏切换按钮、左侧分类按钮、过滤器等）时，按钮周边都会显示点状虚线聚焦边框。本方案旨在以最简单、彻底且不影响原有样式的方案，全局禁绝这一虚线的显现。

## 2. 问题定位
点状虚线是 Qt 默认的聚焦指示器（Focus Indicator）。在默认情况下，继承自 `QAbstractButton` 的所有按钮组件（包括 `QPushButton`、`QToolButton`、`QCheckBox`、`QRadioButton` 等）在获得焦点时都会默认绘制该虚线框。

在 Qt 样式表语法中，通过将基类 `QAbstractButton` 的 `outline` 属性统一声明为 `none`，可以让所有按钮子类在获得聚焦时自动不绘制此边框，从根源上实现全站级的完美屏蔽。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 解决全局所有按钮点击后显示点状聚焦虚线框问题 | 全局设置 `QAbstractButton` 样式 `outline: none;` | ✅ |
| 2    | 定位到 `main` 函数中的 `QApplication a(argc, argv);` 位置后加入全局样式表设置 | 在 `src/main.cpp` 中对应位置加装全局样式表代码 | ✅ |

## 4. 详细解决方案
在 `src/main.cpp` 中初始化 `QApplication` 的位置，加入一行全局样式表设置：

### 修改 `src/main.cpp`
定位到 `main` 函数中的 `QApplication a(argc, argv);` 位置：

```cpp
    // 设置高 DPI 支持：Qt 6 默认行为，此处显式设置 PassThrough 以防旧设备缩放模糊
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication a(argc, argv);

    // 2026-07-26：全局彻底消除所有按钮（QAbstractButton 子类）被点击后产生的点状虚线聚焦框
    a.setStyleSheet("QAbstractButton { outline: none; }");
```

由于 `QAbstractButton` 是所有按钮类的通用抽象基类，此设置会以最内聚、最高效的方式直接且全局作用于应用中的每一个按钮（不影响已设置的其他样式）。

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [x] 模块/文件：`src/main.cpp` 仅此一处修改

**明确禁止越界修改的范围：**
- [x] 禁止逐个修改各页面组件按钮的 `.cpp` 逻辑（无须繁琐地逐个设置 `setFocusPolicy`）。
- [x] 禁止在其他样式表文件中写分散的规则。

## 6. 实现准则与预警【核心】

1. 必须精准在 `QApplication a(argc, argv);` 定义之后、加载主界面及各组件之前设置该全局样式。
2. 此设置仅影响继承自 `QAbstractButton` 的组件，完全不需要修改各按钮的 C++ 逻辑或 Focus 策略。
3. 保证样式设置开箱即用，由于未引用额外的头文件或宏，对现有编译环境和依赖无任何干扰。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 按钮点击虚线 | 全局按钮被点击后不显示点状虚线框，通过全局 QSS 设定 QAbstractButton { outline: none; } 屏蔽 | ✅ |

## 8. 待确认事项
无。
