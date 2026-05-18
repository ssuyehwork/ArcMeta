#include "SettingsWindow.h"
#include "CategoryPasswordDialog.h"
#include "../core/HotkeyManager.h"
#include "../core/ShortcutManager.h"
#include <QHBoxLayout>
#include <QSettings>
#include <functional>
#include <QFileDialog>
#include <QScrollArea>
#include <QApplication>
#include <QInputDialog>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QFileInfo>
#include <QSysInfo>
#include "../core/ClipboardMonitor.h"
#include "ToolTipOverlay.h"
#include "../core/DatabaseManager.h"
#include "../core/KeyboardHook.h"
#include <QClipboard>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// --- HotkeyEdit 实现 ---
HotkeyEdit::HotkeyEdit(QWidget* parent) : QLineEdit(parent) {
    setReadOnly(true);
    setPlaceholderText("按键设置...");
    setAlignment(Qt::AlignCenter);
    setStyleSheet("QLineEdit { background: #1a1a1a; color: #4a90e2; font-weight: bold; border-radius: 4px; padding: 4px; border: 1px solid #333; }");
}

void HotkeyEdit::setKeyData(uint mods, uint vk) {
    m_mods = mods;
    m_vk = vk;
    setText(keyToString(mods, vk));
}

void HotkeyEdit::keyPressEvent(QKeyEvent* event) {
    int key = event->key();
    if (key == Qt::Key_Escape || key == Qt::Key_Backspace) {
        m_mods = 0;
        m_vk = 0;
        setText("");
        return;
    }

    if (key >= Qt::Key_Control && key <= Qt::Key_Meta) return;

    uint winMods = 0;
    if (event->modifiers() & Qt::ControlModifier) winMods |= 0x0002; // MOD_CONTROL
    if (event->modifiers() & Qt::AltModifier)     winMods |= 0x0001; // MOD_ALT
    if (event->modifiers() & Qt::ShiftModifier)   winMods |= 0x0004; // MOD_SHIFT
    if (event->modifiers() & Qt::MetaModifier)    winMods |= 0x0008; // MOD_WIN

    m_mods = winMods;
    m_vk = event->nativeVirtualKey();
    if (m_vk == 0) m_vk = key; // 兜底处理

    setText(keyToString(m_mods, m_vk));
}

QString HotkeyEdit::keyToString(uint mods, uint vk) {
    if (vk == 0) return "";
    QStringList parts;
    if (mods & 0x0002) parts << "Ctrl";
    if (mods & 0x0001) parts << "Alt";
    if (mods & 0x0004) parts << "Shift";
    if (mods & 0x0008) parts << "Win";
    
    // 简单模拟 VK 到 字符串转换
    QKeySequence ks(vk);
    parts << ks.toString();
    return parts.join(" + ");
}

// --- ShortcutEdit 实现 ---
ShortcutEdit::ShortcutEdit(QWidget* parent) : QLineEdit(parent) {
    setReadOnly(true);
    setPlaceholderText("录制快捷键...");
}

void ShortcutEdit::setKeySequence(const QKeySequence& seq) {
    m_seq = seq;
    setText(m_seq.toString());
}

void ShortcutEdit::keyPressEvent(QKeyEvent* event) {
    int key = event->key();
    if (key == Qt::Key_Escape || key == Qt::Key_Backspace) {
        m_seq = QKeySequence();
        setText("");
        return;
    }
    if (key >= Qt::Key_Control && key <= Qt::Key_Meta) return;

    m_seq = QKeySequence(event->modifiers() | key);
    setText(m_seq.toString());
}

// --- SettingsWindow 实现 ---
SettingsWindow::SettingsWindow(QWidget* parent)
    : FramelessDialog("系统设置", parent)
{
    // 移除 setFixedSize，改为自适应宽度，锁定最小高度
    setFixedWidth(700);
    setMinimumHeight(400);
    
    initUi();
    loadSettings();

    // 初始调整一次高度
    QTimer::singleShot(50, [this]() { adjustHeightToContent(false); });
}

