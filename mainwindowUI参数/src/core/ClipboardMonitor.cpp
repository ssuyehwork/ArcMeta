#include "ClipboardMonitor.h"
#include <QElapsedTimer>
#include <QMimeData>
#include <QDebug>
#include <QApplication>
#include <QImage>
#include <QBuffer>
#include <QUrl>
#include <QFileInfo>
#include <QDir>
#include <QCryptographicHash>
#include <QTimer>
#include <QSettings>

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

ClipboardMonitor& ClipboardMonitor::instance() {
    static ClipboardMonitor inst;
    return inst;
}

ClipboardMonitor::ClipboardMonitor(QObject* parent) : QObject(parent) {
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, &ClipboardMonitor::onClipboardChanged);
    reloadBlacklist();
    // qDebug() << "[ClipboardMonitor] 初始化完成，开始监听...";
}

void ClipboardMonitor::reloadBlacklist() {
    QSettings blacklistSettings("RapidNotes", "Security");
    QStringList rawList = blacklistSettings.value("avoidanceBlacklist").toStringList();
    
    // [OPTIMIZED] 预处理黑名单：去除空格、转小写、统一去掉 .exe 后缀
    m_blacklistCache.clear();
    for (const QString& item : rawList) {
        QString cleaned = item.trimmed().toLower();
        if (cleaned.endsWith(".exe")) {
            cleaned = cleaned.left(cleaned.length() - 4);
        }
        if (!cleaned.isEmpty() && !m_blacklistCache.contains(cleaned)) {
            m_blacklistCache << cleaned;
        }
    }
    // qDebug() << "[ClipboardMonitor] 黑名单缓存已刷新并优化，有效项数:" << m_blacklistCache.size();
}

void ClipboardMonitor::skipNext() {
    m_skipNext = true;
    // [CRITICAL] 增加 2 秒自动过期逻辑。
    // 如果系统模拟复制操作失败（如目标应用卡死），dataChanged 信号将永远不会触发。
    // 此时若不重置标志位，用户手动进行的下一次复制也会被永久忽略。
    QTimer::singleShot(2000, this, [this]() {
        if (m_skipNext) {
            m_skipNext = false;
            qDebug() << "[ClipboardMonitor] skipNext 标志位因超时已自动重置";
        }
    });
}

void ClipboardMonitor::onClipboardChanged() {
    static int s_callCount = 0;
    s_callCount++;
    QElapsedTimer diagClock;
    diagClock.start();
    qDebug() << "[ClipboardMonitor DIAG] ===== onClipboardChanged 第" << s_callCount << "次调用 =====";

    bool forced = m_forceNext;
    QString forcedType = m_forcedType;
    m_forceNext = false;
    m_forcedType = "";

    if (m_skipNext || m_ignore) {
        m_skipNext = false;
        qDebug() << "[ClipboardMonitor DIAG] 跳过 (skipNext/ignore) | 耗时 =" << diagClock.elapsed() << "ms";
        return;
    }

    // 抓取来源窗口信息 (对标 Ditto)
    QString sourceApp = forced ? "RapidNotes (内建工具)" : "未知应用";
    QString sourceTitle = "未知窗口";

    // 1. 过滤本程序自身的复制 (通过进程 ID 判定，比 activeWindow 更可靠)
#ifdef Q_OS_WIN
    HWND hwnd = GetForegroundWindow();
    if (hwnd) {
        DWORD processId;
        GetWindowThreadProcessId(hwnd, &processId);

        // [OPTIMIZED] 进程级避让黑名单校验 (使用缓存，杜绝高频 I/O)
        if (!m_blacklistCache.isEmpty()) {
            HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processId);
            if (hProc) {
                wchar_t exePath[MAX_PATH];
                if (GetModuleFileNameExW(hProc, NULL, exePath, MAX_PATH)) {
                    // [OPTIMIZED] 匹配逻辑简化，使用已经预处理好的缓存
                    QString procName = QFileInfo(QString::fromWCharArray(exePath)).baseName().toLower();
                    if (m_blacklistCache.contains(procName)) {
                        qDebug() << "[ClipboardMonitor] 触发进程避让黑名单，停止捕获:" << procName;
                        CloseHandle(hProc);
                        return;
                    }
                }
                CloseHandle(hProc);
            }
        }
        
        // 既然已经拿到了 HWND 和 PID，直接抓取标题和应用名，避免重复调用系统 API
        wchar_t title[512];
        if (GetWindowTextW(hwnd, title, 512)) {
            sourceTitle = QString::fromWCharArray(title);
        }

        // [CRITICAL] 过滤逻辑精细化：仅针对主窗口和快速笔记窗口的常规复制操作进行拦截（防止回环）。
        // 如果开启了 forced (forceNext)，说明是内建工具的主动行为，必须予以记录，无论当前窗口是谁。
        if (processId == GetCurrentProcessId()) {
            if (!forced) {
                // [NOTE] 如果没有强制标志，且活跃窗口是主界面，则判定为需要过滤的内部回环
                if (sourceTitle == "RapidNotes" || sourceTitle == "快速笔记") {
                    return;
                }
            } else {
                // 如果是强制记录，确保来源 App 显示为内建工具
                sourceApp = "RapidNotes (内建工具)";
            }
        }

        // [FIX] 仅当不是内建工具强制触发时，才去查询进程的可执行文件名作为来源 App
        if (!forced) {
            HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processId);
            if (hProcess) {
                wchar_t exePath[MAX_PATH];
                if (GetModuleFileNameExW(hProcess, NULL, exePath, MAX_PATH)) {
                    sourceApp = QFileInfo(QString::fromWCharArray(exePath)).baseName();
                }
                CloseHandle(hProcess);
            }
        }
    }
