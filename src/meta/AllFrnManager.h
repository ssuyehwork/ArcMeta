#pragma once

#include <QString>
#include <QMap>
#include <string>

namespace ArcMeta {

/**
 * @brief 全局 FRN 与 metadata.scch 映射管理器
 * 2026-05-17 按照用户要求：用于在项目根目录记录与同步所有 metadata.scch 的 FRN，实现分布式 SCCH 内存对账
 */
class AllFrnManager {
public:
    /**
     * @brief 登记 FRN 及其物理路径
     * @param frn 文件夹的 NTFS FRN 物理身份
     * @param path 该目录当前的绝对物理路径
     */
    static void registerFrn(const std::wstring& frn, const std::wstring& path);

    /**
     * @brief 获取所有已登记的 FRN -> 物理路径映射表
     */
    static QMap<QString, QString> getAllFrns();

    /**
     * @brief 立即保存 FRN 映射表到磁盘
     * 2026-06-xx 物理加固：支持手动触发持久化
     */
    static void saveImmediately();
};

} // namespace ArcMeta