void SettingsWindow::initUi() {
    auto* mainLayout = new QHBoxLayout(m_contentArea);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 左侧导航
    m_navBar = new QListWidget();
    m_navBar->setFixedWidth(160);
    m_navBar->setSpacing(0);
    m_navBar->setStyleSheet(
        "QListWidget { background-color: #1e1e1e; border: none; border-right: 1px solid #333; outline: none; padding: 0px; }"
        "QListWidget::item { height: 40px; min-height: 40px; max-height: 40px; padding: 0px; padding-left: 15px; margin: 0px; color: #aaa; border: none; }"
        "QListWidget::item:selected { background-color: #3e3e42; color: #3a90ff; border-left: 3px solid #3a90ff; }" // 2026-03-xx 统一选中色
        "QListWidget::item:hover { background-color: #3e3e42; }" // 2026-03-xx 统一悬停色
    );
    
    // 2026-03-xx 按照用户最高要求：彻底移除软件激活与设备信息页面
    QStringList categories = {"安全设置", "全局热键", "局内快捷键", "截图设置", "通用设置"};
    m_navBar->addItems(categories);
    connect(m_navBar, &QListWidget::currentRowChanged, this, &SettingsWindow::onCategoryChanged);

    // 右侧内容
    m_contentStack = new QStackedWidget();
    m_contentStack->addWidget(createSecurityPage());
    m_contentStack->addWidget(createGlobalHotkeyPage());
    m_contentStack->addWidget(createAppShortcutPage());
    m_contentStack->addWidget(createScreenshotPage());
    m_contentStack->addWidget(createGeneralPage());

    // 2026-03-xx [CORE-FIX] 引入 QScrollArea 包裹 StackedWidget。
    // 之前直接将内容塞入布局，当页面内容过长时会导致滚动失效及高度计算混乱。
    auto* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setWidget(m_contentStack);
    scrollArea->setStyleSheet("QScrollArea { background: transparent; } QScrollBar:vertical { width: 8px; background: transparent; }");

    auto* rightLayout = new QVBoxLayout();
    rightLayout->setContentsMargins(20, 20, 20, 20);
    rightLayout->setSpacing(25); 
    rightLayout->addWidget(scrollArea, 1);
    
    // 底部按钮
    auto* btnLayout = new QHBoxLayout();
    
    auto* btnRestore = new QPushButton("恢复默认设置");
    btnRestore->setFixedSize(120, 36);
    btnRestore->setStyleSheet("QPushButton { background: #444; color: #ccc; border-radius: 4px; font-weight: normal; }"
                              "QPushButton:hover { background: #555; color: white; }");
    connect(btnRestore, &QPushButton::clicked, this, &SettingsWindow::onRestoreDefaults);
    btnLayout->addWidget(btnRestore);

    btnLayout->addStretch();
    auto* btnSave = new QPushButton("保存并生效");
    btnSave->setFixedSize(120, 36);
    btnSave->setStyleSheet("QPushButton { background: #3a90ff; color: white; border-radius: 4px; font-weight: bold; }"
                           "QPushButton:hover { background: #2b7ae6; }");
    connect(btnSave, &QPushButton::clicked, this, &SettingsWindow::onSaveClicked);
    btnLayout->addWidget(btnSave);
    rightLayout->addLayout(btnLayout);

    mainLayout->addWidget(m_navBar);
    mainLayout->addLayout(rightLayout);
    
    m_navBar->setCurrentRow(0);
}

QWidget* SettingsWindow::createSecurityPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0); // 移除冗余边距，由父布局统一控制
    layout->setSpacing(20);

    m_lblPwdStatus = new QLabel("当前状态：未设置锁定窗口密码");
    m_lblPwdStatus->setStyleSheet("color: #888; font-size: 14px;");
    layout->addWidget(m_lblPwdStatus);

    m_btnSetPwd = new QPushButton("设置锁定窗口密码");
    m_btnModifyPwd = new QPushButton("修改启动密码");
    m_btnRemovePwd = new QPushButton("彻底移除密码");

    QString btnStyle = "QPushButton { height: 40px; background: #2d2d2d; color: #eee; border: 1px solid #444; border-radius: 6px; }"
                       "QPushButton:hover { background: #3d3d3d; }";
    m_btnSetPwd->setStyleSheet(btnStyle);
    m_btnModifyPwd->setStyleSheet(btnStyle);
    m_btnRemovePwd->setStyleSheet("QPushButton { height: 40px; background: #442222; color: #f66; border: 1px solid #633; border-radius: 6px; }"
                                  "QPushButton:hover { background: #552222; }");

    connect(m_btnSetPwd, &QPushButton::clicked, this, &SettingsWindow::onSetPassword);
    connect(m_btnModifyPwd, &QPushButton::clicked, this, &SettingsWindow::onModifyPassword);
    connect(m_btnRemovePwd, &QPushButton::clicked, this, &SettingsWindow::onRemovePassword);

    layout->addWidget(m_btnSetPwd);
    layout->addWidget(m_btnModifyPwd);
    layout->addWidget(m_btnRemovePwd);

    layout->addSpacing(10);
    m_checkIdleLock = new QCheckBox("30秒全系统闲置后自动锁定应用");
    m_checkIdleLock->setStyleSheet("color: #ccc; font-size: 14px;");
    
    // 2026-03-xx 按照用户要求：取消勾选自动锁定功能时需要进行密码验证
    connect(m_checkIdleLock, &QCheckBox::clicked, this, [this](bool checked) {
        if (!checked) {
            // 尝试取消勾选
            QSettings settings("RapidNotes", "QuickWindow");
            QString realPwd = settings.value("appPassword").toString();
            
            // 如果没设密码，允许直接关闭（虽然逻辑上自锁需要密码，但防御性处理）
            if (realPwd.isEmpty()) return;

            bool ok = false;
            QString input = QInputDialog::getText(this, "身份验证", 
                                                  "关闭自动锁定功能需要验证密码：", 
                                                  QLineEdit::Password, "", &ok);
            
            if (!ok || input != realPwd) {
                // 验证失败或取消，强制恢复勾选
                m_checkIdleLock->setChecked(true);
                if (ok) {
                    ToolTipOverlay::instance()->showText(QCursor::pos(), 
                        "<b style='color: #e74c3c;'>❌ 密码验证失败</b>");
                }
            } else {
                ToolTipOverlay::instance()->showText(QCursor::pos(), 
                    "<b style='color: #2ecc71;'>✅ 身份验证通过</b>");
            }
        }
    });
    
    layout->addWidget(m_checkIdleLock);

    layout->addSpacing(20);
    layout->addWidget(new QLabel("进程级避让黑名单 (在此类应用中停止自动采集)："));
    m_editAvoidanceBlacklist = new QPlainTextEdit();
    m_editAvoidanceBlacklist->setPlaceholderText("例如:\nKeePass.exe\nBitwarden.exe\nbank_app.exe");
    m_editAvoidanceBlacklist->setStyleSheet("QPlainTextEdit { background: #1a1a1a; color: #eee; border: 1px solid #333; border-radius: 4px; padding: 5px; }");
    m_editAvoidanceBlacklist->setFixedHeight(100);
    layout->addWidget(m_editAvoidanceBlacklist);
    layout->addWidget(new QLabel("<span style='color: #666; font-size: 11px;'>提示：当上述进程处于活动窗口时，软件将暂停剪贴板监控以保护隐私。</span>"));

    layout->addStretch();
    return page;
}

