#pragma once 
#include <QString> 
 
namespace QuarkMeta { 
class FileFilterService { 
public: 
    // 统一过滤无用辅助配置文件、缩略图、系统缓存目录及 .arc 资产包 
    static bool isAuxiliaryFile(const QString& path, bool filterArc = true); 
}; 
} 
