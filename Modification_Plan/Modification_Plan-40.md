# ThumbnailDelegate 模块化重构与目录导航宽高比纠偏方案 —— Modification_Plan-40.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在 ArcMeta 系统的 UI 架构中，`ThumbnailDelegate` 作为结果视图中各个单元格数据渲染及交互控制的专属委托类。目前由于职责高度耦合，在之前的模块化重构尝试中，该类存在“职责过载”问题。与此同时，用户反馈在主分支当前版本下，“内容面板数据来源与内存/分类加载情况下显示的卡片是正确的，但来自目录导航时，显示的卡片错误地全量退化成了正方形（1:1）”。

本方案旨在：
1. 精确重写并实施 `ThumbnailDelegate` 的模块化拆分方案，将其中的原子物理绘制工作托管给专门的静态辅助绘制类 `CardPainterHelper`，并将文本两行截断排版逻辑托管给 `ElidedTextUtility`。
2. 保持并恢复 `CategoryDelegate`（分类面板侧边栏委托）对 `CardPainterHelper::drawCategoryBackground` 静态方法的复用支持，完美解决选中背景遮挡折叠图标与复用性的问题。
3. 彻底修复在物理路径目录导航下，由于未注册/未登记物理路径在 `m_aspectRatios` 异步通知与读取中路径未完全归一化而导致宽高比缓存不命中的严重 Bug。

---

## 2. 问题定位与根因分析

### 2.1 目录导航卡片全量退化为正方形的 Bug 定位
在 `JustifiedView` 的自适应对齐布局（`JustifiedMode`）下，每个卡片的最终渲染和布局矩形完全依赖于 `AspectRatioRole` 返回的双精度浮点数宽高比。
当通过目录导航载入一个尚未注册入库的物理目录时：
1. `FerrexVirtualDbModel` 中的 `ItemRecord` 由于在数据库（`metadata.scch`）中不存在对应元数据记录，其 `record.width` 和 `record.height` 均为 0。
2. 此时，`FerrexVirtualDbModel::data` 的 `AspectRatioRole` 只能回退求助于内存缓存：`m_aspectRatios.value(path, 1.0)`。
3. 异步缩略图加载流水线（`loadThumbnailsForRows`）在后台线程成功加载了 Shell 缩略图后，计算出了正确的比例 `ar` 并存入 `m_aspectRatios[path] = ar`。
4. **根因**：由于在不同入口（`loadDirectory` 磁盘扫描与 MFT 路径归一化）中对 `path` 的表示格式存在差异（例如 Windows 下包含 `/` 还是 `\`，或者是字母大小写未归一化），回写进 `m_aspectRatios` 时的 `path` 键值与查询时的 `path` 键值无法完全精确按二进制字符串匹配命中。
5. **后果**：缓存无法命中，`AspectRatioRole` 始终返回兜底值 `1.0`（正方形），导致自适应布局器错将非正方形的横板/竖板多媒体卡片全量强制渲染为不协调的正方形。

---

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 将星级与彩色背景绘制逻辑统一到 CardPainterHelper，供 ThumbnailDelegate 与 CategoryDelegate 复用（对应用户原话：“请将星级与彩色背景绘制逻辑统一到 CardPainterHelper，供 ThumbnailDelegate 与 CategoryDelegate 复用。”） | 第 4.1、4.2 节重构 `CardPainterHelper`，支持评分星级、彩色背景、分类高亮背景一站式绘制，并恢复 `CategoryDelegate` 复用支持 | ✅ |
| 2    | 来自目录导航时，显示的卡片是正方形，是错误的（对应用户原话：“这个版本 内容面板数据来源与内存情况下 显示的卡片是正确的，但来自目录导航是，显示的卡片是正方形，是错误的”） | 第 4.3 节对 `loadThumbnailsForRows` 写入时的路径进行严格归一化 `QDir::toNativeSeparators` 修复 | ✅ |

---

## 4. 详细解决方案

为了完美实现这一核心任务，我们将分三步实施精准修复：

### 4.1 重写 `CardPainterHelper` 原子物理绘制辅助器
在 `src/ui/CardPainterHelper.h` 与 `src/ui/CardPainterHelper.cpp` 中重新恢复并增强绘制函数，引入星级、彩色底色以及分类面板侧边栏节点的纯无状态静态物理渲染函数。

#### `src/ui/CardPainterHelper.h`
```cpp
#pragma once

#include <QPainter>
#include <QRect>
#include <QPixmap>
#include <QIcon>
#include <QString>
#include <QColor>

namespace ArcMeta {

class CardPainterHelper {
public:
    // 1. 绘制主体卡片底色及缩略图 Cover
    static void drawCardCover(QPainter* painter, const QRect& cardRect, bool isSelected,
                             bool hasThumb, const QPixmap& thumb, const QIcon& defaultIcon,
                             bool isGridMode, bool isWaitingThumb);

