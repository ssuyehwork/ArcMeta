一、 归档至 Modification_Plan/Architecture and Planning.md 的新增内容
### 1.11 列表视图列标题中文名称绑定规范
- **列表模型标题绑定规则**：列表视图绑定的数据模型（如 `LibraryAssetModel` 和 `DiskItemModel`）必须显式重写 `headerData` 方法。
- **列对应中文字符串定义**：在 `orientation == Qt::Horizontal` 且 `role == Qt::DisplayRole` 的情况下，必须严格根据列的 section 返回对应中文字符串：
  - `0` -> `"名称"`
  - `1` -> `"状态"`
  - `2` -> `"评分"`
  - `3` -> `"尺寸"`
  - `4` -> `"类型"`
  - `5` -> `"大小"`
  - `6` -> `"修改日期"`
- 严禁依赖默认实现 or 返回数字作为列标题，保障工业级界面的中文化完整度。
二、 新创建的方案文档：Modification_Plan/列表视图列标题数字显示排查与修复.md 完整内容
# 列表视图列标题数字显示排查与修复 —— 列表视图列标题数字显示排查与修复.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在列表视图模式下，中心内容区域的列标题当前显示为数字（“1”、“2”、“3”、“4”、“5”、“6”、“7”），严重损害了中文化的界面的直观性与可用性。本方案旨在为列表模型正确绑定中文列标题。

## 2. 问题定位
- 列表视图所用控件 `m_treeView` 绑定的源模型分为 `LibraryAssetModel` 和 `DiskItemModel`，这两个模型都间接继承自 `QAbstractTableModel`，并且列数均为 7。
- 经代码排查，由于两模型及其基类均未重写 `headerData(int section, Qt::Orientation orientation, int role)` 函数，Qt 默认在 `orientation == Qt::Horizontal` 且 `role == Qt::DisplayRole` 时返回列的数字序号（自 1 开始）。
- 解决方案是在这两个子类模型中重写 `headerData` 方法，并在水平方向的 DisplayRole 下根据列 Section 映射并返回对应的中文列名称：
  - Section 0: 名称
  - Section 1: 状态
  - Section 2: 评分
  - Section 3: 尺寸
  - Section 4: 类型
  - Section 5: 大小
  - Section 6: 修改日期

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 0    | Step 1 中确认的"核心问题"：列表视图列标题数字显示排查与修复 | 本方案核心事件名：列表视图列标题数字显示排查与修复 | ✅ |
| 1    | 列表视图的列标题，你去排查一下，显示的是不是数字？ | 在背景与定位中确认其显示数字，并在本方案中彻底修复 | ✅ |
| 2    | 列标题应正确显示各业务列中文名称（名称、状态、评分、尺寸、类型、大小、修改日期） | 在 `LibraryAssetModel` 和 `DiskItemModel` 的 `headerData` 中绑定对应的中文字符串 | ✅ |

## 4. 详细解决方案
本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 修改 `src/ui/models/LibraryAssetModel.h`
重写 `headerData` 虚函数声明。

<<<<<<< SEARCH int rowCount(const QModelIndex& parent = QModelIndex()) const override; int columnCount(const QModelIndex& parent = QModelIndex()) const override; QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override; bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override; Qt::ItemFlags flags(const QModelIndex& index) const override;

const std::vector<ArcMeta::ItemRecord>& allRecords() const override { return m_allRecords; }
======= int rowCount(const QModelIndex& parent = QModelIndex()) const override; int columnCount(const QModelIndex& parent = QModelIndex()) const override; QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override; bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override; Qt::ItemFlags flags(const QModelIndex& index) const override; QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

const std::vector<ArcMeta::ItemRecord>& allRecords() const override { return m_allRecords; }
REPLACE


### 4.2 修改 `src/ui/models/LibraryAssetModel.cpp`
实现 `headerData` 函数，根据 Section 映射返回中文名称。

<<<<<<< SEARCH int LibraryAssetModel::columnCount(const QModelIndex&) const { return 7; }

