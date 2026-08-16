#ifndef LOGGER_H
#define LOGGER_H

#include <QString>
#include <QStringList>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QMutex>
#include <QMutexLocker>
#include <QFileInfo>
#include <QDir>
#include <QThread>
#include <QWaitCondition>
#include <atomic>

namespace ArcMeta {

/**
 * @brief 异步日志写出线程，避免在写日志时发生磁盘 I/O 阻塞。
 */
class LoggerWriterThread : public QThread {
    Q_OBJECT
public:
    LoggerWriterThread(const QString& fileName, QObject* parent = nullptr)
        : QThread(parent), m_fileName(fileName), m_stopped(false) {}

    ~LoggerWriterThread() {
        stop();
    }

    void append(const QString& msg) {
        QMutexLocker locker(&m_mutex);
        m_queue.append(msg);
        m_cond.wakeOne();
    }

    void stop() {
        {
            QMutexLocker locker(&m_mutex);
            if (m_stopped) return;
            m_stopped = true;
            m_cond.wakeAll();
        }
        wait(); // 等待线程安全退场
    }

protected:
    void run() override {
        QStringList localQueue;
        while (true) {
            {
                QMutexLocker locker(&m_mutex);
                while (m_queue.isEmpty() && !m_stopped) {
                    m_cond.wait(&m_mutex);
                }
                if (m_queue.isEmpty() && m_stopped) {
                    break;
                }
                localQueue = m_queue;
                m_queue.clear();
            }

            if (!localQueue.isEmpty()) {
                writeBatch(localQueue);
                localQueue.clear();
            }
        }
    }

private:
    void writeBatch(const QStringList& batch) {
        rotateLogFiles(m_fileName);

        QFile file(m_fileName);
        if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            QTextStream out(&file);
            for (const QString& msg : batch) {
                out << msg << Qt::endl;
            }
            out.flush();
            file.flush();
            file.close();
        }
    }

    void rotateLogFiles(const QString& fileName) {
        QFileInfo info(fileName);
        if (info.exists() && info.size() > 4 * 1024 * 1024) { // 4MB 阈值
            QString oldFile = fileName + ".old";
            QFile::remove(oldFile);
            if (!QFile::rename(fileName, oldFile)) {
                QFile file(fileName);
                (void)file.open(QIODevice::WriteOnly | QIODevice::Truncate);
                file.close();
            }
        }
    }

    QString m_fileName;
    bool m_stopped;
    QMutex m_mutex;
    QWaitCondition m_cond;
    QStringList m_queue;
};

/**
 * @brief 独立日志工具类，绕过 qDebug 直接写入本地文件
 */
class Logger {
public:
    static void rotateLogFiles(const QString&) {}
    static void log(const QString&) {}
    static void stopAsyncLogger() {}
};

} // namespace ArcMeta

#endif // LOGGER_H
