#include "HelpWindow.h"
#include "StringUtils.h"
#include <algorithm>

HelpWindow::HelpWindow(QWidget* parent) : FramelessDialog("使用说明", parent) {
    // [用户修改要求] 重构为双栏布局（左侧热键，右侧详情），调高高度并隐藏最大化按钮。
    setObjectName("HelpWindow");
    loadWindowSettings();
    setFixedSize(650, 750); // 调高窗口高度
    if (m_maxBtn) m_maxBtn->hide(); // 隐藏最大化按钮
    setupData();
    initUI();
}

void HelpWindow::setupData() {
    auto add = [&](const QString& key, const QString& desc) {
        m_helpData.append({key, desc});
    };

    // [MODIFIED] 2026-04-xx 按照用户要求：全量同步 ShortcutManager 真实逻辑，修正陈旧描述

    // --- 系统全局热键 ---
    add("Alt + A", "全局灵感上下文 (呼出最近发送项的连击菜单)");
    add("Alt + Space", "呼出/隐藏快速窗口");
    add("Ctrl + S", "浏览器智能采集 (仅在浏览器活跃时生效)");
    add("Ctrl + Shift + E", "全局一键收藏 (捕获剪贴板最后一次内容)");
    add("Ctrl + Shift + V", "全局纯文本粘贴");
    add("Ctrl + Shift + Alt + S", "全局应用锁定 (App Lock)");

    // --- 窗口局内热键 ---
    add("Alt + D", "置顶/取消置顶项目 (Pin)");
    add("Alt + Q", "切换窗口始终最前 (Stay on Top)");
    add("Alt + W", "显示/隐藏侧边栏");
    add("Alt + Up / Down", "手动调整排序 (支持项目或分类)");
    add("Ctrl + 0 ~ 5", "设置星级评分 (0 为取消评分)");
    add("Ctrl + A", "全选列表内容");
    add("Ctrl + B", "编辑选中项");
    add("Ctrl + C", "提取纯文本内容 (排除标题等元数据干扰)");
    add("Ctrl + E", "开启高级筛选面板 / 切换收藏状态");
    add("Ctrl + F", "聚焦搜索框并全选内容");
    add("Ctrl + G", "折叠/展开筛选器组");
    add("Ctrl + N", "新建灵感笔记");
    add("Ctrl + R", "联动显示/隐藏面板 (侧边栏+高级筛选)");
    add("Ctrl + S", "立即锁定当前分类 / 保存编辑器修改");
    add("Ctrl + W", "关闭当前窗口 (隐藏至托盘)");
    add("Ctrl + Alt + C", "复制选中项标签");
    add("Ctrl + Alt + V", "将标签粘贴至选中项");
    add("Ctrl + Alt + S", "显示/隐藏已加锁的分类");
    add("Ctrl + Shift + A", "一键归位至‘全部数据’视图");
    add("Ctrl + Shift + S", "闪速锁定所有分类");
    add("Delete", "将选中项删除");
    add("Enter / 双击", "选中项自动上屏 (粘贴至目标软件)");
    add("F2", "重命名 (支持列表项目或侧边栏分类)");
    add("F4", "重复上一次操作 (如批量移动、粘贴标签)");
    add("F5", "刷新当前数据列表");
    add("PgUp / PgDn", "数据列表翻页 (上一页/下一页)");
    add("Shift + Delete", "彻底删除项目 (不可恢复)");
    add("Space", "快速预览选中项内容");
    add("Tab", "在搜索框、列表与侧边栏之间循环切换焦点");
    add("波浪键 (~) / Backspace", "一键归位至‘全部数据’视图");

    // [用户修改要求] 按照字母顺序对所有快捷键进行全局排序，移除所有分组标题
    std::sort(m_helpData.begin(), m_helpData.end(), [](const HelpItem& a, const HelpItem& b) {
        return a.key.toLower() < b.key.toLower();
    });
}

void HelpWindow::initUI() {
    auto* layout = new QHBoxLayout(m_contentArea);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 左侧列表
    m_listWidget = new QListWidget();
    m_listWidget->setFixedWidth(220);
    m_listWidget->setStyleSheet(R"(
        QListWidget { 
            background-color: #252526; 
            border: none; 
            border-right: 1px solid #333;
            color: #BBB;
            outline: none;
            padding: 5px;
        }
        QListWidget::item {
            height: 25px;
            padding-left: 10px;
            border-radius: 4px;
            margin: 0px 5px;
            font-size: 11px;
        }
        QListWidget::item:hover {
            background-color: #2D2D2D;
        }
        QListWidget::item:selected {
            background-color: #094771;
            color: white;
        }
    )");

    // 右侧详情
    m_detailView = new QTextBrowser();
    m_detailView->setFrameShape(QFrame::NoFrame);
    m_detailView->setStyleSheet(R"(
        QTextBrowser { 
            background-color: #1e1e1e; 
            color: #DDD; 
            padding: 30px;
            line-height: 1.6;
            border: none;
        }
    )");

    layout->addWidget(m_listWidget);
    layout->addWidget(m_detailView);

    // [用户修改要求] 全局字母顺序排列，移除分类分组
    for (int i = 0; i < m_helpData.size(); ++i) {
        auto* listItem = new QListWidgetItem(m_helpData[i].key);
        listItem->setData(Qt::UserRole, i);
        m_listWidget->addItem(listItem);
    }

    connect(m_listWidget, &QListWidget::itemClicked, this, &HelpWindow::onItemSelected);

    // 默认选中第一项
    if (m_listWidget->count() > 0) {
        m_listWidget->setCurrentRow(0);
        onItemSelected(m_listWidget->item(0));
    }
}

void HelpWindow::onItemSelected(QListWidgetItem* item) {
    if (!item) return;

    int idx = item->data(Qt::UserRole).toInt();
    const auto& data = m_helpData[idx];

    // [用户修改要求] 详情页同样移除多余的分类显示，仅展示热键及用途描述
    QString html = QString(R"(
        <h1 style='color: #F1C40F; font-size: 24px; margin-bottom: 20px; font-family: Consolas, monospace;'>%1</h1>
        <div style='color: #DDD; font-size: 14px; line-height: 1.8; border-top: 1px solid #333; padding-top: 20px;'>
            %2
        </div>
    )").arg(data.key).arg(data.description);

    m_detailView->setHtml(html);
}
