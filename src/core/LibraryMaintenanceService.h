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
 
    // 🚀 【SRP 拆分】：专门承接后台物理磁盘托管包盘点与 SQLite 幽灵数据异步强力清除逻辑 
    void scanAndCleanEmptyArcsAsync(); 
 
signals: 
    void cleanProgress(int percent); 
    void cleanFinished(int cleanCount, int ghostCount, int orphanCount); 
 
private: 
    explicit LibraryMaintenanceService(QObject* parent = nullptr) : QObject(parent) {} 
}; 
 
} // namespace ArcMeta 
