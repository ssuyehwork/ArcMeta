#pragma once

#include "FramelessDialog.h"
#include "components/FlowLayout.h"
#include "../meta/MetadataManager.h"
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QLabel>
#include <QFrame>
#include <QButtonGroup>

namespace QuarkMeta {

/**
 * @brief 高级标签管理弹窗 (模块化组件，对应图二/图三规范)
 */
class TagManagerDialog : public FramelessDialog {
    Q_OBJECT
public:
    /**
     * @brief 全局统一模块化静态调用入口
     * @param parent 父窗口指针
     * @param currentPath 当前操作的文件/目录绝对路径
     * @param isMirrorSource 是否处于托管库模式 (true: 托管库, false: 磁盘导航模式)
     */
    static void showDialog(QWidget* parent, const QString& currentPath, bool isMirrorSource);

protected:
    explicit TagManagerDialog(const QString& currentPath, bool isMirrorSource, QWidget* parent = nullptr);
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onSearchTextChanged(const QString& text);
    void onSidebarToggled(bool checked);
    void onSidebarItemClicked(int id);

private:
    void initContent();
    void applyTheme();
    void refreshTags();
    void createTag(const QString& tagName);

    QString m_currentPath;
    bool m_isMirrorSource = false;
    QString m_currentFilter = "all"; // "all" | "uncategorized" | "frequent"

    // 顶部组件
    QLineEdit* m_searchEdit = nullptr;
    QPushButton* m_btnToggleSidebar = nullptr;

    // 左侧 180px 侧边栏
    QFrame* m_sidebar = nullptr;
    QVBoxLayout* m_sidebarLayout = nullptr;
    QButtonGroup* m_sidebarGroup = nullptr;

    // 右侧内容区
    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_contentWidget = nullptr;
    QVBoxLayout* m_contentLayout = nullptr;

    // 动态新增提示胶囊 (`+ 新增 "关键字"`)
    QWidget* m_addNewTagWidget = nullptr;
    QPushButton* m_btnAddNewTag = nullptr;

    // 数据列表容器
    QVBoxLayout* m_tagsScrollLayout = nullptr;

    // 数据缓存
    static QStringList s_sessionRecentTags; // 全局会话级“最近使用”历史队列
    QMap<QString, int> m_allTagCounts;
};

} // namespace QuarkMeta
