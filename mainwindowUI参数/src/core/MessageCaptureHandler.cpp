#include "MessageCaptureHandler.h"
#include "KeyboardHook.h"
#include "DatabaseManager.h"
#include "ClipboardMonitor.h"
#include <QClipboard>
#include <QApplication>
#include <QDebug>
#include <QDateTime>
#include <QTimer>
#include <QFileInfo>

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

MessageCaptureHandler& MessageCaptureHandler::instance() {
    static MessageCaptureHandler inst;
    return inst;
}

MessageCaptureHandler::MessageCaptureHandler(QObject* parent) : QObject(parent) {}

void MessageCaptureHandler::init() {
    connect(&KeyboardHook::instance(), &KeyboardHook::enterPressedInOtherApp, this, &MessageCaptureHandler::onEnterPressed);
    // qDebug() << "[MessageCaptureHandler] 初始化完成，开始监听外部应用回车键";
}

void MessageCaptureHandler::onEnterPressed(bool ctrl, bool shift, bool alt) {
    // 简单的频率限制，防止长按回车或高频重复触发 (500ms 内仅一次)
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    if (currentTime - m_lastTriggerTime < 500) return;
    m_lastTriggerTime = currentTime;

    qDebug() << "[Capture] 触发捕获，C:" << ctrl << " S:" << shift << " A:" << alt;

    // 使用定时器序列，避免在 Hook 回调中执行过长的阻塞操作
    QTimer::singleShot(0, [this, ctrl, shift, alt]() {
        // 告知剪贴板监控器跳过接下来的变更，由我们手动处理
        ClipboardMonitor::instance().skipNext();

        // 1. 模拟全选 (Win32 API 直接实现)
#ifdef Q_OS_WIN
        keybd_event(VK_SHIFT, 0, KEYEVENTF_KEYUP, 0);
        keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
        keybd_event(VK_CONTROL, 0, 0, 0);
        keybd_event('A', 0, 0, 0);
        keybd_event('A', 0, KEYEVENTF_KEYUP, 0);
        keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
#endif
        
        QTimer::singleShot(50, [this, ctrl, shift, alt]() {
            // 2. 模拟复制
#ifdef Q_OS_WIN
            keybd_event(VK_SHIFT, 0, KEYEVENTF_KEYUP, 0);
            keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
            keybd_event(VK_CONTROL, 0, 0, 0);
            keybd_event('C', 0, 0, 0);
            keybd_event('C', 0, KEYEVENTF_KEYUP, 0);
            keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
#endif
            
            QTimer::singleShot(150, [this, ctrl, shift, alt]() {
                QString text = QApplication::clipboard()->text().trimmed();
                
                // 3. 模拟按下 END 键以取消全选状态，防止回车键删除选中的文字
#ifdef Q_OS_WIN
                keybd_event(VK_END, 0, 0, 0);
                keybd_event(VK_END, 0, KEYEVENTF_KEYUP, 0);

                // 4. 补发原始回车键（带上原来的修饰键状态）
                if (ctrl) keybd_event(VK_CONTROL, 0, 0, 0);
                if (alt) keybd_event(VK_MENU, 0, 0, 0);
                if (shift) keybd_event(VK_SHIFT, 0, 0, 0);

                keybd_event(VK_RETURN, 0, 0, 0);
                keybd_event(VK_RETURN, 0, KEYEVENTF_KEYUP, 0);

                if (shift) keybd_event(VK_SHIFT, 0, KEYEVENTF_KEYUP, 0);
                if (alt) keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
                if (ctrl) keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
#endif

                if (text.isEmpty()) {
                    qDebug() << "[Capture] 捕获文本为空，取消保存";
                    return;
                }

                // 4. 获取来源应用信息
                QString sourceApp = "未知应用";
                QString sourceTitle = "未知窗口";
#ifdef Q_OS_WIN
                HWND hwnd = GetForegroundWindow();
                if (hwnd) {
                    wchar_t title[512];
                    if (GetWindowTextW(hwnd, title, 512)) {
                        sourceTitle = QString::fromWCharArray(title);
                    }
                    DWORD pid;
                    GetWindowThreadProcessId(hwnd, &pid);
                    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
                    if (hProcess) {
                        wchar_t exePath[MAX_PATH];
                        if (GetModuleFileNameExW(hProcess, NULL, exePath, MAX_PATH)) {
                            sourceApp = QFileInfo(QString::fromWCharArray(exePath)).baseName();
                        }
                        CloseHandle(hProcess);
                    }
                }
#endif

                // 5. 保存到数据库
                // 取第一行作为标题
                QString firstLine = text.section('\n', 0, 0).trimmed();
                QString title;
                if (firstLine.isEmpty()) {
                    title = "自动捕获消息";
                } else {
                    title = firstLine.left(40);
                    if (firstLine.length() > 40) title += "...";
                }
                
                // 使用特定的 item_type: captured_message
                DatabaseManager::instance().addNoteAsync(title, text, {"自动捕获"}, "", -1, "captured_message", QByteArray(), sourceApp, sourceTitle);
                qDebug() << "[Capture] 已保存捕获的消息:" << title;
            });
        });
    });
}
