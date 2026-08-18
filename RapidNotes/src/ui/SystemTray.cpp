#include "SystemTray.h"
#include "StringUtils.h"
#include "../core/DatabaseManager.h"

#include "IconHelper.h"
#include <QApplication>
#include <QIcon>
#include <QStyle>

SystemTray::SystemTray(QObject* parent) : QObject(parent) {
    m_trayIcon = new QSystemTrayIcon(this);
    
    // 复刻 Python 版：使用渲染的悬浮球作为托盘图标
    // 2026-03-20 恢复原版托盘图标
    m_trayIcon->setIcon(QIcon(":/app_icon.png"));
    // 2026-03-xx 按照项目宪法，严禁使用原生 ToolTip。
    // 托盘图标即便不支持 Overlay 渲染，也必须置空，保持全局纯净。
    m_trayIcon->setToolTip("");

    m_menu = new QMenu();
    IconHelper::setupMenu(m_menu);
    m_menu->setStyleSheet(
        "QMenu { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; padding: 4px; } "
        /* 10px 间距规范：padding-left 10px + icon margin-left 6px */
        "QMenu::item { padding: 6px 10px 6px 10px; border-radius: 3px; } "
        "QMenu::icon { margin-left: 6px; } "
        "QMenu::item:selected { background-color: #3e3e42; color: white; }" // 2026-03-xx 统一菜单悬停色为 #3e3e42
    );
    

    m_menu->addAction(IconHelper::getIcon("zap", "#aaaaaa", 18), "显示懒人笔记", this, &SystemTray::showQuickWindow);
    
    // 2026-04-xx 按照用户要求：修复显示悬浮球功能。补全 eye 图标，并支持显隐翻转逻辑
    auto* showBallAction = m_menu->addAction(IconHelper::getIcon("eye", "#aaaaaa", 18), "显示悬浮球", this, &SystemTray::showFloatingBallRequested);
    showBallAction->setCheckable(true);
    showBallAction->setChecked(true); // 初始悬浮球默认是开启的
    showBallAction->setObjectName("showBallAction");

    m_menu->addAction(IconHelper::getIcon("tag", "#aaaaaa", 18), "标签管理", this, &SystemTray::showTagManagerRequested);
    
    m_menu->addSeparator();
    
    // [REMOVED] 2026-03-20 移除显示/隐藏悬浮球菜单项

    m_menu->addAction(IconHelper::getIcon("help", "#aaaaaa", 18), "使用说明", this, &SystemTray::showHelpRequested);
    m_menu->addAction(IconHelper::getIcon("settings", "#aaaaaa", 18), "设置", this, &SystemTray::showSettings);
    m_menu->addSeparator();
    m_menu->addAction(IconHelper::getIcon("power", "#aaaaaa", 18), "退出 RapidNotes", this, &SystemTray::quitApp);

    m_trayIcon->setContextMenu(m_menu);

    connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason){
        if (reason == QSystemTrayIcon::Trigger) {
            emit showQuickWindow();
        }
    });
}

void SystemTray::show() {
    m_trayIcon->show();
}

void SystemTray::setFloatingBallVisible(bool visible) {
    QAction* act = m_menu->findChild<QAction*>("showBallAction");
    if (act) {
        act->setChecked(visible);
        act->setText(visible ? "隐藏悬浮球" : "显示悬浮球");
    }
}
