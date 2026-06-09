#include <QCoreApplication>
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QDebug>
#include <QThread>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

/**
 * RapidNotes 看门狗守护程序
 * 职责：监听主进程状态，实现崩溃自动重启，合法退出则跟随销毁。
 */

int main(int argc, char *argv[]) {
    // 设置不使用 UI，减小开销
    QCoreApplication a(argc, argv);

    // 解析监控的 PID
    int pid = 0;
    QStringList args = a.arguments();
    for (int i = 0; i < args.size() - 1; ++i) {
        if (args[i] == "--pid") {
            pid = args[i + 1].toInt();
            break;
        }
    }

    // 如果未传 PID 或 PID 非法，直接退出
    if (pid <= 0) return -1;

#ifdef Q_OS_WIN
    // 逻辑：通过 Windows API 获取进程同步句柄，等待其信号。
    // 该方法比死循环检测 CPU 占用低且响应及时。
    HANDLE hProcess = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid);
    if (hProcess == NULL) {
        // 进程可能已经结束
    } else {
        // 阻塞直到主进程消失（或崩溃）
        WaitForSingleObject(hProcess, INFINITE);
        CloseHandle(hProcess);
    }
#else
    // 备用简易轮询逻辑
    while (true) {
        if (QProcess::execute("kill", QStringList() << "-0" << QString::number(pid)) != 0) break;
        QThread::msleep(1000);
    }
#endif

    // 主进程已消失，开始判定退出性质
    QString appDirPath = QCoreApplication::applicationDirPath();
    QString lockPath = appDirPath + "/exit_gracefully.lock";

    if (QFile::exists(lockPath)) {
        // 情况 A：发现合法退出标识位
        // 托盘菜单的“退出”操作会提前写入此文件。
        // 我们只需清理锁文件并安静退出看门狗。
        QFile::remove(lockPath);
        return 0;
    } else {
        // 情况 B：未发现标识位，判定为异常闪退
        // 重新拉起 RapidNotes.exe
        QString appPath = appDirPath + "/RapidNotes.exe";
        
        // 稍微延迟一下，避免短时间内连续快速崩溃导致重启风暴
        QThread::msleep(500);
        
        if (QFile::exists(appPath)) {
            QProcess::startDetached(appPath);
        }
    }

    return 0;
}
