# 視覺完美度補完與樣式對齊方案 (Analysis & Modification Plan) - #52

## 1. 深度審計：視覺偏差點對比

根據用戶提供的最新參考圖，現有代碼在“元數據”面板重構中存在以下像素級偏差，必須立即修正：

| 視覺元素 | 圖片預期 (Expected) | 現有實現 (Actual) | 修正措施 |
| :--- | :--- | :--- | :--- |
| **顏色膠囊背景** | 獨立的深色半透明圓角長條，包裹所有色點 | 硬編碼背景，與面板底色融合感差 | 重繪膠囊背景，增加層次感 |
| **編輯框圓角** | **所有**可編輯區域（名稱、備注、鏈接、標簽）具备 4px 圓角與深色塊背景 | 部分組件使用了透明背景或無邊框，缺乏“框”感 | 統一 QSS 樣式，強制 4px 圓角 + #1E1E1E 背景 |
| **分類/文件夾** | 同樣具備圓角深色塊背景，風格統一 | 雖然是只讀，但目前樣式與編輯框不完全一致 | 鏡像編輯框樣式，維持視覺一致性 |
| **彈性高度** | 名稱、備注、鏈接均應隨內容自由撐開 | 已初步實現彈性，但 padding 與邊距需微調以匹配圖片 | 優化彈性高度計算偏移量 |

---

## 2. 像素級重構方案

### 2.1 顏色膠囊 (PaletteCapsule) 的物理增強
調整 `paintEvent` 繪製邏輯，確保其在任何背景下都能清晰浮現。

- **背景繪製**：
  ```cpp
  painter.setBrush(QColor(30, 30, 30, 200)); // 深色半透明，增加浮動感
  painter.setPen(QPen(QColor(60, 60, 60), 1)); // 1px 深灰色邊框，勾勒邊緣
  painter.drawRoundedRect(rect(), 12, 12);
  ```
- **懸停反饋**：
  將 1px 白色邊緣改為 `rgba(255, 255, 255, 200)`，避免過於刺眼。

### 2.2 統一編輯器樣式系統 (The 4px Standard)
為 `src/ui/MetaPanel.cpp` 中的所有組件定義統一的視覺契約：

- **目標組件**：`m_nameEdit`, `m_noteEdit`, `m_linkEdit`, `m_categoryEdit`, `m_tagEdit`
- **QSS 規範**：
  ```css
  background-color: #1E1E1E;
  border: 1px solid #2D2D2D; /* 微弱邊框增加深度感 */
  border-radius: 4px;
  padding: 6px 10px;
  color: #EEEEEE;
  ```

### 2.3 彈性組件 (ElasticEdit) 的細節微調
修正 `adjustHeight` 中的高度補償值，確保文字上下邊距平衡。
- **補償值**：從 `+4` 調整為 `+12` (考慮到上下各 6px 的 padding)。

---

## 3. 逐文件改動計劃

#### 1. `src/ui/MetaPanel.cpp`
- **PaletteCapsule::paintEvent**：更換畫筆與畫刷顏色，增加 1px 外部描邊。
- **MetaPanel::initUi**：
  - 統一更新所有組件的 `setStyleSheet` 調用。
  - 確保標簽輸入框 `m_tagEdit` 的 margin 被移除，改為撐滿布局的 4px 圓角塊。
- **ElasticEdit::adjustHeight**：增加垂直 Padding 補全邏輯。

---

## 4. 性能與兼容性測試
- **UI 響應**：圓角與半透明繪製對現代 CPU/GPU 無壓力。
- **布局流動**：驗證當文件名極長導致換行時，下方的“備注”和“鏈接”能否平滑下移。

---
**分析人員：** 資深程序员 (Jules)
**日期：** 2024-05-22
