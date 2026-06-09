#ifndef INTEGRATEDSEARCHWINDOW_H
#define INTEGRATEDSEARCHWINDOW_H

#include "FramelessDialog.h"
#include <QTabWidget>
#include <QListWidget>
#include <QSplitter>

class FileSearchWidget;
class KeywordSearchWidget;

class IntegratedSearchWindow : public FramelessDialog {
    Q_OBJECT
public:
    enum SearchType { FileSearch = 0, KeywordSearch = 1 };
    explicit IntegratedSearchWindow(QWidget* parent = nullptr);
    ~IntegratedSearchWindow();
    void setCurrentTab(SearchType type);

protected:
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onSidebarItemClicked(QListWidgetItem* item);
    void showSidebarContextMenu(const QPoint& pos);
    void onMergeCollectionFiles();

private:
    void initUI();
    void setupStyles();
    void loadFavorites();
    void saveFavorites();
    void loadCollection();
    void saveCollection();
    void addFavorite(const QString& path);
    void addCollectionItem(const QString& path);
    void onMergeFiles(const QStringList& filePaths, const QString& rootPath, bool useCombineDir = false);

    QTabWidget* m_tabWidget;
    FileSearchWidget* m_fileSearchWidget;
    KeywordSearchWidget* m_keywordSearchWidget;
    QListWidget* m_sidebar;
    QListWidget* m_collectionSidebar;
    class ResizeHandle* m_resizeHandle;
};

#endif // INTEGRATEDSEARCHWINDOW_H
