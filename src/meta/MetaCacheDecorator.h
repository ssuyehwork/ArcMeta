#pragma once 
#include "../core/ItemRecord.h" 
#include <vector> 
 
namespace QuarkMeta { 
class MetaCacheDecorator { 
public: 
    // 按条目物理父目录自动建立缓存池并线程安全地装配离散 JSON 业务元数据（全面支持单级与多级递归目录） 
    static void decorate(std::vector<ItemRecord>& records); 
}; 
} 
