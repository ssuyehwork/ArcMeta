# 修改方案：利用 SIIGBF_WONTADORN 屏蔽 Windows 原生缩略图装饰边框 (Modification_Plan-125.md)

## 1. 深度根因分析

### 1.1 问题描述
用户反馈：“仍然不行，你去对比FERREX-META版本试试看”
结合用户提供的 `.ai` 文件在高分辨率卡片列表下的截图，我们可以清晰地观察到，当 Windows 系统中安装了 Adobe Illustrator 或其他第三方 PDF/AI 缩略图插件时，缩略图加载线程可以通过 Windows Shell 的 `IShellItemImageFactory::GetImage` 成功提取出对应的预览图像。
然而，在默认情况下，Windows Shell 取得图像后，会对其进行系统默认的“视觉装饰（Adornments）”，通常会为主体图像添加一层类似文档纸张的额外阴影、白边或细小的灰色矩形边框。这使得最终加载出的卡片即便在有图的状态下，内部依然会带有一圈极不和谐的冗余原生外围装饰边框，破坏了暗色卡片背景的纯净和整体美观度。

### 1.2 解决方案
根据微软 MSDN 官方文档，`IShellItemImageFactory::GetImage` 支持 `SIIGBF` 选项枚举中的 `SIIGBF_WONTADORN` 选项（其对应的十六进制常量为 `0x00000040`）：
- **`SIIGBF_WONTADORN` 的作用**：明确指示 Windows 图像工厂**不要为返回的缩略图图层添加任何原生视觉修饰、叠加图标、圆角白边、或文档折角/相框边缘等背景和边框装饰**。它会保证返回的 `HBITMAP` 为 100% 原始、纯粹、无多余衬底边框修饰的图像资产。
- **具体做法**：
  在 `WindowsShellThumbnailProvider::getShellThumbnail` 中，将调用 `pFactory->GetImage` 时的参数变更为复合标志 `SIIGBF_THUMBNAILONLY | SIIGBF_WONTADORN`。为了在较老版本的 SDK 或 MinGW 编译器上获得 100% 稳妥的兼容性，我们在代码中增加对 `SIIGBF_WONTADORN` 的防御性预处理器定义。

---

## 2. 修改边界声明【范围】

本方案涉及一个平台相关基础类库文件的物理代码调整，具体的修改边界如下：

### 物理文件修改清单：
1. `src/ui/WindowsShellThumbnailProvider.cpp`
   - 为确保老旧 MinGW / Windows SDK 环境正常编译，在头部或者函数内部追加 `#ifndef SIIGBF_WONTADORN` 的防御性宏定义。
   - 修改 `getShellThumbnail` 方法内部的 `pFactory->GetImage` 参数，由 `SIIGBF_THUMBNAILONLY` 变更为 `SIIGBF_THUMBNAILONLY | SIIGBF_WONTADORN`。

---

## 3. 详细物理改动细节

### 3.1 `src/ui/WindowsShellThumbnailProvider.cpp`
- **定位代码位置**：`getShellThumbnail` 方法
- **代码变动内容**：
```cpp
<<<<<<< SEARCH
#ifdef Q_OS_WIN
    ComInitializer comInit;
    PIDLIST_ABSOLUTE pidl = nullptr;
    HRESULT hr = SHParseDisplayName(path.toStdWString().c_str(), nullptr, &pidl, 0, nullptr);
    if (FAILED(hr)) return QImage();
    IShellItem* pItem = nullptr;
    hr = SHCreateItemFromIDList(pidl, IID_IShellItem, (void**)&pItem);
    ILFree(pidl);
    if (SUCCEEDED(hr)) {
        IShellItemImageFactory* pFactory = nullptr;
        hr = pItem->QueryInterface(IID_IShellItemImageFactory, (void**)&pFactory);
        if (SUCCEEDED(hr)) {
            SIZE nativeSize = { size, size };
            HBITMAP hBitmap = nullptr;
            hr = pFactory->GetImage(nativeSize, SIIGBF_THUMBNAILONLY, &hBitmap);
            if (SUCCEEDED(hr) && hBitmap) {
=======
#ifdef Q_OS_WIN
#ifndef SIIGBF_WONTADORN
#define SIIGBF_WONTADORN 0x00000040
#endif
    ComInitializer comInit;
    PIDLIST_ABSOLUTE pidl = nullptr;
    HRESULT hr = SHParseDisplayName(path.toStdWString().c_str(), nullptr, &pidl, 0, nullptr);
    if (FAILED(hr)) return QImage();
    IShellItem* pItem = nullptr;
    hr = SHCreateItemFromIDList(pidl, IID_IShellItem, (void**)&pItem);
    ILFree(pidl);
    if (SUCCEEDED(hr)) {
        IShellItemImageFactory* pFactory = nullptr;
        hr = pItem->QueryInterface(IID_IShellItemImageFactory, (void**)&pFactory);
        if (SUCCEEDED(hr)) {
            SIZE nativeSize = { size, size };
            HBITMAP hBitmap = nullptr;
            hr = pFactory->GetImage(nativeSize, static_cast<SIIGBF>(SIIGBF_THUMBNAILONLY | SIIGBF_WONTADORN), &hBitmap);
            if (SUCCEEDED(hr) && hBitmap) {
>>>>>>> REPLACE
```
