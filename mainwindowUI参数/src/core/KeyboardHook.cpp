#include "KeyboardHook.h"
#include <QDebug>

#ifdef Q_OS_WIN
HHOOK g_hHook = nullptr;
#endif

KeyboardHook& KeyboardHook::instance() {
    static KeyboardHook inst;
    return inst;
}

KeyboardHook::KeyboardHook() {}

KeyboardHook::~KeyboardHook() {
    stop();
}

void KeyboardHook::start() {
#ifdef Q_OS_WIN
    if (g_hHook) return;
    g_hHook = SetWindowsHookEx(WH_KEYBOARD_LL, HookProc, GetModuleHandle(NULL), 0);
    if (g_hHook) {
        m_active = true;
        // qDebug() << "Keyboard hook started";
    }
#endif
}

void KeyboardHook::stop() {
#ifdef Q_OS_WIN
    if (g_hHook) {
        UnhookWindowsHookEx(g_hHook);
        g_hHook = nullptr;
        m_active = false;
        qDebug() << "Keyboard hook stopped";
    }
#endif
}

#ifdef Q_OS_WIN
LRESULT CALLBACK KeyboardHook::HookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* pKey = (KBDLLHOOKSTRUCT*)lParam;
        
        // 忽略所有模拟按键，防止无限循环
        if (pKey->flags & LLKHF_INJECTED) {
            return CallNextHookEx(g_hHook, nCode, wParam, lParam);
        }

        bool isKeyDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        bool isKeyUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);

        // [USER_REQUEST] 锁定应用热键增强拦截逻辑：物理级拦截 Ctrl+Shift+Alt+S
        // 由于 RegisterHotKey 优先级可能低于某些截图软件(如PixPin)的钩子，此处采用 LL_HOOK 强制抢占并吞掉消息
        if (pKey->vkCode == 'S' && isKeyDown) {
            bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000);
            bool shift = (GetKeyState(VK_SHIFT) & 0x8000);
            bool alt = (GetKeyState(VK_MENU) & 0x8000);

            if (ctrl && shift && alt) {
                // 异步触发锁定，避免在钩子回调中直接执行耗时UI操作
                QMetaObject::invokeMethod(&KeyboardHook::instance(), "globalLockRequested", Qt::QueuedConnection);
                return 1; // 强制拦截，防止透传给 PixPin 或系统
            }
        }

        // [NEW] CapsLock 映射 Enter 拦截逻辑 (添加 Ctrl 组合键作为白名单释放)
        if (KeyboardHook::instance().m_capsLockToEnterEnabled && pKey->vkCode == VK_CAPITAL) {
            bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000);
            if (!ctrlPressed) {
                if (isKeyDown) {
                    // 模拟按下 Enter
                    keybd_event(VK_RETURN, 0, 0, 0);
                } else if (isKeyUp) {
                    // 模拟弹起 Enter
                    keybd_event(VK_RETURN, 0, KEYEVENTF_KEYUP, 0);
                }
                // 返回 1 拦截原始 CapsLock 事件，阻止大小写切换
                return 1;
            } else {
                // 如果按下了 Ctrl，则需要欺骗 Windows，单纯发一个 CapsLock 切换信号
                if (isKeyDown) {
                    // 1. 模拟抬起外层按着的 Ctrl 键
                    keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
                    // 2. 模拟按下纯粹的 CapsLock
                    keybd_event(VK_CAPITAL, 0, 0, 0);
                    // 3. 模拟弹起纯粹的 CapsLock（完成状态切换）
                    keybd_event(VK_CAPITAL, 0, KEYEVENTF_KEYUP, 0);
                    // 4. 将 Ctrl 键被按下的状态重新恢复给系统
                    keybd_event(VK_CONTROL, 0, 0, 0);
                }
                // 返回 1 拦截这次错误的 "Ctrl+CapsLock" 原始组合消息，我们已经在上面自造了完整状态事件
                return 1;
            }
        }


        // 工具箱数字拦截 (仅在使能时触发)
        if (KeyboardHook::instance().m_digitInterceptEnabled) {
            if (pKey->vkCode >= 0x30 && pKey->vkCode <= 0x39) {
                if (isKeyDown) {
                    int digit = pKey->vkCode - 0x30;
                    qDebug() << "Digit pressed:" << digit;
                    emit KeyboardHook::instance().digitPressed(digit);
                }
                // 按下和弹起都拦截
                return 1;
            }
        }
    }
    return CallNextHookEx(g_hHook, nCode, wParam, lParam);
}
#endif
