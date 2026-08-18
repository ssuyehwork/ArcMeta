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
#include <QTimer>
#include <QThreadPool>

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
#include "meta/MediaExtractorPipeline.h"
#include "meta/DatabaseManager.h"
#include "core/CoreController.h"

/**
 * @brief 自定义日志重定向。极速格式化日志内容后投递到异步缓冲区，杜绝同步磁盘等待。
 */
void customMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg) {
    Q_UNUSED(context);
    QString level;
    switch (type) {
        case QtDebugMsg:    level = "DEBUG";    break;
        case QtInfoMsg:     level = "INFO ";    break;
        case QtWarningMsg:  level = "WARN ";    break;
        case QtCriticalMsg: level = "CRIT ";    break;
        case QtFatalMsg:    level = "FATAL";    break;
    }
    // 投递至异步 RingBuffer 日志引擎写出，线程安全且极其高效
    ArcMeta::Logger::log(QString("[%1] %2").arg(level, msg));
}

/**
 * @brief 退出时调用的清场函数，优雅停止各子系统线程、确保数据完整落盘不损坏。
 */
void onApplicationAboutToQuit(HANDLE hMutex) {
    // 1. 阻塞等待全局工作线程池中所有子任务退场，防止多线程写冲突与硬截断
    QThreadPool::globalInstance()->waitForDone();

    // 2. 将高频落盘缓存中的所有待写数据同步强力落盘写入，安全闭卷
    ArcMeta::DatabaseManager::instance().flushAll(true);


    // 4. 释放 COM 套间环境
#ifdef Q_OS_WIN
    CoUninitialize();

    // 5. 释放单实例互斥量锁
    if (hMutex) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }
#endif
}

int main(int argc, char *argv[]) {
    // -------------------------------------------------------------
    // 重构 2：启动安全。最顶端优先执行单实例互斥量哨兵检测
    // -------------------------------------------------------------
    HANDLE hMutex = nullptr;
#ifdef Q_OS_WIN
    hMutex = CreateMutexA(NULL, TRUE, "QuarkMeta_SingleInstance_Mutex");
    if (hMutex == NULL || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hMutex) CloseHandle(hMutex);
        // 单实例检测失败，由于尚未影响任何运行状态及日志，直接优雅退出
        return 0;
    }
#else
    QString lockPath = QDir::tempPath() + "/QuarkMeta_SingleInstance.lock";
    static QLockFile lockFile(lockPath);
    if (!lockFile.tryLock(100)) {
        return 0;
    }
#endif


    // 1. 安装自定义日志处理器（使用超高吞吐无阻塞的内存队列异步写入）
    qInstallMessageHandler(customMessageHandler);

    // 设置高 DPI 支持：Qt 6 默认行为，此处显式设置 PassThrough 以防旧设备缩放模糊
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication a(argc, argv);

    // 2026-07-26：全局彻底消除所有按钮（QAbstractButton 子类）被点击后产生的点状虚线聚焦框
    a.setStyleSheet("QAbstractButton { outline: none; }");

    // 2026-06-xx 按照用户要求：全局统一设置蓝色透明框选样式
    QPalette p = a.palette();
    p.setColor(QPalette::Highlight, QColor(52, 152, 219));      // #3498db (蓝色)
    p.setColor(QPalette::HighlightedText, Qt::white);
    a.setPalette(p);

    a.setQuitOnLastWindowClosed(false);
    
    // 2026-04-14 按照用户要求：物理加固图标加载逻辑
    // 杜绝相对路径幻觉，强制使用 Qt 资源系统 (:/) 加载 app_icon.ico，确保托盘显示不失效
    a.setWindowIcon(QIcon(":/app_icon.ico"));

    a.setApplicationName("QuarkMeta");
    a.setOrganizationName("QuarkMeta");

    // -------------------------------------------------------------
    // 重构 4：COM 亲和性。在 QApplication 实例化后，安全初始化 COM 环境
    // -------------------------------------------------------------
#ifdef Q_OS_WIN
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
#endif

    // -------------------------------------------------------------
    // 重构 3：生命期。引入 AppCoreController 统一拓扑预热
    // -------------------------------------------------------------
    ArcMeta::CoreController::initializeCoreComponents();

    // -------------------------------------------------------------
    // 重构 5：多段启动。MainWindow 放置于栈上局部作用域，利用 RAII 自动且安全析构，规避 Double Free
    // -------------------------------------------------------------
    ArcMeta::MainWindow w;
    
    // 启动异步系统扫描与监控监听
    ArcMeta::CoreController::instance().startSystem();

    // 利用主线程第一个 Tick 调度，平滑显示窗口，消解首帧信号洪暴导致的渲染卡顿
    QTimer::singleShot(0, [&w]() {
        w.show();
    });

    // -------------------------------------------------------------
    // 重构 6：Clean Shutdown 退出清场机制挂接
    // -------------------------------------------------------------
    QObject::connect(&a, &QApplication::aboutToQuit, [&a, hMutex]() {
        onApplicationAboutToQuit(hMutex);
    });

    return a.exec();
}
