#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QCheckBox>
#include <QScrollArea>
#include <QPushButton>
#include <QLineEdit>
#include <QMap>
#include <QStringList>
#include "MetaPanel.h" // 引用 FlowLayout

namespace ArcMeta {

class SearchHistoryPanel;

// ─── 物理色块控件 (ColorBlock) ─────────────────────────────────────
class ColorBlock : public QWidget {
    Q_OBJECT
public:
    explicit ColorBlock(const QColor& color, QWidget* parent = nullptr);
    void setCount(int count);
    void setChecked(bool checked);
    bool isChecked() const { return m_checked; }
    QColor color() const { return m_color; }

signals:
    void clicked(const QColor& color);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent*) override;

private:
    QColor m_color;
    int    m_count = 0;
    bool   m_checked = false;
    bool   m_hovered = false;
};

// ─── 色相滑块 (内嵌版) ─────────────────────────────────────────────
class InlineHueSlider : public QWidget {
    Q_OBJECT
public:
    explicit InlineHueSlider(QWidget* parent = nullptr);
    void setHue(int h);
    int hue() const { return m_h; }

signals:
    void hueChanged(int h);
    void sliderReleased();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void updateFromPos(int x);
    int m_h = 0;
};

struct FilterState {
    QList<int>   ratings;
    QStringList  colors;
    QStringList  tags;
    QStringList  types;
    QStringList  createDates;   // "YYYY-MM-DD"
    QStringList  modifyDates;
    int          colorTolerance = 30; // 2026-05-17 按照用户要求：自定义颜色相近色容差（0~100），由 ColorPicker 准确度滑条驱动

    // 2026-xx-xx 按照用户要求：新增 5 个主选项的快速文本过滤字段
    QString colorFilterText;
    QString tagFilterText;
    QString typeFilterText;
    QString createDateFilterText;
    QString modifyDateFilterText;
};

/**
 * @brief 筛选面板 — 动态 Adobe Bridge 风格
 *
 * 由 MainWindow 在目录切换后调用 populate() 驱动数据填充。
 * 每行整体可点击（不需要对准复选框）。
 */
class FilterPanel : public QFrame {
    Q_OBJECT

public:
    explicit FilterPanel(QWidget* parent = nullptr);
    ~FilterPanel() override = default;


    void populate(
        const QMap<int, int>&        ratingCounts,
        const QMap<QString, int>&    colorCounts,
        const QMap<QString, int>&    tagCounts,
        const QMap<QString, int>&    typeCounts,
        const QMap<QString, int>&    createDateCounts,
        const QMap<QString, int>&    modifyDateCounts
    );

    FilterState currentFilter() const { return m_filter; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    void filterChanged(const FilterState& state);

public slots:
    void clearAllFilters();

private:
    void rebuildGroups();

    // 2026-05-17 根因修复：增加 outHdrLayout 参数，让调用方直接往标题行布局追加按钮
    // 彻底替代绝对定位方案，消除非布局子控件撑高 wrapper 导致的留白
    QWidget*   buildGroup(const QString& title, QVBoxLayout*& outContentLayout,
                          QHBoxLayout** outHdrLayout = nullptr);
    QCheckBox* addFilterRow(QVBoxLayout* layout, const QString& label,
                            int count, const QColor& dotColor = Qt::transparent);

    static QMap<QString, QColor> s_colorMap();

    FilterState m_filter;
    QString     m_hueSliderColor;
    QStringList m_recentColors; // LRU 缓存

    QMap<int, int>      m_ratingCounts;
    QMap<QString, int>  m_colorCounts;
    QMap<QString, int>  m_tagCounts;
    QMap<QString, int>  m_typeCounts;
    QMap<QString, int>  m_createDateCounts;
    QMap<QString, int>  m_modifyDateCounts;

    QVBoxLayout*  m_mainLayout      = nullptr;
    QScrollArea*  m_scrollArea      = nullptr;
    QWidget*      m_container       = nullptr;
    QVBoxLayout*  m_containerLayout = nullptr;
    QPushButton*  m_btnClearAll     = nullptr;

    // 2026-xx-xx 新增快速输入框成员
    QLineEdit*    m_editColor       = nullptr;
    QLineEdit*    m_editTag         = nullptr;
    QLineEdit*    m_editType        = nullptr;
    QLineEdit*    m_editCreateDate  = nullptr;
    QLineEdit*    m_editModifyDate  = nullptr;

    SearchHistoryPanel* m_historyPanel = nullptr;

    // 辅助方法：处理历史记录
    void saveFilterHistory(const QString& key, const QString& text);
    QStringList getFilterHistory(const QString& key) const;
};

} // namespace ArcMeta
