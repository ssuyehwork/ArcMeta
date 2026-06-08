#ifndef SETTINGSWINDOW_H
#define SETTINGSWINDOW_H

#include "FramelessDialog.h"
#include <QLineEdit>
#include <QKeySequence>
#include <QKeyEvent>
#include <QPushButton>
#include <QLabel>
#include <QListWidget>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QPropertyAnimation>

/**
 * @brief 全局热键捕获控件
 */
class HotkeyEdit : public QLineEdit {
    Q_OBJECT
public:
    explicit HotkeyEdit(QWidget* parent = nullptr);
    void setKeyData(uint mods, uint vk);
    uint mods() const { return m_mods; }
    uint vk() const { return m_vk; }

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    uint m_mods = 0;
    uint m_vk = 0;
    QString keyToString(uint mods, uint vk);
};

/**
 * @brief 局内快捷键捕获控件
 */
class ShortcutEdit : public QLineEdit {
    Q_OBJECT
public:
    explicit ShortcutEdit(QWidget* parent = nullptr);
    void setKeySequence(const QKeySequence& seq);
    QKeySequence keySequence() const { return m_seq; }

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    QKeySequence m_seq;
};

/**
 * @brief 设置窗口
 */
class SettingsWindow : public FramelessDialog {
    Q_OBJECT
public:
    explicit SettingsWindow(QWidget* parent = nullptr);

private slots:
    void onCategoryChanged(int index);
    void onSaveClicked();
    void onRestoreDefaults();
    
    // 安全设置相关
    void onSetPassword();
    void onModifyPassword();
    void onRemovePassword();
    void updateSecurityUI();

    // 软件激活相关
    void onVerifySecretKey();

    // 截图设置相关
    void onBrowsePath();

    // 设备信息相关
    void onCopyDeviceInfo();

private:
    void initUi();
    void loadSettings();
    void adjustHeightToContent(bool animated = true);
    
    QWidget* createSecurityPage();
    QWidget* createGlobalHotkeyPage();
    QWidget* createAppShortcutPage();
    QWidget* createScreenshotPage();
    QWidget* createGeneralPage();
    QWidget* createActivationPage();
    QWidget* createDeviceInfoPage();

    QListWidget* m_navBar;
    QStackedWidget* m_contentStack;
    QPropertyAnimation* m_heightAnim = nullptr;

    // 安全设置组件
    QPushButton* m_btnSetPwd;
    QPushButton* m_btnModifyPwd;
    QPushButton* m_btnRemovePwd;
    QLabel* m_lblPwdStatus;
    class QCheckBox* m_checkIdleLock;
    class QPlainTextEdit* m_editAvoidanceBlacklist;

    // 全局热键组件
    HotkeyEdit* m_hkQuickWin;
    HotkeyEdit* m_hkFavorite;
    HotkeyEdit* m_hkScreenshot;
    HotkeyEdit* m_hkOcr;
    HotkeyEdit* m_hkAcquire;
    HotkeyEdit* m_hkLock;
    HotkeyEdit* m_hkPurePaste;
    HotkeyEdit* m_hkToolbox;

    // 截图设置组件
    QLineEdit* m_editScreenshotPath;
    class QCheckBox* m_checkOcrAutoCopy;
    class QCheckBox* m_checkSilentCapture;

    // 通用设置组件
    class QCheckBox* m_checkAutoStart;
    class QCheckBox* m_checkCapsLockToEnter;
    class QCheckBox* m_checkFireworks;
    class QCheckBox* m_checkCopyToolTip;
    class QPlainTextEdit* m_editBrowserExes;

    // 软件激活组件
    QLineEdit* m_editSecretKey;

    // 设备信息组件
    class QPlainTextEdit* m_editDeviceInfo;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
};

#endif // SETTINGSWINDOW_H
