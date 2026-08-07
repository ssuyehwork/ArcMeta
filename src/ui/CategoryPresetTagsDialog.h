#pragma once

#include "FramelessDialog.h"
#include <vector>
#include <string>
#include <QString>
#include <QStringList>
#include <QScrollArea>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSplitter>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QShowEvent>

namespace ArcMeta {

class FlowLayout;

/**
 * @brief 标签项按钮，包含左侧时钟/圆点图标、富文本展示、4px圆角
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
 * @brief 设置自动标签主对话框 (单窗口一体化高精细度架构)
 */
class CategoryPresetTagsDialog : public FramelessDialog {
    Q_OBJECT
public:
    enum FocusZone { SearchBar, SidebarZone, GridZone };

    explicit CategoryPresetTagsDialog(const QString& folderName,
                                     const std::vector<std::wstring>& initialTags,
                                     QWidget* parent = nullptr);
    ~CategoryPresetTagsDialog() override;

    /**
     * @brief 获取最终用户设置的预设标签列表
     */
    std::vector<std::wstring> getPresetTags() const;

    /**
     * @brief 展开/折叠左侧侧边栏
     */
    void toggleSidebar();
    void setSidebarVisible(bool visible);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onSearchTextChanged(const QString& text);
    void onTagSelectedFromPicker(const QString& tagName);
    void onRemoveTag(const QString& tagName);

private:
    void initLayout();
    void addTagPill(const QString& tagName);

    // 标签网格与侧边栏的同步刷新
    void refreshSidebar();
    void refreshList();
    void updateSelectionHighlight();
    void updateSidebarHighlight();
    void selectCurrent();

    // 静态会话级最近使用队列，支持秒级秒刷新
    static QStringList s_sessionRecentTags;
    void recordTagUsage(const QString& tagName);

    // 核心信息
    QString m_folderName;
    std::vector<std::wstring> m_initialTags;

    // UI 组件：顶层已选胶囊区
    QWidget* m_tagsContainer = nullptr;
    FlowLayout* m_tagsFlow = nullptr;

    // UI 组件：中层一体化搜索与标签选择区
    QLineEdit* m_searchEdit = nullptr;
    QSplitter* m_splitter = nullptr;

    // 侧边栏
    QWidget* m_sidebarWidget = nullptr;
    QScrollArea* m_sidebarScroll = nullptr;
    QWidget* m_sidebarContainer = nullptr;
    QVBoxLayout* m_sidebarLayout = nullptr;
    QList<QPushButton*> m_sidebarButtons;
    int m_selectedSidebarIndex = 0;
    int m_selectedGroupId = -1; // -1: 全部, -2: 未分类, >0: 组ID

    // 分割线与滚动区
    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_scrollContainer = nullptr;
    QVBoxLayout* m_scrollLayout = nullptr;

    // 标签网格分组
    QWidget* m_recentGroup = nullptr;
    QLabel* m_recentLabel = nullptr;
    QWidget* m_recentContainer = nullptr;
    QGridLayout* m_recentGrid = nullptr;

    QWidget* m_globalGroup = nullptr;
    QLabel* m_globalLabel = nullptr;
    QWidget* m_globalContainer = nullptr;
    QGridLayout* m_globalGrid = nullptr;

    QLabel* m_hintLabel = nullptr;

    // 键盘轮转焦点
    FocusZone m_focusZone = SearchBar;

    // 标签库缓存
    QList<TagItemButton*> m_visibleButtons;
    int m_selectedIndex = -1;

    QMap<QString, int> m_allTags;
    QList<QPair<QString, int>> m_topTags;
};

} // namespace ArcMeta
