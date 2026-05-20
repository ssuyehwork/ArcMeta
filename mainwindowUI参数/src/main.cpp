#include <QSettings>
#include <QApplication>
#include <QFile>
#include <QMessageBox>
#include <QCoreApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTimer>
#include <QThreadPool>
#include <QPointer>
#include <functional>

#include "core/DatabaseManager.h"
#include "ui/ToolTipOverlay.h"
#include "ui/StringUtils.h"

#ifdef RAPID_NOTES_TARGET
#include "core/HotkeyManager.h"
#include "core/ClipboardMonitor.h"
#include "core/KeyboardHook.h"
#include "core/ReminderService.h"
#include "core/HttpServer.h"
#include "ui/FireworksOverlay.h"
#endif

#ifdef RAPID_MANAGER_TARGET
#include "ui/MainWindow.h"
#include "ui/FireworksOverlay.h"
#endif

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);
    a.setOrganizationName("RapidDev");
    a.setWindowIcon(QIcon(":/app_icon.png"));

#ifdef RAPID_MANAGER_TARGET
    a.setApplicationName("RapidManager");
    QString serverName = "RapidManager_SingleInstance_Server";
    QString dbFileName = "manager_kernel.db";
    a.setQuitOnLastWindowClosed(true);
#else
    a.setApplicationName("RapidNotes");
    QString serverName = "RapidNotes_SingleInstance_Server";
    QString dbFileName = "inspiration.db";
    a.setQuitOnLastWindowClosed(false);
#endif

    QLocalSocket socket;
    socket.connectToServer(serverName);
    if (socket.waitForConnected(500)) return 0;

    QLocalServer::removeServer(serverName);
    QLocalServer server;
    server.listen(serverName);

    QFile styleFile(":/qss/dark_style.qss");
    if (styleFile.open(QFile::ReadOnly)) a.setStyleSheet(styleFile.readAll());

    QString dbPath = QCoreApplication::applicationDirPath() + "/" + dbFileName;
    if (!DatabaseManager::instance().init(dbPath)) {
        QMessageBox::critical(nullptr, "启动失败", "无法加载加密数据库外壳。");
        return -1;
    }

#ifdef RAPID_MANAGER_TARGET
    FireworksOverlay::instance();
    MainWindow mainWin;
    mainWin.setObjectName("RapidMainWindow");
    mainWin.setWindowTitle("RapidManager - KingPenguin");
    mainWin.show();
#else
    HttpServer::instance().start(23333);
    FireworksOverlay::instance(); 
    KeyboardHook::instance().start();
    HotkeyManager::instance().reapplyHotkeys();
    ReminderService::instance().start();

    QObject::connect(&ClipboardMonitor::instance(), &ClipboardMonitor::newContentDetected, 
        [](const QString& content, const QString& type, const QByteArray& data, const QString& sourceApp, const QString& sourceTitle){
        DatabaseManager::instance().addNoteAsync("New Note", content, {}, "", -1, type, data, sourceApp, sourceTitle);
    });
#endif

    int result = a.exec();
    DatabaseManager::instance().closeAndPack();
    return result;
}
