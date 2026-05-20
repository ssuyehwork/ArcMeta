#ifndef CLIPBOARDMONITOR_H
#define CLIPBOARDMONITOR_H

#include <QObject>
#include <QClipboard>
#include <QGuiApplication>
#include <QCryptographicHash>
#include <QStringList>

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

    // [NEW] 黑名单热加载
    void reloadBlacklist();

signals:
    void newContentDetected(const QString& content, const QString& type, const QByteArray& data = QByteArray(),
                            const QString& sourceApp = "", const QString& sourceTitle = "");
    void clipboardChanged();

private slots:
    void onClipboardChanged();

private:
    ClipboardMonitor(QObject* parent = nullptr);
    QString m_lastHash;
    bool m_skipNext = false;
    bool m_ignore = false;
    bool m_forceNext = false;
    // [CRITICAL] 记录强制触发时的预设类型
    QString m_forcedType;

    // [NEW] 黑名单缓存，避免高频 I/O
    QStringList m_blacklistCache;
};

#endif // CLIPBOARDMONITOR_H