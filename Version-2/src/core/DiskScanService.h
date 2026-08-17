#pragma once

#include <QString>
#include <vector>
#include <functional>
#include "ItemRecord.h"

namespace ArcMeta {

/**
 * @brief 【物理隔离模块一】纯磁盘导航扫描服务
 *
 * 红线：本文件绝不允许 #include MetadataManager.h / CategoryRepo.h / AssetImporter.h。
 * 只使用 QDir / QFileInfo 做原始文件系统遍历，原样展示物理文件夹与文件名，
 * 绝不对 .arc 容器做任何解包翻译——这是磁盘模式与内存数据库模式 100% 隔离的物理保证。
 */
class DiskScanService {
public:
    /**
     * @brief 扫描指定物理路径，返回原始条目列表（不做任何 .arc 语义翻译）
     * @param path 起始物理路径
     * @param recursive 是否递归扫描子目录
     * @param shouldContinue 取消检查回调；每处理一个条目前调用一次，返回 false 时立即中止扫描
     */
    static std::vector<ItemRecord> scanDirectory(const QString& path,
                                                  bool recursive,
                                                  const std::function<bool()>& shouldContinue);
};

} // namespace ArcMeta
