#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QListWidget>
#include <QSplitter>
#include <QMap>
#include <QStringList>
#include <vector>
#include "../meta/TagRepo.h"
#include "FlowLayout.h"

namespace ArcMeta {

/**
 * @brief 标签管理面板 (取代 ContentPanel 显示标签全景)
 */
class TagManagementPanel : public QWidget {
    Q_OBJECT
public:
    explicit TagManagementPanel(QWidget* parent = nullptr);

    /**
     * @brief 物理对账：从数据库重新加载所有标签并刷新 UI
     */
    void refresh();

signals:
    /**
     * @brief 触发全局搜索该标签
     */
    void tagSearchRequested(const QString& tag);

    /**
     * @brief 标签元数据发生重大变更（如重命名、删除），通知全局刷新
     */
    void tagMetadataChanged();

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void initUi();
    void setupSidebar();
    void setupMainArea();

    /**
     * @brief 渲染过滤后的标签列表
     */
    void renderFilteredTags();

    /**
     * @brief 创建一个标签按钮小部件
     */
    QWidget* createTagWidget(const TagDef& tag);

    // UI 组件
    QVBoxLayout* m_mainLayout = nullptr;
    QListWidget* m_sidebar = nullptr;
    QLineEdit* m_searchEdit = nullptr;

    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_scrollContainer = nullptr;
    QVBoxLayout* m_containerLayout = nullptr;

    // 数据
    std::vector<TagDef> m_allTags;
    std::vector<TagGroup> m_groups;

    // 过滤状态
    QString m_currentSearch;
    int m_currentFilterType = 0; // 0: All, 1: Favorites, 100+: GroupId
};

} // namespace ArcMeta
