#include "DiskScanService.h" 
#include "FileFilterService.h" 
#include <QDir> 
#include <QFileInfo> 
 
namespace ArcMeta { 
 
std::vector<ItemRecord> DiskScanService::scanDirectory(const QString& path, 
                                                        bool recursive, 
                                                        const std::function<bool()>& shouldContinue) { 
    std::vector<ItemRecord> allItems; 
 
    std::function<void(const QString&, bool)> scanDir; 
    scanDir = [&](const QString& p, bool rec) { 
        QDir dir(p); 
        if (!dir.exists()) return; 
 
        QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name); 
        for (const QFileInfo& info : entries) { 
            if (shouldContinue && !shouldContinue()) return; 
 
            QString absPath = info.absoluteFilePath(); 
             
            // 🚨 统一调用文件过滤服务（归一化处理所有辅助文件、.arc、.arcmeta） 
            if (FileFilterService::isAuxiliaryFile(absPath)) continue; 
 
            ItemRecord itemRec = ItemRecord::create(absPath, nullptr, false); 
            allItems.push_back(itemRec); 
 
            if (rec && info.isDir()) { 
                scanDir(absPath, true); 
            } 
        } 
    }; 
 
    scanDir(path, recursive); 
    return allItems; 
}

} // namespace ArcMeta