QWidget* SettingsWindow::createActivationPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(15);

    QVariantMap trialStatus = DatabaseManager::instance().getTrialStatus();
    bool isActive = trialStatus["is_activated"].toBool();

    if (isActive) {
        layout->addWidget(new QLabel("软件激活："));
        
        QString code = trialStatus["activation_code"].toString();
        QString masked = "";
        for (int i = 0; i < code.length(); ++i) {
            if (code[i] == '-') masked += '-';
            else if (i < 2 || (i >= 6 && i <= 7) || i >= code.length() - 4) masked += code[i];
            else masked += '*';
        }
        if (masked.isEmpty()) masked = "CA****82-****-********E25C";
        
        auto* lblActivated = new QLabel(QString("<div align='center'><b style='color: #2ecc71; font-size: 16px;'>✅ 已成功激活</b><br><br><span style='color: #a0a0a0; font-size: 16px; font-family: monospace; letter-spacing: 2px;'>%1</span></div>").arg(masked));
        lblActivated->setAlignment(Qt::AlignCenter);
        lblActivated->setStyleSheet("background: #1a1a1a; border: 1px solid #2ecc71; border-radius: 4px; padding: 20px;");
        layout->addWidget(lblActivated);
        
        auto* lblThanks = new QLabel("感谢您的支持！");
        lblThanks->setAlignment(Qt::AlignCenter);
        lblThanks->setStyleSheet("color: #aaa; font-size: 13px; margin-top: 5px;");
        layout->addWidget(lblThanks);

        layout->addSpacing(30);
        auto* btnReset = new QPushButton("重置当前设备授权");
        btnReset->setFixedSize(200, 40);
        btnReset->setStyleSheet("QPushButton { background: #442222; color: #f66; border: 1px solid #633; border-radius: 6px; font-weight: bold; }"
                                "QPushButton:hover { background: #552222; }");
        
        // 2026-03-xx 按照用户要求：在已激活界面添加重置功能按钮
        connect(btnReset, &QPushButton::clicked, this, [this]() {
            bool ok = false;
            QString input = QInputDialog::getText(this, "安全确认", 
                                                  "确认要重置本设备的激活状态吗？\n重置后需重新输入激活码。\n请输入“RESET”以继续：", 
                                                  QLineEdit::Normal, "", &ok);
            
            if (ok && input == "RESET") {
                DatabaseManager::instance().resetActivation();
                ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>✅ 授权已成功重置，程序即将退出</b>", 1500);
                
                // 重置后强制退出程序，以确保内存中的授权状态完全刷新
                QTimer::singleShot(1500, []() {
                    qApp->exit(0);
                });
            }
        });

        auto* resetContainer = new QHBoxLayout();
        resetContainer->addStretch();
        resetContainer->addWidget(btnReset);
        resetContainer->addStretch();
        layout->addLayout(resetContainer);
        
        layout->addStretch();
        return page;
    }

    layout->addWidget(new QLabel("软件激活："));
    
    m_editSecretKey = new QLineEdit();
    m_editSecretKey->installEventFilter(this);
    m_editSecretKey->setEchoMode(QLineEdit::Password);
    m_editSecretKey->setPlaceholderText("请输入激活密钥...");
    m_editSecretKey->setStyleSheet("QLineEdit { height: 36px; padding: 0 10px; background: #1a1a1a; color: #fff; border: 1px solid #333; border-radius: 4px; }");
    layout->addWidget(m_editSecretKey);

    // 2026-03-xx 按照用户要求：正版化彻底移除“激活尝试次数”的所有相关显示

    auto* btnActivate = new QPushButton("立即激活");
    btnActivate->setFixedHeight(40);
    btnActivate->setStyleSheet("QPushButton { background: #3a90ff; color: white; border-radius: 4px; font-weight: bold; }"
                               "QPushButton:hover { background: #2b7ae6; }");
    connect(btnActivate, &QPushButton::clicked, this, &SettingsWindow::onVerifySecretKey);
    layout->addWidget(btnActivate);

    auto* lblContact = new QLabel("联系激活：<b style='color: #4a90e2;'>Telegram：TLG_888</b>");
    lblContact->setAlignment(Qt::AlignCenter);
    lblContact->setStyleSheet("color: #aaa; font-size: 13px; margin-top: 5px;");
    layout->addWidget(lblContact);

    layout->addWidget(new QLabel("<span style='color: #666; font-size: 11px;'>提示：输入正确的密钥并激活。</span>"));

    layout->addStretch();
    return page;
}



