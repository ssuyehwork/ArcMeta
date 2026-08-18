#ifndef RECENTNOTESWINDOW_H
#define RECENTNOTESWINDOW_H

#include <QWidget>
#include <QList>
#include <QVariant>
#include <QModelIndex>
#include <QPointer>

// 前向声明
// class CleanListView; // 改用 QListView
class QListView;
class NoteModel;
class QSortFilterProxyModel;
class QVBoxLayout;
class QGraphicsDropShadowEffect;

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// ============================================================
// RecentNotesWindow — Alt+A 触发的最近访问笔记列表窗口
//
// 功能：
//   - 按下 Alt+A 后在鼠标附近弹出
//   - 显示"最近访问"分类的数据（过去7天内访问过的笔记）
//   - 按 last_accessed_at DESC 排序（最新访问在最前面）
//   - ↑↓ 键导航，Enter / 单击选中后：复制内容到剪贴板 + 自动粘贴到先前激活的窗口
//   - Esc 强制关闭，失去焦点自动关闭
//   - 完全独立于置顶逻辑，不显示置顶指示
// ============================================================

class RecentNotesWindow : public QWidget {
    Q_OBJECT

public:
    enum ViewType { Recent, Favorite };
    static RecentNotesWindow* instance();
    
    // 在 globalPos 附近弹出（自动防止越出屏幕边缘）
    void popup(const QPoint& globalPos);

protected:
    bool event(QEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void onItemDoubleClicked(const QModelIndex& index);
    void onItemClicked(const QModelIndex& index);
    void switchView(ViewType type);
    void updateTabStyles();

private:
    explicit RecentNotesWindow(QWidget* parent = nullptr);
    ~RecentNotesWindow() override;
    
    void initUI();
    void loadRecentNotes();
    void activateCurrentItem();
    void adjustPosition(const QPoint& preferredPos);
    void sendNote(const QVariantMap& note);
    // 2026-05-04 按照用户要求：移除预览功能声明
    
    // ---- UI 组件 ----
    QListView* m_listView = nullptr;
    NoteModel* m_model = nullptr;
    QVBoxLayout* m_mainLayout = nullptr;
    QWidget* m_cardWidget = nullptr;
    QGraphicsDropShadowEffect* m_shadowEffect = nullptr;
    
    // ---- 数据 ----
    QList<QVariantMap> m_recentNotes;
    
    QWidget* m_tabContainer = nullptr;
    class QPushButton* m_tabRecent = nullptr;
    class QPushButton* m_tabFavorite = nullptr;
    ViewType m_viewType = Recent;
    
    // ---- Windows：粘贴目标窗口快照 ----
#ifdef Q_OS_WIN
    HWND m_prevHwnd = nullptr;
    HWND m_prevFocusHwnd = nullptr;
    DWORD m_prevThreadId = 0;
#endif

    static RecentNotesWindow* s_instance;
};

#endif // RECENTNOTESWINDOW_H
