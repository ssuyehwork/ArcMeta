#pragma once
#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QFrame>
#include <QStyle>
#include <vector>
#include <string>

namespace ArcMeta {

class ElasticEdit : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit ElasticEdit(QWidget* parent = nullptr);
    void adjustHeight();
protected:
    void keyPressEvent(QKeyEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
};

class TagPill : public QWidget {
    Q_OBJECT
public:
    explicit TagPill(const QString& text, QWidget* parent = nullptr);
signals:
    void deleteRequested(const QString& text);
protected:
    void paintEvent(QPaintEvent* event) override;
private:
    QString m_text;
    QPushButton* m_closeBtn = nullptr;
};

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
    int smartSpacing(QStyle::PixelMetric pm) const;
    QList<QLayoutItem *> itemList;
    int m_hSpace;
    int m_vSpace;
};

class StarRatingWidget : public QWidget {
    Q_OBJECT
public:
    explicit StarRatingWidget(QWidget* parent = nullptr);
    void setRating(int rating);
    int rating() const { return m_rating; }
signals:
    void ratingChanged(int rating);
protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
private:
    int m_rating = 0;
};

class ColorPickerWidget : public QWidget {
    Q_OBJECT
public:
    explicit ColorPickerWidget(QWidget* parent = nullptr);
    void setColor(const std::wstring& colorName);
    std::wstring color() const { return m_currentColor; }
signals:
    void colorChanged(const std::wstring& colorName);
protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
private:
    std::wstring m_currentColor = L"";
    struct ColorEntry {
        std::wstring name;
        QColor value;
    };
    std::vector<ColorEntry> m_colors;
};

class PaletteCapsule : public QWidget {
    Q_OBJECT
public:
    explicit PaletteCapsule(QWidget* parent = nullptr);
    void setPalette(const QVector<QPair<QColor, float>>& palette);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
signals:
    void colorSelected(const QColor& color);
protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
private:
    QVector<QPair<QColor, float>> m_palette;
    int m_hoverIndex = -1;
    const int m_dotSize = 16;
    const int m_padding = 8;
    const int m_spacing = 6;
};

class MetaPanel : public QFrame {
    Q_OBJECT
public:
    explicit MetaPanel(QWidget* parent = nullptr);
    ~MetaPanel() override = default;

    void updateInfo(const QString& name, const QString& type, const QString& size,
                    const QString& ctime, const QString& mtime, const QString& atime,
                    const QString& path, bool encrypted);
    void setPalettes(const QVector<QPair<QColor, float>>& palette);
signals:
    void metadataChanged(int rating, const std::wstring& color);
    void searchByColor(const QColor& color);
public:
    void setRating(int rating);
    void setColor(const std::wstring& color);
    void setPinned(bool pinned);
    void setTags(const QStringList& tags);
    void setNote(const std::wstring& note);
    void setURL(const std::wstring& url);
    void setCategory(const QString& category);
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
private:
    void initUi();
    void addInfoRow(const QString& label, QLabel*& valueLabel);
    QFrame* createSeparator();
    QWidget* createSectionBox(const QString& iconName, const QString& title, QWidget* content);
    QVBoxLayout* m_mainLayout = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_container = nullptr;
    QVBoxLayout* m_containerLayout = nullptr;
    ElasticEdit* m_nameEdit = nullptr;
    QLabel* lblType = nullptr, *lblSize = nullptr, *lblCtime = nullptr, *lblMtime = nullptr, *lblAtime = nullptr, *lblPath = nullptr, *lblEncrypted = nullptr;
    PaletteCapsule* m_paletteCapsule = nullptr;
    QWidget* m_tagContainer = nullptr;
    FlowLayout* m_tagFlowLayout = nullptr;
    QLineEdit* m_tagEdit = nullptr;
    ElasticEdit* m_noteEdit = nullptr, *m_linkEdit = nullptr, *m_categoryEdit = nullptr;
private slots:
    void onTagAdded();
    void onTagDeleted(const QString& text);
};

} // namespace ArcMeta
