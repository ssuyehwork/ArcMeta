#ifndef NOMINMAX
#define NOMINMAX
#endif
//2813583 main 禁止删除此行
#include <QApplication>
#include "FramelessDialog.h"
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QSvgRenderer>
#include <QPainter>
#include <QLockFile>
#include <QDir>
#include <QMutex>

#ifdef Q_OS_WIN
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#endif
#include "ui/UiHelper.h"
#include "ui/Logger.h"
#include "ui/MainWindow.h"


#include "meta/MetadataManager.h"
#include "meta/CategoryRepo.h"
#include "mft/MftReader.h"
#include "core/CoreController.h"

/**
 * @brief 自定义日志处理程序，将 qDebug 消息重定向至本地 .log 文件
 * 2026-03-xx 按照用户要求：在手动运行 .exe 时，通过日志文件排查初始化挂起或信号丢失问题。
 */
void customMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg) {
    Q_UNUSED(context); 
    static QMutex s_logMutex; // 2026-07-xx 物理加固：增加互斥锁，确保多线程扫描时日志不交织且不丢失
    QMutexLocker locker(&s_logMutex);

    static int writeCount = 0;
    QString fileName = "arcmeta_debug.log";

    // 2026-xx-xx 按照 Plan-97：同步容量哨兵逻辑
    if (++writeCount >= 100) {
        ArcMeta::Logger::rotateLogFiles(fileName);
        writeCount = 0;
    }

    QFile logFile(fileName);
    // 2026-07-xx 按照用户要求 (1.18)：开启强力落盘模式
    // 即使发生系统级闪退，也要确保最后一条日志已写入物理磁盘
    if (logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream textStream(&logFile);
        QString timeStr = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
        QString level;
        switch (type) {
            case QtDebugMsg:    level = "DEBUG";    break;
            case QtInfoMsg:     level = "INFO ";    break;
            case QtWarningMsg:  level = "WARN ";    break;
            case QtCriticalMsg: level = "CRIT ";    break;
            case QtFatalMsg:    level = "FATAL";    break;
        }
        textStream << QString("[%1][%2] %3").arg(timeStr, level, msg) << Qt::endl;
        textStream.flush();
        logFile.flush(); // 强制物理落盘
        logFile.close();
    }
}

int main(int argc, char *argv[]) {
    // 2026-xx-xx 按照 Plan-97：启动自检，容量哨兵立即执行
    ArcMeta::Logger::rotateLogFiles("arcmeta_debug.log");

    // 2026-07-xx 按照用户要求 (1.20)：主程序限制单实例运行，防止无限打开
#ifdef Q_OS_WIN
    // Windows: 使用 Mutex 实现，确保程序异常退出后资源能被 OS 自动回收
    HANDLE hMutex = CreateMutexA(NULL, TRUE, "ArcMeta_SingleInstance_Mutex");
    if (hMutex == NULL || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hMutex) CloseHandle(hMutex);
        // 单实例检测失败，由于此时 QApplication 尚未启动，直接退出
        return 0;
    }
#else
    // 非 Windows (Linux/macOS): 使用 QLockFile 确保单实例运行
    QString lockPath = QDir::tempPath() + "/ArcMeta_SingleInstance.lock";
    static QLockFile lockFile(lockPath); // 必须静态或在 main 作用域持续存在
    if (!lockFile.tryLock(100)) { // 尝试等待 100ms
        return 0;
    }
#endif

    // 初始化 COM 环境 (多媒体缩略图提取需要)
#ifdef Q_OS_WIN
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
#endif

    // 1. 安装自定义日志处理器：确保从程序启动的第一秒开始就能捕获所有调试信息
    qInstallMessageHandler(customMessageHandler);

    // 设置高 DPI 支持：Qt 6 默认行为，此处显式设置 PassThrough 以防旧设备缩放模糊
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication a(argc, argv);

    // 2026-06-xx 按照用户要求：全局统一设置蓝色透明框选样式
    QPalette p = a.palette();
    p.setColor(QPalette::Highlight, QColor(52, 152, 219));      // #3498db (蓝色)
    p.setColor(QPalette::HighlightedText, Qt::white);
    a.setPalette(p);

    a.setQuitOnLastWindowClosed(false);
    
    // 2026-04-14 按照用户要求：物理加固图标加载逻辑
    // 杜绝相对路径幻觉，强制使用 Qt 资源系统 (:/) 加载 app_icon.ico，确保托盘显示不失效
    a.setWindowIcon(QIcon(":/app_icon.ico"));

    a.setApplicationName("ArcMeta");
    a.setOrganizationName("ArcMetaTeam");

    // 2026-05-27 物理修复：在主线程预热元数据管理器单例
    // 确保其内部的 QTimer 等对象归属于主线程，避免跨线程创建导致的行为不确定性
    ArcMeta::MetadataManager::instance();
    // 2026-06-xx 物理修复：在主线程预热 CategoryRepo，解决 QTimer 跨线程启动导致的内存与磁盘不一致
    ArcMeta::CategoryRepo::initialize();

    // 3. 简化启动：直接显示主窗口
    // 2026-04-13 按用户要求移除 LoadingWindow 和 initializeHotIcons()
    ArcMeta::MainWindow* w = new ArcMeta::MainWindow();
    
    w->show();

    // 5. 启动异步系统扫描（后台初始化，UI 可响应）
    ArcMeta::CoreController::instance().startSystem();

    int ret = a.exec();

    // 程序退出前释放单实例锁
#ifdef Q_OS_WIN
    if (hMutex) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }
#endif

    return ret;
}
