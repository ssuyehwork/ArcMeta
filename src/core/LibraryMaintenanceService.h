#pragma once 
#include <QObject> 
#include <QStringList> 
 
namespace ArcMeta { 
 
class LibraryMaintenanceService : public QObject { 
    Q_OBJECT 
public: 
    static LibraryMaintenanceService& instance() { 
        static LibraryMaintenanceService inst; 
        return inst; 
    } 
 
    // 🚀 【SRP 拆分】：后台专职三步走完整清扫（空包物理删除 + 幽灵元数据擦除 + 孤立分类关系清洗） 
    void scanAndCleanEmptyArcsAsync(); 
 
signals: 
    void cleanFinished(int cleanCount, int ghostCount, int orphanCount); 
 
private: 
    explicit LibraryMaintenanceService(QObject* parent = nullptr) : QObject(parent) {} 
}; 
 
} // namespace ArcMeta 
