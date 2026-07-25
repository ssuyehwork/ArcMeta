# Modification Plan - 任何视图模式禁止文件与文件夹同行混排方案

本专项方案致力于彻底阻断在任何内容区视图模式（包括 `GridMode` 网格模式与 `JustifiedMode` 自适应流式模式）下，文件夹与文件在同一行并排、混排的问题。

---

## 1. 拟修改的文件清单

- **`src/ui/JustifiedView.cpp`**:
  - 重构网格视图（`GridMode`）下的排版换行计算，注入严格的类型拦截机制，在检测到类型突变时强制断行折行，绝对不允许文件与文件夹并排混排。
  - 保留并捍卫自适应模式（`JustifiedMode`）下的原有类型隔离强制断行，万无一失。

---

## 2. 核心技术修改细节

### 2.1 网格模式（`GridMode`）折行判定重构
定位到 `src/ui/JustifiedView.cpp` 中 `JustifiedView::doLayout()` 函数的 `GridMode` 分支：

```cpp
<<<<<<< SEARCH
        int i = 0;
        while (i < count) {
            int numInRow = std::min(maxNumInRow, count - i);
            int currentX = margin;
            for (int j = 0; j < numInRow; ++j) {
                int itemIdx = i + j;
                m_geometries[itemIdx] = { QRect(currentX, currentY, itemWidth, itemHeight), itemIdx };
                currentX += itemWidth + standardSpacing;
            }
            currentY += itemHeight + spacing;
            i += numInRow;
        }
=======
        int i = 0;
        while (i < count) {
            int rowStart = i;
            
            // 1. 获取本行第一个元素的类型 (是否是文件夹/分类)
            QModelIndex firstIdx = model()->index(rowStart, 0);
            QString firstType = model()->data(firstIdx, TypeRole).toString();
            bool isFirstDir = (firstType == "folder" || firstType == "category");

            // 2. 循环塞入元素，一旦遇到类型变动 (例如从文件夹突变为普通文件)，立即在这里截断退出当前行，强制折行
            int numInRow = 0;
            while (numInRow < maxNumInRow && (rowStart + numInRow) < count) {
                int nextIdx = rowStart + numInRow;
                QModelIndex idx = model()->index(nextIdx, 0);
                QString nextType = model()->data(idx, TypeRole).toString();
                bool isNextDir = (nextType == "folder" || nextType == "category");

                if (isNextDir != isFirstDir) {
                    break; // 类型改变，立即断开，不允许继续混排同行
                }
                numInRow++;
            }

            int currentX = margin;
            for (int j = 0; j < numInRow; ++j) {
                int itemIdx = rowStart + j;
                m_geometries[itemIdx] = { QRect(currentX, currentY, itemWidth, itemHeight), itemIdx };
                currentX += itemWidth + standardSpacing;
            }
            currentY += itemHeight + spacing;
            i += numInRow; // 推进到下一行
        }
>>>>>>> REPLACE
```

---

## 3. 验证与回归测试

1. 切换到**网格模式**和**自适应模式**，无论如何缩放或调整窗口：
   - 文件夹（及子分类）和文件均泾渭分明，文件夹位于最上方，文件位于最下方；
   - 绝无任何文件能够被拉入到含有文件夹的那一行里并排混排；
   - 数据的加载、排列和滚动等基础功能顺畅无破损。