QWidget* SettingsWindow::createDeviceInfoPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(15);

    layout->addWidget(new QLabel("当前设备指纹信息 (用于软件激活绑定)："));

    m_editDeviceInfo = new QPlainTextEdit();
    m_editDeviceInfo->setReadOnly(true);
    m_editDeviceInfo->setStyleSheet("QPlainTextEdit { background: #1a1a1a; color: #3a90ff; border: 1px solid #333; border-radius: 4px; padding: 12px; font-family: 'Consolas', monospace; font-size: 13px; }");
    
    // 获取设备信息
    QString diskSn = "RAPID_MANAGER_DEVIC_DECOUPLED";
    QString machineId = QSysInfo::machineUniqueId();
    if (machineId.isEmpty()) machineId = QSysInfo::bootUniqueId();
    
    QString info = QString("Disk SN: %1\nMachine ID: %2\nOS: %3")
                   .arg(diskSn.isEmpty() ? "Unknown" : diskSn)
                   .arg(machineId.isEmpty() ? "Unknown" : machineId)
                   .arg(QSysInfo::prettyProductName());
    
    m_editDeviceInfo->setPlainText(info);
    layout->addWidget(m_editDeviceInfo);

    auto* btnCopy = new QPushButton("复制设备指纹信息");
    btnCopy->setFixedHeight(40);
    btnCopy->setStyleSheet("QPushButton { background: #2d2d2d; color: #eee; border: 1px solid #444; border-radius: 4px; font-weight: bold; }"
                            "QPushButton:hover { background: #3d3d3d; color: #fff; }");
    connect(btnCopy, &QPushButton::clicked, this, &SettingsWindow::onCopyDeviceInfo);
    layout->addWidget(btnCopy);

    auto* tip = new QLabel("提示：若激活遇到问题，请将上述信息复制并发送给管理员。");
    tip->setStyleSheet("color: #666; font-size: 12px;");
    layout->addWidget(tip);

    layout->addStretch();
    return page;
}

void SettingsWindow::onCopyDeviceInfo() {
    if (!m_editDeviceInfo) return;
    
    QString info = m_editDeviceInfo->toPlainText();
    QApplication::clipboard()->setText(info);
    
    ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>✅ 设备信息已成功复制到剪贴板</b>");
}

void SettingsWindow::onVerifySecretKey() {
    if (!m_editSecretKey) return;
    
    QString key = m_editSecretKey->text().trimmed();
    if (DatabaseManager::instance().verifyActivationCode(key)) {
        m_editSecretKey->clear();
        ToolTipOverlay::instance()->showText(QCursor::pos(), 
            "<b style='color: #2ecc71;'>✅ 激活成功，感谢支持！</b>", 700, QColor("#2ecc71"));
            
        // 成功激活后，将导航栏的"软件激活"项移除，并可能切换到另一个设置页（或关闭弹窗）
        // 简单处理：给用户文字提示，UI不再需要停留在激活输入界面
        auto* oldPage = m_contentStack->widget(5);
        
        // 移除旧控件并添加已激活文本
        QWidget* parent = m_editSecretKey->parentWidget();
        if (parent) {
            QVBoxLayout* layout = qobject_cast<QVBoxLayout*>(parent->layout());
            if (layout) {
                // 清理所有旧的子控件 (跳过第一个"软件激活：")
                QLayoutItem *child;
                while ((child = layout->takeAt(1)) != nullptr) {
                    if (child->widget()) {
                        child->widget()->deleteLater();
                    }
                    delete child;
                }
                
                // 设置为null防崩溃
                m_editSecretKey = nullptr;
                
                QString code = key;
                QString masked = "";
                for (int i = 0; i < code.length(); ++i) {
                    if (code[i] == '-') masked += '-';
                    else if (i < 2 || (i >= 6 && i <= 7) || i >= code.length() - 4) masked += code[i];
                    else masked += '*';
                }
                if (masked.isEmpty()) masked = "CA****82-****-********E25C";
                
                auto* lblActivated = new QLabel(QString("<div align='center'><b style='color: #2ecc71; font-size: 16px;'>✅ 已成功激活</b><br><br><span style='color: #a0a0a0; font-size: 16px; font-family: monospace; letter-spacing: 2px;'>%1</span></div>").arg(masked));
                lblActivated->setAlignment(Qt::AlignCenter);
                lblActivated->setStyleSheet("background: #1a1a1a; border: 1px solid #2ecc71; border-radius: 4px; padding: 20px;");
                layout->addWidget(lblActivated);
                
                auto* lblThanks = new QLabel("感谢您的支持！");
                lblThanks->setAlignment(Qt::AlignCenter);
                lblThanks->setStyleSheet("color: #aaa; font-size: 13px; margin-top: 5px;");
                layout->addWidget(lblThanks);
                
                layout->addStretch();
            }
        }
    } else {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e74c3c;'>[ERR] 密钥错误，激活失败</b>");
    }
}

