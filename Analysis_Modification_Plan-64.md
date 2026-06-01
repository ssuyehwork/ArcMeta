## 分析计划 #64 ｜ [2025-05-14 10:20:00]

### § 0 需求原文（逐字引用用户描述，禁止意译）
> 看了截图，`showEvent` 没有解决问题，因为根本原因不在这里。
> 
> 真正的根因是：**`resizeEvent` 里的 `m_container->setFixedWidth(viewportW)` 在首次显示时 `viewportW` 为 0**，导致容器宽度被锁死为 0，内容全部不可见。只有窗口最大化再还原触发第二次 `resizeEvent` 时，`viewportW` 才有正确值。
> 
> --- 
> 
> 给 Jules 的修复提示词： 
> 
> --- 
> 
> **任务：修复 MetaPanel 首次显示内容不可见的根本原因** 
> 
> 在 `MetaPanel.cpp` 的 `resizeEvent` 中，找到这一行： 
> 
> ```cpp 
> if (m_container && viewportW > 0) { 
>     m_container->setFixedWidth(viewportW); 
> } 
> ``` 
> 
> **删除 `m_container->setFixedWidth(viewportW)` 这一行**，改为只设置最大宽度： 
> 
> ```cpp 
> if (m_container && viewportW > 0) { 
>     m_container->setMaximumWidth(viewportW); 
> } 
> ``` 
> 
> 同时**删除整个 `showEvent` 函数的实现和声明**，它是无效的补丁，不再需要。 
> 
> **不得修改任何其他代码**

### § 1 现状诊断（Current State Analysis）

**1.1 涉及文件清单**
| 文件名 | 完整路径 | 当前职责 | 代码行数 |
|--------|----------|----------|----------|
| MetaPanel.h | src/ui/MetaPanel.h | 元数据面板类定义 | ~190 |
| MetaPanel.cpp | src/ui/MetaPanel.cpp | 元数据面板类实现 | ~460 |

**1.2 现有架构问题定位**
- 问题一：`MetaPanel` 首次显示内容仍不可见。
  - 根因：在 `resizeEvent` 中，代码试图通过 `m_container->setFixedWidth(viewportW)` 来消除横向滚动条。但在 `MetaPanel` 首次构造并尚未完全布局到父窗口之前，`m_scrollArea->viewport()->width()` 返回值为 0。调用 `setFixedWidth(0)` 导致 `m_container` 被物理锁死为 0 宽度，子控件由于宽度限制而变得不可见。
  - 修正：改用 `setMaximumWidth(viewportW)` 既能限制最大宽度防止溢出，又不会在宽度为 0 时强制挤压容器，允许容器在渲染初期保持其 natural size。

**1.3 调用链 / 数据流图**
```
MetaPanel::resizeEvent(event)
  └─► viewportW = m_scrollArea->viewport()->width() (首次为 0)
        └─► m_container->setFixedWidth(0)  <-- 致命死锁：容器宽度被锁死为零
```

### § 2 方案设计（Solution Architecture）

**2.1 技术选型决策**
| 候选方案 | 优点 | 缺点 | 是否采用 | 放弃原因 |
|----------|------|------|----------|----------|
| 方案 A：改用 setMaximumWidth | 允许在 0 宽度时容器保持自适应，待有宽度后再限制 | 无 | ✅ 采用 | — |
| 方案 B：增加 viewportW > 20 的判断 | 简单 | 但在某些窄窗口下可能失效 | ❌ 放弃 | 方案 A 更符合 Qt 布局哲学 |

**2.2 目标架构设计**
- 核心改动逻辑：
  1. 移除 `showEvent`：该补丁已被证明无法解决根因。
  2. 修改 `resizeEvent`：将 `setFixedWidth` 降级为 `setMaximumWidth`，解除对初次渲染时容器宽度的强制清零。

**2.3 逐文件改动计划**
| # | 文件名 | 完整路径 | 变更类型 | 改动内容摘要 | 改动行数（估） |
|---|--------|----------|----------|--------------|----------------|
| 1 | MetaPanel.h | src/ui/MetaPanel.h | 修改 | 移除 `showEvent` 声明 | 1 行 |
| 2 | MetaPanel.cpp | src/ui/MetaPanel.cpp | 修改 | 移除 `showEvent` 实现，修改 `resizeEvent` 逻辑 | 15 行 |

**2.4 性能影响评估**
- 无变化。

### § 3 风险矩阵（Risk Matrix）

| 风险项 | 触发条件 | 严重程度(1-5) | 概率(1-5) | 风险值 | 应对措施 |
|--------|----------|--------------|-----------|--------|----------|
| 横向溢出 | 当容器内容宽度超过 viewport 时 | 2 | 2 | 4 | setMaximumWidth 依然保留了宽度上限约束 |

### § 4 依赖与兼容性检查
- 无影响。

### § 5 测试策略（Test Strategy）
- 验证：冷启动应用，打开元数据面板，内容应立即呈现，无需调整窗口大小。

### § 6 回滚方案（Rollback Plan）
- 回滚至本次修改之前的版本。

### § 7 执行前置条件
- [ ] 用户已审阅并批准本分析计划

### § 8 审批状态

**✅ 2025-05-14 10:25:00 用户明确指示直接修改，无需等待审批。**

---

### § 9 执行结果（事后填写）

**实际完成时间：** 2025-05-14 10:35:00
**对应 Modification_Record.md 变更序号：** #53 ~ #54
**计划偏差说明：**
  - 原计划 vs 实际执行差异：与计划一致。
  - 超出计划范围的操作：无。
**最终状态：** ✅ 完成
