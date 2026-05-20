#include "HotkeyManager.h"
#include <QCoreApplication>
#include <QDebug>
#include <QSettings>
#include "../ui/StringUtils.h"

HotkeyManager& HotkeyManager::instance() {
    static HotkeyManager inst;
    return inst;
}

HotkeyManager::HotkeyManager(QObject* parent) : QObject(parent) {
    qApp->installNativeEventFilter(this);

    // [NEW] 注册焦点变化回调，实现热键动态开关。
    // 当检测到窗口切换时，立即重新评估是否需要注册 Ctrl+S 热键。
    StringUtils::setFocusCallback([this](bool isBrowser){
        // qDebug() << "[HotkeyManager] 收到焦点切换通知，浏览器活跃状态:" << isBrowser;
        this->reapplyHotkeys();
    });
}

HotkeyManager::~HotkeyManager() {
    // 退出时取消所有注册
}

bool HotkeyManager::registerHotkey(int id, uint modifiers, uint vk) {
#ifdef Q_OS_WIN
    if (RegisterHotKey(nullptr, id, modifiers, vk)) {
        return true;
    }
    
    QString keyDesc = QString("ID=%1").arg(id);
    if (id == 1) keyDesc = "Alt+Space (快速窗口)";
    else if (id == 2) keyDesc = "Ctrl+Shift+E (全局收藏)";
    // 用户要求：全能截屏由 Ctrl+Alt+A 修改为 Alt+X
    else if (id == 3) keyDesc = "Alt+X (全局截屏)";
    else if (id == 4) keyDesc = "Ctrl+S (全局采集)";
    else if (id == 5) keyDesc = "Ctrl+Shift+Alt+S (全局锁定)";
    // 用户要求：截图取文 (OCR) 由 Ctrl+Alt+Q 修改为 Alt+C
    else if (id == 6) keyDesc = "Alt+C (截图取文)";
    // 用户要求：工具箱由应用级升级为全局热键 Ctrl+Shift+T
    else if (id == 8) keyDesc = "Ctrl+Shift+T (全局工具箱)";
    else if (id == 9) keyDesc = "Alt+A (灵感连击菜单)";

    qWarning().noquote() << QString("[HotkeyManager] 注册热键失败: %1 (错误代码: %2). 该快捷键可能已被系统或其他软件占用。")
                            .arg(keyDesc).arg(GetLastError());
#endif
    return false;
}

void HotkeyManager::unregisterHotkey(int id) {
#ifdef Q_OS_WIN
    if (UnregisterHotKey(nullptr, id)) {
        // 用户要求：暂时注释掉此处的注销成功日志
        // qDebug() << "[HotkeyManager] 成功注销热键 ID:" << id;
    }
#endif
}