QWidget* SettingsWindow::createGlobalHotkeyPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    
    auto addRow = [&](const QString& label, HotkeyEdit*& edit) {
        auto* hl = new QHBoxLayout();
        hl->addWidget(new QLabel(label));
        edit = new HotkeyEdit();
        edit->setFixedWidth(200);
        hl->addWidget(edit);
        layout->addLayout(hl);
    };

    layout->addWidget(new QLabel("系统全局热键，修改后点击保存立即生效："));
    layout->addSpacing(10);
    addRow("激活快速笔记窗口:", m_hkQuickWin);
    addRow("快速收藏/加星:", m_hkFavorite);
    addRow("截图功能:", m_hkScreenshot);
    addRow("截图取文 (OCR):", m_hkOcr);
    addRow("浏览器文本采集:", m_hkAcquire);
    addRow("全局锁定:", m_hkLock);
    addRow("全局纯净粘贴:", m_hkPurePaste);
    addRow("全局呼出工具箱:", m_hkToolbox);
    
    layout->addStretch();
    return page;
}

QWidget* SettingsWindow::createAppShortcutPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    
    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet("QScrollArea { border: none; background: transparent; }");
    
    auto* container = new QWidget();
    auto* vLayout = new QVBoxLayout(container);
    
    auto& sm = ShortcutManager::instance();
    QString currentCat = "";
    
    for (const auto& info : sm.getAllShortcuts()) {
        if (info.category != currentCat) {
            currentCat = info.category;
            auto* catLabel = new QLabel(currentCat);
            catLabel->setStyleSheet("color: #3a90ff; font-weight: bold; margin-top: 15px; border-bottom: 1px solid #333;");
            vLayout->addWidget(catLabel);
        }
        
        auto* row = new QHBoxLayout();
        row->addWidget(new QLabel(info.description));
        auto* edit = new ShortcutEdit();
        edit->setKeySequence(sm.getShortcut(info.id));
        edit->setProperty("id", info.id);
        edit->setFixedWidth(150);
        row->addWidget(edit);
        vLayout->addLayout(row);
    }
    
    scroll->setWidget(container);
    layout->addWidget(scroll);
    return page;
}

QWidget* SettingsWindow::createScreenshotPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    
    layout->addWidget(new QLabel("截图自动保存路径："));
    auto* row = new QHBoxLayout();
    m_editScreenshotPath = new QLineEdit();
    m_editScreenshotPath->installEventFilter(this);
    row->addWidget(m_editScreenshotPath);
    
    auto* btnBrowse = new QPushButton("浏览...");
    connect(btnBrowse, &QPushButton::clicked, this, &SettingsWindow::onBrowsePath);
    row->addWidget(btnBrowse);
    layout->addLayout(row);
    
    layout->addWidget(new QLabel("提示：若未设置，默认保存至程序目录下的 /RPN_screenshot"));

    layout->addSpacing(20);
    layout->addWidget(new QLabel("截图取文 (OCR) 设置："));
    m_checkOcrAutoCopy = new QCheckBox("OCR识别后自动复制并隐藏窗口");
    m_checkOcrAutoCopy->setStyleSheet("color: #ccc; font-size: 14px;");
    layout->addWidget(m_checkOcrAutoCopy);

    m_checkSilentCapture = new QCheckBox("开启静默采集模式 (采集时不弹出预览窗口)");
    m_checkSilentCapture->setStyleSheet("color: #ccc; font-size: 14px;");
    layout->addWidget(m_checkSilentCapture);

    layout->addStretch();
    return page;
}

