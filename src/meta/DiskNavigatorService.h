#pragma once 
#include <string> 
#include <unordered_map> 
#include <functional> 
#include "MetadataDefs.h" 
#include "AmMetaJson.h" 

namespace ArcMeta { 
class DiskNavigatorService { 
public: 
    static DiskNavigatorService& instance(); 

    // 1. 读取指定物理目录下的所有项目的离散元数据 (加载 .QuarkMeta.json)
    std::unordered_map<std::wstring, ItemMeta> loadDirectoryItems(const std::wstring& folderPath); 

    // 2. 获取单个磁盘文件的 ItemMeta 
    bool getItemMeta(const std::wstring& filePath, ItemMeta& outMeta); 

    // 3. 修改并原子落盘单个项目的元数据到其父目录 .QuarkMeta.json (若是目录则同步自身)
    void saveItemMeta(const std::wstring& filePath, std::function<void(ItemMeta&)> updater); 

    // 4. 本地文件/文件夹重命名时同步更新 JSON 记录 
    void handleDiskRename(const std::wstring& oldPath, const std::wstring& newPath, bool isDir); 

private: 
    DiskNavigatorService() = default; 
    ~DiskNavigatorService() = default; 
}; 
} 
