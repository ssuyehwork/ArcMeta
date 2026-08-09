#pragma once

#include <QString>
#include <QStringList>
#include <vector>
#include <QColor>
#include <utility>
#include <string>

namespace ArcMeta {

// 前置声明 RuntimeMeta，避免循环依赖
struct RuntimeMeta;

/**
 * @brief 轻量级条目记录，用于虚拟化模型索引
 */
struct ItemRecord {
    QString volume;
    QString frn;
    QString path; 
    bool isDir = false;
    bool isCategory = false;
    int categoryId = 0;
    QString categoryName;
    QString categoryColor;

    // 双轨回收站与分组展示专属字段
    bool isGroupHeader = false;
    QString groupName;
    bool isDiskTrash = false;
    int diskTrashId = 0;
    QString originalPath;

    // 2026-06-xx 物理对标：注入核心元数据，杜绝 UI 渲染时的同步 I/O
    int rating = 0;
    QString manualColor;
    QString autoColor;
    QStringList tags;
    std::string folderId;
    bool pinned = false;
    bool encrypted = false;
    double registrationProgress = -1.0; // 初始为 -1.0 表示未计算
    QString url;  // 2026-07-xx 支撑筛选：链接
    QString note; // 2026-07-xx 支撑筛选：备注
    int width = 0;
    int height = 0;

    // 2026-06-xx 极致优化：预取物理属性，实现渲染零 I/O
    long long size = 0;
    long long mtime = 0;
    long long ctime = 0;
    long long atime = 0;
    long long added_at = 0;
    bool isEmpty = false;
    bool isManaged = false; // 预存受控状态
    QString suffix;
    QString filename; // 缓存文件名以供排序时 O(1) 提取，消除高频 QFileInfo 构造开销
    std::vector<std::pair<QColor, float>> palettes; // 烘焙物理色板，消除 filterAcceptsRow 锁争抢

    static ItemRecord create(const QString& path, const RuntimeMeta* providedMeta = nullptr, bool isFromMemory = false);
    static void fromMetadata(ItemRecord& r, const RuntimeMeta& meta);
};

} // namespace ArcMeta
