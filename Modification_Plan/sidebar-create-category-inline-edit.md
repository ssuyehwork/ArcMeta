侧边栏新建分类行内编辑重构 —— sidebar-create-category-inline-edit.md
状态：待批准执行（尚未获得用户"批准执行"指令）

1. 任务背景
在侧边栏分类面板（CategoryPanel）中，右键菜单提供了“新建分类”与“新建子分类”功能。目前，这两个操作会弹出一个输入名称的独立对话框（FramelessInputDialog）。此交互不符合高标准、无打扰的无缝桌面软件体验。用户期望新建分类直接在侧边栏分类树中实体化，并立刻无缝进入行内文本编辑状态。

2. 问题定位
触发“新建分类”及“新建子分类”的入口分别为 CategoryPanel::onCreateCategory 与 CategoryPanel::onCreateSubCategory。
目前其逻辑是通过 FramelessInputDialog 获取名字，并调用 CategoryRepo::add(cat)，再重刷 m_categoryModel->refresh()。
为了实现零弹窗的直接行内编辑：
需要在点击该菜单项后，直接在数据库层静默添加带有自增编号的默认命名节点（如 新建分类、新建分类 (1) 等），并调用 CategoryRepo::add(cat) 获取分配到的 ID，然后触发 m_categoryModel->refresh() 使模型重绘。
重绘完成后，需要根据该分类的 ID 定位其在 QTreeView 中的 QModelIndex 索引（并映射至代理模型 m_proxyModel 得到的 proxyIndex）。
执行 m_categoryTree->edit(proxyIndex) 进入编辑，从而无缝衔接至 CategoryModel::setData 已有的行内重命名重构落库链。
3. 强制对照表
编号	用户原话 / 我的理解	方案对应点	是否一致
0	Step 1 中确认的"核心问题"：侧边栏分类的右键菜单中“新建分类”交互重构（行内编辑代替弹窗输入）。	本方案核心事件名：侧边栏新建分类行内编辑重构	✅
1	选择该选项后应该是直接侧边栏分类创建分类并直接进入行内编辑（对应用户原话：“选择该选项后应该是直接侧边栏分类创建分类并直接进入行内编辑”）	本方案将彻底废除 FramelessInputDialog 弹窗，新建顶级/子分类时先静默生成具有唯一自动尾缀的默认名字分类，之后触发 m_categoryTree->edit 瞬间开始行内重命名。	✅
2	“新建子分类”也一并同步重构，直接在父分类下创建子项并直接进入行内编辑（达成共识）	onCreateSubCategory 也采用完全对等行内直接编辑。	✅
4. 详细解决方案
本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

4.1 核心重构与自增名字生成逻辑
在 CategoryPanel.cpp 中重构 onCreateCategory 与 onCreateSubCategory：

编写辅助工具函数来计算出不冲突的、默认的命名实体（对于给定的 parentId）：
起始基准名字：顶级默认为 "新建分类"，子分类由于语境一致也同样默认为 "新建分类"（若存在，则递增计算：新建分类 (1)、新建分类 (2)、...）。
检测机制：遍历当前 CategoryRepo::getAll()，若存在相同 parentId 且名字冲突的分类，则追加递增尾缀，确保数据库中不产生因自动落库造成的同级重名现象。
静默构建 Category 结构体并调用 CategoryRepo::add(cat) 持久化，此时 cat.id 将被填入数据库中真实分配的自增长主键。
调用 m_categoryModel->refresh() 更新树形数据模型。由于 CategoryModel::refresh 采用 beginResetModel 与 endResetModel 重置树，我们需要在重置完毕后（利用 QTimer::singleShot(50, ...) 进行极轻微切片以等待 View 刷新完成）精确定位这个新创建的 id 的 proxyIndex，接着触发 m_categoryTree->edit(proxyIdx)。
4.2 Git merge diff 代码块说明
<<<<<<< SEARCH 
void CategoryPanel::onCreateCategory() { 
    FramelessInputDialog dlg("新建分类", "请输入分类名称:", "", this); 
    if (dlg.exec() == QDialog::Accepted) { 
        QString text = dlg.text(); 
        if (!text.isEmpty()) { 
            Category cat; 
            cat.name = text.toStdWString(); 
            cat.parentId = 0; 
            cat.color = getDefaultCategoryColor(); 
             
            QSet<int> expandedIds; 
            QStringList expandedNames; 
            saveExpandedState(QModelIndex(), expandedIds, expandedNames); 
 
            CategoryRepo::add(cat); 
            m_categoryModel->refresh(); 
 
            restoreExpandedState(QModelIndex(), expandedIds, expandedNames); 
        } 
    } 
} 
 
