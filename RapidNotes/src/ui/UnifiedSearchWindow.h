#ifndef UNIFIEDSEARCHWINDOW_H
#define UNIFIEDSEARCHWINDOW_H

#include "FramelessDialog.h"
#include "ResizeHandle.h"
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QThread>
#include <QStackedWidget>
#include <atomic>

class FileSearchHistoryPopup;

/**
 * @brief 扫描线程 (从 FileSearchWindow 移动)
 */
class UnifiedScannerThread : public QThread {
    Q_OBJECT
public:
    explicit UnifiedScannerThread(const QString& folderPath, QObject* parent = nullptr);
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
 * @brief 文件夹收藏侧边栏 (左侧)
 */
class FolderSidebarListWidget : public QListWidget {
    Q_OBJECT
public:
    explicit FolderSidebarListWidget(QWidget* parent = nullptr);
signals:
    void folderDropped(const QString& path);
protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
};

/**
 * @brief 文件收藏侧边栏 (右侧)
 */
class FileCollectionSidebarWidget : public QListWidget {
    Q_OBJECT
public:
    explicit FileCollectionSidebarWidget(QWidget* parent = nullptr);
signals:
    void filesDropped(const QStringList& paths);
protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
};

/**
 * @brief 文件查找组件 (中间区域 - 页面1)
 */
class FileSearchContentWidget : public QWidget {
    Q_OBJECT
public:
    explicit FileSearchContentWidget(QWidget* parent = nullptr);
    ~FileSearchContentWidget();

    void setPath(const QString& path);
    QString getPath() const;
    void startScan(const QString& path);

    // 历史记录操作接口 (由 UnifiedSearchWindow 调用)
    void addHistoryEntry(const QString& path);
    void addSearchHistoryEntry(const QString& text);

signals:
    void addFileToCollection(const QString& path);
    void scanStarted(const QString& path);

private slots:
    void selectFolder();
    void onPathReturnPressed();
    void onFileFound(const QString& name, const QString& path, bool isHidden);
    void onScanFinished(int count);
    void refreshList();
    void showFileContextMenu(const QPoint& pos);
    void copySelectedFiles();
    void onEditFile();
    void onCutFile();
    void onDeleteFile();
    void onMergeSelectedFiles();
    void updateShortcuts();

private:
    void initUI();
    
    QLineEdit* m_pathInput;
    QLineEdit* m_searchInput;
    QLineEdit* m_extInput;
    QLabel* m_infoLabel;
    QCheckBox* m_showHiddenCheck;
    QListWidget* m_fileList;
    
    UnifiedScannerThread* m_scanThread = nullptr;
    FileSearchHistoryPopup* m_pathPopup = nullptr;
    FileSearchHistoryPopup* m_searchPopup = nullptr;
    
    struct FileData {
        QString name;
        QString path;
        bool isHidden;
    };
    QList<FileData> m_filesData;
    int m_visibleCount = 0;
    int m_hiddenCount = 0;

    friend class UnifiedSearchWindow;
};

// 预留 KeywordSearchWidget 的前置声明 (假设我们继续使用 KeywordSearchWidget)
class KeywordSearchWidget;

/**
 * @brief 统一搜索窗口：整合文件查找与关键字查找
 */
class UnifiedSearchWindow : public FramelessDialog {
    Q_OBJECT
public:
    explicit UnifiedSearchWindow(QWidget* parent = nullptr);
    ~UnifiedSearchWindow();

    void switchToPage(int index);

    // 暴露给内部组件的历史记录接口
    void addHistoryEntry(bool isPath, const QString& path);
    void addSearchHistoryEntry(const QString& text);
    QStringList getHistory(bool isPath) const;
    void clearHistory(bool isPath);
    void removeHistoryEntry(bool isPath, const QString& text);

    // 允许内部组件访问私有成员
    friend class FileSearchContentWidget;
    friend class KeywordSearchWidget;

protected:
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void initUI();
    void setupStyles();
    
    // 收藏夹持久化
    void loadFavorites();
    void saveFavorites();
    void loadCollection();
    void saveCollection();
    
    void addFavorite(const QString& path);
    void addCollectionItem(const QString& path);
    void onMergeFiles(const QStringList& filePaths, const QString& rootPath, bool useCombineDir = false);

    // 侧边栏
    FolderSidebarListWidget* m_folderSidebar;
    FileCollectionSidebarWidget* m_fileCollectionSidebar;
    
    // 中间区域
    QPushButton* m_btnFileSearch;
    QPushButton* m_btnKeywordSearch;
    QStackedWidget* m_stackedWidget;
    FileSearchContentWidget* m_fileSearchWidget;
    KeywordSearchWidget* m_keywordSearchWidget;
    
    bool verifyExportPermission(); // 2026-03-20 增加导出前的统一身份验证逻辑

    ResizeHandle* m_resizeHandle;
};

#endif // UNIFIEDSEARCHWINDOW_H
