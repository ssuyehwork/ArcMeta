#include <QSettings>
#include <QApplication>
#include <QColor>
#include <QThread>
#include <QFile>
#include <QCursor>
#include "ui/FramelessDialog.h" // 2026-04-08 引入无边框对话框支持
#include <QMessageBox>
#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <QDateTime>
#include <QFileInfo>
#include <QBuffer>
#include <QUrl>
#include <QTimer>
#include <QThreadPool>
#include <QElapsedTimer>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QRegularExpression>
#include <QKeyEvent>
#include <QLineEdit>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QTextCursor>
#include <QTextDocument>
#include <functional>
#include <utility>
#include "core/DatabaseManager.h"
#include "core/HotkeyManager.h"
#include "core/ClipboardMonitor.h"
#include "ui/IconHelper.h"
#include "ui/QuickWindow.h"
#include "ui/FloatingBall.h"
#include "ui/SystemTray.h"

#include <QAbstractItemView>
#include <QHelpEvent>
#include <QModelIndex>

#include "ui/TagManagerWindow.h"
#include "ui/HelpWindow.h"
#include "ui/FireworksOverlay.h"
#include "ui/SettingsWindow.h"
#include "ui/ActivationDialog.h"
#include "ui/NoteEditWindow.h"
#include "ui/ToolTipOverlay.h"
#include "ui/StringUtils.h"
#include "core/KeyboardHook.h"
#include "core/FileCryptoHelper.h"
#include "core/FileStorageHelper.h"
#include "core/Logger.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#include <dbghelp.h>
#endif

