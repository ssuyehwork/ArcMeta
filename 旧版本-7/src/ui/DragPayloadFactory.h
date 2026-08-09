#pragma once 
 
#include <QMimeData> 
#include <QModelIndexList> 
#include <QStringList> 
 
namespace ArcMeta { 
 
class DragPayloadFactory { 
public: 
    // 从选中索引列表快速提取 QUrl 列表，无磁盘 IO 阻塞 
    static QMimeData* createMimeDataFromIndexes(const QModelIndexList& indexes); 
 
    // 快速判定 MIME 数据是否包含本地文件格式 (O(1) 匹配，不解析完整文本) 
    static bool hasLocalUriFormat(const QMimeData* mimeData); 
 
    // 从 MIME 数据中提取绝对物理路径列表 
    static QStringList extractPathsFromMime(const QMimeData* mimeData); 
}; 
 
} // namespace ArcMeta 