QWidget* SettingsWindow::createGeneralPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(15);

    m_checkAutoStart = new QCheckBox("开机自动启动");
    m_checkAutoStart->setStyleSheet("color: #ccc; font-size: 14px;");
    layout->addWidget(m_checkAutoStart);

    layout->addSpacing(10);
    // [NEW] CapsLock 映射 Enter 选项
    m_checkCapsLockToEnter = new QCheckBox("将 CapsLock (大写锁定) 替代 Enter (回车键)");
    m_checkCapsLockToEnter->setStyleSheet("color: #ccc; font-size: 14px;");
    layout->addWidget(m_checkCapsLockToEnter);
    
    auto* capsTip = new QLabel("提示：开启后，单独按下 CapsLock 将触发回车键功能（全局生效）；\n若需要切换大写锁定状态，请使用组合键：Ctrl + CapsLock。");
    capsTip->setWordWrap(true);
    capsTip->setStyleSheet("color: #666; font-size: 12px; line-height: 1.4; padding-left: 24px;");
    layout->addWidget(capsTip);

    layout->addSpacing(10);
    // 2026-03-xx 按照用户要求：烟花特效与 ToolTip 提示实现单选逻辑（支持全不选）
    m_checkFireworks = new QCheckBox("启用复制烟花特效 (Ctrl + C 时触发)");
    m_checkFireworks->setStyleSheet("color: #ccc; font-size: 14px;");
    layout->addWidget(m_checkFireworks);

    m_checkCopyToolTip = new QCheckBox("启用复制 ToolTip 提示 (显示简短文字)");
    m_checkCopyToolTip->setStyleSheet("color: #ccc; font-size: 14px;");
    layout->addWidget(m_checkCopyToolTip);

    connect(m_checkFireworks, &QCheckBox::clicked, this, [this](bool checked){
        if (checked) m_checkCopyToolTip->setChecked(false);
    });
    connect(m_checkCopyToolTip, &QCheckBox::clicked, this, [this](bool checked){
        if (checked) m_checkFireworks->setChecked(false);
    });

    layout->addSpacing(20);
    layout->addWidget(new QLabel("浏览器采集进程白名单 (每行一个 .exe)："));
    m_editBrowserExes = new QPlainTextEdit();
    m_editBrowserExes->setPlaceholderText("例如:\nchrome.exe\nmsedge.exe");
    m_editBrowserExes->setStyleSheet("QPlainTextEdit { background: #1a1a1a; color: #eee; border: 1px solid #333; border-radius: 4px; padding: 5px; }");
    m_editBrowserExes->setFixedHeight(120);
    layout->addWidget(m_editBrowserExes);

    layout->addStretch();
    return page;
}

bool SettingsWindow::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (auto* edit = qobject_cast<QLineEdit*>(watched)) {
            if (watched->inherits("HotkeyEdit") || watched->inherits("ShortcutEdit")) {
                return FramelessDialog::eventFilter(watched, event);
            }
            if (keyEvent->key() == Qt::Key_Up) {
                edit->setCursorPosition(0);
                return true;
            } else if (keyEvent->key() == Qt::Key_Down) {
                edit->setCursorPosition(edit->text().length());
                return true;
            }
        }
    }
    return FramelessDialog::eventFilter(watched, event);
}

void SettingsWindow::onCategoryChanged(int index) {
    // 2026-03-xx [PERF-FIX] 移除导致卡顿的动态高度调整动画。
    // 改为固定高度 + 内部滚动模式。
    m_contentStack->setCurrentIndex(index);
}

void SettingsWindow::adjustHeightToContent(bool) {
    // 2026-03-xx [PERF-FIX] 彻底移除递归布局计算和动画，固定标准窗口尺寸。
    resize(700, 600);
}

