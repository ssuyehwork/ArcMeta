#ifndef FILESEARCHWIDGET_H
#define FILESEARCHWIDGET_H

#include <QWidget>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QThread>
#include <QPair>
#include <QSplitter>
#include <QLabel>
#include <atomic>

class FileSearchHistoryPopup;

/**
 * @brief 扫描线程：实现增量扫描与目录剪枝
 */
class ScannerThread : public QThread {
    Q_OBJECT
public:
    explicit ScannerThread(const QString& folderPath, QObject* parent = nullptr);
    void stop();

signals:
    void fileFound(const QString& name, const QString& path, bool isHidden);
    void finished(int count);

protected:
    void run() override;

private:
    QString m_folderPath;
    std::atomic<bool> m_isRunning{true};
};

/**
 * @brief 文件查找核心部件
 */
class FileSearchWidget : public QWidget {
    Q_OBJECT
public:
    explicit FileSearchWidget(QWidget* parent = nullptr);
    ~FileSearchWidget();

    // 历史记录操作接口
    void addHistoryEntry(const QString& path);
    QStringList getHistory() const;
    void clearHistory();
    void removeHistoryEntry(const QString& path);
    void useHistoryPath(const QString& path);
    void setSearchPath(const QString& path);
    QString currentPath() const;
    void clearAllInputs();
    void focusSearchInput(); // [USER_REQUEST] 新增聚焦搜索框接口

signals:
    void requestAddFileFavorite(const QStringList& paths);
    void requestAddFolderFavorite(const QString& path);

public:
    // 文件名搜索历史相关
    void addSearchHistoryEntry(const QString& text);
    QStringList getSearchHistory() const;
    void removeSearchHistoryEntry(const QString& text);
    void clearSearchHistory();

    // 后缀名搜索历史相关
    void addExtHistoryEntry(const QString& text);
    QStringList getExtHistory() const;
    void removeExtHistoryEntry(const QString& text);
    void clearExtHistory();

private slots:
    void selectFolder();
    void onFavoriteFile();
    void onPathReturnPressed();
    void startScan(const QString& path);
    void onFileFound(const QString& name, const QString& path, bool isHidden);
    void onScanFinished(int count);
    void refreshList();
    void showFileContextMenu(const QPoint& pos);
    void copySelectedFiles();
    void onEditFile();
    void onCutFile();
    void onDeleteFile();
    void onMergeSelectedFiles();
    void onMergeFolderContent();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void initUI();
    void setupStyles();
    void saveFileFavorites();
    void refreshFileFavoritesList(const QString& filterPath = QString());
    void onMergeFiles(const QStringList& filePaths, const QString& rootPath);

    QLineEdit* m_pathInput;
    QLineEdit* m_searchInput;
    QLineEdit* m_extInput;
    QLabel* m_infoLabel;
    QCheckBox* m_showHiddenCheck;
    QListWidget* m_fileList;
    
    ScannerThread* m_scanThread = nullptr;
    FileSearchHistoryPopup* m_historyPopup = nullptr;
    
    struct FileData {
        QString name;
        QString path;
        bool isHidden;
    };
    QList<FileData> m_filesData;
    int m_visibleCount = 0;
    int m_hiddenCount = 0;
    bool verifyExportPermission(); // 2026-03-20 增加导出前的统一身份验证逻辑
};

#endif // FILESEARCHWIDGET_H
