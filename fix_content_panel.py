import sys

with open('src/ui/ContentPanel.cpp', 'r', encoding='utf-8') as f:
    content = f.read()

# 1. Update updateGridSize
old_update_grid_size = """void ContentPanel::updateGridSize() {
    // 2026-06-05 按照用户要求：彻底重构为正方形布局，名称外置
    // 2026-06-05 按照要求：将最小值锁定为 56
    m_zoomLevel = qBound(56, m_zoomLevel, 128);

    if (auto* jv = qobject_cast<JustifiedView*>(m_gridView)) {
        jv->setTargetRowHeight(m_zoomLevel);
    } else if (auto* lv = qobject_cast<QListView*>(m_gridView)) {
        lv->setIconSize(QSize(m_zoomLevel, m_zoomLevel));
        int side = m_zoomLevel + 46; // 正方形边长
        int ratingH = 22;           // 2026-05-17 按照要求：为卡片外的评分区预留高度
        int nameH = (int)(m_zoomLevel * 0.25); // 名称高度
        int gap = 6;                // 间距归一化

        // 总高度 = 正方形边长 + 间距1 + 评分高度 + 间距2 + 名称高度 + 底部缓冲
        int totalH = side + gap + ratingH + gap + nameH + 8;
        lv->setGridSize(QSize(side, totalH));
    }

    // 2026-06-05 按照要求：持久化保存当前的缩放级别
    QSettings settings("ArcMeta团队", "ArcMeta");
    settings.setValue("UI/GridZoomLevel", m_zoomLevel);

    qDebug() << "[GridSize] Zoom:" << m_zoomLevel;
}"""

new_update_grid_size = """void ContentPanel::updateGridSize() {
    // 2026-06-xx 物理重构：按照 Plan-12 要求实现自适应布局
    // m_zoomLevel 被重新定义为自适应布局的“目标行高”
    m_zoomLevel = qBound(64, m_zoomLevel, 320);

    if (auto* jv = qobject_cast<JustifiedView*>(m_gridView)) {
        jv->setTargetRowHeight(m_zoomLevel);
    }

    // 2026-06-05 按照要求：持久化保存当前的缩放级别
    QSettings settings("ArcMeta团队", "ArcMeta");
    settings.setValue("UI/GridZoomLevel", m_zoomLevel);

    qDebug() << "[GridSize] TargetRowHeight:" << m_zoomLevel;
}"""

# Try to find and replace without exact matching if indentation differs
import re
pattern = r"void ContentPanel::updateGridSize\(\) \{.*?qDebug\(\) << \"\[GridSize\] Zoom:\" << m_zoomLevel;\s*\}"
content = re.sub(pattern, new_update_grid_size, content, flags=re.DOTALL)

# 2. Update initGridView to remove setAspectRatioRole(AspectRatioRole) since it's now default
old_init_grid_view_part = """    auto* justifiedView = qobject_cast<JustifiedView*>(m_gridView);
    if (justifiedView) {
        justifiedView->setAspectRatioRole(AspectRatioRole);
        auto* delegate = new ThumbnailDelegate(this);"""

new_init_grid_view_part = """    auto* justifiedView = qobject_cast<JustifiedView*>(m_gridView);
    if (justifiedView) {
        // Role 已对齐，JustifiedView 默认使用 UserRole + 2
        auto* delegate = new ThumbnailDelegate(this);"""

content = content.replace(old_init_grid_view_part, new_init_grid_view_part)

with open('src/ui/ContentPanel.cpp', 'w', encoding='utf-8') as f:
    f.write(content)
