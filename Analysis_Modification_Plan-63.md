## 分析计划 #63 ｜ [2025-05-14 10:00:00]

### § 0 需求原文（逐字引用用户描述，禁止意译）
> 根本原因是：`MetaPanel` 在首次显示时，`QScrollArea` 内部的 `m_container` 没有正确计算尺寸，导致内容不渲染。当窗口最大化再还原时，触发了 `resizeEvent`，强制重新计算布局，内容才正常显示出来。
> 
> --- 
> 
> 给Jules的修复提示词： 
> 
> --- 
> 
> **任务：修复 MetaPanel 首次显示内容不渲染的问题** 
> 
> 根本原因：`QScrollArea` 的子容器 `m_container` 在首次 `show` 时未触发正确的尺寸计算，需要在 `showEvent` 中手动强制刷新布局。 
> 
> 在 `MetaPanel` 中重写 `showEvent`，在 `MetaPanel.h` 的 class 内添加声明： 
> ```cpp 
> protected: 
>     void showEvent(QShowEvent* event) override; 
> ``` 
> 
> 在 `MetaPanel.cpp` 中添加实现： 
> ```cpp 
> void MetaPanel::showEvent(QShowEvent* event) { 
>     QFrame::showEvent(event); 
>     // 首次显示时强制刷新滚动区域内容布局 
>     if (m_container) { 
>         m_container->updateGeometry(); 
>         m_container->adjustSize(); 
>     } 
>     if (m_scrollArea) { 
>         m_scrollArea->updateGeometry(); 
>     } 
> } 
> ``` 
> 
> **不得修改任何其他代码**

### § 1 现状诊断（Current State Analysis）

**1.1 涉及文件清单**
| 文件名 | 完整路径 | 当前职责 | 代码行数 |
|--------|----------|----------|----------|
| MetaPanel.h | src/ui/MetaPanel.h | 元数据面板类定义 | ~190 |
| MetaPanel.cpp | src/ui/MetaPanel.cpp | 元数据面板类实现 | ~450 |

**1.2 现有架构问题定位**
- 问题一：`MetaPanel` 首次显示时内容不渲染。
  - 根因：Qt 的 `QScrollArea` 在某些复杂的嵌套布局或动态切换场景下，其 `widget()`（即 `m_container`）可能不会在初次 `show()` 时立即计算出正确的 `sizeHint` 或触发布局更新，导致子控件尺寸为零或未排布。
  - 影响面：用户在点击查看元数据时，看到的是空白面板，必须通过手动缩放窗口触发 `resizeEvent` 才能恢复正常。

**1.3 调用链 / 数据流图**
```
MainWindow -> MetaPanel::show()
  └─► QWidget::showEvent() (默认实现)
        └─► (预期触发布局计算，但 QScrollArea 内部 m_container 未能正确计算尺寸)
```

### § 2 方案设计（Solution Architecture）

**2.1 技术选型决策**
| 候选方案 | 优点 | 缺点 | 是否采用 | 放弃原因 |
|----------|------|------|----------|----------|
| 方案 A：在 showEvent 中手动调用 updateGeometry/adjustSize | 直接、针对性强，解决首次渲染问题 | 无明显缺点 | ✅ 采用 | — |
| 方案 B：在构造函数中调用 | 此时控件尚未完全显示，计算可能不准 | 无效 | ❌ 放弃 | 时机过早 |

**2.2 目标架构设计**
- 设计原则遵循：显式生命周期管理。
- 核心改动逻辑：重写 `showEvent` 虚函数，在控件显示瞬间强制 `m_container` 重新评估几何尺寸并调整大小，同时通知 `m_scrollArea` 更新几何属性以适配子控件。
- 新调用链 / 数据流：
```
MetaPanel::showEvent()
  ├─► QFrame::showEvent() (基类处理)
  ├─► m_container->updateGeometry()
  ├─► m_container->adjustSize()
  └─► m_scrollArea->updateGeometry()
```

**2.3 逐文件改动计划**
| # | 文件名 | 完整路径 | 变更类型 | 改动内容摘要 | 改动行数（估） |
|---|--------|----------|----------|--------------|----------------|
| 1 | MetaPanel.h | src/ui/MetaPanel.h | 修改 | 声明 `showEvent` 覆盖函数 | 2 行 |
| 2 | MetaPanel.cpp | src/ui/MetaPanel.cpp | 修改 | 实现 `showEvent` 强制刷新逻辑 | 10 行 |

**2.4 性能影响评估**
- 时间复杂度变化：O(1) 的额外布局计算。
- 空间复杂度变化：无变化。
- 预期响应时间影响：忽略不计。

### § 3 风险矩阵（Risk Matrix）

| 风险项 | 触发条件 | 严重程度(1-5) | 概率(1-5) | 风险值 | 应对措施 |
|--------|----------|--------------|-----------|--------|----------|
| 递归布局触发 | 若 adjustSize 内部又触发 show | 2 | 1 | 2 | Qt 内部有重入保护，且此处仅在首次 show 执行 |

### § 4 依赖与兼容性检查
- 无新增依赖。
- 保持向后兼容。

### § 5 测试策略（Test Strategy）
- 单元测试：无。
- 集成测试：启动应用，点击元数据面板，检查内容是否立即显示。
- 回归风险点：检查面板在频繁显示/隐藏时的稳定性。

### § 6 回滚方案（Rollback Plan）
- 回滚步骤：使用 `git checkout` 撤销对 `MetaPanel.h` 和 `MetaPanel.cpp` 的修改。

### § 7 执行前置条件
- [ ] 用户已审阅并批准本分析计划

### § 8 审批状态

**✅ 2025-05-14 10:05:00 已获授权，开始执行**

---

### § 9 执行结果（事后填写）

**实际完成时间：** 2025-05-14 10:15:00
**对应 Modification_Record.md 变更序号：** #51 ~ #52
**计划偏差说明：**
  - 原计划 vs 实际执行差异：与计划一致。
  - 超出计划范围的操作：无。
**最终状态：** ✅ 完成