#else
    QWidget* activeWin = QApplication::activeWindow();
    if (activeWin && !forced) {
        QString title = activeWin->windowTitle();
        if (title == "RapidNotes" || title == "快速笔记") {
            return;
        }
    }
#endif

    const QMimeData* mimeData = QGuiApplication::clipboard()->mimeData();
    if (!mimeData) return;

    QString type;
    QString content;
    QByteArray dataBlob;

    // 优先级 1: 本地文件
    if (mimeData->hasUrls()) {
        QStringList paths;
        for (const QUrl& url : mimeData->urls()) {
            if (url.isLocalFile()) {
                paths << QDir::toNativeSeparators(url.toLocalFile());
            }
        }
        if (!paths.isEmpty()) {
            type = "file";
            content = paths.join(";");
        }
    }

    // 优先级 2: 截图 (仅当不是文件时)
    if (type.isEmpty() && mimeData->hasImage()) {
        // [OPTIMIZATION] 剪贴板大图捕获保护：
        // 在进行昂贵的 PNG 编码前，先通过 imageData 变体尝试预判尺寸
        QImage img = qvariant_cast<QImage>(mimeData->imageData());
        if (!img.isNull()) {
            // 如果图片超过 20MB (约 5000x4000 32bpp)，则进行降采样处理，防止主线程卡死和 OOM
            qint64 estimatedSize = img.width() * img.height() * 4;
            if (estimatedSize > 20 * 1024 * 1024) {
                img = img.scaled(2560, 2560, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }

            type = "image";
            content = "[截图]";
            QBuffer buffer(&dataBlob);
            buffer.open(QIODevice::WriteOnly);
            img.save(&buffer, "PNG");
        }
    }

    // 优先级 3: 文本 (包括网页链接)
    if (type.isEmpty() && mimeData->hasText() && !mimeData->text().trimmed().isEmpty()) {
        type = "text";
        content = mimeData->text();
    }

    // 如果都没有识别出来，可能是空文本或不支持的格式
    if (type.isEmpty()) {
        // 2026-03-xx 按照用户要求，支持空内容提示逻辑：即便识别失败也发送信号，由接收端判断是否提示“复制失败”
        // [FIX] 信号连接处需要 content(arg1), type(arg2), data(arg3)...
        emit newContentDetected("", "", QByteArray(), sourceApp, sourceTitle);
        return;
    }

    // [CRITICAL] 如果指定了强制类型，则覆盖，用于区分普通文本与截图识别出的文字
    if (forced && !forcedType.isEmpty()) {
        type = forcedType;
    }

    // SHA256 去重
    QByteArray hashData = dataBlob.isEmpty() ? content.toUtf8() : dataBlob;
    QString currentHash = QCryptographicHash::hash(hashData, QCryptographicHash::Sha256).toHex();
    
    if (currentHash == m_lastHash) {
        qDebug() << "[ClipboardMonitor DIAG] SHA256 去重命中，跳过 | 本次 onClipboardChanged 耗时 =" << diagClock.elapsed() << "ms";
        return;
    }
    m_lastHash = currentHash;

    // [CRITICAL] 2026-03-14 架构级优化：优先级调整
    // 按照用户需求，必须【绝对优先】执行 ToolTipOverlay
    // 因此这里先发射 newContentDetected (它连接到 ToolTip 显示)
    qDebug() << "[ClipboardMonitor DIAG] 信号发射前 | onClipboardChanged 总耗时 =" << diagClock.elapsed() << "ms | type =" << type;
    emit newContentDetected(content, type, dataBlob, sourceApp, sourceTitle);

    // 然后再发射 clipboardChanged (它连接到烟花特效等其他非核心反馈)
    // 只有当内容确实发生变化（非重复，非程序内部回环）时，才允许触发
    emit clipboardChanged();

    qDebug() << "[ClipboardMonitor] 捕获新内容 (来自:" << sourceApp << "):" << type << content.left(30);
}