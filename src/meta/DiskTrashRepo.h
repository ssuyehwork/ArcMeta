#pragma once 
#include <vector> 
#include <string> 
 
namespace ArcMeta { 
 
struct DiskTrashRawItem { 
    int id; 
    std::wstring trashPath; 
    std::wstring originalPath; 
    std::wstring fileName; 
    bool isFolder; 
    long long fileSize; 
    long long deletedAt; 
}; 
 
class DiskTrashRepo { 
public: 
    // 获取当前活动连接库中的所有物理回收记录（通过 DatabaseManager 全局互斥锁保证绝对多线程安全） 
    static std::vector<DiskTrashRawItem> getAllTrashItems(); 
}; 
 
} 
