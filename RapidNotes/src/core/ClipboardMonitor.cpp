#include "ClipboardMonitor.h"
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
#include <QtConcurrent>

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
}

void ClipboardMonitor::skipNext() {
    m_skipNext = true;
    QTimer::singleShot(2000, this, [this]() {
        if (m_skipNext) {
            m_skipNext = false;
        }
    });
}

void ClipboardMonitor::onClipboardChanged() {
    if (m_skipNext || m_ignore) {
        m_skipNext = false;
        return;
    }

    const QMimeData* mimeData = QGuiApplication::clipboard()->mimeData();
    if (!mimeData) return;

    QString type;
    QString content;
    QImage sourceImage;

    // 1. [THREAD SAFE] 主线程立即抓取易失数据
    if (mimeData->hasUrls()) {
        QStringList paths;
        for (const QUrl& url : mimeData->urls()) {
            if (url.isLocalFile()) paths << QDir::toNativeSeparators(url.toLocalFile());
        }
        if (!paths.isEmpty()) { type = "file"; content = paths.join(";"); }
    }
    
    if (type.isEmpty() && mimeData->hasImage()) {
        type = "image";
        content = "[截图]";
        // 立即克隆 QImage，防止后台线程访问已销毁的 QMimeData
        sourceImage = qvariant_cast<QImage>(mimeData->imageData());
    }

    if (type.isEmpty() && mimeData->hasText() && !mimeData->text().trimmed().isEmpty()) {
        type = "text";
        content = mimeData->text();
    }

    if (type.isEmpty()) return;

    // 2. 状态捕获
    bool forced = m_forceNext;
    QString forcedType = m_forcedType;
    m_forceNext = false;
    m_forcedType = "";
    if (forced && !forcedType.isEmpty()) type = forcedType;

    // 3. ✅ [ULTIMATE FAST] 瞬间发射 UI 提示信号 (使用专用的 requestFeedback 信号)
    emit requestFeedback(content, type);

    // 4. 将后续繁重任务全部移入后台线程执行
    // 注意：只传递 content/type/sourceImage 等值副本，严禁传递 mimeData 指针
    (void)QtConcurrent::run([this, content, type, sourceImage, forced]() {
        QByteArray dataBlob;
        QString finalApp = "未知应用";
        QString finalTitle = "未知窗口";

        // 后台抓取来源信息
#ifdef Q_OS_WIN
        HWND hwnd = GetForegroundWindow();
        if (hwnd) {
            DWORD processId;
            GetWindowThreadProcessId(hwnd, &processId);
            if (processId == GetCurrentProcessId() && !forced) return; // 内部回环拦截

            wchar_t titleArr[512];
            if (GetWindowTextW(hwnd, titleArr, 512)) finalTitle = QString::fromWCharArray(titleArr);

            HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processId);
            if (hProc) {
                wchar_t exePath[MAX_PATH];
                if (GetModuleFileNameExW(hProc, NULL, exePath, MAX_PATH)) finalApp = QFileInfo(QString::fromWCharArray(exePath)).baseName();
                CloseHandle(hProc);
            }
        }
#endif

        // 后台处理图片降采样与编码 (使用主线程捕获的副本)
        if (type == "image" && !sourceImage.isNull()) {
            QImage img = sourceImage;
            if (img.width() * img.height() * 4 > 20 * 1024 * 1024) {
                img = img.scaled(2560, 2560, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
            QBuffer buffer(&dataBlob);
            buffer.open(QIODevice::WriteOnly);
            img.save(&buffer, "PNG");
        }

        // 后台去重校验 (使用互斥锁保护线程安全)
        QByteArray hashData = dataBlob.isEmpty() ? content.toUtf8() : dataBlob;
        QString currentHash = QCryptographicHash::hash(hashData, QCryptographicHash::Sha256).toHex();
        
        {
            QMutexLocker locker(&m_hashMutex);
            if (currentHash == m_lastHash) return;
            m_lastHash = currentHash;
        }

        // 执行真正的业务逻辑（由 main.cpp 中的处理逻辑承接）
        emit clipboardChanged();
        
        // 最终发射唯一的业务信号，触发数据库入库
        QMetaObject::invokeMethod(this, [=]() {
            emit newContentDetected(content, type, dataBlob, finalApp, finalTitle);
        }, Qt::QueuedConnection);
    });
}
