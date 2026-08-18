#include "MetaCacheDecorator.h" 
#include "QuarkMetaJson.h" 
#include <QFileInfo> 
#include <unordered_map> 
#include <memory> 
 
namespace QuarkMeta { 
void MetaCacheDecorator::decorate(std::vector<ItemRecord>& records) { 
    if (records.empty()) return; 
 
    // 按父目录路径建立离散 JSON 缓存池，避免重复读取同一目录的配置文件 
    std::unordered_map<std::wstring, std::shared_ptr<QuarkMetaJson>> jsonCacheMap; 
 
    for (auto& itemRec : records) { 
        if (itemRec.isCategory) continue; 
 
        QFileInfo info(itemRec.path); 
        std::wstring dirPath = info.absolutePath().toStdWString(); 
 
        auto cacheIt = jsonCacheMap.find(dirPath); 
        if (cacheIt == jsonCacheMap.end()) { 
            auto jsonCache = std::make_shared<QuarkMetaJson>(dirPath); 
            jsonCache->load(); 
            jsonCacheMap[dirPath] = jsonCache; 
            cacheIt = jsonCacheMap.find(dirPath); 
        } 
 
        const auto& cachedItems = cacheIt->second->items(); 
        std::wstring fileName = info.fileName().toStdWString(); 
         
        auto it = cachedItems.find(fileName); 
        if (it != cachedItems.end()) { 
            itemRec.rating = it->second.rating; 
            itemRec.manualColor = QString::fromStdWString(it->second.color); 
            itemRec.pinned = it->second.pinned; 
            itemRec.note = QString::fromStdWString(it->second.note); 
            itemRec.url = QString::fromStdWString(it->second.url); 
            itemRec.tags.clear(); 
            for (const auto& t : it->second.tags) { 
                itemRec.tags.append(QString::fromStdWString(t)); 
            } 
            itemRec.width = it->second.width; 
            itemRec.height = it->second.height; 
            itemRec.autoColor = QString::fromStdWString(it->second.autoColor); 
            itemRec.added_at = it->second.addedAt; 
 
            itemRec.palettes.clear(); 
            for (const auto& pe : it->second.palettes) { 
                itemRec.palettes.push_back({pe.color, pe.ratio}); 
            } 
        } 
    } 
} 
} 
