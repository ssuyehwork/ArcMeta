# 备份备注

**备份时间**：2026-08-16 11:14:46  
**备份目录**：Buk_20260816_111441  

---

1. 修复查重解决选中“使用已存在文件”时老资产关联新分类逻辑，防止分类计数清零；
2. 修复 SVG 无显式 width/height 时的 viewBox 尺寸兜底以及历史 0x0 SVG 的开库回填自愈；
3. 优化 DuplicateDetectorService::detectDuplicates，引入 alreadyMatched 标记与 break 熔断机制，杜绝重复添加同一文件的冲突组。