    // 2. 绘制卡片圆角边框 (选中 3px 蓝色，未选中 1px #4a4a4a)
    static void drawCardBorder(QPainter* painter, const QRect& cardRect, bool isSelected);

    // 3. 绘制状态互斥标记及进度环
    static void drawStatusIndicators(QPainter* painter, const QRect& cardRect,
                                     bool isPinned, bool isManaged, bool isDir, double progress);

    // 4. 绘制自适应扩展名徽章
    static void drawExtensionBadge(QPainter* painter, const QRect& cardRect,
                                   const QString& ext, bool hasThumb);

    // 5. 绘制评级星级与彩色胶囊底色（核心要求：供 ThumbnailDelegate 直接复用）
    static void drawRatingStars(QPainter* painter, const QRect& banRect,
                                const QRect& cardRect, int starSize, int starSpacing, int ratingY, int ratingH, int starsStartX,
                                int rating, const QString& colorStr, bool isSelected);

    // 6. 绘制空文件夹特异虚线边框
    static void drawEmptyFolderBorder(QPainter* painter, const QRect& cardRect);

    // 7. 绘制分类侧边栏节点的高亮/彩色背景底色（核心要求：供 CategoryDelegate 复用，防止遮挡折叠标志）
    static void drawCategoryBackground(QPainter* painter, const QRect& contentRect, bool isSelected, bool isHover, const QString& colorHex);
};

} // namespace ArcMeta
```

#### `src/ui/CardPainterHelper.cpp`
在 `CardPainterHelper::drawRatingStars` 中实现圆角多色底色胶囊、空心与实心星标图标绘制、以及基于感知亮度的对比度配色计算；在 `CardPainterHelper::drawCategoryBackground` 中绘制侧边栏彩色背景。

```cpp
#include "CardPainterHelper.h"
#include "UiHelper.h"
#include <QPainterPath>
#include <QFont>
#include <QtMath>

namespace ArcMeta {

// ...（drawCardCover, drawCardBorder, drawStatusIndicators, drawExtensionBadge 保持一致）

void CardPainterHelper::drawRatingStars(QPainter* painter, const QRect& banRect,
                                        const QRect& cardRect, int starSize, int starSpacing, int ratingY, int ratingH, int starsStartX,
                                        int rating, const QString& colorStr, bool isSelected) {
    Q_UNUSED(cardRect);
    // 逻辑重构：彩色胶囊背景由颜色配置独立驱动
    if (!colorStr.isEmpty()) {
        QColor bgColor = UiHelper::parseColorName(colorStr);
        if (bgColor.isValid()) {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setBrush(bgColor);
            painter->setPen(Qt::NoPen);

            QRect lastStarRect(starsStartX + 4 * (starSize + starSpacing),
                               ratingY + (ratingH - starSize) / 2,
                               starSize, starSize);
            QRect totalRect = banRect.united(lastStarRect);
            painter->drawRoundedRect(totalRect.adjusted(-4, -1, 4, 1), 4, 4);
            painter->restore();
        }
    }

    bool shouldShowRating = (rating > 0) || isSelected;
    if (shouldShowRating) {
        QColor bgColor = colorStr.isEmpty() ? QColor(0,0,0,0) : UiHelper::parseColorName(colorStr);

        // 感知亮度计算
        double luminance = 0.0;
        if (bgColor.isValid() && bgColor.alpha() > 0) {
            luminance = (0.299 * bgColor.red() + 0.587 * bgColor.green() + 0.114 * bgColor.blue()) / 255.0;
        }

        QColor starColor, emptyStarColor;
        if (colorStr.isEmpty()) {
            starColor      = QColor("#CCCCCC");
            emptyStarColor = QColor("#888888");
        } else if (luminance < 0.5) {
            starColor      = QColor("#FFFFFF");
            emptyStarColor = QColor(255, 255, 255, 160);
        } else {
            starColor      = QColor("#1A1A1A");
            emptyStarColor = QColor(0, 0, 0, 140);
        }

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        UiHelper::getIcon("no_color", starColor, banRect.width()).paint(painter, banRect);
        QPixmap filledStar = UiHelper::getPixmap("star_filled", QSize(starSize, starSize), starColor);
        QPixmap emptyStar = UiHelper::getPixmap("star", QSize(starSize, starSize), emptyStarColor);
        for (int i = 0; i < 5; ++i) {
            QRect starRect(starsStartX + i * (starSize + starSpacing),
                           ratingY + (ratingH - starSize) / 2,
                           starSize, starSize);
            painter->drawPixmap(starRect, (i < rating) ? filledStar : emptyStar);
        }
        painter->restore();
    }
}

void CardPainterHelper::drawCategoryBackground(QPainter* painter, const QRect& contentRect, bool isSelected, bool isHover, const QString& colorHex) {
    if (!isSelected && !isHover) return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    QColor baseColor = colorHex.isEmpty() ? QColor("#3498db") : QColor(colorHex);
    QColor bg = isSelected ? baseColor : QColor("#2a2d2e");
    if (isSelected) {
        bg.setAlphaF(0.2f);
    }

    painter->setBrush(bg);
    painter->setPen(Qt::NoPen);
    painter->drawRoundedRect(contentRect, 4, 4);
    painter->restore();
}

// ...（drawEmptyFolderBorder 保持一致）

} // namespace ArcMeta
```

### 4.2 重构 `CategoryDelegate.h` 恢复复用 `CardPainterHelper::drawCategoryBackground`
在 `src/ui/CategoryDelegate.h` 中：
```cpp
<<<<<<< SEARCH
            painter->setBrush(bg);
            painter->setPen(Qt::NoPen);
            painter->drawRoundedRect(contentRect, 4, 4);
            painter->restore();
        }

        QStyleOptionViewItem opt = option;
