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

namespace ArcMeta {

class TagManagerDialog : public FramelessDialog {
    Q_OBJECT
public:
    /**
     * @brief 模块化入口：在全软件任何位置一键弹出标签管理弹窗
     */
    static void showDialog(QWidget* parent, const QString& currentPath, bool isMirrorSource);

protected:
    explicit TagManagerDialog(const QString& currentPath, bool isMirrorSource, QWidget* parent = nullptr);
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onSearchTextChanged(const QString& text);
    void onSidebarToggled(bool checked);

private:
    void initContent();
    void applyTheme();
    void refreshTags();
    void createTag(const QString& tagName);

    QString m_currentPath;
    bool m_isMirrorSource = false;

    // 顶部组件
    QLineEdit* m_searchEdit = nullptr;
    QPushButton* m_btnToggleSidebar = nullptr;

    // 左侧 180px 侧边栏
    QFrame* m_sidebar = nullptr;
    QVBoxLayout* m_sidebarLayout = nullptr;

    // 右侧内容区
    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_contentWidget = nullptr;
    QVBoxLayout* m_contentLayout = nullptr;

    // 动态新增提示胶囊
    QWidget* m_addNewTagWidget = nullptr;
    QPushButton* m_btnAddNewTag = nullptr;

    // 标签容器与流式布局
    QWidget* m_recentTagsContainer = nullptr;
    FlowLayout* m_recentFlowLayout = nullptr;

    QWidget* m_allTagsContainer = nullptr;
    FlowLayout* m_allFlowLayout = nullptr;

    // 记忆数据
    QStringList m_recentTags;
    QMap<QString, int> m_allTagCounts;
};

} // namespace ArcMeta
