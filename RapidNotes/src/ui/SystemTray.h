#ifndef SYSTEMTRAY_H
#define SYSTEMTRAY_H

#include <QSystemTrayIcon>
#include <QMenu>
#include <QObject>

class SystemTray : public QObject {
    Q_OBJECT
public:
    explicit SystemTray(QObject* parent = nullptr);
    void show();
    void setFloatingBallVisible(bool visible);

signals:
    void showQuickWindow();
    void showTagManagerRequested();
    void showFloatingBallRequested();
    void showHelpRequested();
    void showSettings();
    void quitApp();


private:
    QSystemTrayIcon* m_trayIcon;
    QMenu* m_menu;
    QAction* m_quitAction; // 保持习惯，虽然之前没显式写
};

#endif // SYSTEMTRAY_H
