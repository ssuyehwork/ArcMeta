#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QScrollArea>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QList>
#include <QMap>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QShowEvent>
#include <QFocusEvent>

namespace ArcMeta {

class FlowLayout;

/**
 * @brief 标签检索/选择弹出框中的单个标签项按钮
 */
class TagItemButton : public QPushButton {
    Q_OBJECT
public:
    explicit TagItemButton(const QString& name, int count, QWidget* parent = nullptr);
    QString tagName() const { return m_tagName; }
    void setSelected(bool selected);
    bool isSelected() const { return m_selected; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString m_tagName;
    int m_count;
    bool m_selected = false;
};

/**
 * @brief 标签检索与选择悬浮弹出框
 */
class TagPickerPopover : public QWidget {
    Q_OBJECT
public:
    explicit TagPickerPopover(QWidget* parent = nullptr);
    ~TagPickerPopover() override;

    /**
     * @brief 弹出并展示在指定位置下方
     */
    void showAt(const QPoint& globalPos);

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
    void refreshList();
    void updateSelectionHighlight();
    void selectCurrent();

    QLineEdit* m_searchEdit = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_scrollContainer = nullptr;
    QVBoxLayout* m_scrollLayout = nullptr;

    // 分组
    QWidget* m_recentGroup = nullptr;
    QLabel* m_recentLabel = nullptr;
    QWidget* m_recentContainer = nullptr;
    FlowLayout* m_recentFlow = nullptr;

    QWidget* m_globalGroup = nullptr;
    QLabel* m_globalLabel = nullptr;
    QWidget* m_globalContainer = nullptr;
    FlowLayout* m_globalFlow = nullptr;

    QLabel* m_hintLabel = nullptr;

    // 数据缓存
    QList<TagItemButton*> m_visibleButtons;
    int m_selectedIndex = -1;

    QMap<QString, int> m_allTags;
    QList<QPair<QString, int>> m_topTags;
};

} // namespace ArcMeta
