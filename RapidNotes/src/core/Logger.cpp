#include "Logger.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QDateTime>
#include <QMutex>
#include <QMutexLocker>
#ifdef Q_OS_WIN
#include <windows.h> // 2026-04-18 新增：用于 FlushFileBuffers 物理落盘
#endif


QString Logger::s_logPath = "";
static QMutex s_logMutex;

void Logger::init() {
    QString appPath = QCoreApplication::applicationDirPath();
    QString logDir = appPath + "/logs";
    QDir dir(logDir);
    if (!dir.exists()) dir.mkpath(".");

    s_logPath = logDir + "/log_" + QDateTime::currentDateTime().toString("yyyy-MM-dd") + ".txt";

    // 2026-03-xx 按照用户要求：立即执行过期日志清理逻辑
    cleanOldLogs();

    // 2026-04-18 按照用户要求：同时执行崩溃转储 (.dmp) 清理逻辑
    cleanOldDumps();

    // 注册全局消息处理器
    qInstallMessageHandler(Logger::messageHandler);
    
    qInfo() << "--- [Logger] 日志系统初始化完成，当前日志:" << s_logPath << "---";
}

void Logger::messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg) {
    if (s_logPath.isEmpty()) return;

    QMutexLocker locker(&s_logMutex);
    QFile file(s_logPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream stream(&file);
        
        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
        QString typeStr;
        switch (type) {
            case QtDebugMsg:    typeStr = "[DEBUG]"; break;
            case QtInfoMsg:     typeStr = "[INFO ]"; break;
            case QtWarningMsg:  typeStr = "[WARN ]"; break;
            case QtCriticalMsg: typeStr = "[CRIT ]"; break;
            case QtFatalMsg:    typeStr = "[FATAL]"; break;
        }

        // 格式化输出：时间戳 [等级] [源文件:行号] 消息
        stream << timestamp << " " << typeStr;
        if (context.file) {
            QString fileName = QFileInfo(context.file).fileName();
            stream << " [" << fileName << ":" << context.line << "]";
        }
        stream << " " << msg << Qt::endl;
        
        // 2026-04-18 核心改进：针对由于 _commit 句柄错配导致的崩溃，修正为 Win32 原生 FlushFileBuffers
        file.flush();
#ifdef Q_OS_WIN
        // QFile::handle() 在 Windows 下返回的是 HANDLE。必须使用 FlushFileBuffers 而非 _commit。
        // 此修复解决了程序在记录第一条日志时即自杀的致命 Bug。
        if (file.handle() != -1) {
            FlushFileBuffers((HANDLE)file.handle());
        }
#endif
        
        file.close();
    }
}

void Logger::cleanOldLogs() {
    // 2026-03-xx 按照用户要求：核心逻辑，仅保留最近 2 天的日志
    QString logDir = QCoreApplication::applicationDirPath() + "/logs";
    QDir dir(logDir);
    if (!dir.exists()) return;

    QStringList filter;
    filter << "log_*.txt";
    QFileInfoList logs = dir.entryInfoList(filter, QDir::Files, QDir::Name); // 按名称正序，即旧的在前

    QDate today = QDate::currentDate();
    QDate threshold = today.addDays(-1); // 包含今天和昨天，前天及以前的都删掉

    for (const QFileInfo& log : logs) {
        // 从文件名解析日期：log_YYYY-MM-DD.txt
        QString fileName = log.baseName(); // "log_2026-03-20"
        QString dateStr = fileName.mid(4);
        QDate logDate = QDate::fromString(dateStr, "yyyy-MM-dd");

        if (logDate.isValid() && logDate < threshold) {
            if (QFile::remove(log.absoluteFilePath())) {
                // 由于此时消息处理器尚未接管，此处输出仅在控制台可见（若有）
                qDebug() << "[Logger] 已自动清理过期日志:" << log.fileName();
            }
        }
    }
}

void Logger::cleanOldDumps() {
    // 2026-04-18 按照用户要求：清理过期的崩溃转储文件 (.dmp)
    // 策略：保留最近 7 天的文件，且总数不超过 10 个
    QString appDir = QCoreApplication::applicationDirPath();
    QDir dir(appDir);
    if (!dir.exists()) return;

    QStringList filter;
    filter << "crash_*.dmp";
    QFileInfoList dumps = dir.entryInfoList(filter, QDir::Files, QDir::Time); // 最近的在前（无需 Reversed）

    QDate today = QDate::currentDate();
    QDate threshold = today.addDays(-7);

    int count = 0;
    for (const QFileInfo& dump : dumps) {
        count++;
        bool shouldDelete = false;

        // 策略1：日期清理（超过7天）
        // 文件名格式：crash_YYYYMMDD_HHmmss.dmp
        QString fileName = dump.baseName(); // "crash_20260418_142145"
        if (fileName.length() >= 14) {
            QString datePart = fileName.mid(6, 8); // "20260418"
            QDate dumpDate = QDate::fromString(datePart, "yyyyMMdd");
            if (dumpDate.isValid() && dumpDate < threshold) {
                shouldDelete = true;
            }
        }

        // 策略2：数量上限（超过10个，从第11个开始删）
        if (count > 10) {
            shouldDelete = true;
        }

        if (shouldDelete) {
            if (QFile::remove(dump.absoluteFilePath())) {
                qDebug() << "[Logger] 已自动清理过期/多余的 DUMP 文件:" << dump.fileName();
            }
        }
    }
}

