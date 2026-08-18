#pragma once
#include <QWidget>
#include <QListWidget>
#include <QLineEdit>
#include <QLabel>
#include <QTimer>
#include <QVariantMap>
#include <QList>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// ============================================================
// IdeaQuickPanel — Alt+A 全局热键触发的灵感速查浮窗
//
// 功能：
//   - 按下 Alt+A 后在鼠标附近弹出
//   - 显示最近更新的 20 条数据（按 updated_at DESC）
//   - 顶部搜索框实时过滤
//   - ↑↓ 键导航，Enter / 单击选中后：
//       复制内容到剪贴板 + 自动粘贴到先前激活的窗口
//   - 失去焦点自动关闭，Esc 强制关闭
// ============================================================

class IdeaQuickPanel : public QWidget {
    Q_OBJECT

public:
    // 全局单例，懒加载
    static IdeaQuickPanel* instance();

    // 在 globalPos 附近弹出（自动防止越出屏幕边缘）
    // 调用前必须先捕获目标窗口句柄（内部已处理）
    void popup(const QPoint& globalPos);

protected:
    bool event(QEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    explicit IdeaQuickPanel(QWidget* parent = nullptr);
    ~IdeaQuickPanel() override;

    // 从数据库加载并填充列表，keyword 为空时取全量 Top20
    void loadNotes(const QString& keyword = "");

    // 激活当前选中条目：复制 + 延迟粘贴
    void activateCurrentItem();

    // 计算并设定窗口坐标（避免越屏）
    void adjustPosition(const QPoint& preferredPos);

    // ---- UI 组件 ----
    QLineEdit*   m_searchEdit  = nullptr;
    QListWidget* m_listWidget  = nullptr;
    QLabel*      m_countLabel  = nullptr;
    QTimer*      m_searchTimer = nullptr;   // 搜索防抖 200ms

    // ---- 数据 ----
    QList<QVariantMap> m_notes;             // 当前加载的笔记列表（与 listWidget 行一一对应）

    // ---- Windows：粘贴目标窗口快照 ----
#ifdef Q_OS_WIN
    HWND  m_prevHwnd      = nullptr;        // 弹出前的前台窗口
    HWND  m_prevFocusHwnd = nullptr;        // 弹出前的键盘焦点控件
    DWORD m_prevThreadId  = 0;
#endif

    static IdeaQuickPanel* s_instance;
};
