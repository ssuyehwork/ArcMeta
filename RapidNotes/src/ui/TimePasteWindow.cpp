#include "TimePasteWindow.h"
#include "IconHelper.h"
#include "../core/KeyboardHook.h"
#include <QDateTime>
#include <QMouseEvent>
#include <QApplication>
#include <QClipboard>
#include <QPushButton>
#include <QPainter>
#include <QPainterPath>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

TimePasteWindow::TimePasteWindow(QWidget* parent) : FramelessDialog("时间输出工具", parent) {
    setObjectName("TimePasteWindow");
    setFixedSize(380, 330); 

    loadWindowSettings();
    initUI();

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &TimePasteWindow::updateDateTime);
    m_timer->start(100);
    updateDateTime();

    // 使用 QueuedConnection 确保钩子回调立即返回，避免阻塞导致按键泄漏
    connect(&KeyboardHook::instance(), &KeyboardHook::digitPressed, this, &TimePasteWindow::onDigitPressed, Qt::QueuedConnection);
}

TimePasteWindow::~TimePasteWindow() {
}

void TimePasteWindow::initUI() {
    auto* layout = new QVBoxLayout(m_contentArea);
    layout->setContentsMargins(20, 10, 20, 20);
    layout->setSpacing(10);

    m_dateLabel = new QLabel();
    m_dateLabel->setAlignment(Qt::AlignCenter);
    m_dateLabel->setStyleSheet("color: #B0B0B0; font-size: 16px; padding: 5px;");
    layout->addWidget(m_dateLabel);

    m_timeLabel = new QLabel();
    m_timeLabel->setAlignment(Qt::AlignCenter);
    m_timeLabel->setStyleSheet("color: #E0E0E0; font-size: 28px; font-weight: bold; padding: 5px; font-family: 'Consolas', 'Monaco', monospace;");
    layout->addWidget(m_timeLabel);

    auto* sep = new QLabel();
    sep->setFixedHeight(2);
    sep->setStyleSheet("background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 transparent, stop:0.5 #555555, stop:1 transparent);");
    layout->addWidget(sep);

    m_buttonGroup = new QButtonGroup(this);
    m_radioPrev = new QRadioButton("退 (往前 N 分钟)");
    m_radioPrev->setChecked(true);
    m_radioPrev->setStyleSheet(getRadioStyle());
    m_buttonGroup->addButton(m_radioPrev, 0);
    layout->addWidget(m_radioPrev);

    m_radioNext = new QRadioButton("进 (往后 N 分钟)");
    m_radioNext->setStyleSheet(getRadioStyle());
    m_buttonGroup->addButton(m_radioNext, 1);
    layout->addWidget(m_radioNext);

    auto* tip = new QLabel("按主键盘数字键 0-9 输出时间");
    tip->setAlignment(Qt::AlignCenter);
    tip->setStyleSheet("color: #666666; font-size: 11px; padding: 5px;");
    layout->addWidget(tip);

}

QString TimePasteWindow::getRadioStyle() {
    return "QRadioButton { color: #E0E0E0; font-size: 14px; padding: 6px; spacing: 8px; } "
           "QRadioButton::indicator { width: 18px; height: 18px; border-radius: 9px; border: 2px solid #555555; background: #2A2A2A; } "
           "QRadioButton::indicator:checked { background: qradialgradient(cx:0.5, cy:0.5, radius:0.5, fx:0.5, fy:0.5, stop:0 #4A9EFF, stop:0.7 #4A9EFF, stop:1 #2A2A2A); border: 2px solid #4A9EFF; } "
           "QRadioButton::indicator:hover { border: 2px solid #4A9EFF; }";
}

void TimePasteWindow::updateDateTime() {
    QDateTime now = QDateTime::currentDateTime();
    m_dateLabel->setText(now.toString("yyyy-MM-dd"));
    m_timeLabel->setText(now.toString("HH:mm:ss"));
}

void TimePasteWindow::onDigitPressed(int digit) {
    if (!isVisible()) return;

    QDateTime target = QDateTime::currentDateTime();
    if (m_radioPrev->isChecked())
        target = target.addSecs(-digit * 60);
    else
        target = target.addSecs(digit * 60);
    
    QString timeStr = target.toString("HH:mm");

    // 1. 立即更新剪贴板（满足用户同步需求）
    QApplication::clipboard()->setText(timeStr);

    // 2. 异步延迟处理，确保系统剪贴板通知完成且焦点稳定
    QTimer::singleShot(100, this, [timeStr]() {
#ifdef Q_OS_WIN
        // A. 显式释放所有修饰键 (L/R Ctrl, Shift, Alt, Win)，防止干扰模拟输入
        INPUT releaseInputs[8];
        memset(releaseInputs, 0, sizeof(releaseInputs));
        BYTE keys[] = { VK_LCONTROL, VK_RCONTROL, VK_LSHIFT, VK_RSHIFT, VK_LMENU, VK_RMENU, VK_LWIN, VK_RWIN };
        for (int i = 0; i < 8; ++i) {
            releaseInputs[i].type = INPUT_KEYBOARD;
            releaseInputs[i].ki.wVk = keys[i];
            releaseInputs[i].ki.dwFlags = KEYEVENTF_KEYUP;
        }
        SendInput(8, releaseInputs, sizeof(INPUT));

        // B. 使用 Unicode 方式模拟打字输入 (比 Ctrl+V 稳定，不产生剪贴板竞争)
        int len = timeStr.length();
        QVector<INPUT> inputs(len * 2);
        for (int i = 0; i < len; ++i) {
            inputs[i*2].type = INPUT_KEYBOARD;
            inputs[i*2].ki.wVk = 0;
            inputs[i*2].ki.wScan = timeStr[i].unicode();
            inputs[i*2].ki.dwFlags = KEYEVENTF_UNICODE;
            
            inputs[i*2+1] = inputs[i*2];
            inputs[i*2+1].ki.dwFlags |= KEYEVENTF_KEYUP;
        }
        SendInput(inputs.size(), inputs.data(), sizeof(INPUT));
#endif
    });
}

void TimePasteWindow::showEvent(QShowEvent* event) {
    FramelessDialog::showEvent(event);
    KeyboardHook::instance().setDigitInterceptEnabled(true);

#ifdef Q_OS_WIN
    // 设置 WS_EX_NOACTIVATE 使得点击窗口时（如切换加减模式）不会夺取当前编辑器的焦点
    HWND hwnd = (HWND)winId();
    SetWindowLong(hwnd, GWL_EXSTYLE, GetWindowLong(hwnd, GWL_EXSTYLE) | WS_EX_NOACTIVATE);
#endif
}

void TimePasteWindow::hideEvent(QHideEvent* event) {
    KeyboardHook::instance().setDigitInterceptEnabled(false);
    FramelessDialog::hideEvent(event);
}
