#ifndef CLIPBOARDMONITOR_H
#define CLIPBOARDMONITOR_H

#include <QObject>
#include <QClipboard>
#include <QGuiApplication>
#include <QCryptographicHash>
#include <QStringList>
#include <QMutex>

class ClipboardMonitor : public QObject {
    Q_OBJECT
public:
    static ClipboardMonitor& instance();
    void skipNext();
    // [CRITICAL] 明确开启/关闭全局忽略标记，用于采集等批量操作，杜绝重复入库
    void setIgnore(bool ignore) { m_ignore = ignore; }
    // [CRITICAL] forceNext 支持传入预设类型（如 ocr_text），确保识别出的文字使用专用图标
    void forceNext(const QString& type = "") { m_forceNext = true; m_forcedType = type; }
    void clearLastHash() { m_lastHash = ""; }

signals:
    // [NEW] 专门用于瞬时 UI 反馈的信号
    void requestFeedback(const QString& content, const QString& type);
    
    // 业务逻辑信号（数据完整后发射）
    void newContentDetected(const QString& content, const QString& type, const QByteArray& data = QByteArray(),
                            const QString& sourceApp = "", const QString& sourceTitle = "");
    void clipboardChanged();

private slots:
    void onClipboardChanged();

private:
    ClipboardMonitor(QObject* parent = nullptr);
    QString m_lastHash;
    QMutex m_hashMutex; // [NEW] 保护异步哈希校验的线程安全
    bool m_skipNext = false;
    bool m_ignore = false;
    bool m_forceNext = false;
    // [CRITICAL] 记录强制触发时的预设类型
    QString m_forcedType;
};

#endif // CLIPBOARDMONITOR_H
