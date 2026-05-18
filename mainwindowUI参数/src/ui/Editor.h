#ifndef EDITOR_H
#define EDITOR_H

#include <QTextEdit>
#include <QSyntaxHighlighter>
#include <QRegularExpression>

class MarkdownHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit MarkdownHighlighter(QTextDocument* parent = nullptr);
protected:
    void highlightBlock(const QString& text) override;
private:
    struct HighlightingRule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    QList<HighlightingRule> m_highlightingRules;
};

#include <QStackedWidget>

class InternalEditor : public QTextEdit {
    Q_OBJECT
public:
    explicit InternalEditor(QWidget* parent = nullptr);
    void insertTodo();
    void highlightSelection(const QColor& color);
protected:
    void insertFromMimeData(const QMimeData* source) override;
    void keyPressEvent(QKeyEvent* e) override;
};

class Editor : public QWidget {
    Q_OBJECT
public:
    explicit Editor(QWidget* parent = nullptr);
    void setNote(const QVariantMap& note, bool isPreview = false);
    void setPlainText(const QString& text);
    QString toPlainText() const;
    QString toHtml() const;
    void setPlaceholderText(const QString& text);
    void togglePreview(bool preview);
    void setReadOnly(bool ro);

    bool isRich() const; // [NEW] 2026-03-xx 探测文档是否包含富文本特征
    QString getOptimizedContent() const; // [NEW] 2026-03-xx 智能返回 HTML 或纯文本，保持存储纯净

    bool eventFilter(QObject* watched, QEvent* event) override;
    
    // 代理 InternalEditor 的功能
    void undo() { m_edit->undo(); }
    void redo() { m_edit->redo(); }
    void insertTodo() { m_edit->insertTodo(); }
    void highlightSelection(const QColor& color) { m_edit->highlightSelection(color); }
    void clearFormatting();
    void toggleList(bool ordered);
    
    // 搜索功能
    bool findText(const QString& text, bool backward = false);

private:
    QStackedWidget* m_stack;
    InternalEditor* m_edit;
    QTextEdit* m_preview;
    MarkdownHighlighter* m_highlighter;
    QVariantMap m_currentNote;
    bool m_isRichText = false;
    double m_zoomFactor = 1.0;
};

#endif // EDITOR_H