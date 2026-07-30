当执行 `AssetImporter`（资产打包与分流入库）以及后台 `MediaExtractorPipeline`（多媒体提取流水线）逻辑时，**并不是所有文件都会生成 `_thumbnail.png` 缩略图**。

根据 ArcMeta 的提取规则与白名单机制，只有**需要从源文件中解包/截取/渲染出图像预览**的文件类型才会生成 `_thumbnail.png`。具体分类如下：

---

### 一、 需要生成 `_thumbnail.png` 的文件类型

#### 1. 平面设计与专业源文件（重点提取对象）
这类文件体积大或格式特殊，无法直接由系统常规方式秒开，入库时必须在 `.arc` 内部渲染并落盘高清 PNG 缩略图：
* **PSD / PSB**（Photoshop 拼合图/首帧缩略图）
* **AI**（Illustrator 提取内嵌 PDF/JPEG 预览图）
* **EPS / PDF**（矢量与文档首页渲染图）
* **SVG**（通过 `QSvgRenderer` 渲染为高清 PNG 位图）

#### 2. 常规位图与大图格式
* **高分辨率位图**：`jpg`, `jpeg`, `png`, `bmp`, `webp`, `gif`, `tiff`, `tif`
* **RAW 相机原图**（如 `cr2`, `nef`, `arw`, `dng` 等，若启用了 RAW 解码提取）

#### 3. 视频与动态媒体格式
* **视频文件**：`mp4`, `mkv`, `avi`, `mov`, `wmv`, `flv`, `webm`, `m4v`
  *(通过 FFmpeg/多媒体提取器截取 15% 关键帧或首帧，渲染为 `_thumbnail.png`)*

#### 4. 三维/模型资产（若配置了内嵌预览提取）
* `fbx`, `obj`, `gltf`, `glb`, `blend`（提取文件自带的嵌入式 Preview 图像）

---

### 二、 明确**不需要**生成 `_thumbnail.png` 的文件类型

下列文件类型在 `AssetImporter` 入库时**绝对不会**生成 `_thumbnail.png`：

1. **纯 Icon/小图标文件**：
   * **类型**：`ico`, `cur`, `ani`
   * **原因**：此类文件本身就是图标集合，内存消耗极小，Qt 和系统自带高清 Icon 绘制引擎，不需要额落盘生成 PNG 缩略图（代码中有 `iconOnlyExts = {"cur", "ico", "ani"}` 的排除规则）。

2. **纯文本 / 代码 / 配置文件**：
   * **类型**：`txt`, `md`, `markdown`, `json`, `xml`, `cpp`, `h`, `py`, `js`, `css`, `html`, `ini`, `yaml`, `log`
   * **原因**：文本文件直接通过文本预览控件或虚拟代码视图进行即时渲染，不浪费磁盘空间保存图片。

3. **二进制 / 压缩包 / 系统文件**：
   * **类型**：`exe`, `dll`, `sys`, `bin`, `dat`, `zip`, `rar`, `7z`, `iso`, `msi`
   * **原因**：无图像内容，直接显示系统默认的类型文件图标。

---

### 💡 总结判断规则

判断一个文件入库时是否会生成 `_thumbnail.png`，最核心的规则是：
$$\text{生成条件} = \text{是图像/设计图/视频/文档首页} \quad \text{AND} \quad \text{非纯 ICO/CUR/ANI 图标}$$