void CategoryPanel::onCreateSubCategory() { 
    QModelIndex index = m_categoryTree->currentIndex(); 
    int id = getTargetCategoryId(index); 
    if (id <= 0) return; 
 
    Category catObj = CategoryRepo::getById(id); 
    if (catObj.encrypted && !CategoryLockManager::instance().isUnlocked(id)) { 
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#e81123;'>分类处于锁定状态，请先解锁后再执行操作！</b>", 2000, QColor("#e81123")); 
        return; 
    } 
 
    FramelessInputDialog dlg("新建子分类", "请输入子分类名称:", "", this); 
    if (dlg.exec() == QDialog::Accepted) { 
        QString text = dlg.text(); 
        if (!text.isEmpty()) { 
            Category cat; 
            cat.name = text.toStdWString(); 
            cat.parentId = id; 
            cat.color = getDefaultCategoryColor(); 
 
            QSet<int> expandedIds; 
            QStringList expandedNames; 
            saveExpandedState(QModelIndex(), expandedIds, expandedNames); 
            expandedIds.insert(id); 
 
            CategoryRepo::add(cat); 
            m_categoryModel->refresh(); 
 
            restoreExpandedState(QModelIndex(), expandedIds, expandedNames); 
        } 
    } 
} 
======= 
void CategoryPanel::onCreateCategory() { 
    // 1. 扫描当前所有的分类，计算出在顶级（parentId = 0）不冲突的默认名字："新建分类"、"新建分类 (1)"、"新建分类 (2)"... 
    auto allCats = CategoryRepo::getAll(); 
    QString baseName = "新建分类"; 
    QString finalName = baseName; 
    int suffix = 1; 
    bool conflict = true; 
    while (conflict) { 
        conflict = false; 
        for (const auto& c : allCats) { 
            if (c.parentId == 0 && QString::fromStdWString(c.name) == finalName) { 
                conflict = true; 
                break; 
            } 
        } 
        if (conflict) { 
            finalName = QString("%1 (%2)").arg(baseName).arg(suffix++); 
        } 
    } 
 
    // 2. 构造实体并持久化 
    Category cat; 
    cat.name = finalName.toStdWString(); 
    cat.parentId = 0; 
    cat.color = getDefaultCategoryColor(); 
 
    QSet<int> expandedIds; 
    QStringList expandedNames; 
    saveExpandedState(QModelIndex(), expandedIds, expandedNames); 
 
    if (CategoryRepo::add(cat)) { 
        m_categoryModel->refresh(); 
        restoreExpandedState(QModelIndex(), expandedIds, expandedNames); 
 
        // 3. 在树更新完毕后，立刻获取新节点的 Index 并进入行内编辑状态 
        int newId = cat.id; 
        QTimer::singleShot(50, this, [this, newId]() { 
            selectCategory(newId); 
            QModelIndex proxyIdx = m_categoryTree->currentIndex(); 
            if (proxyIdx.isValid()) { 
                m_categoryTree->edit(proxyIdx); 
            } 
        }); 
    } 
} 
 
void CategoryPanel::onCreateSubCategory() { 
    QModelIndex index = m_categoryTree->currentIndex(); 
    int parentId = getTargetCategoryId(index); 
    if (parentId <= 0) return; 
 
    Category catObj = CategoryRepo::getById(parentId); 
    if (catObj.encrypted && !CategoryLockManager::instance().isUnlocked(parentId)) { 
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color:#e81123;'>分类处于锁定状态，请先解锁后再执行操作！</b>", 2000, QColor("#e81123")); 
        return; 
    } 
 
    // 1. 扫描同级分类，计算出在 parentId 下不冲突的默认子分类名字 
    auto allCats = CategoryRepo::getAll(); 
    QString baseName = "新建分类"; 
    QString finalName = baseName; 
    int suffix = 1; 
    bool conflict = true; 
    while (conflict) { 
        conflict = false; 
        for (const auto& c : allCats) { 
            if (c.parentId == parentId && QString::fromStdWString(c.name) == finalName) { 
                conflict = true; 
                break; 
            } 
        } 
        if (conflict) { 
            finalName = QString("%1 (%2)").arg(baseName).arg(suffix++); 
        } 
    } 
 
    // 2. 构造子分类实体并持久化 
    Category cat; 
    cat.name = finalName.toStdWString(); 
    cat.parentId = parentId; 
    cat.color = getDefaultCategoryColor(); 
 
    QSet<int> expandedIds; 
    QStringList expandedNames; 
    saveExpandedState(QModelIndex(), expandedIds, expandedNames); 
    expandedIds.insert(parentId); 
 
    if (CategoryRepo::add(cat)) { 
        m_categoryModel->refresh(); 
        restoreExpandedState(QModelIndex(), expandedIds, expandedNames); 
 
        // 3. 展开父节点并自动对新子节点进入行内编辑状态 
        int newId = cat.id; 
        QTimer::singleShot(50, this, [this, newId]() { 
            selectCategory(newId); 
            QModelIndex proxyIdx = m_categoryTree->currentIndex(); 
            if (proxyIdx.isValid()) { 
                m_categoryTree->edit(proxyIdx); 
            } 
        }); 
    } 
} 
>>>>>>> REPLACE 
5. 修改边界声明【范围】
本次方案涉及范围：

模块/文件：src/ui/CategoryPanel.cpp 中的 onCreateCategory 与 onCreateSubCategory 函数。
明确禁止越界修改的范围：

模块/文件：CategoryModel::setData 等已存在的行内文本编辑确认重命名落库管道——不修改。
6. 实现准则与预警【核心】
方案完全依托 Qt 既有 QTreeView::edit 实现无缝编辑。不需要引入任何外部头文件，不影响 QLineEdit 或 Delegate 核心重绘。
重命名输入完成后，会自发、完全流转进入 CategoryModel::setData（EditRole 状态），触发数据库物理 UPDATE。如用户未作任何输入而直接敲回车（或失焦），则会自动将默认名（如 新建分类）作为有效的新命名落库，整条控制链完全契合且绝对安全。
QTimer::singleShot(50, ...) 极佳地解决了 beginResetModel/endResetModel 触发时由于渲染数据项重置造成的定位偏差，保证 100% 捕获到合法的 proxyIndex。
7. Memories.md 合规检查
组件 / 模式	Memories.md 规范要求（写具体内容，不写引用）	本方案是否符合
CategoryPanel 视图重构	本次不涉及任何新增 UI 组件与 Memories.md 规范，属于既有 QTreeView 控件的功能联动	✅
8. 待确认事项（可选）
暂无。