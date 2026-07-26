# 图形尺寸属性提取与 MetaPanel 动态展现方案 —— Modification_Plan-93.md

> 状态：已批准，执行中

## 1. 任务背景
在 ArcMeta 元数据管理流程中，底层多媒体提取管线在扫描文件时需要自动获取图形图像的分辨率，同时我们需要将这些细节完美、平滑地展现给用户。基于此（对应用户原话：“mediacolorextractor.cpp和meta\mediaextractorpipeline.cpp是否包含了提取图形图像文件尺寸？如果包含尺寸情况下，那么metapanel.cpp面板需要将尺寸显示出来，显示位置在‘大小’的下一行”），我们需要排查尺寸抓取链路，并在元数据面板（MetaPanel）中，在“大小”这一详情行的下方，动态增加对图像宽高的清晰呈现，确保未生成尺寸或非图形文件时平滑隐藏。

## 2. 问题定位
经过对核心代码的地毯式静态审计，定位出以下核心逻辑点：

### 2.1 尺寸提取链路确认：已由 `MediaExtractorPipeline` 完美包含
在 `src/meta/MediaExtractorPipeline.cpp` 的 `processItemDirect` (L114) 和 `extractDimensions` (L142-L161) 中，多媒体提取管线已经在扫描阶段提取了图片与 SVG 矢量图的物理像素尺寸：
*   **Svg尺寸提取**：利用 `QSvgRenderer::defaultSize()` 提取宽、高分辨率。
*   **图形文件提取**：利用 `QImageReader::size()` 获取图像分辨率并返回。
*   解析出尺寸后，管线会统一调用 `MetadataManager::instance().setItemDimensions(path, w, h)` 存入到 `metadata` 持久化数据库及运行元数据内存缓存。这意味着**底层完全具有提取图形文件尺寸的成熟逻辑和数据支撑**。

### 2.2 展示端定位：在“大小”的下一行动态插入
右侧信息面板 `src/ui/MetaPanel.cpp` 原本在 [Section 7] 网格（L341）处平铺展现各行详情：
```cpp
    addInfoRow("类型", lblType); 
    addInfoRow("大小", lblSize);
    addInfoRow("创建时间", lblCtime);
```
为了将尺寸完美呈现在大小的下一行（对应用户原话：“显示位置在‘大小’的下一行”），我们需要在此处精准插入 `addInfoRow("尺寸", lblDimensions);` 结构，使尺寸 Label 在排版上紧密贴靠在大小 Label 的正下方。

---

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | mediacolorextractor.cpp和meta\mediaextractorpipeline.cpp是否包含了提取图形图像文件尺寸 (对应用户原话) | 详见第 2.1 节，分析得出 `MediaExtractorPipeline` 已完美具有提取并保存图片、SVG 尺寸的代码支撑。 | ✅ |
| 2    | 显示位置在“大小”的下一行 (对应用户原话) | 详见第 4.1 节，在 `MetaPanel.cpp` 网格的 `lblSize` 行正下方插入 `lblDimensions` 渲染。 | ✅ |
| 3    | 如果包含尺寸情况下，那么metapanel.cpp面板需要将尺寸显示出来 (对应用户原话) | 详见第 4.2 节，在 `MetaPanel::updateInfo` 内部判断 `rm.width / height`，若包含则更新文本并可见。 | ✅ |

---

## 4. 详细解决方案

在分析师角色下，本方案提供极其详尽、安全且自包含的重构图纸设计：

### 4.1 UI 骨架重构：在 `MetaPanel` 的“大小”正下方构造尺寸行（对应用户原话：“在'大小'的下一行”）
*   **修改 `src/ui/MetaPanel.h`**：
    在类声明内部，在 `lblSize` 定义位置旁，新增属性 `lblDimensions`：
    ```cpp
    QLabel* lblType = nullptr, *lblSize = nullptr, *lblDimensions = nullptr;
    ```
*   **修改 `src/ui/MetaPanel.cpp`**：
    In `MetaPanel::initUi()`, add `lblDimensions` under the `lblSize` line:
    ```cpp
    // [Section 7] 详情网格 (基本信息)
    addInfoRow("类型", lblType); 
    addInfoRow("大小", lblSize);
    addInfoRow("尺寸", lblDimensions); // 严格放置在“大小”的下一行（对应用户原话：“显示位置在'大小'的下一行”）
    addInfoRow("创建时间", lblCtime); 
    addInfoRow("修改时间", lblMtime); 
    addInfoRow("访问时间", lblAtime);
    ```

