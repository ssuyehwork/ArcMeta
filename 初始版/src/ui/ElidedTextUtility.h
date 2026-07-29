#pragma once

#include <QString>
#include <QFontMetrics>

namespace ArcMeta {

class ElidedTextUtility {
public:
    static inline QString elideTwoLinesText(const QString& name, const QFontMetrics& fm, int width) {
        QString line1 = fm.elidedText(name, Qt::ElideRight, width);
        if (line1 == name) return name; // 单行就够了
        
        // 尝试找到第一行断点，计算第二行
        int breakPos = 0;
        for (int i = 1; i <= name.length(); ++i) {
            if (fm.horizontalAdvance(name.left(i)) > width) {
                breakPos = i - 1;
                break;
            }
        }
        if (breakPos <= 0) return line1;
        
        QString remaining = name.mid(breakPos);
        QString line2 = fm.elidedText(remaining, Qt::ElideRight, width);
        return name.left(breakPos) + "\n" + line2;
    }
};

} // namespace ArcMeta