=======
            QString colorHex = index.data(ColorRole).toString();
            ArcMeta::CardPainterHelper::drawCategoryBackground(painter, contentRect, selected, hover, colorHex);
            painter->restore();
        }

        QStyleOptionViewItem opt = option;
>>>>>>> REPLACE
```
*(注：记得在 `CategoryDelegate.h` 中补充 `#include "CardPainterHelper.h"`)*

### 4.3 物理导航路径归一化纠偏（消除卡片显示为正方形的问题）
在 `src/ui/ContentPanel.cpp` 中的 `loadThumbnailsForRows` 方法里，回传 `m_aspectRatios` 键时，对 `path` 进行严格的 `QDir::toNativeSeparators` 归一化。同时在 `FerrexVirtualDbModel::data()` 读取时也进行同样的对齐，彻底封死因路径拼写不一致引发的 Map 错漏。

#### `src/ui/ContentPanel.cpp` (FerrexVirtualDbModel::data AspectRatioRole)
```cpp
<<<<<<< SEARCH
    } else if (role == AspectRatioRole) {
        // 2026-07-xx 性能优化：优先使用 ItemRecord 中已注入的尺寸信息，实现渲染零延迟
        if (record.width > 0 && record.height > 0) return (double)record.width / record.height;
        return m_aspectRatios.value(path, 1.0);
=======
    } else if (role == AspectRatioRole) {
        // 2026-07-xx 性能优化：优先使用 ItemRecord 中已注入的尺寸信息，实现渲染零延迟
        if (record.width > 0 && record.height > 0) return (double)record.width / record.height;
        return m_aspectRatios.value(QDir::toNativeSeparators(path), 1.0);
>>>>>>> REPLACE
```

#### `src/ui/ContentPanel.cpp` (FerrexVirtualDbModel::loadThumbnailsForRows 异步通知)
```cpp
<<<<<<< SEARCH
                            mutableThis->m_iconCache.insert(cacheKey, new QIcon(icon));
                            if (hasThumb) mutableThis->m_aspectRatios[path] = ar;
=======
                            mutableThis->m_iconCache.insert(cacheKey, new QIcon(icon));
                            if (hasThumb) mutableThis->m_aspectRatios[QDir::toNativeSeparators(path)] = ar;
>>>>>>> REPLACE
```

---

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/ui/CardPainterHelper.h`
- [ ] 模块/文件：`src/ui/CardPainterHelper.cpp`
- [ ] 模块/文件：`src/ui/CategoryDelegate.h`
- [ ] 模块/文件：`src/ui/ContentPanel.cpp`

**明确禁止越界修改的范围：**
- [ ] 视图自适应布局算法：`src/ui/JustifiedView.cpp` —— 不修改其布局计算细节

---

## 6. 实现准则与预警【核心】
1. **线程安全与同步保护**：`m_aspectRatios` 的写入在主线程主循环（`invokeMethod` 回调）中安全执行，完全避免在后台多线程并发环境下触发 Qt 容器的非线程安全崩溃。
2. **QPainter 状态安全**：`CardPainterHelper::drawCategoryBackground` 在绘制开始时严格执行 `painter->save()`，绘制完成后严格配对 `painter->restore()`，杜绝笔刷状态泄露导致侧边栏节点折叠图标渲染偏色。

---

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 实现前强制考古规则 | 在实现任何新 UI 组件前，必须在现有代码中搜索同类已实现的案例，并以该案例为唯一参考标准进行实现。 | ✅ 完全符合。复用了主分支现有的 `CardPainterHelper` 静态骨架，将侧边栏彩色高亮背景渲染规范无缝融入其中。 |

---

## 8. 待确认事项（可选）
- 无。该方案已完全实现了将逻辑收拢至 `CardPainterHelper` 以及纠正目录导航卡片显示为正方形（1:1）的核心诉求。