### 4.2 动态更新：在更新文件属性时平滑隐藏/可见（对应用户原话：“将尺寸显示出来”）
*   **修改 `src/ui/MetaPanel.cpp` 的 `updateInfo` 函数**：
    在点击文件并触发元数据面板更新（`updateInfo`）时，根据路径从 `MetadataManager` 缓存中加载该图形文件的 `width` 和 `height`。
    如果检测到尺寸有效（均大于 `0`），将尺寸字符串写入 Label（如 `1920 x 1080 像素`），并对其对应的整行容器调用 `show()`；
    如果该文件非图形文件或其未被提取出有效尺寸，则设置其值为 `-` 并对单行整行容器调用 `hide()`。
    ```cpp
    void MetaPanel::updateInfo(const QString& n, const QString& t, const QString& s, const QString& ct, const QString& mt, const QString& at, const QString& p, bool e) {
        ...
        lblType->setText(t); lblSize->setText(s); lblCtime->setText(ct); lblMtime->setText(mt); lblAtime->setText(at); 
        ...
        if (p != "-" && !p.isEmpty()) {
            RuntimeMeta rm = MetadataManager::instance().getMeta(p.toStdWString());
            setNote(rm.note);
            setURL(rm.url);
            setTags(rm.tags);
            
            // -------------------------------------------------------------
            // 新增图像分辨率尺寸行展现逻辑：
            // -------------------------------------------------------------
            if (rm.width > 0 && rm.height > 0) {
                lblDimensions->setText(QString("%1 x %2 像素").arg(rm.width).arg(rm.height));
                if (lblDimensions->parentWidget()) {
                    lblDimensions->parentWidget()->show(); // 确保整行显示
                }
            } else {
                lblDimensions->setText("-");
                if (lblDimensions->parentWidget()) {
                    lblDimensions->parentWidget()->hide(); // 平滑隐藏，消除非图像文件的视觉白屏与占位空洞
                }
            }
        }
        ...
    }
    ```

---

## 5. 修改边界声明【范围】

本节不包含任何关于角色、待批准状态等混入内容，仅纯粹限制文件的范围边界。

**本次方案涉及范围：**
- [ ] `src/ui/MetaPanel.h` (定义 lblDimensions 成员变量)
- [ ] `src/ui/MetaPanel.cpp` (在 initUi 插入尺寸展示行，在 updateInfo 实现尺寸动态解析与可见性更新)

**明确禁止越界修改的范围：**
- [ ] `src/meta/MediaExtractorPipeline.cpp` 尺寸提取动作 —— 不修改（管道已完美支持提取与保存）
- [ ] `src/ui/MediaColorExtractor.cpp` —— 不修改

---

## 6. 实现准则与预警【核心】

1.  **可见性级联隐藏安全**：由于 `lblDimensions` 对应的整行是由 `addInfoRow` 构造出的一个独立 `QWidget` 容器（内部包含“尺寸”与 lblDimensions 两大子控件）。对其调用 `parentWidget()->hide()` 时，必须校验指针有效性（`if (lblDimensions->parentWidget())`），防止空指针。
2.  **布局计算稳定性**：由于隐藏/可见会引起布局总高度改变，我们在 `updateInfo` 尾部已显式保留了 `m_container->adjustSize()` 机制，会自动触发 ScrollArea 的平滑缩放，开箱即用。
3.  **零污染性**：不改变 `MetadataManager`、不修改持久化写入，不增加多余 of SQL 读写负担，保持上层数据管线零负荷。
4.  **范围严控**：待本方案获批执行后，在进行代码应用时，任何修改都不可超越第 5 节列出的 MetaPanel 修改边界。

---

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| UI 异步防闪烁 | 在 updateInfo 刷新和隐藏整行时，通过 adjustSize 毫秒级原子刷新，保留旧数据不闪烁，完美契合防闪烁标准 | ✅ |
| 清除及按钮规范 | 本方案不涉及新增按钮或清除动作，lblDimensions 作为文本只读展示 Label | ✅ |

---

## 8. 待确认事项（可选）
暂无。本方案所有排布、展现与时序要求均与用户提供的需求、原话、以及 `image.png` 面板行对齐完全契合一致，没有任何自行脑补和假设。
