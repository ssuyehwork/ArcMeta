#ifndef ARCMETA_TAG_MANAGER_VIEW_H
#define ARCMETA_TAG_MANAGER_VIEW_H

#include <QWidget>
#include <QMap>
#include <QStringList>
#include <QScrollArea>
#include <QSplitter>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMenu>

namespace ArcMeta {

/**
 * @brief 标签管理专属视图
 * 包含左侧标签侧边栏和右侧标签内容区
 */
class TagManagerView : public QWidget {
    Q_OBJECT
public:
    explicit TagManagerView(QWidget* parent = nullptr);

    /**
     * @brief 刷新标签数据并重建 UI
     */
    void refresh();

signals:
    /**
     * @brief 请求搜索含此标签的项目
     */
    void requestSearchTag(const QString& tag);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void initUi();
    void setupSidebar();
    void setupContentArea();

    // 侧边栏组件
    QWidget* m_sidebar = nullptr;
    QVBoxLayout* m_sidebarLayout = nullptr;
    QWidget* m_groupContainer = nullptr;
    QLabel* m_tagCountLabel = nullptr;

    // 内容区组件
    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_contentWidget = nullptr;

    QSplitter* m_splitter = nullptr;

    /**
     * @brief 获取常用标签 (示例逻辑)
     */
    QStringList getFrequentTags() const;

    // 数据
    QMap<QString, int> m_tagCounts;

    struct TagGroup {
        int id;
        QString name;
        QString color;
        QStringList tags;
    };
    QList<TagGroup> m_tagGroups;
};

} // namespace ArcMeta

#endif // ARCMETA_TAG_MANAGER_VIEW_H
