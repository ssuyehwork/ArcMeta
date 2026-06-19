#include "TrayController.h"
#include <QApplication>
#include <QIcon>
#include <QDebug>
#include <QProgressDialog>
#include "../mft/MftReader.h"
#include "../meta/DatabaseManager.h"
#include "BatchProgressDialog.h"

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
    // 2026-07-xx 按照用户要求 (1.21)：退出流程优化与文件占用解决
    if (m_trayIcon) m_trayIcon->hide();

    // 1. 弹出模态进度提示 (退出反馈)
    BatchProgressDialog* progress = new BatchProgressDialog("正在安全保存数据并退出...", nullptr);
    progress->setWindowModality(Qt::ApplicationModal);
    progress->show();
    progress->setStatus("正在停止后台扫描线程...");

    // 2. 强制中断所有后台任务
    MftReader::instance().clear(); 

    // 3. 步进式备份持久化
    progress->setStatus("正在将内存数据持久化至磁盘...");
    int totalSteps = 0;
    while (!DatabaseManager::instance().flushStep()) {
        totalSteps++;
        QApplication::processEvents(); // 维持界面响应
        if (totalSteps % 10 == 0) {
            progress->setStatus(QString("正在保存数据 (已执行 %1 步拷贝)...").arg(totalSteps));
        }
    }

    // 4. 显式释放所有句柄 (解除占用)
    DatabaseManager::instance().shutdown();

    progress->accept();
    delete progress;

    qDebug() << "[Exit] 数据保存完成，物理占用已释放。程序正式退出。";
    QApplication::quit();
}

} // namespace ArcMeta
