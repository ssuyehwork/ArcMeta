#pragma once

#include <QMap>
#include <QString>

namespace ArcMeta {

/**
 * @brief 全局统一的检索与统计快照结构体
 */
struct ScanStats {
    QMap<int, int> ratingCounts;
    QMap<QString, int> colorCounts;
    QMap<QString, int> typeCounts;
    QMap<QString, int> createDateCounts;
    QMap<QString, int> modifyDateCounts;
    int emptyFolderCount = 0;

    int hasLinkCount = 0;
    int noLinkCount = 0;
    int hasNoteCount = 0;
    int noNoteCount = 0;
    int ratioHorizontalCount = 0;
    int ratioVerticalCount = 0;
    int ratioSquareCount = 0;
    int ratio169Count = 0;
    int duplicateCount = 0;
    int uniqueCount = 0;
};

} // namespace ArcMeta