void SettingsWindow::loadSettings() {
    // 1. 加载安全设置
    updateSecurityUI();
    QSettings securitySettings("RapidNotes", "Security");
    m_editAvoidanceBlacklist->setPlainText(securitySettings.value("avoidanceBlacklist").toStringList().join("\n"));
    m_checkIdleLock->setChecked(securitySettings.value("idleLockEnabled", false).toBool());

    // 2. 加载全局热键
    QSettings hotkeys("RapidNotes", "Hotkeys");
    m_hkQuickWin->setKeyData(hotkeys.value("quickWin_mods", 0x0001).toUInt(), hotkeys.value("quickWin_vk", 0x20).toUInt());
    m_hkFavorite->setKeyData(hotkeys.value("favorite_mods", 0x0002 | 0x0004).toUInt(), hotkeys.value("favorite_vk", 0x45).toUInt());
    // [UPDATE] 同步 HotkeyManager 中的最新默认热键 (Alt+X / Alt+C)
    m_hkScreenshot->setKeyData(hotkeys.value("screenshot_mods", 0x0001).toUInt(), hotkeys.value("screenshot_vk", 0x58).toUInt());
    m_hkOcr->setKeyData(hotkeys.value("ocr_mods", 0x0001).toUInt(), hotkeys.value("ocr_vk", 0x43).toUInt());
    m_hkAcquire->setKeyData(hotkeys.value("acquire_mods", 0x0002).toUInt(), hotkeys.value("acquire_vk", 0x53).toUInt());
    m_hkLock->setKeyData(hotkeys.value("lock_mods", 0x0001 | 0x0002 | 0x0004).toUInt(), hotkeys.value("lock_vk", 0x53).toUInt());
    m_hkPurePaste->setKeyData(hotkeys.value("purePaste_mods", 0x0002 | 0x0004).toUInt(), hotkeys.value("purePaste_vk", 0x56).toUInt());
    m_hkToolbox->setKeyData(hotkeys.value("toolbox_mods", 0x0002 | 0x0004).toUInt(), hotkeys.value("toolbox_vk", 0x54).toUInt());

    // 3. 局内快捷键在创建页面时已加载

    // 4. 加载截图路径及 OCR 设置
    QSettings ss("RapidNotes", "Screenshot");
    m_editScreenshotPath->setText(ss.value("savePath", qApp->applicationDirPath() + "/RPN_screenshot").toString());

    QSettings ocr("RapidNotes", "OCR");
    m_checkOcrAutoCopy->setChecked(ocr.value("autoCopy", false).toBool());
    m_checkSilentCapture->setChecked(ocr.value("silentCapture", false).toBool());

    // 5. 加载通用设置
    QSettings gs("RapidNotes", "General");

    // 加载开机启动状态 (通过注册表)
#ifdef Q_OS_WIN
    QSettings bootSettings("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);
    m_checkAutoStart->setChecked(bootSettings.contains("RapidNotes"));
#endif

    bool capsLockToEnter = gs.value("capsLockToEnter", false).toBool();
    m_checkCapsLockToEnter->setChecked(capsLockToEnter);
    KeyboardHook::instance().setCapsLockToEnterEnabled(capsLockToEnter);

    m_checkFireworks->setChecked(gs.value("showFireworks", true).toBool());
    m_checkCopyToolTip->setChecked(gs.value("showCopyToolTip", false).toBool());

    // 加载浏览器白名单
    QSettings as("RapidNotes", "Acquisition");
    QStringList browserExes = as.value("browserExes").toStringList();
    if (browserExes.isEmpty()) {
        browserExes = {
            "chrome.exe", "msedge.exe", "firefox.exe", "brave.exe", 
            "opera.exe", "iexplore.exe", "vivaldi.exe", "safari.exe",
            "arc.exe", "sidekick.exe", "maxthon.exe", "thorium.exe"
        };
    }
    m_editBrowserExes->setPlainText(browserExes.join("\n"));
}

void SettingsWindow::updateSecurityUI() {
    QSettings settings("RapidNotes", "QuickWindow");
    bool hasPwd = !settings.value("appPassword").toString().isEmpty();
    
    m_btnSetPwd->setVisible(!hasPwd);
    m_btnModifyPwd->setVisible(hasPwd);
    m_btnRemovePwd->setVisible(hasPwd);
    m_lblPwdStatus->setText(hasPwd ? "当前状态：已启用启动密码" : "当前状态：未设置锁定窗口密码");
}

void SettingsWindow::onSetPassword() {
    CategoryPasswordDialog dlg("设置锁定窗口密码", this);
    if (dlg.exec() == QDialog::Accepted) {
        QSettings settings("RapidNotes", "QuickWindow");
        settings.setValue("appPassword", dlg.password());
        settings.setValue("appPasswordHint", dlg.passwordHint());
        updateSecurityUI();
        DatabaseManager::instance().notifyAppLockSettingsChanged();
    }
}

void SettingsWindow::onModifyPassword() {
    // 简单起见，这里复用对话框，逻辑上通常先验证旧密码，这里按提示直接覆盖或弹出交互
    CategoryPasswordDialog dlg("修改启动密码", this);
    QSettings settings("RapidNotes", "QuickWindow");
    dlg.setInitialData(settings.value("appPasswordHint").toString());
    if (dlg.exec() == QDialog::Accepted) {
        settings.setValue("appPassword", dlg.password());
        settings.setValue("appPasswordHint", dlg.passwordHint());
        updateSecurityUI();
        DatabaseManager::instance().notifyAppLockSettingsChanged();
    }
}

void SettingsWindow::onRemovePassword() {
    // 移除前需要验证（此处为了逻辑闭环简单弹窗验证，或要求输入当前密码）
    QSettings settings("RapidNotes", "QuickWindow");
    QString realPwd = settings.value("appPassword").toString();

    // 弹出简单对话框要求确认
    bool ok = false;
    QString input = QInputDialog::getText(this, "身份验证", "请输入当前密码以移除：", QLineEdit::Password, "", &ok);
    if (ok && input == realPwd) {
        settings.remove("appPassword");
        settings.remove("appPasswordHint");
        updateSecurityUI();
        DatabaseManager::instance().notifyAppLockSettingsChanged();
    } else if (ok) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), 
            "<b style='color: #e74c3c;'>❌ 密码错误，无法移除</b>");
    }
}

void SettingsWindow::onBrowsePath() {
    QString dir = QFileDialog::getExistingDirectory(this, "选择截图保存目录", m_editScreenshotPath->text());
    if (!dir.isEmpty()) {
        m_editScreenshotPath->setText(dir);
    }
}

