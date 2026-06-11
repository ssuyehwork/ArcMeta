#include "TrayController.h"
#include <QApplication>
#include <QIcon>
#include <QDebug>
#include "../mft/MftReader.h"

namespace ArcMeta {

TrayController::TrayController(QMainWindow* mainWindow)
    : QObject(mainWindow), m_mainWindow(mainWindow) {
    m_trayIcon = new QSystemTrayIcon(this);
    
    // 2026-04-14 物理加固：锁定图标来源为 Qt 资源系统中的标准 ico
    m_trayIcon->setIcon(QIcon(":/app_icon.ico"));
    m_trayIcon->setToolTip("ArcMeta");

    m_trayMenu = new QMenu(mainWindow);
    m_trayMenu->setStyleSheet(
        "QMenu { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; padding: 4px; border-radius: 8px; }"
        "QMenu::item { padding: 6px 25px 6px 10px; border-radius: 4px; font-size: 12px; }"
        "QMenu::item:selected { background-color: #3E3E42; color: white; }"
    );

    QAction* showAction = m_trayMenu->addAction("显示主界面");
    m_trayMenu->addSeparator();
    QAction* quitAction = m_trayMenu->addAction("退出 ArcMeta");

    connect(showAction, &QAction::triggered, this, &TrayController::onShowMainWindow);
    connect(quitAction, &QAction::triggered, this, &TrayController::onQuitApp);

    m_trayIcon->setContextMenu(m_trayMenu);

    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &TrayController::onTrayActivated);
}

TrayController::~TrayController() {
    if (m_trayIcon) {
        m_trayIcon->hide();
    }
}

void TrayController::show() {
    m_trayIcon->show();
}

void TrayController::hide() {
    m_trayIcon->hide();
}

void TrayController::onTrayActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
        if (m_mainWindow->isVisible()) {
            m_mainWindow->hide();
        } else {
            onShowMainWindow();
        }
    }
}

void TrayController::onShowMainWindow() {
    m_mainWindow->showNormal();
    m_mainWindow->activateWindow();
}

void TrayController::onQuitApp() {
    // 2026-06-xx 按照用户要求：重构退出逻辑，增加退出反馈并防止后台残留
    if (m_trayIcon) m_trayIcon->hide();

    // 1. 创建临时的模态退出提示框（置顶）
    QDialog quitDlg(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    quitDlg.setFixedSize(320, 100);
    quitDlg.setStyleSheet("background-color: #1E1E1E; border: 1px solid #333; border-radius: 6px;");

    auto* layout = new QVBoxLayout(&quitDlg);
    auto* msgLbl = new QLabel("正在安全保存元数据并退出 ArcMeta...", &quitDlg);
    msgLbl->setStyleSheet("color: #EEE; font-size: 13px;");
    msgLbl->setAlignment(Qt::AlignCenter);
    layout->addWidget(msgLbl);

    quitDlg.show();
    QApplication::processEvents();

    // 2. 停止 MFT 扫描与 USN 监控 (异步转同步，确保资源释放)
    MftReader::instance().clear(); 

    // 3. 执行分步式数据库存盘
    DatabaseManager::instance().flushAll();

    quitDlg.accept();
    QApplication::quit();
}

} // namespace ArcMeta