#ifdef Q_OS_WIN
// 2026-03-xx 按照用户要求：增加 DUMP 收集机制，确保应用崩溃时能捕获堆栈以便排查 Bug
LONG WINAPI ApplicationCrashHandler(EXCEPTION_POINTERS* pException) {
    // [FIX] 2026-04-18 健壮性加固：防止在极早期崩溃（如 QApplication 尚未生成）时调用静态函数导致二次崩溃
    QString appDir = QCoreApplication::instance() ? QCoreApplication::applicationDirPath() : ".";
    QString dumpPath = appDir + "/crash_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".dmp";
    HANDLE hFile = CreateFileW((LPCWSTR)dumpPath.utf16(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    
    if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION dumpInfo;
        dumpInfo.ThreadId = GetCurrentThreadId();
        dumpInfo.ExceptionPointers = pException;
        dumpInfo.ClientPointers = TRUE;

        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, MiniDumpNormal, &dumpInfo, NULL, NULL);
        CloseHandle(hFile);

        // 2026-04-18 按照用户要求：在文本日志中同步锁定崩溃现场，解决“闪退无日志”问题
        qCritical() << "****************************************************";
        qCritical() << "[CRASH DETECTED] 程序发生致命错误！";
        qCritical() << "[CRASH DETECTED] DUMP 文件已生成:" << dumpPath;
        qCritical() << "****************************************************";

        // 2026-04-08 按照用户要求：崩溃提示也切换为无边框样式
        FramelessMessageBox dlg("程序异常终止", 
            QString("<b>抱歉，RapidNotes 遭遇了无法恢复的内部错误。</b><br><br>崩溃日志已保存至：<br>%1<br><br>请将此文件发送给开发人员以协助修复。").arg(dumpPath));
        dlg.exec();
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

int main(int argc, char *argv[]) {
    // 2026-03-xx 按照用户要求：初始化 DUMP 收集机制
#ifdef Q_OS_WIN
    SetUnhandledExceptionFilter(ApplicationCrashHandler);
#endif

    QApplication a(argc, argv);

    // 2026-03-xx 按照用户要求：初始化本地日志系统（必须在 QApplication 实例化后调用 applicationDirPath 才合法）
    Logger::init();
    
    // [PERF] 2026-04-05 清理并重新创建性能报告文件
    QFile::remove(QCoreApplication::applicationDirPath() + "/startup_perf.txt");

    QElapsedTimer bootTimer;
    bootTimer.start();
    DatabaseManager::instance().logStartup(">>> 程序入口启动...");
    a.setQuitOnLastWindowClosed(false);

    // 单实例运行保护
    QString serverName = "RapidNotes_SingleInstance_Server";
    QLocalSocket socket;
    socket.connectToServer(serverName);
    if (socket.waitForConnected(500)) {
        // 如果已经运行，发送 SHOW 信号并退出当前进程
        socket.write("SHOW");
        socket.waitForBytesWritten(1000);
        return 0;
    }
    QLocalServer::removeServer(serverName);
    QLocalServer server;
    if (!server.listen(serverName)) {
        qWarning() << "无法启动单实例服务器";
    }

    // 加载全局样式表
    QFile styleFile(":/qss/dark_style.qss");
    if (styleFile.open(QFile::ReadOnly)) {
        a.setStyleSheet(styleFile.readAll());
    }

    // 1. 初始化数据库 (外壳文件名改为 inspiration.db)
    QString dbPath = QCoreApplication::applicationDirPath() + "/inspiration.db";
    // qDebug() << "[Main] 数据库外壳路径:" << dbPath;

    if (!DatabaseManager::instance().init(dbPath)) {
        // 2026-03-15 [UI-FIX] 启动失败时显示更具体的原因。
        QString reason = DatabaseManager::instance().getLastError();
        if (reason.isEmpty()) reason = "无法加载加密外壳、解密失败或数据库损坏。";
        
        // 2026-04-08 按照用户要求：启动错误提示切换为无边框样式
        FramelessMessageBox dlg("启动失败 (RapidNotes)", 
            QString("<b>程序初始化遭遇异常，无法继续：</b><br><br>%1<br><br>建议尝试删除 data 目录下的 kernel 文件后重试。").arg(reason));
        dlg.exec();
            
        return -1;
    }
    DatabaseManager::instance().logStartup(QString("DatabaseManager::init 完成，耗时: %1ms").arg(bootTimer.elapsed()));

    // [ARCH-CLEANUP] 定义统一的程序退出流
    auto doSafeExit = [&]() {
        static bool isExiting = false;
        if (isExiting) return;
        isExiting = true;

        // 2026-04-18 [WATCHDOG] 向看门狗发送“合法退出”信号：创建标识文件
        QFile lockFile(QCoreApplication::applicationDirPath() + "/exit_gracefully.lock");
        if (lockFile.open(QIODevice::WriteOnly)) {
            lockFile.write("GRACEFUL_EXIT");
            lockFile.close();
        }

        // 2026-03-15 核心优化：视觉先行。立即隐藏窗口。
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (widget && widget->objectName() != "ToolTipOverlay") {
                widget->hide();
            }
        }

        // [DE-SHELL] 数据库已改为明文直连，退出不再有耗时的加密操作，实现秒级退出
        DatabaseManager::instance().closeAndPack();
        QApplication::quit();
    };

    // 1.1 2026-03-xx 按照用户要求：正版授权强制校验逻辑
    QVariantMap trialStatus = DatabaseManager::instance().getTrialStatus();

    // [CRITICAL] 跨设备一致性检查：如果指纹不匹配（解密失败），视为非法拷贝运行，直接拦截退出
    if (trialStatus["fingerprint_mismatch"].toBool()) {
        // 2026-03-xx 按照用户要求：检测到硬件指纹不匹配时，弹出告知并强制重置激活状态后退出。
        // 2026-04-08 按照用户要求：安全拦截提示切换为无边框样式
        FramelessMessageBox dlg("系统提示", "<b>[安全拦截] 检测到硬件指纹不匹配。</b><br><br>由于当前设备的硬件指纹与授权记录不符，系统已自动重置本地激活状态以确保证版授权安全。<br><br>请联系管理员获取适用于当前新设备的专属授权码，并重新进行激活。程序将立即退出。");
        dlg.exec();
        return 0;
    }

    // 强制激活流：未激活状态下必须通过 ActivationDialog 验证，否则不允许进入主程序
    if (!trialStatus["is_activated"].toBool()) {
        QString reason = "<b>欢迎使用 RapidNotes 正版软件</b><br><br>检测到当前设备尚未激活，请输入您的专属授权密钥以继续：";
            
        ActivationDialog dlg(reason);
        if (dlg.exec() != QDialog::Accepted) {
            doSafeExit();
            return 0; 
        }
        // 验证成功后，重新同步最新的授权状态
        trialStatus = DatabaseManager::instance().getTrialStatus();
    }

    // 2. 初始化核心 UI 组件 (懒人笔记窗口与悬浮球)
    QElapsedTimer uiTimer;
    uiTimer.start();
    QuickWindow* quickWin = new QuickWindow();
    quickWin->setObjectName("QuickWindow");
    quickWin->showAuto();
    
    // 2026-04-05 [RESTORE] 按照用户要求，1:1 复刻旧版悬浮球
    FloatingBall* ball = new FloatingBall();
    ball->setObjectName("FloatingBall");
    {
        QSettings gs("RapidNotes", "General");
        bool ballVisible = gs.value("floatingBallVisible", true).toBool();
        ball->setVisible(ballVisible);
    }

    // 按照旧版集成逻辑，设置全局应用图标
    a.setWindowIcon(FloatingBall::generateBallIcon());

    DatabaseManager::instance().logStartup(QString("QuickWindow 构建与显示触发完成，耗时: %1ms").arg(uiTimer.elapsed()));
    DatabaseManager::instance().logStartup(QString("--- 总启动流程完成 (a.exec() 就绪) | 总耗时: %1ms ---").arg(bootTimer.elapsed()));

    // 3. 初始化特效层
    FireworksOverlay::instance(); 

    // 2026-03-20 按照用户要求，恢复原版笔记本图标
    a.setWindowIcon(QIcon(":/app_icon.png"));

    // 4. 子窗口延迟加载策略
    TagManagerWindow* tagMgrWin = nullptr;
    HelpWindow* helpWin = nullptr;

    // [WINDOW_MANAGER_PRE] 临时内部类，未来可迁移至独立文件
    struct WindowManager {
        static void toggle(QWidget* win, QWidget* parentWin = nullptr) {
            if (!win) return;
            if (win->isVisible()) {
                win->hide();
            } else {
                if (parentWin) {
                    if (parentWin->objectName() == "QuickWindow") {
                        win->move(parentWin->x() - win->width() - 10, parentWin->y());
                    } else {
                        win->move(parentWin->geometry().center() - win->rect().center());
                    }
                }
                win->show();
                win->raise();
                win->activateWindow();
            }
        }
    };

    auto checkLockAndExecute = [&](std::function<void()> func) {
        if (quickWin->isLocked()) {
            quickWin->showAuto();
            return;
        }
        func();
    };

    // [RESTORE] 悬浮球交互逻辑复刻
    // [FIX] 2026-04-xx 按照用户指示：物理移除失效的 isActiveWindow() 判定，修复双击最小化功能
    QObject::connect(ball, &FloatingBall::doubleClicked, [=](){
        if (quickWin->isVisible() && !quickWin->isMinimized()) {
            quickWin->showMinimized();
        } else {
            quickWin->showAuto();
        }
    });
    QObject::connect(ball, &FloatingBall::requestQuickWindow, [=](){
        quickWin->showAuto();
    });
    QObject::connect(ball, &FloatingBall::requestNewIdea, [=, &checkLockAndExecute](){
        checkLockAndExecute([=](){
            NoteEditWindow* win = new NoteEditWindow();
            QObject::connect(win, &NoteEditWindow::noteSaved, quickWin, &QuickWindow::refreshData);
            win->show();
        });
    });

    // [USER_REQUEST] 定义可复用的采集逻辑
    auto doAcquire = [=, &checkLockAndExecute, &quickWin]() {
        checkLockAndExecute([&](){
            qDebug() << "[Acquire] 触发采集流程，开始环境检测...";
#ifdef Q_OS_WIN
            // [USER_REQUEST] 核心修复：支持从 UI 按钮点击触发的采集。
            // 如果是通过点击 UI 按钮触发，当前活跃窗口是 RapidNotes。
            // 我们必须检测记录的 m_lastActiveHwnd 是否为浏览器，并先切回该窗口。
            HWND target = GetForegroundWindow();
            bool isFromUI = (target == (HWND)quickWin->winId());
            
            if (isFromUI) {
                target = quickWin->m_lastActiveHwnd;
                if (target && IsWindow(target)) {
                    if (IsIconic(target)) ShowWindow(target, SW_RESTORE);
                    SetForegroundWindow(target);
                    // 留出时间让窗口激活
                    QThread::msleep(100); 
                }
            }

            if (!StringUtils::isBrowserWindow(target)) {
                qDebug() << "[Acquire] 拒绝执行：目标窗口非浏览器环境。";
                ToolTipOverlay::instance()->showText(QCursor::pos(), "✖ 智能采集仅支持浏览器环境");
                return;
            }

            ClipboardMonitor::instance().setIgnore(true);
            QApplication::clipboard()->clear();

            // 如果是通过热键触发，需要释放可能按下的 S 键以防干扰
            keybd_event('S', 0, KEYEVENTF_KEYUP, 0); 

            keybd_event(VK_CONTROL, 0, 0, 0);
            keybd_event('C', 0, 0, 0);
            keybd_event('C', 0, KEYEVENTF_KEYUP, 0);
            // 这里不立即抬起 Ctrl，因为某些应用接收消息较慢
#endif
            QTimer::singleShot(500, [=](){
#ifdef Q_OS_WIN
                keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
#endif
                QString text = QApplication::clipboard()->text();
                ClipboardMonitor::instance().setIgnore(false);
                if (text.trimmed().isEmpty()) {
                    ToolTipOverlay::instance()->showText(QCursor::pos(), "✖ 未能采集到内容，请确保已选中浏览器中的文本");
                    return;
                }
                auto pairs = StringUtils::smartSplitPairs(text);
                if (pairs.isEmpty()) return;
                int catId = quickWin->getCurrentCategoryId();
                for (const auto& pair : std::as_const(pairs)) {
                    QStringList tags = {"采集"};
                    if (StringUtils::containsThai(pair.first) || StringUtils::containsThai(pair.second)) tags << "泰文";
                    DatabaseManager::instance().addNoteAsync(pair.first, pair.second, tags, "", catId, "text");
                }
                ToolTipOverlay::instance()->showText(QCursor::pos(), QString("[OK] 已智能采集 %1 条灵感").arg(pairs.size()));
            });
        });
    };

    // [USER_REQUEST] 定义可复用的纯净粘贴逻辑
    auto doPurePaste = [=, &quickWin]() {
        QString text = QApplication::clipboard()->text();
        if (!text.isEmpty()) {
            ClipboardMonitor::instance().skipNext();
            QApplication::clipboard()->setText(text);
#ifdef Q_OS_WIN
            // [USER_REQUEST] 核心修复：点击 UI 按钮粘贴时，必须切回先前活跃的目标窗口
            HWND target = GetForegroundWindow();
            if (target == (HWND)quickWin->winId()) {
                target = quickWin->m_lastActiveHwnd;
                if (target && IsWindow(target)) {
                    if (IsIconic(target)) ShowWindow(target, SW_RESTORE);
                    SetForegroundWindow(target);
                    QThread::msleep(200); // 粘贴需要更稳定的激活状态
                }
            }

            INPUT inputs[6];
            memset(inputs, 0, sizeof(inputs));
            // 确保用户的 Shift 已抬起 (针对 Ctrl+Shift+V 热键)
            inputs[0].type = INPUT_KEYBOARD; inputs[0].ki.wVk = VK_SHIFT; inputs[0].ki.dwFlags = KEYEVENTF_KEYUP;
            // 模拟 Ctrl+V
            inputs[1].type = INPUT_KEYBOARD; inputs[1].ki.wVk = VK_CONTROL;
            inputs[2].type = INPUT_KEYBOARD; inputs[2].ki.wVk = 'V';
            inputs[3].type = INPUT_KEYBOARD; inputs[3].ki.wVk = 'V'; inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
            inputs[4].type = INPUT_KEYBOARD; inputs[4].ki.wVk = VK_CONTROL; inputs[4].ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(5, inputs, sizeof(INPUT));
#endif
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>[OK] 已纯净粘贴文本</b>");
        }
    };


    // 5. 开启全局键盘钩子 (支持快捷键重映射)
    // 2026-04-09 按照用户要求：启动时强制同步 CapsLock 重映射配置，确保功能即刻生效
    {
        QSettings gs("RapidNotes", "General");
        KeyboardHook::instance().setCapsLockToEnterEnabled(gs.value("capsLockToEnter", false).toBool());
    }
    KeyboardHook::instance().start();

    // 6. 注册全局热键 (从配置加载)
    HotkeyManager::instance().reapplyHotkeys();

    // [USER_REQUEST] 响应来自底层钩子的强制锁定请求，解决热键冲突问题
    QObject::connect(&KeyboardHook::instance(), &KeyboardHook::globalLockRequested, [&](){
        quickWin->doGlobalLock();
    });
    
    QObject::connect(&HotkeyManager::instance(), &HotkeyManager::hotkeyPressed, [&](int id){
        // 2026-05-04 按照用户要求：添加热键事件调试信息
        qDebug() << "[Main] 接收到热键事件 ID:" << id;
        
        if (id == 1) {
            qDebug() << "[Main] 处理空格键热键 (Alt+Space)";
            if (quickWin->isVisible() && quickWin->isActiveWindow()) {
                qDebug() << "[Main] QuickWindow 可见且激活，执行隐藏";
                quickWin->hide();
            } else {
                qDebug() << "[Main] QuickWindow 不可见或未激活，执行显示";
                // [USER_REQUEST] 热键唤起前，立即捕获当前活动窗口。
                // 这在窗口已显示但未激活，且用户通过热键再次触发时尤为关键，能确保 m_lastActiveHwnd 始终指向"真正的"外部目标。
                quickWin->recordLastActiveWindow(nullptr);
                quickWin->showAuto();
            }
        } else if (id == 2) {
            checkLockAndExecute([&](){
                // [USER_REQUEST] 核心修复：改用物理 ID 绝对定位，收藏“绝对最后创建”的那条数据
                int lastId = DatabaseManager::instance().getLastCreatedNoteId();
                if (lastId > 0) {
                    DatabaseManager::instance().updateNoteState(lastId, "is_favorite", 1);
                    qDebug() << "[Main] 已成功执行 Ctrl+Shift+E 一键收藏 -> ID:" << lastId;
                    
                    // 2026-03-xx 按照项目规范，提供视觉反馈
                    ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ECC71;'>★ 已收藏最后一条灵感</b>");
                } else {
                    ToolTipOverlay::instance()->showText(QCursor::pos(), "✖ 未发现可收藏的灵感");
                }
            });
        } else if (id == 4) {
            doAcquire();
        } else if (id == 5) {
            // 全局锁定
            quickWin->doGlobalLock();
        } else if (id == 7) {
            doPurePaste();
        } else if (id == 8) {

        } else if (id == 9) {
            // 2026-03-20 [NEW] 全局 Alt+A 连击菜单
            quickWin->showContextNotesMenu();
        }
    });

    // 7. 系统托盘
    QObject::connect(&server, &QLocalServer::newConnection, [&](){
        QLocalSocket* conn = server.nextPendingConnection();
        if (conn->waitForReadyRead(500)) {
            QByteArray data = conn->readAll();
            if (data == "SHOW") {
                quickWin->showAuto();
            }
            conn->disconnectFromServer();
        }
    });

    SystemTray* tray = new SystemTray(&a);
    QObject::connect(tray, &SystemTray::showQuickWindow, [=](){
        quickWin->recordLastActiveWindow(nullptr);
        quickWin->showAuto();
    });
    
    // 2026-04-xx 按照用户要求：重构悬浮球显示信号连接，实现逻辑闭环与状态双向同步
    QObject::connect(tray, &SystemTray::showFloatingBallRequested, [=](){
        bool nextVisible = !ball->isVisible();
        ball->setVisible(nextVisible);
        tray->setFloatingBallVisible(nextVisible);
        
        QSettings gs("RapidNotes", "General");
        gs.setValue("floatingBallVisible", nextVisible);
    });

    // [NEW] 2026-04-xx 处理悬浮球自身的可见性变化信号，同步托盘勾选状态
    QObject::connect(ball, &FloatingBall::visibilityChanged, [=](bool visible){
        tray->setFloatingBallVisible(visible);
        QSettings gs("RapidNotes", "General");
        gs.setValue("floatingBallVisible", visible);
    });

    QObject::connect(tray, &SystemTray::showTagManagerRequested, [=, &tagMgrWin](){
        checkLockAndExecute([=, &tagMgrWin](){
            if (!tagMgrWin) {
                tagMgrWin = new TagManagerWindow();
                tagMgrWin->setObjectName("TagManagerWindow");
            }
            WindowManager::toggle(tagMgrWin);
        });
    });

    QObject::connect(tray, &SystemTray::showHelpRequested, [=, &helpWin](){
        checkLockAndExecute([=, &helpWin](){
            if (!helpWin) {
                helpWin = new HelpWindow();
                helpWin->setObjectName("HelpWindow");
            }
            WindowManager::toggle(helpWin);
        });
    });
    QObject::connect(tray, &SystemTray::showSettings, [=](){
        checkLockAndExecute([=](){
            static QPointer<SettingsWindow> settingsWin;
            if (settingsWin) {
                settingsWin->showNormal();
                settingsWin->raise();
                settingsWin->activateWindow();
                return;
            }

            settingsWin = new SettingsWindow();
            settingsWin->setObjectName("SettingsWindow");
            settingsWin->setAttribute(Qt::WA_DeleteOnClose);
            
            // 核心修复：先计算位置并移动，确保窗口 show() 的那一刻就在正确的位置，杜绝闪烁
            QScreen *screen = QGuiApplication::primaryScreen();
            if (screen) {
                QRect screenGeom = screen->geometry();
                settingsWin->move(screenGeom.center() - settingsWin->rect().center());
            }
            
            settingsWin->show();
            settingsWin->raise();
            settingsWin->activateWindow();
        });
    });
    QObject::connect(tray, &SystemTray::quitApp, doSafeExit);
    tray->setFloatingBallVisible(ball->isVisible());
    tray->show();

    // 8. 监听剪贴板 (智能标题与自动分类)
    QObject::connect(&ClipboardMonitor::instance(), &ClipboardMonitor::clipboardChanged, [=](){
        // [DIAG] 追踪烟花特效耗时
        QElapsedTimer fw;
        fw.start();
        // 触发烟花爆炸特效
        FireworksOverlay::instance()->explode(QCursor::pos());
        qDebug() << "[Clipboard->ToolTip DIAG] FireworksOverlay::explode 耗时 =" << fw.elapsed() << "ms";
    });

    // [REPAIR] 2026-03-xx 核心修复：主线程解放方案
    // ToolTip 显示后紧随的大量同步逻辑（字符串处理、异步 DB 排队等）会阻塞事件循环，
    // 导致计时器信号无法准时派发。我们将重处理逻辑推入 singleShot(0)，确保事件循环立刻回转。
    // ✅ [ARCH-FIX] 2026-05-xx 信号解耦重构
    // 第一步：连接到瞬时反馈信号，实现零延时 UI 弹窗
    QObject::connect(&ClipboardMonitor::instance(), &ClipboardMonitor::requestFeedback,
        [](const QString& content, const QString& type){
        
        static QElapsedTimer s_lastTipTimer;
        if (!s_lastTipTimer.isValid()) {
            s_lastTipTimer.start();
        } else if (s_lastTipTimer.elapsed() < 700) {
            return;
        }
        s_lastTipTimer.restart();

        QSettings gs("RapidNotes", "General");
        if (!gs.value("showCopyToolTip", false).toBool()) return;
        
        QString displayContent;
        if (!type.isEmpty() && type != "image" && type != "file" && type != "folder" && type != "files" && type != "folders") {
            displayContent = content.trimmed().left(20);
            if (content.trimmed().length() > 20) displayContent += "...";
        } else {
            if (type == "image") displayContent = "图片";
            else if (type == "file" || type == "folder" || type == "files" || type == "folders") {
                QStringList paths = content.split(";", Qt::SkipEmptyParts);
                if (!paths.isEmpty()) {
                    QString firstPath = paths.first();
                    QString firstName = QFileInfo(firstPath).fileName();
                    if (firstName.isEmpty()) firstName = firstPath;
                    if (paths.size() > 1) displayContent = QString("%1 等 %2 个项目").arg(firstName).arg((int)paths.size());
                    else displayContent = firstName;
                } else displayContent = "文件";
            } else displayContent = type;
        }

        ToolTipOverlay::instance()->showText(QCursor::pos(), 
            QString("<html>"
                    "<div style='white-space:nowrap; color:#2ecc71;'><b>已复制：</b></div>"
                    "<div style='white-space:nowrap; color:#EEEEEE;'>%1</div>"
                    "</html>").arg(displayContent.toHtmlEscaped()), 1000);
    });

    // 第二步：连接到业务逻辑信号，此时数据已完整并已去重，执行入库逻辑
    QObject::connect(&ClipboardMonitor::instance(), &ClipboardMonitor::newContentDetected, 
        [quickWin](const QString& content, const QString& type, const QByteArray& data,
            const QString& sourceApp, const QString& sourceTitle){
        // 旧方案 singleShot(0) 仍在主线程执行，文件检测/正则/DB写入等耗时操作会阻塞事件循环，
        // 导致 m_hideTimer 的 timeout 信号无法准时派发，表现为 ToolTip 显示 2-3 秒。
        // 新方案使用 QThreadPool 后台线程，主线程仅负责 ToolTip 显示/隐藏。
        (void)QThreadPool::globalInstance()->start([content, type, data, sourceApp, sourceTitle]() {
            QElapsedTimer heavyClock;
            heavyClock.start();
            qDebug() << "[Clipboard->ToolTip DIAG] 后台线程开始执行";
            int catId = -1;
            if (DatabaseManager::instance().isAutoCategorizeEnabled()) {
                catId = DatabaseManager::instance().activeCategoryId();
            }
            
            QString title;
            QString finalContent = content;
            QString finalType = type;

            if (type == "image") {
                title = "[截图] " + QDateTime::currentDateTime().toString("MMdd_HHmm");
            } else if (type == "file" || type == "text") {
                QStringList files;
                if (type == "file") {
                    files = content.split(";", Qt::SkipEmptyParts);
                } else {
                    QString trimmed = content.trimmed();
                    if ((trimmed.startsWith("\"") && trimmed.endsWith("\"")) || (trimmed.startsWith("'") && trimmed.endsWith("'"))) {
                        trimmed = trimmed.mid(1, trimmed.length() - 2);
                    }
                    QFileInfo info(trimmed);
                    if (info.exists() && info.isAbsolute()) {
                        files << trimmed;
                        finalType = info.isDir() ? "folder" : "file";
                    }
                }

                if (!files.isEmpty()) {
                    QString firstPath = files.first();
                    if (firstPath.endsWith("/") || firstPath.endsWith("\\")) firstPath.chop(1);
                    QFileInfo info(firstPath);
                    QString name = info.fileName();
                    if (name.isEmpty()) name = firstPath;

                    if (files.size() > 1) {
                        int dirCount = 0;
                        for (const QString& path : files) {
                            if (QFileInfo(path).isDir()) dirCount++;
                        }
                        if (dirCount == files.size()) {
                            title = QString("Copied Folders - %1 等 %2 个文件夹").arg(name).arg((int)files.size());
                            finalType = "folders";
                        } else if (dirCount == 0) {
                            title = QString("Copied Files - %1 等 %2 个文件").arg(name).arg((int)files.size());
                            finalType = "files";
                        } else {
                            title = QString("Copied Items - %1 等 %2 个项目").arg(name).arg((int)files.size());
                            finalType = "files";
                        }
                    } else {
                        if (info.isDir()) {
                            title = "Copied Folder - " + name;
                            finalType = "folder"; 
                        } else {
                            title = "Copied File - " + name;
                            finalType = "file";
                        }
                    }
                } else if (type == "file") {
                    title = "[未知文件]";
                } else {
                    QString firstLine = content.section('\n', 0, 0).trimmed();
                    if (firstLine.isEmpty()) title = "无标题灵感";
                    else {
                        title = firstLine.left(40);
                        if (firstLine.length() > 40) title += "...";
                    }
                }
            }

            QStringList tags;
            if (type == "text") {
                QString trimmed = content.trimmed();
                static QRegularExpression hexRegex("^#([A-Fa-f0-9]{6}|[A-Fa-f0-9]{3})$");
                static QRegularExpression rgbRegex(R"(^(\d{1,3}),\s*(\d{1,3}),\s*(\d{1,3})$)");

                QRegularExpressionMatch hexMatch = hexRegex.match(trimmed);
                bool isColor = false;
                if (hexMatch.hasMatch()) {
                    if (!tags.contains("HEX")) tags << "HEX";
                    isColor = true;
                } else {
                    QRegularExpressionMatch rgbMatch = rgbRegex.match(trimmed);
                    if (rgbMatch.hasMatch()) {
                        int r = rgbMatch.captured(1).toInt();
                        int g = rgbMatch.captured(2).toInt();
                        int b = rgbMatch.captured(3).toInt();
                        if (r <= 255 && g <= 255 && b <= 255) {
                            if (!tags.contains("RGB")) tags << "RGB";
                            isColor = true;
                        }
                    }
                }
                if (isColor) {
                    for (const QString& t : {"色码", "色值", "颜值", "颜色码"}) {
                        if (!tags.contains(t)) tags << t;
                    }
                    // 2026-04-xx 按照用户要求：色码自动归类到 "Color" 分类，即使未开启自动归档开关也强制执行
                    catId = DatabaseManager::instance().getOrCreateCategoryByName("Color");
                }

                if (trimmed.startsWith("http://") || trimmed.startsWith("https://") || trimmed.startsWith("www.")) {
                    finalType = "link";
                    tags << "链接" << "网址";
                    QUrl url(trimmed.startsWith("www.") ? "http://" + trimmed : trimmed);
                    QString host = url.host();
                    QStringList hostParts = host.split('.', Qt::SkipEmptyParts);
                    QString domainTitle;
                    if (!hostParts.isEmpty()) {
                        if (hostParts.first().toLower() == "www" && hostParts.size() > 1) domainTitle = hostParts[1];
                        else domainTitle = hostParts.first();
                    }
                    if (!domainTitle.isEmpty()) {
                        domainTitle[0] = domainTitle[0].toUpper();
                        title = domainTitle;
                        for (QString part : std::as_const(hostParts)) {
                            part = part.trimmed();
                            if (part.toLower() == "www" || part.toLower() == "com" || part.toLower() == "cn" || part.toLower() == "net") continue;
                            if (!part.isEmpty()) {
                                part[0] = part[0].toUpper();
                                if (!tags.contains(part)) tags << part;
                            }
                        }
                    }
                }
            }
            
            if (!finalType.isEmpty()) {
                qDebug() << "[Clipboard->ToolTip DIAG] addNoteAsync 准备调用 | 后台线程已耗时 =" << heavyClock.elapsed() << "ms";
                DatabaseManager::instance().addNoteAsync(title, finalContent, tags, "", catId, finalType, data, sourceApp, sourceTitle);
                qDebug() << "[Clipboard->ToolTip DIAG] addNoteAsync 返回 | 后台线程总耗时 =" << heavyClock.elapsed() << "ms";
            }
        });
    });

    // 2026-04-18 [WATCHDOG] 启动看门狗守护进程
    // 让看门狗监听当前程序的 PID，一旦闪退则自动拉起
    QString watchdogPath = QCoreApplication::applicationDirPath() + "/Watchdog.exe";
    if (QFile::exists(watchdogPath)) {
        QProcess::startDetached(watchdogPath, QStringList() << "--pid" << QString::number(QCoreApplication::applicationPid()));
        qDebug() << "[WATCHDOG] 已启动守护进程，正在监听 PID:" << QCoreApplication::applicationPid();
    }

    int result = a.exec();
    
    // [BLOCK] 如果正常循环结束（例如调用了 quit），确保执行最后一遍物理清理
    DatabaseManager::instance().closeAndPack();
    
    return result;
}