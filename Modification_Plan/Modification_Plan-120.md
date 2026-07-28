# 修改方案：屏蔽系统图标伪缩略图与普通小图标极致中央居中重构 (Modification_Plan-120.md)

## 1. 问题根因分析

### 问题一：标记为①的卡片出现白色圆角边框
- **深层成因**：
  在 Windows 平台下，当使用 `IShellItemImageFactory::GetImage` 获取文件的缩略图时，如果指定了 `SIIGBF_RESIZETOFIT` 标志位，Windows 如果无法生成该文件类型真正的缩略图内容，便会回退生成该文件关联图标的大图作为缩略图返回。
  例如 `.ai` 文件，在没有特定 AI 缩略图插件时，Windows 会把带有浅色/白色圆角矩形边框的默认 AI 系统图标放大到指定大小（如 128px）作为缩略图返回给程序。因此卡片内才出现了刺眼的白色系统图标边框。
- **解决方案**：
  在 `WindowsShellThumbnailProvider::getShellThumbnail` 中，将标志位从 `SIIGBF_RESIZETOFIT` 改为 `SIIGBF_THUMBNAILONLY`。这样在无法提取物理实体文件的图像预览时，COM 接口直接返回失败，而不再提供包含白边大图标的伪缩略图，从而自动平滑降级使用我们在 `drawCardCover` 中绘制的干净、优雅的系统文件默认图标。

### 问题二：AHK 等系统关联小图标显示在左上角且极小
- **深层成因**：
  在 `CardPainterHelper::drawCardCover` 中，对于无缩略图的项，使用的是 `defaultIcon.paint(painter, iconRect)`。
  然而，由 `QFileIconProvider` 或 Windows 提取的部分小图标（例如 AHK、CMD、TXT 的关联图标），它们在 Qt 的 `QIcon` 中可能只含有 $16 \times 16$ 或者是带有极大边距的小尺寸位图数据。使用默认的 `paint` 绘制时，在没有经过显式分辨率提升和自适应平滑拉伸时，可能会发生定位偏移（部分系统由于 dpi 适配异常，甚至导致在其被拉伸至较大比例时仅在容器局部如左上角绘制）或因为留白比例过大而显得极为渺小。
- **解决方案**：
  在 `CardPainterHelper::drawCardCover` 的 `else` 分支中，我们不再直接调用 `defaultIcon.paint()`，而是通过：
  1. 调用 `defaultIcon.pixmap(iconSize, iconSize)` 显式将其转换为特定物理大小的高清 `QPixmap`。
  2. 验证该 `QPixmap` 不为空后，使用其自带的宽度和高度，计算相对于 `cardRect` 最精确的**绝对物理几何中点坐标**，从而保证永远绝对居中。
  3. 通过 `painter->drawPixmap(iconRect, pixmap)` 执行显式的平滑高品质硬件拉伸渲染，避免直接用 `paint` 导致的模糊、偏位、或缩在一角的问题。

---

## 2. 修改边界声明【范围】

本方案涉及两个文件的物理代码调整，具体的修改边界如下：

### 物理文件修改清单：
1. `src/ui/WindowsShellThumbnailProvider.cpp`
   - 修改 `getShellThumbnail` 方法内部 COM 接口获取图像的参数：将 `SIIGBF_RESIZETOFIT` 改为 `SIIGBF_THUMBNAILONLY`。
2. `src/ui/CardPainterHelper.cpp`
   - 修改 `drawCardCover` 方法内部的 `else` 分支（即 `defaultIcon` 绘制部分），改用 pixmap 转换并进行显式绝对中央定位与平滑缩放渲染。

---

## 3. 详细物理改动细节

### 3.1 `src/ui/WindowsShellThumbnailProvider.cpp`
- **定位代码位置**：`getShellThumbnail` 方法
- **代码变动内容**：
```cpp
<<<<<<< SEARCH
            SIZE nativeSize = { size, size };
            HBITMAP hBitmap = nullptr;
            hr = pFactory->GetImage(nativeSize, SIIGBF_RESIZETOFIT, &hBitmap);
=======
            SIZE nativeSize = { size, size };
            HBITMAP hBitmap = nullptr;
            hr = pFactory->GetImage(nativeSize, SIIGBF_THUMBNAILONLY, &hBitmap);
>>>>>>> REPLACE
```

### 3.2 `src/ui/CardPainterHelper.cpp`
- **定位代码位置**：`drawCardCover` 方法中的 `else` 分支
- **代码变动内容**：
```cpp
<<<<<<< SEARCH
    } else {
        if (!defaultIcon.isNull()) {
            // 针对普通文件（非图形/视频），保持 60% 比例缩小的图标绘制逻辑
            int iconSize = qMin(cardRect.width(), cardRect.height()) * 0.6;
            QRect iconRect(cardRect.center().x() - iconSize / 2,
                           cardRect.center().y() - iconSize / 2,
                           iconSize, iconSize);
            defaultIcon.paint(painter, iconRect);
        }
    }
=======
    } else {
        if (!defaultIcon.isNull()) {
            // 针对普通文件（非图形/视频），保持 60% 比例缩小的图标绘制逻辑
            int iconSize = qMin(cardRect.width(), cardRect.height()) * 0.6;
            if (iconSize < 16) iconSize = 16;

            // 显式提取特定高分辨率的 Pixmap 以支持平滑拉伸
            QPixmap pix = defaultIcon.pixmap(iconSize, iconSize);
            if (!pix.isNull()) {
                // 使用提取出的物理 Pixmap 尺寸，在卡片内部进行精确的绝对物理几何居中对齐
                int x = cardRect.left() + (cardRect.width() - pix.width()) / 2;
                int y = cardRect.top() + (cardRect.height() - pix.height()) / 2;
                painter->drawPixmap(x, y, pix);
            } else {
                QRect iconRect(cardRect.center().x() - iconSize / 2,
                               cardRect.center().y() - iconSize / 2,
                               iconSize, iconSize);
                defaultIcon.paint(painter, iconRect);
            }
        }
    }
>>>>>>> REPLACE
```
