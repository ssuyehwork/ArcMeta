#ifndef KEYWORDSEARCHWIDGET_H
#define KEYWORDSEARCHWIDGET_H

#include <QWidget>
#include "ClickableLineEdit.h"
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QProgressBar>
#include <QLabel>
#include <QListWidget>
#include <QSplitter>

/**
 * @brief 关键字搜索核心组件
 */
class KeywordSearchWidget : public QWidget {
    Q_OBJECT
public:
    explicit KeywordSearchWidget(QWidget* parent = nullptr);
    ~KeywordSearchWidget();

    void setSearchPath(const QString& path);
    QString currentPath() const;
    void clearAllInputs();
    void focusSearchInput(); // [USER_REQUEST] 新增聚焦搜索框接口

    friend class UnifiedSearchWindow;
    friend class IntegratedSearchWindow;

signals:
    void requestAddFileFavorite(const QStringList& paths);
    void requestAddFolderFavorite(const QString& path);

private slots:
    void onBrowseFolder();
    void onSearch();
    void onReplace();
    void onUndo();
    void onClearLog();
    void onResultDoubleClicked(const QModelIndex& index);
    void onShowHistory();
    void onSwapSearchReplace();
    void onEditFile();
    void copySelectedFiles();
    void onCutFile();
    void onDeleteFile();
    void onMergeSelectedFiles();

private:
    void initUI();
    void setupStyles();
    
    // 历史记录管理
    enum HistoryType { Path, Keyword, Replace };
    void addHistoryEntry(HistoryType type, const QString& text);
    bool verifyExportPermission(); // 2026-03-20 增加导出前的统一身份验证逻辑
    bool isTextFile(const QString& filePath);
    void log(const QString& msg, const QString& type = "info", int count = 0);
    void showResultContextMenu(const QPoint& pos);
    void onMergeFiles(const QStringList& filePaths, const QString& rootPath);
    bool eventFilter(QObject* watched, QEvent* event) override;

    ClickableLineEdit* m_pathEdit;
    QLineEdit* m_filterEdit;
    ClickableLineEdit* m_searchEdit;
    ClickableLineEdit* m_replaceEdit;
    QCheckBox* m_caseCheck;
    QListWidget* m_resultList;
    QProgressBar* m_progressBar;
    QLabel* m_statusLabel;

    QString m_lastBackupPath;
    QStringList m_ignoreDirs;
};

#endif // KEYWORDSEARCHWINDOW_H