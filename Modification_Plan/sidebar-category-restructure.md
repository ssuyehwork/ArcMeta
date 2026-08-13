# 侧边栏分类层级平铺与控制按钮美化 —— sidebar-category-restructure.md

> 状态：已批准，执行中 / 已执行完成

## 1. 任务背景
在 ArcMeta 应用的侧边栏中，目前用户自定义创建的分类文件夹在视觉层级上存在一定的缩进，且“文件夹 (N)”组标识的设计较偏向普通的文本，缺乏按钮的质感和职能，这容易给用户带来层级嵌套的误解。
为了让界面更加直观和现代化，需要将所有自定义创建的分类升级为无缩进的一等公民（一级分类），并将“文件夹 (N)”彻底美化为一个拥有独立按钮质感、职能精确（专用来隐藏或显示自定义文件夹）的专用按钮。
更重要的是，原本采用外部垂直堆叠布局会导致按钮错位至顶端。本方案采用**“单树占位挂载机制”**，通过在模型中挂载一个无字无图标的哨兵行 `FOLDER_GROUP_PLACEHOLDER_ID = -100`，并在 `modelReset` 时通过 `setIndexWidget()` 将真实的按钮控件精密嵌入树中该行，完美从物理上保证按钮永远卡在系统项/快速访问与自定义分类之间。

## 2. 问题定位
- **模块 1：** `src/ui/CategoryModel.cpp`
  - **位置：** `refresh()` 中
  - **原因：** 需要在挂载“快速访问”与遍历自定义分类之间，插入占位哨兵行：
  ```cpp
        {
            QStandardItem* folderGroupPlaceholder = new QStandardItem("");
            folderGroupPlaceholder->setData("folder_group_placeholder", TypeRole);
            folderGroupPlaceholder->setData(FOLDER_GROUP_PLACEHOLDER_ID, IdRole);
            folderGroupPlaceholder->setFlags(Qt::NoItemFlags);
            root->appendRow(folderGroupPlaceholder);
        }
  ```
- **模块 2：** `src/ui/CategoryPanel.cpp`
  - **位置：** `initUi()` 中 `m_btnFolderGroup` 的 QSS 样式表及 `modelReset` 挂载
  - **原因：** 去除多余的 `addWidget(m_btnFolderGroup)`。在 `modelReset` 回调中，定位到 FOLDER_GROUP_PLACEHOLDER_ID 的 QModelIndex 后，调用 `setIndexWidget(proxyIdx, m_btnFolderGroup)`，使其完美卡在分类树正中！

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 0    | Step 1 中确认的"核心问题"：侧边栏自定义分类的层级重构与隐藏控制按钮优化 | 本方案核心事件名：侧边栏分类层级平铺与控制按钮美化 | ✅ |
| 1    | 将标记为①的主分类““文件夹 (N)” (▼ / ▶)”变成按钮，该按钮是专用来隐藏或显示自定义创建的分类文件夹（对应用户原话：“将标记为①的主分类““文件夹 (N)” (▼ / ▶)”变成按钮，该按钮是专用来隐藏或显示自定义创建的分类文件夹”） | 重新设计并美化 `m_btnFolderGroup` 的 QSS，使之呈现极佳的圆角、悬浮高亮、对齐间距按钮特性，并挂载于占位行上。 | ✅ |
| 2    | 将标记为②二级分类升级为一级分类（一等公民）（对应用户原话：“将标记为②二级分类升级为一级分类（一等公民）”） | 自定义分类节点升级为一级分类。按钮和自定义分类通过 15px 的左侧内边距左对齐，完全升级为无缩进的一等公民。 | ✅ |

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 在 `src/ui/CategoryModel.h` 中定义共享哨兵常量
```cpp
class CategoryModel : public QStandardItemModel {
    Q_OBJECT
public:
    static constexpr int CAT_GROUP_SYS_ID = -9;
    static constexpr int FOLDER_GROUP_PLACEHOLDER_ID = -100;
```

### 4.2 修改 `src/ui/CategoryModel.cpp` 挂载占位行
```cpp
        // 5. 挂载“快速访问”
        if (favGroup) {
            root->appendRow(favGroup);
        }

        // 5.5 挂载占位行
        {
            QStandardItem* folderGroupPlaceholder = new QStandardItem("");
            folderGroupPlaceholder->setData("folder_group_placeholder", TypeRole);
            folderGroupPlaceholder->setData(FOLDER_GROUP_PLACEHOLDER_ID, IdRole);
            folderGroupPlaceholder->setFlags(Qt::NoItemFlags);
            root->appendRow(folderGroupPlaceholder);
        }
```

### 4.3 修改 `src/ui/CategoryPanel.cpp` 嵌入真实控件
```cpp
        // 2026-08-xx 按照用户要求修复：定位"文件夹分组占位行"，把真实按钮控件贴合嵌入该行
        if (m_categoryTree && m_proxyModel && m_btnFolderGroup) {
            for (int i = 0; i < m_proxyModel->rowCount(); ++i) {
                QModelIndex proxyIdx = m_proxyModel->index(i, 0);
                if (proxyIdx.data(IdRole).toInt() == CategoryModel::FOLDER_GROUP_PLACEHOLDER_ID) {
                    m_categoryTree->setIndexWidget(proxyIdx, m_btnFolderGroup);
                    break;
                }
            }
        }
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [x] 模块/文件：`src/ui/CategoryModel.h` 增加 `FOLDER_GROUP_PLACEHOLDER_ID` 常量
- [x] 模块/文件：`src/ui/CategoryModel.cpp` 追加占位行
- [x] 模块/文件：`src/ui/CategoryPanel.cpp` 精确嵌入 `m_btnFolderGroup`

**明确禁止越界修改的范围：**
- [x] 数据库底层操作——不修改。

## 6. 实现准则与预警【核心】
- **完美对齐与无警告编译**：
  在应用此 QSS 变更时，必须确保不凭空捏造任何成员变量或私有静态锁，严格尊重已声明的变量生命周期。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 输入框清除功能 | 所有可编辑的输入框一键清除，一律且仅允许采用 Qt 原生的 setClearButtonEnabled(true)，不涉及本方案 | ✅ |
| 窗口置顶 | 窗口置顶状态一律使用 Win32 原生 SetWindowPos 并搭配 SWP_NOSENDCHANGING 标志，不涉及本方案 | ✅ |
| 标题栏悬停与按下色值 | Hover 状态背景色 #3E3E42（Style::HoverBackground），Pressed 状态 #4E4E52（Style::PressedBackground），不涉及本方案 | ✅ |

## 8. 待确认事项（可选）
暂无。
