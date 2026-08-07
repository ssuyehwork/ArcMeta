#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QScrollArea>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QList>
#include <QMap>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QShowEvent>
#include <QFocusEvent>
#include <QFrame>

namespace ArcMeta {

/**
 * @brief 1:1复刻的标签项按钮，包含左侧时钟/圆点图标、富文本展示、4px圆角
 */
class TagItemButton : public QPushButton {
    Q_OBJECT
public:
    enum IconType { Clock, CircleFilled };
    explicit TagItemButton(const QString& name, int count, IconType iconType, QWidget* parent = nullptr);

    QString tagName() const { return m_tagName; }
    void setSelected(bool selected);
    bool isSelected() const { return m_selected; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString m_tagName;
    int m_count;
    IconType m_iconType;
    bool m_selected = false;

    QLabel* m_iconLabel = nullptr;
    QLabel* m_textLabel = nullptr;
};

/**
 * @brief 界面二：按需悬浮标签选择框 (图 3)
 */
class TagPickerPopover : public QWidget {
    Q_OBJECT
public:
    enum FocusZone { SearchBar, SidebarZone, GridZone };

    explicit TagPickerPopover(QWidget* parent = nullptr);
    ~TagPickerPopover() override;

    /**
     * @brief 弹出并展示在指定位置下方
     */
    void showAt(const QPoint& globalPos);

    /**
     * @brief 切换左侧栏显示与折叠状态
     */
    void toggleSidebar();
    void setSidebarVisible(bool visible);

signals:
    void tagSelected(const QString& tagName);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onSearchTextChanged(const QString& text);

private:
    void refreshSidebar();
    void refreshList();
    void updateSelectionHighlight();
    void updateSidebarHighlight();
    void selectCurrent();

    // 静态会话级最近使用队列
    static QStringList s_sessionRecentTags;
    void recordTagUsage(const QString& tagName);

    // 搜索栏
    QLineEdit* m_searchEdit = nullptr;

    // 双栏布局
    QHBoxLayout* m_columnsLayout = nullptr;

    // 左侧栏：标签组侧边栏
    QWidget* m_sidebarWidget = nullptr;
    QScrollArea* m_sidebarScroll = nullptr;
    QWidget* m_sidebarContainer = nullptr;
    QVBoxLayout* m_sidebarLayout = nullptr;
    QList<QPushButton*> m_sidebarButtons;
    int m_selectedSidebarIndex = 0;
    int m_selectedGroupId = -1; // -1: 全部, -2: 未分类, >0: 自定义组ID

    // 中间分割线
    QFrame* m_dividerLine = nullptr;

    // 右侧栏：滚动列表区
    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_scrollContainer = nullptr;
    QVBoxLayout* m_scrollLayout = nullptr;

    // 右侧分组容器
    QWidget* m_recentGroup = nullptr;
    QLabel* m_recentLabel = nullptr;
    QWidget* m_recentContainer = nullptr;
    QGridLayout* m_recentGrid = nullptr;

    QWidget* m_globalGroup = nullptr;
    QLabel* m_globalLabel = nullptr;
    QWidget* m_globalContainer = nullptr;
    QGridLayout* m_globalGrid = nullptr;

    QLabel* m_hintLabel = nullptr;

    // 键盘焦点游走
    FocusZone m_focusZone = SearchBar;

    // 数据缓存
    QList<TagItemButton*> m_visibleButtons;
    int m_selectedIndex = -1;

    QMap<QString, int> m_allTags;
    QList<QPair<QString, int>> m_topTags;
};

} // namespace ArcMeta