void LibraryAssetModel::setRecords(const std::vector
int LibraryAssetModel::columnCount(const QModelIndex&) const { return 7; }

QVariant LibraryAssetModel::headerData(int section, Qt::Orientation orientation, int role) const { if (orientation == Qt::Horizontal && role == Qt::DisplayRole) { switch (section) { case 0: return QString("名称"); case 1: return QString("状态"); case 2: return QString("评分"); case 3: return QString("尺寸"); case 4: return QString("类型"); case 5: return QString("大小"); case 6: return QString("修改日期"); default: break; } } return QAbstractTableModel::headerData(section, orientation, role); }

void LibraryAssetModel::setRecords(const std::vector

REPLACE


### 4.3 修改 `src/ui/models/DiskItemModel.h`
重写 `headerData` 虚函数声明。

<<<<<<< SEARCH int rowCount(const QModelIndex& parent = QModelIndex()) const override; int columnCount(const QModelIndex& parent = QModelIndex()) const override; QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override; bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override; Qt::ItemFlags flags(const QModelIndex& index) const override;

const std::vector<ArcMeta::ItemRecord>& allRecords() const override { return m_allRecords; }
======= int rowCount(const QModelIndex& parent = QModelIndex()) const override; int columnCount(const QModelIndex& parent = QModelIndex()) const override; QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override; bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override; Qt::ItemFlags flags(const QModelIndex& index) const override; QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

const std::vector<ArcMeta::ItemRecord>& allRecords() const override { return m_allRecords; }
REPLACE


### 4.4 修改 `src/ui/models/DiskItemModel.cpp`
实现 `headerData` 函数，根据 Section 映射返回中文名称。

<<<<<<< SEARCH int DiskItemModel::columnCount(const QModelIndex&) const { return 7; }

void DiskItemModel::setRecords(const std::vector
int DiskItemModel::columnCount(const QModelIndex&) const { return 7; }

QVariant DiskItemModel::headerData(int section, Qt::Orientation orientation, int role) const { if (orientation == Qt::Horizontal && role == Qt::DisplayRole) { switch (section) { case 0: return QString("名称"); case 1: return QString("状态"); case 2: return QString("评分"); case 3: return QString("尺寸"); case 4: return QString("类型"); case 5: return QString("大小"); case 6: return QString("修改日期"); default: break; } } return QAbstractTableModel::headerData(section, orientation, role); }

void DiskItemModel::setRecords(const std::vector

REPLACE


## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/ui/models/LibraryAssetModel.h` (第 18 行左右声明 `headerData` 函数)
- [ ] 模块/文件：`src/ui/models/LibraryAssetModel.cpp` (第 36 行左右实现 `headerData` 函数)
- [ ] 模块/文件：`src/ui/models/DiskItemModel.h` (第 18 行左右声明 `headerData` 函数)
- [ ] 模块/文件：`src/ui/models/DiskItemModel.cpp` (第 34 行左右实现 `headerData` 函数)

**明确禁止越界修改的范围：**
- [ ] `LibraryAssetModel` / `DiskItemModel` 其它现有逻辑函数（如 `data()`、`flags()`、`setData()`）——不修改
- [ ] 其它视图面板（如 `ContentPanel`）——不修改

## 6. 实现准则与预警【核心】
1. 必须精准调用父类默认实现 `QAbstractTableModel::headerData(section, orientation, role)` 作为降级保护，避免对其它 Role 或 Orientation 产生副作用。
2. 所修改的文件属于核心列表模型，修改时必须与声明行和前文结构 100% 对齐，杜绝由于拼写错误或少传参数引起的编译失败。
3. 没有任何未使用的多余变量，避免产生编译警告引起编译阻断。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| UI/交互与偏好规范 | 本次没有新增任何清除按钮、窗口置顶、标题栏按钮、滑杆等，亦没有使用 rgba 蒙版 | ✅ |
| 元数据管理与搜索规范 | 仅重写 `headerData` 的中文文本返回，不改变任何底层逻辑与过滤规则，不会引发搜索、排序逻辑变化 | ✅ |
| 异步加载与防闪烁 | 本方案未干涉 `clear`、`setRecords` 的原子性更新流程与 `m_loadRequestId` 回调校验机制 | ✅ |

## 8. 待确认事项（可选）
无