void SettingsWindow::onSaveClicked() {
    // 1. 保存全局热键
    QSettings hotkeys("RapidNotes", "Hotkeys");
    hotkeys.setValue("quickWin_mods", m_hkQuickWin->mods());
    hotkeys.setValue("quickWin_vk", m_hkQuickWin->vk());
    hotkeys.setValue("favorite_mods", m_hkFavorite->mods());
    hotkeys.setValue("favorite_vk", m_hkFavorite->vk());
    hotkeys.setValue("screenshot_mods", m_hkScreenshot->mods());
    hotkeys.setValue("screenshot_vk", m_hkScreenshot->vk());
    hotkeys.setValue("ocr_mods", m_hkOcr->mods());
    hotkeys.setValue("ocr_vk", m_hkOcr->vk());
    hotkeys.setValue("acquire_mods", m_hkAcquire->mods());
    hotkeys.setValue("acquire_vk", m_hkAcquire->vk());
    hotkeys.setValue("lock_mods", m_hkLock->mods());
    hotkeys.setValue("lock_vk", m_hkLock->vk());
    hotkeys.setValue("purePaste_mods", m_hkPurePaste->mods());
    hotkeys.setValue("purePaste_vk", m_hkPurePaste->vk());
    hotkeys.setValue("toolbox_mods", m_hkToolbox->mods());
    hotkeys.setValue("toolbox_vk", m_hkToolbox->vk());
    
    HotkeyManager::instance().reapplyHotkeys();

    // 2. 保存局内快捷键
    auto& sm = ShortcutManager::instance();
    auto edits = m_contentStack->widget(2)->findChildren<ShortcutEdit*>();
    for (auto* edit : edits) {
        sm.setShortcut(edit->property("id").toString(), edit->keySequence());
    }
    sm.save();

    // 3. 保存截图及 OCR 设置
    QSettings ss("RapidNotes", "Screenshot");
    ss.setValue("savePath", m_editScreenshotPath->text());

    QSettings ocr("RapidNotes", "OCR");
    ocr.setValue("autoCopy", m_checkOcrAutoCopy->isChecked());
    ocr.setValue("silentCapture", m_checkSilentCapture->isChecked());

    // 4. 保存通用设置
    QSettings gs("RapidNotes", "General");

    // 保存开机启动状态
#ifdef Q_OS_WIN
    QSettings bootSettings("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);
    if (m_checkAutoStart->isChecked()) {
        QString appPath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
        bootSettings.setValue("RapidNotes", "\"" + appPath + "\"");
    } else {
        bootSettings.remove("RapidNotes");
    }
#endif

    bool capsLockToEnter = m_checkCapsLockToEnter->isChecked();
    gs.setValue("capsLockToEnter", capsLockToEnter);
    KeyboardHook::instance().setCapsLockToEnterEnabled(capsLockToEnter);

    gs.setValue("showFireworks", m_checkFireworks->isChecked());
    gs.setValue("showCopyToolTip", m_checkCopyToolTip->isChecked());

    // 保存浏览器白名单
    QSettings as("RapidNotes", "Acquisition");
    QStringList browserExes = m_editBrowserExes->toPlainText().split("\n", Qt::SkipEmptyParts);
    for(QString& s : browserExes) s = s.trimmed().toLower();
    as.setValue("browserExes", browserExes);

    // 保存避让黑名单
    QSettings securitySettings("RapidNotes", "Security");
    QStringList blacklist = m_editAvoidanceBlacklist->toPlainText().split("\n", Qt::SkipEmptyParts);
    for(QString& s : blacklist) s = s.trimmed();
    securitySettings.setValue("avoidanceBlacklist", blacklist);
    securitySettings.setValue("idleLockEnabled", m_checkIdleLock->isChecked());

    // [CRITICAL] 触发黑名单热重载
    ClipboardMonitor::instance().reloadBlacklist();

    ToolTipOverlay::instance()->showText(QCursor::pos(), 
        "<b style='color: #2ecc71;'>✅ 设置已保存并立即生效</b>");
}

void SettingsWindow::onRestoreDefaults() {
    bool ok = false;
    QString input = QInputDialog::getText(this, "恢复默认设置", 
                                          "确认恢复默认设置？所有配置都将被重置。\n请输入“confirm”以继续：", 
                                          QLineEdit::Normal, "", &ok);
    
    if (ok && input.toLower() == "confirm") {
        // 1. 清除各部分的设置
        QSettings("RapidNotes", "Hotkeys").clear();
        QSettings("RapidNotes", "QuickWindow").clear();
        QSettings("RapidNotes", "Screenshot").clear();
        QSettings("RapidNotes", "Acquisition").clear();
        
        // 2. 局内快捷键重置
        ShortcutManager::instance().resetToDefaults();
        ShortcutManager::instance().save();
        
        // 3. 立即重载热键
        HotkeyManager::instance().reapplyHotkeys();
        
        // 4. 重新加载界面
        loadSettings();
        
        ToolTipOverlay::instance()->showText(QCursor::pos(), 
            "<b style='color: #3498db;'>ℹ️ 已恢复默认设置</b>");
    }
}
