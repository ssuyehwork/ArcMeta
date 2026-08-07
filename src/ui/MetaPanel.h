#pragma once

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QTextEdit>
#include <QLineEdit>
#include <QFrame>
#include <QStyle>
#include <vector>
#include <string>

namespace ArcMeta {

/**
 * @brief ElasticEdit: 弹性高度编辑框，内容自动撑开高度
 */
class ElasticEdit : public QTextEdit {
    Q_OBJECT
public:
    explicit ElasticEdit(QWidget* parent = nullptr);
    void adjustHeight();
signals:
    void returnPressed();
protected:
    void keyPressEvent(QKeyEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
};

/**
 * @brief Tag Pill 圆角标签组件
 */
class TagPill : public QWidget {
    Q_OBJECT
public:
    explicit TagPill(const QString& text, QWidget* parent = nullptr);
    void setData(const QString& text);
signals:
    void deleteRequested(const QString& text);
protected:
    void paintEvent(QPaintEvent* event) override;
private:
    QString m_text;
    QLabel* m_label = nullptr;
    QPushButton* m_closeBtn = nullptr;
};

/**
 * @brief 流式布局容器 (用于展示标签与调色盘)
 */
class FlowLayout : public QLayout {
public:
    explicit FlowLayout(QWidget *parent, int margin = -1, int hSpacing = -1, int vSpacing = -1);
    ~FlowLayout();
    void addItem(QLayoutItem *item) override;
    int horizontalSpacing() const;
    int verticalSpacing() const;
    Qt::Orientations expandingDirections() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int) const override;
    int count() const override;
    QLayoutItem *itemAt(int index) const override;
    QSize minimumSize() const override;
    void setGeometry(const QRect &rect) override;
    QSize sizeHint() const override;
    QLayoutItem *takeAt(int index) override;
private:
    int doLayout(const QRect &rect, bool testOnly) const;
    QList<QLayoutItem *> itemList;
    int m_hSpace;
    int m_vSpace;
};

/**
 * @brief ColorPill: 单个颜色块组件
 */
class ColorPill : public QWidget {
    Q_OBJECT
public:
    explicit ColorPill(const QColor& color, float ratio, QWidget* parent = nullptr);
    void setData(const QColor& color, float ratio);
signals:
    void colorSelected(const QColor& color);
    void requestSetAsPrimary(const QColor& color);
protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
private:
    QColor m_color;
    float m_ratio;
    bool m_hovered = false;
};

/**
 * @brief 元数据面板 (纯 View 视图层重构 + ABI 兼容层)
 */
class MetaPanel : public QFrame {
    Q_OBJECT
public:
    explicit MetaPanel(QWidget* parent = nullptr);
    ~MetaPanel() override = default;

    // 纯 UI 渲染接口
    void updateInfo(const QString& name, const QString& type, const QString& size,
                    const QString& ctime, const QString& mtime, const QString& atime,
                    const QString& path, bool encrypted, int width = 0, int height = 0);

    void setSelectedPaths(const QStringList& paths) { m_selectedPaths = paths; }
    void setPalettes(const QVector<QPair<QColor, float>>& palette);
    void setTags(const QStringList& tags);

    // 兼容 QString 与 std::wstring 的属性设置
    void setNote(const QString& note);
    void setNote(const std::wstring& note);

    void setURL(const QString& url);
    void setURL(const std::wstring& url);

    void setCategory(const QString& category);

    // 兼容旧版调用的占位空实现（解决外部编译报错，保持纯 View 职责）
    void setRating(int rating) { Q_UNUSED(rating); }
    void setColor(const std::wstring& color) { Q_UNUSED(color); }
    void setPinned(bool pinned) { Q_UNUSED(pinned); }

signals:
    // 兼容外部旧信号绑定
    void metadataChanged(int rating, const std::wstring& color);

    // 单向数据流通知信号 (由 Controller 统一接管数据库处理)
    void noteEdited(const QStringList& paths, const QString& newNote);
    void linkEdited(const QStringList& paths, const QString& newLink);
    void primaryColorChanged(const QString& path, const QColor& color);
    void tagsChanged(const QStringList& paths, const QStringList& tags);
    void searchByColor(const QColor& color);
    void renameRequested(const QString& oldPath, const QString& newPath);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void initUi();
    void adjustFlowHeights();
    void addInfoRow(const QString& label, QLabel*& valueLabel);
    QFrame* createSeparator();

    QVBoxLayout* m_mainLayout = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_container = nullptr;
    QVBoxLayout* m_containerLayout = nullptr;
    
    ElasticEdit* m_nameEdit = nullptr;
    QLabel* lblType = nullptr, *lblSize = nullptr, *lblDimensions = nullptr;
    QLabel* lblCtime = nullptr, *lblMtime = nullptr, *lblAtime = nullptr;
    ElasticEdit* m_pathEdit = nullptr;
    QLabel* lblEncrypted = nullptr;
    
    QWidget* m_paletteBox = nullptr;
    FlowLayout* m_paletteFlowLayout = nullptr;
    
    QWidget* m_tagBox = nullptr;
    QWidget* m_tagContainer = nullptr;
    FlowLayout* m_tagFlowLayout = nullptr;
    ElasticEdit* m_tagEdit = nullptr;
    
    ElasticEdit* m_noteEdit = nullptr;
    ElasticEdit* m_linkEdit = nullptr;
    ElasticEdit* m_categoryEdit = nullptr;

    QStringList m_selectedPaths;

    QList<TagPill*> m_tagPool;
    QList<ColorPill*> m_colorPool;
    QTimer* m_adjustTimer = nullptr;

    bool m_isInternalUpdating = false;

private slots:
    void onTagAdded();
    void onTagDeleted(const QString& text);
    void setAsPrimaryColor(const QColor& color);
};

} // namespace ArcMeta