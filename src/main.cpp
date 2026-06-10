#ifndef NOMINMAX
#define NOMINMAX
#endif
//2813583 main 禁止删除此行
#include <QApplication>
#include <QMessageBox>
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QSvgRenderer>
#include <QPainter>
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include "ui/UiHelper.h"
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
    QFile logFile("arcmeta_debug.log");
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
        logFile.close();
    }
}

int main(int argc, char *argv[]) {
    qint64 mainStartTime = QDateTime::currentMSecsSinceEpoch();

    // 初始化 COM 环境 (多媒体缩略图提取需要)
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    // 1. 安装自定义日志处理器：确保从程序启动的第一秒开始就能捕获所有调试信息
    qInstallMessageHandler(customMessageHandler);
    qDebug() << "================ ArcMeta 启动加载 ================";
    qDebug() << "[PERF] 程序入口点计时开始";

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
    qint64 metaInitStart = QDateTime::currentMSecsSinceEpoch();
    ArcMeta::MetadataManager::instance();
    // 2026-06-xx 物理修复：在主线程预热 CategoryRepo，解决 QTimer 跨线程启动导致的内存与磁盘不一致
    ArcMeta::CategoryRepo::initialize();
    qDebug() << "[PERF] MetadataManager/CategoryRepo 单例预热耗时:" << (QDateTime::currentMSecsSinceEpoch() - metaInitStart) << "ms";

    // 3. 简化启动：直接显示主窗口
    // 2026-04-13 按用户要求移除 LoadingWindow 和 initializeHotIcons()
    qint64 windowCreateStart = QDateTime::currentMSecsSinceEpoch();
    ArcMeta::MainWindow* w = new ArcMeta::MainWindow();
    qDebug() << "[PERF] MainWindow 构造耗时:" << (QDateTime::currentMSecsSinceEpoch() - windowCreateStart) << "ms";
    
    w->show();
    qDebug() << "[PERF] MainWindow->show() 调用耗时（至首帧渲染前）:" << (QDateTime::currentMSecsSinceEpoch() - windowCreateStart) << "ms";

    // 5. 启动异步系统扫描（后台初始化，UI 可响应）
    ArcMeta::CoreController::instance().startSystem();

    qDebug() << "[PERF] main 函数逻辑执行完毕，进入事件循环。总耗时:" << (QDateTime::currentMSecsSinceEpoch() - mainStartTime) << "ms";

    int ret = a.exec();

    return ret;
}
