#ifndef NOTEEDITWINDOW_H
#define NOTEEDITWINDOW_H

#include <QWidget>
#include <QLineEdit>
#include "ClickableLineEdit.h"
#include <QComboBox>
#include <QTextEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QButtonGroup>
#include <QVBoxLayout>
#include <QSplitter>
#include <QLabel>
#include "Editor.h" 

class QShortcut;

class NoteEditWindow : public QWidget {
    Q_OBJECT
public:
    explicit NoteEditWindow(int noteId = 0, QWidget* parent = nullptr);
    void setDefaultCategory(int catId);
    /**
     * @brief 预设初始数据 (用于合并数据等场景)
     * 2026-04-xx 按照用户要求：支持从外部载入初始标题、内容与标签
     */
    void setInitialData(const QString& title, const QString& content, const QStringList& tags);

signals:
    void noteSaved();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    void initUI();
    void setupShortcuts();
    void updateShortcuts();
    void loadNoteData(int id);
    void setupLeftPanel(QVBoxLayout* layout);
    void setupRightPanel(QVBoxLayout* layout);
    QPushButton* createColorBtn(const QString& color, int id);
    
private slots:
    void toggleMaximize();
    void toggleStayOnTop();
    void saveNote();
    void onSaveFinished();
    void toggleSearchBar();
    void openTagSelector();
    void openExpandedTitleEditor();

private:
    int m_noteId;
    int m_catId = -1;
    
    // 原始属性记录，防止编辑保存时破坏元数据 (用于 updateNote)
    QString m_origItemType;
    QByteArray m_origBlob;
    QString m_sourceApp;
    QString m_sourceTitle;

    // 窗口控制
    bool m_isMaximized = false;
    bool m_isStayOnTop = false;
    QRect m_normalGeometry;
    QPoint m_dragPos;
    
    // UI 控件引用
    QWidget* m_titleBar;
    QLabel* m_winTitleLabel;
    QPushButton* m_maxBtn;
    QPushButton* m_btnStayOnTop;
    QSplitter* m_splitter;
    QTextEdit* m_titleEdit;
    ClickableLineEdit* m_tagEdit;
    QList<QShortcut*> m_shortcutObjs;
    QButtonGroup* m_colorGroup;
    QCheckBox* m_defaultColorCheck;
    Editor* m_contentEdit;
    QTextEdit* m_remarkEdit = nullptr;

    // 搜索栏
    QWidget* m_searchBar;
    QLineEdit* m_searchEdit;
};

#endif // NOTEEDITWINDOW_H