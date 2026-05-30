# 分析计划 #49 ｜ [2024-05-22 14:00:00]

### § 0 需求原文
> 我需要對“元數據”面板進行重構，我期望像图片这样。由上往下显示数据的顺序是：
> 颜色（颜色显示在胶囊状里）→ 名称（将名称显示在编辑框里，可直接编辑名称不含扩展名）→ 备注编辑框 → 链接编辑框 → 标签 → 分类（文件所在的侧边栏分类 / 文件所在的文件夹）→ 元数据

### § 1 现状诊断（Current State Analysis）

**1.1 涉及文件清单**
| 文件名 | 完整路径 | 当前职责 | 代码行数 |
|--------|----------|----------|----------|
| MetaPanel.h | src/ui/MetaPanel.h | 元数据面板接口定义 | ~150 行 |
| MetaPanel.cpp | src/ui/MetaPanel.cpp | UI 构建与交互逻辑实现 | ~480 行 |
| MetadataManager.h | src/meta/MetadataManager.h | 元数据镜像结构定义 | ~60 行 |
| MetadataManager.cpp | src/meta/MetadataManager.cpp | 元数据持久化逻辑 | ~500 行 |

**1.2 现有架构问题定位**
- 问题一：布局顺序陈旧 — `MetaPanel.cpp` (initUi)
  - 根因：当前布局是早期开发的垂直堆叠，缺乏分组感，且数据展示顺序（名称、类型、大小在最前）不符合用户最新的视觉流向要求。
  - 影响面：右侧“元数据”面板的整体视觉表现。

- 问题二：交互模型缺失 — `MetaPanel.cpp`
  - 根因：文件名目前是只读的 `QLabel`。需求要求支持直接编辑（去除扩展名），这涉及到文件系统物理重命名与元数据索引同步。
  - 备注：目前缺少“链接 (URL)”字段的存储支持。

**1.3 调用链 / 数据流图**
```
[当前数据流]
MainWindow (SelectionChanged) 
  └─► MetaPanel::updateInfo() -> 设置各只读 QLabel
  └─► MetaPanel::setNote() -> 设置 QPlainTextEdit

[目标重构数据流]
MetaPanel (m_nameEdit FocusOut)
  └─► QFile::rename() (物理重命名)
  └─► MetadataManager::renameItem() (索引迁移)
  └─► MainWindow/ContentPanel (UI 刷新信号)
```

### § 2 方案设计（Solution Architecture）

**2.1 技术选型决策**
| 候选方案 | 优点 | 缺点 | 是否采用 | 放弃原因 |
|----------|------|------|----------|----------|
| 保持 QLabel 并弹出对话框修改 | 实现简单 | 交互路径长，不直观 | ❌ 放弃  | 不符合用户“直接编辑”的需求 |
| 原地 QLineEdit 编辑 | 极其直观，符合现代设计 | 需处理物理冲突与扩展名保护 | ✅ 采用  | 工业级交互的标准实现 |

**2.2 目标架构设计**
- 设计原则遵循：信息层级化（Information Hierarchy）与所见即所得（WYSIWYG）。
- 核心改动逻辑：
  1. **布局容器化**：将元数据分为“快捷编辑区”、“扩展信息区”和“基本信息区”。
  2. **智能重命名**：`m_nameEdit` 仅显示 BaseName，失焦时自动拼接原 Suffix 执行物理操作。
  3. **胶囊化调色盘**：重构 `setPalettes`，将色块包裹在高度为 24px 的圆角胶囊容器中，显示 `#HEX (Ratio%)`。

**2.3 逐文件改动计划**

#### 1. `MetadataManager.h / .cpp`
- 在 `RuntimeMeta` 结构体中新增 `std::wstring url;`。
- 补全 `setURL(path, url)` 接口并实现 SQL 持久化。

#### 2. `MetaPanel.h`
- 新增成员：`QLineEdit *m_nameEdit`, `QLineEdit *m_linkEdit`, `QWidget *m_categoryBox`。
- 修改 `updateInfo` 签名，剥离出文件全路径以支持重命名操作。

#### 3. `MetaPanel.cpp`
- **initUi() 彻底重写**：
  - [Section 1] 调色盘胶囊 (Palette Capsules)
  - [Section 2] 名称输入框 (Name Edit, 无边框暗色风格)
  - [Section 3] 备注输入框 (Note Edit)
  - [Section 4] 链接输入框 (Link Edit, 带 Link 图标)
  - [Section 5] 标签区域 (Tag Flow)
  - [Section 6] 分类展示 (Category Pills)
  - [Section 7] 详情网格 (基本信息：评分、尺寸、大小、格式等)
- **eventFilter 实现**：
  - 拦截 `m_nameEdit` 的 `FocusOut`，对比原名，若变更则执行 `QFile::rename`。

**2.4 性能影响评估**
- 无明显性能损耗。重命名操作为原子级文件系统调用。

### § 3 风险矩阵（Risk Matrix）

| 风险项 | 触发条件 | 严重程度(1-5) | 概率(1-5) | 风险值 | 应对措施 |
|--------|----------|--------------|-----------|--------|----------|
| 重命名失败 | 目标文件名已存在或被占用 | 4 | 2 | 8 | 增加 `try-catch` 与原始值回滚机制 |
| 扩展名丢失 | 用户输入包含点号 | 3 | 2 | 6 | 逻辑上严格锁定扩展名后缀，编辑框仅映射 BaseName |

### § 4 依赖与兼容性检查
- 数据库需要执行 `ALTER TABLE metadata ADD COLUMN url TEXT;` (由 `MetadataManager` 自动处理)。

### § 5 测试策略（Test Strategy）
- 交互测试：修改名称后，检查磁盘文件是否同步变更，左侧列表是否刷新。
- 布局测试：切换不同类型文件（图片、文档、文件夹），验证“基本信息”区的动态隐藏逻辑。

### § 6 回滚方案（Rollback Plan）
- 还原 `MetaPanel.cpp` 布局代码，数据库字段保持原样不影响兼容。

### § 7 执行前置条件
- [x] 用户已审阅并批准本分析计划

### § 8 审批状态
**⏳ 等待用户审批 — 未获批准前禁止执行任何代码变更**
