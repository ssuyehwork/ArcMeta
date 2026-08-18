#include "FileFilterService.h" 
#include <QFileInfo> 
 
namespace ArcMeta { 
bool FileFilterService::isAuxiliaryFile(const QString& path, bool filterArc) {
    if (path.isEmpty()) return true; 
 
    QFileInfo info(path); 
    QString fileName = info.fileName(); 
 
    // 1. 过滤内部配置文件与缩略图 
    if (fileName.endsWith(".QuarkMeta.json", Qt::CaseInsensitive) ||
        fileName.endsWith("_thumbnail.png", Qt::CaseInsensitive)) { 
        return true;  
    } 
 
    // 2. 过滤缓存目录与 .arc 系统资产包（使其在目录树遍历中隐形） 
    if (fileName.compare(".arcmeta", Qt::CaseInsensitive) == 0) { 
        return true; 
    } 
    
    if (filterArc && fileName.endsWith(".arc", Qt::CaseInsensitive)) {
        return true;
    }
 
    return false; 
} 
} 
