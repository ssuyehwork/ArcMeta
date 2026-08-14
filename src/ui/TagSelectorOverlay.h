#pragma once

#include <QFrame>
#include <QLineEdit>
#include <QListWidget>
#include <QScrollArea>
#include <QLabel>
#include <QPushButton>
#include <QMap>
#include <QStringList>
#include "components/FlowLayout.h"

namespace ArcMeta {

class TagSelectorOverlay : public QFrame {
    Q_OBJECT
public:
    TagSelectorOverlay(const QStringList& initialSelected, QWidget* parent = nullptr);
    ~TagSelectorOverlay() override;

signals:
    void selectionChanged(const QStringList& selectedTags);
    void overlayClosed();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void changeEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void initUi();
    void loadTagsAndGroups();
    void filterTags();
    void populateGrid();
    void toggleTagSelection(const QString& tag);
    void updateSelectionHighlight();
    void handleGridNavigation(int key);
    void handleGroupNavigation(int key);

    // 8方向拉伸大小和拖拽移动辅助函数
    void updateCursorShape(const QPoint& pos);
    int getResizeDirection(const QPoint& pos);

    QStringList m_selectedTags;
    QStringList m_displayedTags;
    QMap<QString, int> m_allTagCounts;

    QLineEdit* m_searchEdit = nullptr;
    QListWidget* m_groupList = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_tagGridWidget = nullptr;
    FlowLayout* m_gridFlowLayout = nullptr;
    QList<QPushButton*> m_tagButtons;
    int m_currentTagIndex = -1;

    // 拖拽与缩放状态
    QPoint m_dragStartPos;
    QRect m_dragStartGeometry;
    bool m_isDragging = false;
    int m_resizeDir = 0; // 0=None, 1=Left, 2=Right, 4=Top, 8=Bottom, etc.
    const int m_margin = 6;
    bool m_wasActivated = false; // 是否已经完成首次激活
};

} // namespace ArcMeta