void HotkeyManager::reapplyHotkeys() {
    QSettings hotkeys("RapidNotes", "Hotkeys");
    
    // 注销旧热键
    unregisterHotkey(1);
    unregisterHotkey(2);
    unregisterHotkey(3);
    unregisterHotkey(4);
    unregisterHotkey(5);
    unregisterHotkey(6);
    unregisterHotkey(7);
    unregisterHotkey(8);
    unregisterHotkey(9);
    
    // 注册新热键（带默认值）
    uint q_mods = hotkeys.value("quickWin_mods", 0x0001).toUInt();  // Alt
    uint q_vk   = hotkeys.value("quickWin_vk", 0x20).toUInt();     // Space
    registerHotkey(1, q_mods, q_vk);
    
    uint f_mods = hotkeys.value("favorite_mods", 0x0002 | 0x0004).toUInt(); // Ctrl+Shift
    uint f_vk   = hotkeys.value("favorite_vk", 0x45).toUInt();              // E
    registerHotkey(2, f_mods, f_vk);
    
    // 用户要求：全能截屏由 Ctrl+Alt+A 修改为 Alt+X
    uint s_mods = hotkeys.value("screenshot_mods", 0x0001).toUInt(); // Alt
    uint s_vk   = hotkeys.value("screenshot_vk", 0x58).toUInt();     // X
    registerHotkey(3, s_mods, s_vk);

    // [CRITICAL] 仅在浏览器激活且非本应用聚焦时，才注册 Ctrl+S 全局采集。
    // 之前版本中，由于全局热键 ID 4 始终在浏览器激活时接管 Ctrl+S，
    // 导致在本应用内部尝试锁定分类时，Ctrl+S 被 OS 拦截无法进入应用事件循环。
    uint a_mods = hotkeys.value("acquire_mods", 0x0002).toUInt();  // Ctrl
    uint a_vk   = hotkeys.value("acquire_vk", 0x53).toUInt();      // S
    
    bool isOwnAppFocused = false;
#ifdef Q_OS_WIN
    DWORD foregroundPid;
    GetWindowThreadProcessId(GetForegroundWindow(), &foregroundPid);
    isOwnAppFocused = (foregroundPid == GetCurrentProcessId());
#endif

    if (StringUtils::isBrowserActive() && !isOwnAppFocused) {
        if (registerHotkey(4, a_mods, a_vk)) {
            qDebug() << "[HotkeyManager] 为浏览器注册采集热键 (Ctrl+S)。";
        }
    } else {
        unregisterHotkey(4);
        qDebug() << "[HotkeyManager] 本应用聚焦(" << isOwnAppFocused << ")或非浏览器，释放 Ctrl+S 通道。";
    }

    uint l_mods = hotkeys.value("lock_mods", 0x0001 | 0x0002 | 0x0004).toUInt(); // Alt+Ctrl+Shift
    uint l_vk   = hotkeys.value("lock_vk", 0x53).toUInt();                      // S
    // 强制先释放 ID 5，防止重复注册导致的 1409 错误
    unregisterHotkey(5);
    registerHotkey(5, l_mods, l_vk);

    // 用户要求：截图取文 (OCR) 由 Ctrl+Alt+Q 修改为 Alt+C
    uint ocr_mods = hotkeys.value("ocr_mods", 0x0001).toUInt();    // Alt
    uint ocr_vk   = hotkeys.value("ocr_vk", 0x43).toUInt();         // C
    registerHotkey(6, ocr_mods, ocr_vk);

    uint p_mods = hotkeys.value("purePaste_mods", 0x0002 | 0x0004).toUInt(); // Ctrl+Shift
    uint p_vk   = hotkeys.value("purePaste_vk", 0x56).toUInt();              // V
    registerHotkey(7, p_mods, p_vk);

    // 用户要求：工具箱由应用级升级为全局热键 Ctrl+Shift+T
    uint t_mods = hotkeys.value("toolbox_mods", 0x0002 | 0x0004).toUInt(); // Ctrl+Shift
    uint t_vk   = hotkeys.value("toolbox_vk", 0x54).toUInt();              // T
    registerHotkey(8, t_mods, t_vk);

    // 2026-03-20 [NEW] 灵感连击菜单全局热键 Alt+A
    uint c_mods = hotkeys.value("contextMenu_mods", 0x0001).toUInt(); // Alt
    uint c_vk   = hotkeys.value("contextMenu_vk", 0x41).toUInt();     // A
    registerHotkey(9, c_mods, c_vk);
    
    // qDebug() << "[HotkeyManager] 所有系统热键已重新评估并应用。";
}

bool HotkeyManager::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) {
#ifdef Q_OS_WIN
    if (eventType == "windows_generic_MSG") {
        MSG* msg = static_cast<MSG*>(message);
        if (msg->message == WM_HOTKEY) {
            int id = static_cast<int>(msg->wParam);
            qDebug() << "[HotkeyManager] 底层捕获到系统热键 ID:" << id << (id == 4 ? " (Ctrl+S 采集抢占)" : "");
            emit hotkeyPressed(id);
            return true;
        }
    }
#endif
    return false;
}
