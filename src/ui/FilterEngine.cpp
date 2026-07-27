#include "FilterEngine.h"
#include "UiHelper.h"
#include <cmath>

namespace ArcMeta {

FilterEngine& FilterEngine::instance() {
    static FilterEngine inst;
    return inst;
}

bool FilterEngine::acceptsRow(const FilterState& currentFilter, const IngestedRecord& record, const QString& fileName) const {
    // --- 按照 Plan-73 & Plan-94：显示/隐藏文件夹/文件与筛选联动 ---
    // 2026-07-xx 逻辑校准：子分类在逻辑上等同于文件夹，受 showFolders 控制
    if (record.isCategory || record.isDir) {
        // 2026-07-xx Plan-94: 判定用户是否在筛选面板中显式勾选了“文件夹”或匹配的“空文件夹”
        bool isFolderExplicitlySelected = currentFilter.types.contains("folder") || 
                                         (record.isEmpty && currentFilter.types.contains("空文件夹"));
        
        // 只有当“顶栏全局开关为隐藏”且“筛选器未显式勾选文件夹”时，才执行拦截
        if (!currentFilter.showFolders && !isFolderExplicitlySelected) {
            return false;
        }
    } else {
        if (!currentFilter.showFiles) return false;
    }

    // 1. 评级过滤 
    if (!currentFilter.ratings.isEmpty()) { 
        int r = record.rating; // 直接从烘焙好的 record 获取，消除 idx.data 虚拟调用开销
        if (!currentFilter.ratings.contains(r)) return false; 
    } 

    // 2. 颜色过滤 (Plan-18: 基于 CIELAB Delta E 的感知筛选逻辑)
    if (!currentFilter.colors.isEmpty() || !currentFilter.colorFilterText.isEmpty()) { 
        bool matchColor = false;

        // 计算自动提取色的匹配面积占比
        auto calculateAutoColorMatchedArea = [&](const QColor& targetCol) -> float {
            if (!targetCol.isValid()) return 0.0f;
            float totalMatchedArea = 0.0f;

            // Case A: 有调色盘数据，累加所有符合色差要求的色块占比
            if (!record.palettes.empty()) {
                for (const auto& pe : record.palettes) {
                    if (UiHelper::calculateDeltaE(targetCol, pe.first) < currentFilter.colorTolerance) {
                        totalMatchedArea += pe.second;
                    }
                }
            } else if (!record.autoColor.isEmpty()) {
                // Case B: 仅有自动主色调数据，若自动主色匹配则占比视为 100%
                QColor recordCol = UiHelper::parseColorName(record.autoColor);
                if (UiHelper::calculateDeltaE(targetCol, recordCol) < currentFilter.colorTolerance) {
                    totalMatchedArea = 1.0f;
                }
            }
            return totalMatchedArea;
        };

        // 判断特定的 targetCol 是否与当前记录匹配（结合手动色与自动色）
        auto isColorMatched = [&](const QColor& targetCol) -> bool {
            if (!targetCol.isValid()) return false;

            // 1. 检查手动色：单一颜色值匹配，不受最小面积占比限制
            if (!record.manualColor.isEmpty()) {
                QColor recordCol = UiHelper::parseColorName(record.manualColor);
                if (UiHelper::calculateDeltaE(targetCol, recordCol) < currentFilter.colorTolerance) {
                    return true;
                }
            }

            // 2. 检查自动色：利用 palettes 占比及 minColorArea 限制
            float area = calculateAutoColorMatchedArea(targetCol);
            if (area > 0.0f && area * 100.0f >= (float)currentFilter.minColorArea) {
                return true;
            }

            return false;
        };

        // 2.0 文本过滤逻辑 (如果存在文本)
        if (!currentFilter.colorFilterText.isEmpty()) {
            QString searchText = currentFilter.colorFilterText.trimmed();
            // 物理规则：支持名称、色值或“无色标”
            if (searchText == "无色标") {
                if (record.manualColor.isEmpty() && record.autoColor.isEmpty()) matchColor = true;
            } else if (searchText.startsWith("#")) {
                QColor targetCol = UiHelper::parseColorName(searchText);
                if (isColorMatched(targetCol)) matchColor = true;
            } else {
                // 模糊匹配颜色名称 (通过反查 colorMap)
                static const QMap<QString, QString> nameToHex = {
                    {"红", "#E24B4A"}, {"橙", "#EF9F27"}, {"黄", "#FECF0E"}, {"绿", "#639922"},
                    {"青", "#1D9E75"}, {"蓝", "#378ADD"}, {"紫", "#7F77DD"}, {"灰", "#5F5E5A"},
                    {"黑", "#000000"}, {"白", "#FFFFFF"}
                };
                for (auto it = nameToHex.begin(); it != nameToHex.end(); ++it) {
                    if (it.key().contains(searchText)) {
                        QColor targetCol = QColor(it.value());
                        if (isColorMatched(targetCol)) { matchColor = true; break; }
                    }
                }
            }
            if (!matchColor) return false; // 文本过滤不通过
        }

        // 2.1 勾选框过滤 (如果存在勾选)
        if (!currentFilter.colors.isEmpty()) {
            matchColor = false;
            for (const QString& fc : currentFilter.colors) {
                // 特殊情况：无色标 (不涉及占比逻辑)
                if (fc.isEmpty()) {
                    if (record.manualColor.isEmpty() && record.autoColor.isEmpty()) { matchColor = true; break; }
                    continue;
                }

                QColor targetCol = UiHelper::parseColorName(fc);
                if (isColorMatched(targetCol)) {
                    matchColor = true;
                    break;
                }
            }
            if (!matchColor) return false;
        }
    }

    // 4. 类型过滤 
    if (!currentFilter.types.isEmpty() || !currentFilter.typeFilterText.isEmpty()) { 
        QString type = (record.isDir || record.isCategory) ? "folder" : "file";
        QString ext = record.isCategory ? "" : record.suffix.toUpper();
        bool matchType = false; 

        if (!currentFilter.typeFilterText.isEmpty()) {
            QString searchText = currentFilter.typeFilterText.trimmed();
            if (searchText == "文件夹" || searchText.toLower() == "folder") {
                if (type == "folder") matchType = true;
            } else if (searchText == "空文件夹") {
                if (type == "folder" && record.isEmpty) matchType = true;
            } else {
                if (ext.contains(searchText.toUpper())) matchType = true;
            }
            if (!matchType) return false;
        }

        if (!currentFilter.types.isEmpty()) {
            matchType = false;
            for (const QString& fType : currentFilter.types) { 
                if (fType == "folder") { 
                    if (type == "folder") { matchType = true; break; } 
                } else if (fType == "file") {
                    if (type != "folder") { matchType = true; break; }
                } else if (fType == "空文件夹") {
                    if (type == "folder" && record.isEmpty) { matchType = true; break; }
                } else { 
                    if (ext == fType.toUpper()) { matchType = true; break; } 
                } 
            } 
            if (!matchType) return false; 
        }
    } 

    // 5. 创建日期过滤 
    if (!currentFilter.createDates.isEmpty() || !currentFilter.createDateFilterText.isEmpty()) { 
        QDate d = QDateTime::fromMSecsSinceEpoch(record.ctime).date();
        QString dStr = d.toString("dd-MM-yyyy"); 
        bool matchDate = false; 

        if (!currentFilter.createDateFilterText.isEmpty()) {
            if (dStr.contains(currentFilter.createDateFilterText.trimmed())) matchDate = true;
            if (!matchDate) return false;
        }

        if (!currentFilter.createDates.isEmpty()) {
            matchDate = false;
            for (const QString& fDate : currentFilter.createDates) { 
                if (fDate == dStr) { matchDate = true; break; } 
            } 
            if (!matchDate) return false; 
        }
    } 

    // 7. 链接过滤 (Plan-30)
    if (currentFilter.linkPresence != FilterState::All) {
        bool hasLink = !record.url.isEmpty();
        if (currentFilter.linkPresence == FilterState::Yes && !hasLink) return false;
        if (currentFilter.linkPresence == FilterState::No && hasLink) return false;
    }

    // 8. 备注过滤 (Plan-30)
    if (currentFilter.notePresence != FilterState::All) {
        bool hasNote = !record.note.isEmpty();
        if (currentFilter.notePresence == FilterState::Yes && !hasNote) return false;
        if (currentFilter.notePresence == FilterState::No && hasNote) return false;
    }

    // 9. 文件大小过滤 (Plan-30)
    if (currentFilter.minSize != -1 && record.size < currentFilter.minSize) return false;
    if (currentFilter.maxSize != -1 && record.size > currentFilter.maxSize) return false;

    // 10. 图像比例过滤 (Plan-29)
    if (currentFilter.ratio != FilterState::AspectAny) {
        // 直接使用 record 中缓存的尺寸信息 (Plan-30 优化：避免重复查询元数据管理器)
        if (record.width > 0 && record.height > 0) {
            double r = (double)record.width / record.height;
            if (currentFilter.ratio == FilterState::Horizontal && record.width <= record.height) return false;
            if (currentFilter.ratio == FilterState::Vertical && record.height <= record.width) return false;
            if (currentFilter.ratio == FilterState::Square && std::abs(r - 1.0) > 0.05) return false;
            if (currentFilter.ratio == FilterState::Ratio169 && std::abs(r - 1.77) > 0.05) return false;
        } else {
            return false; // 无尺寸信息不匹配任何比例筛选
        }
    }

    // 6. 修改日期过滤 
    if (!currentFilter.modifyDates.isEmpty() || !currentFilter.modifyDateFilterText.isEmpty()) { 
        QDate d = QDateTime::fromMSecsSinceEpoch(record.mtime).date();
        QString dStr = d.toString("dd-MM-yyyy"); 
        bool matchDate = false; 

        if (!currentFilter.modifyDateFilterText.isEmpty()) {
            if (dStr.contains(currentFilter.modifyDateFilterText.trimmed())) matchDate = true;
            if (!matchDate) return false;
        }

        if (!currentFilter.modifyDates.isEmpty()) {
            matchDate = false;
            for (const QString& fDate : currentFilter.modifyDates) { 
                if (fDate == dStr) { matchDate = true; break; } 
            } 
            if (!matchDate) return false; 
        }
    }

    // 文件名关键字过滤
    if (!currentFilter.keyword.isEmpty()) {
        return fileName.contains(currentFilter.keyword, Qt::CaseInsensitive);
    }

    return true;
}

} // namespace ArcMeta
