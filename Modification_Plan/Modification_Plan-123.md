# 修改方案：屏蔽系统图标伪缩略图以消除卡片内冗余边框 (Modification_Plan-123.md)

## 1. 问题根因分析

### 问题描述
用户反馈：“这个问题修复无数次了，没有成功修复，所以我期望你扩大范围排查，有的卡片里出现了冗余的边框，这不是我期望的，而我期望的是卡片内没有冗余的边框”

### 深层成因
在 Windows 平台下，当使用 `IShellItemImageFactory::GetImage` 获取文件或文件夹的缩略图时，如果指定了 `SIIGBF_RESIZETOFIT` 标志位，且 Windows 无法生成该文件类型真正的缩略图内容（如未安装对应预览插件的 `.ai`、`.psd` 等文件），Windows 会回退生成该文件关联大图标作为缩略图返回给程序。
这些回退的系统关联图标往往带有浅色/白色圆角矩形背景或者外边框，从而导致最终渲染出的卡片内部出现了刺眼的冗余边框，这违背了用户期望在暗色主题下卡片保持纯净、无冗余边框的设计。

### 解决方案
在 `WindowsShellThumbnailProvider::getShellThumbnail` 中，将获取 Shell 缩略图时的标志位从 `SIIGBF_RESIZETOFIT` 替换为 `SIIGBF_THUMBNAILONLY`。
- **作用机制**：根据 MSDN 官方文档，`SIIGBF_THUMBNAILONLY` 限制了接口只允许返回真正的物理文件内容缩略图。在无法提取真实缩略图的情况下，COM 接口将直接返回失败，从而自动平滑降级，使用我们内部在 `drawCardCover` 中绘制的纯净、优雅、且绝对没有多余外边框的系统默认图标。这样可以从系统源头彻底根除伪缩略图带来的带白边图标污染。

---

## 2. 修改边界声明【范围】

本方案涉及一个文件的物理代码调整，具体的修改边界如下：

### 物理文件修改清单：
1. `src/ui/WindowsShellThumbnailProvider.cpp`
   - 修改 `getShellThumbnail` 方法内部 COM 接口获取图像的参数：将 `SIIGBF_RESIZETOFIT` 改为 `SIIGBF_THUMBNAILONLY`。

---

## 3. 详细物理改动细节

### 3.1 `src/ui/WindowsShellThumbnailProvider.cpp`
- **定位代码位置**：`getShellThumbnail` 方法 (第189行左右)
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
