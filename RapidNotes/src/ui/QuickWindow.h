#ifndef QUICKWINDOW_H
#define QUICKWINDOW_H

#include <QWidget>
#include <QString>
#include <QList>
#include <QVariant>
#include <QModelIndex>
#include <QPointer>
#include <QPoint>
#include <QByteArray>
#include <QElapsedTimer>
#include "../core/DatabaseManager.h"

// 前向声明，减少头文件污染
class SearchLineEdit;
class NoteModel;
class CategoryModel;
class QSortFilterProxyModel;
class QListView;
class QTreeView;
class QStackedWidget;
class QSplitter;
class QLabel;
class QPushButton;
class QLineEdit;
class QShortcut;
class QTimer;
class CategoryLockWidget;
class DropTreeView;
class ClickableLineEdit;
class CleanListView;
class FilterPanel;
class QVBoxLayout;

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#endif

class QuickWindow : public QWidget {
    Q_OBJECT
public:
    explicit QuickWindow(QWidget* parent = nullptr);
    void showAuto();
    void focusLockInput();
    void saveState();
    void restoreState();
    int getCurrentCategoryId() const {
        if (m_currentFilterType == "category") return m_currentFilterValue.toInt();
        return -1;
    }

public slots:
    void refreshData();
    void scheduleRefresh();
    void onNoteAdded(const QVariantMap& note);
    void recordLastActiveWindow(HWND captureHwnd = nullptr);



protected:
#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
#endif
    bool event(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void initUI();
    void setupAppLock();
    void activateNote(const QModelIndex& index);
    void sendNote(const QVariantMap& note); // 2026-03-20 [NEW] 提取统一发送逻辑
    void updateContextSnapshotById(int noteId); // 2026-03-20 [NEW] 滚动更新快照中心
    void setupShortcuts();
    void updatePartitionStatus(const QString& name);
    void refreshSidebar();
    void applyListTheme(const QString& colorHex);
    void safeExpandPartitionTree(); // [USER_REQUEST] 2026-03-xx 物理级预防上锁分类展开
    void updateShortcuts();
    void updateFocusLines();
    void updateAutoCategorizeButton();
    void updateAppLockStatus();
    void toggleLayoutDirection();
    void updateToggleAllIcon(); // 2026-04-xx 按照用户要求：动态更新联动折叠按钮图标
    void setSortModeForCurrentType(const QString& mode); // 2026-05-04 统一排序模式设置
public:
    QString currentCategoryColor() const { return m_currentCategoryColor; }
    bool isRecentlyVisitedView() const { return m_currentFilterType == "recently_visited"; } // 2026-05-04 判断当前是否为最近访问视图
    bool isLocked() const { return m_appLockWidget != nullptr; }

    // 快捷键处理函数
    void doDeleteSelected(bool physical = false);
    void doToggleFavorite();
    void doTogglePin();
    void doNewIdea();
    void doMergeSelected(); // [NEW] 2026-04-xx 按照用户要求：合并选中项目的正文内容创建新数据
    void doCreateByLine(bool fromClipboard);
    void doExtractContent();
    void doEditSelected();
    void doEditNote(int id);
    void doSetRating(int rating);
    void doMoveToCategory(int catId);
    void doRestoreTrash();
    void doPreview();
    void doGlobalLock();
    void toggleStayOnTop(bool checked);
    void toggleSidebar();
    void toggleFilter();
    void toggleAllPanels(); // 2026-04-xx 按照用户要求：一键联动折叠/展开所有侧面板
    void showListContextMenu(const QPoint& pos);
    void showSidebarMenu(const QPoint& pos);

    void updatePreviewContent();
    void handleTagInput();
    void openTagSelector();
    void doMoveNote(DatabaseManager::MoveDirection dir);
    void doCopyTags();
    void doPasteTags();
    void doRepeatAction(); // 添加用于执行 F4 重复动作的槽函数声明
    void showContextNotesMenu(); // 2026-03-20 [NEW] Alt+A 灵感上下文菜单
    void checkIdleLock(); // [NEW] 2026-03-xx 定时器检测系统闲置并自动锁定

    // 辅助函数
    void doImportCategory(int catId);
    void doImportFolder(int catId);
    void doExportCategory(int catId, const QString& catName);
    bool verifyExportPermission();
    void updateLayoutWidth(); // [NEW] 2026-04-05 极致紧凑型布局统一刷新函数
    
    SearchLineEdit* m_searchEdit;
    QWidget* m_listFocusLine;
    QWidget* m_sidebarFocusLine;
    QWidget* m_listWrapper;
    QStackedWidget* m_listStack;
    CleanListView* m_listView;
    CategoryLockWidget* m_lockWidget;
    QWidget* m_appLockWidget = nullptr;
    NoteModel* m_model;
    
    DropTreeView* m_systemTree;
    DropTreeView* m_partitionTree;
    CategoryModel* m_systemModel;
    CategoryModel* m_partitionModel;
    QSortFilterProxyModel* m_systemProxyModel;
    QSortFilterProxyModel* m_partitionProxyModel;
    QVariantMap m_tempSortCriteria; // 临时排序条件，不影响分类排序
    QString m_allDataSortMode; // "全部数据"的排序模式
    
    QTimer* m_searchTimer;
    QTimer* m_refreshTimer;
    QVBoxLayout* m_mainLayout;
    QSplitter* m_splitter;
    QLabel* m_statusLabel;
    QPushButton* m_btnMax;
    QPushButton* m_btnNavDir;
    QPushButton* m_btnAutoCat;
    QPushButton* m_btnToggleAll; // 2026-04-xx 按照用户要求：全面板联动折叠按钮
    QStackedWidget* m_bottomStackedWidget;
    QWidget* m_sidebarWrapper; // 2026-04-xx 按照用户要求：修正侧边栏显隐逻辑，操作外层容器
    FilterPanel* m_filterPanel;
    QWidget* m_filterWrapper;
    ClickableLineEdit* m_tagEdit;
    SearchLineEdit* m_catSearchEdit;
    QLineEdit* m_pageInput;
    QList<QShortcut*> m_shortcuts;
    QTimer* m_idleLockTimer = nullptr; // [NEW] 2026-03-xx 闲置自动锁定定时器

    int m_currentPage = 1;
    int m_totalPages = 1;
    bool m_isLayoutReversed = false;
    QString m_currentFilterType = "recently_visited";
    QVariant m_currentFilterValue = -1;
    QString m_currentCategoryColor = "#4a90e2"; // 默认蓝色
    bool m_isStayOnTop = false;
    // 2026-03-xx 按照用户要求：侧边栏持久化模式标志（true: 人工收起/持久, false: 自动折叠/临时）
    bool m_isSidebarPersistent = true;
    int m_sidebarWidth = 163;
    int m_filterWidth = 163;
    QElapsedTimer m_lastWheelPageTimer;    // [NEW] 用于滚轮翻页防抖

    // 2026-03-20 [NEW] 记录最近一次发送灵感的上下文快照 (上下各5条，共11条)
    QList<QVariantMap> m_contextNotesSnapshot;

#ifdef Q_OS_WIN
public:
    HWND m_lastActiveHwnd = nullptr;
    HWND m_lastFocusHwnd = nullptr;
    DWORD m_lastThreadId = 0;
#endif
};

#endif // QUICKWINDOW_H