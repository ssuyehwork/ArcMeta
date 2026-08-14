# 资产入库缩略图确定性生成整构 —— Modification_Plan-13.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在目前的资产入库流程中，`AssetImporter::importSingleFile` 写入文件容器后，会立即获取其 256x256 的高清缩略图并保存。但在当前实现中，缩略图获取选用的是 `WindowsShellThumbnailProvider::getShellThumbnail`。该 API 基于 Windows Shell 命名空间机制（`SHParseDisplayName` + `IShellItemImageFactory`），其结果高度依赖操作系统自身的后台异步索引状态。
由于我们刚刚亲手把该资产写入磁盘（`QFile::copy`），Shell 的底层索引对新文件的感知必然存在时序上的滞后（Latency），这导致“写文件完成”与“索引状态跟上”发生时序严重错配（对应用户原话：“真正的问题是"流程选错了工具"，不是"这个工具偶尔失灵需要重试"”）。为彻底消除时序问题，本方案将缩略图提取修改为不依赖任何系统索引、直接解析文件字节码进行确定性提取的方案（对应用户原话：“入库这一步该用直接解码，不该用 Shell 查询”）。

## 2. 问题定位
- **入库场景与工具错配**：`WindowsShellThumbnailProvider::getShellThumbnail` 更适用于磁盘模式（`DiskNav`）下对早已存在、Shell 已经完美索引完的文件的随机浏览。而对于由我们自己代码刚写入、100% 确定其长什么样的“新建导入资产”，强行过道 Shell 查询属于选错工具（对应用户原话：“"刚导入的资产"这个场景，从一开始就不该走这条路”）。
- **同步确定性成功保障**：`MediaColorExtractor::getImageForAnalysis` 采取字节级直接解码的方式，不通过 Shell 或系统缓存，能够在文件落地后的下一微秒以 100% 的确定性解码出正确的图像内容，彻底摆脱系统异步索引的干扰，实现真正的“零延迟、100% 成功、零打补丁”（对应用户原话：“缩略图生成在导入这一刻就是确定性成功的”）。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 真正的问题是"流程选错了工具"，不该用 Shell 查询 | 4.1、4.2 节，替换为直接读取文件字节并解码的确定性提取器 | ✅ |
| 2    | importSingleFile() 里这一步改用与 MediaExtractorPipeline 一致的直接解码方式 | 4.2 节，引入 `MediaColorExtractor::getImageForAnalysis` 进行提取并保存缩略图 | ✅ |
| 3    | 这样一来缩略图生成在导入这一刻就是确定性成功的 | 4.2 节，通过不查索引查文件本身的方案，彻底消除时序卡顿和生成失败隐患 | ✅ |

## 4. 详细解决方案
本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 引入 `MediaColorExtractor` 依赖头文件
在 `src/util/AssetImporter.cpp` 的顶部，精确引入所需的直接解析提取器头文件 `#include "../ui/MediaColorExtractor.h"`。

```
<<<<<<< SEARCH
#include "../ui/WindowsShellThumbnailProvider.h"
#include <QDir>
#include <QFileInfo>
=======
#include "../ui/WindowsShellThumbnailProvider.h"
#include "../ui/MediaColorExtractor.h"
#include <QDir>
#include <QFileInfo>
>>>>>>> REPLACE
```

### 4.2 重整缩略图生成为直接字节解码模式
在 `src/util/AssetImporter.cpp` 中的 `importSingleFile` 函数内，将原有的 Shell 命名空间提取方式替换为直接解码文件本身的 `MediaColorExtractor::getImageForAnalysis`。

```
<<<<<<< SEARCH
    // 4. 提取 256x256 高清预渲染缩略图 [baseName]_thumbnail.png
    QImage thumb = WindowsShellThumbnailProvider::getShellThumbnail(destPath, 256);
    if (!thumb.isNull()) {
        QString baseName = QFileInfo(fileName).completeBaseName();
        thumb.save(containerDir + "/" + baseName + "_thumbnail.png", "PNG");
    }
=======
    // 4. 提取 256x256 高清预渲染缩略图 [baseName]_thumbnail.png
    // 🚨 极致自包含整构：选用直接字节解码提取器，彻底解除对 Windows Shell 命名空间异步索引的时序依赖
    QImage thumb = MediaColorExtractor::getImageForAnalysis(destPath, 256);
    if (!thumb.isNull()) {
        QString baseName = QFileInfo(fileName).completeBaseName();
        thumb.save(containerDir + "/" + baseName + "_thumbnail.png", "PNG");
    }
>>>>>>> REPLACE
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] `src/util/AssetImporter.cpp`（引入 MediaColorExtractor.h 头文件并替换 getShellThumbnail 提取流程）

**明确禁止越界修改的范围：**
- [ ] `src/ui/WindowsShellThumbnailProvider.cpp` 磁盘模式下基于 Shell 缩略图缓存的高保真获取机制——不修改
- [ ] `src/ui/MediaColorExtractor.cpp` 的像素矩阵压缩和色彩空间解析算法——不修改

## 6. 实现准则与预警【核心】
1. **防空指针与空图写入**：`MediaColorExtractor::getImageForAnalysis` 同样返回 QImage，如文件不可读或非图片/不可识别格式，将正常返回空图。原有 `!thumb.isNull()` 条件分支完美包容该空状态，具有完美的兼容性。
2. **零时序依赖**：因为使用的是自包含的磁盘读取和直接二进制解码，只要 `QFile::copy` 已经在磁盘完成了数据同步，该调用即可百分之百、稳定地获得结果。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 双轨制数据路由分流 | 各自独立，托管库独立对数据进行分析和资产封装，互不干扰 | ✅ 符合。在库内资产入库阶段采用自包含的高内聚提取器，而对磁盘模式不作改变，完美符合规范要求。 |

## 8. 待确认事项（可选）
- **无**。
