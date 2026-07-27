#include "FileSystemService.h"
#include "util/ShellHelper.h"
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QUrl>
#include <QDir>

namespace ArcMeta {

FileSystemService& FileSystemService::instance() {
    static FileSystemService inst;
    return inst;
}

FileSystemService::FileSystemService(QObject* parent) : QObject(parent) {}

void FileSystemService::performCopy(const QStringList& paths, bool cutMode) {
    QList<QUrl> urls; 
    for (const auto& path : paths) { 
        if (!path.isEmpty()) urls << QUrl::fromLocalFile(path); 
    } 
 
    if (urls.isEmpty()) return; 
 
    QMimeData* mime = new QMimeData(); 
    mime->setUrls(urls); 
     
    if (cutMode) { 
        QByteArray effectData; 
        effectData.append((char)2);  
        mime->setData("Preferred DropEffect", effectData); 
    } 
 
    QApplication::clipboard()->setMimeData(mime); 
}

bool FileSystemService::performPaste(const QString& currentPath, QStringList& outFromPaths, bool& outIsMove) {
    if (currentPath.isEmpty() || currentPath == "computer://") return false; 
 
    const QMimeData* mime = QApplication::clipboard()->mimeData(); 
    if (!mime || !mime->hasUrls()) return false; 
 
    QList<QUrl> urls = mime->urls(); 
    for (const QUrl& url : urls) {
        outFromPaths << url.toLocalFile();
    }
     
    if (outFromPaths.isEmpty()) return false; 
 
    outIsMove = false; 
    if (mime->hasFormat("Preferred DropEffect")) { 
        QByteArray effect = mime->data("Preferred DropEffect"); 
        if (!effect.isEmpty() && (effect.at(0) & 0x02)) outIsMove = true; 
    } 
 
    return ShellHelper::copyOrMoveItems(outFromPaths, currentPath, outIsMove);
}

} // namespace ArcMeta
