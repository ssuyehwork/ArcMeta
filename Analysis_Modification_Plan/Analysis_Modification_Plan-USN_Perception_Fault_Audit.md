# 逻辑审计报告 - USN 感知失效深度分析

## 1. 问题描述
用户观察到主程序启动后，`UsnWatcher` 仅在初始化时有动作，之后在托管库内的新增/删减操作均未触发解析逻辑。

## 2. 核心排查结论
经代码审计，`UsnWatcher` 线程本身在持续运行，但变动信号在流转过程中遭遇了多重“逻辑拦截”，导致最终解析逻辑未点火。

### 2.1 拦截点 A：FRN 位宽判定冲突 (致命级)
- **现状**：
    - `MftReader::makeKey` 将 64 位 FRN 截断为 48 位索引号用于生成复合信号 Key。
    - `AutoImportManager::startListening` 缓存的托管库根 FRN 是通过 WinAPI 获取的完整 64 位值。
- **后果**：`isUnderManagedLibrary` 执行 FRN 链溯源时，拿着截断后的 48 位 ID 去匹配缓存中的 64 位 ID，匹配永远失败。变动信号被判定为“库外事件”而静默丢弃。

### 2.2 拦截点 B：信号“洪流抑制”导致逻辑丢失 (严重级)
- **现状**：`MftReader::updateEntriesFromUsnBatch` 规定若单次批处理变动 > 50 项，仅发射 `dataChanged(-1)`。
- **后果**：`AutoImportManager` 仅订阅了 `entryAdded` 等细粒度信号。在大批量拷贝文件入库时，信号被抑制，导致解析逻辑完全不触发。

### 2.3 拦截点 C：USN 自愈逻辑不闭环 (架构缺陷)
- **现状**：`UsnWatcher::run` 在捕捉到 `ERROR_JOURNAL_DELETE_IN_PROGRESS` 等错误时，仅重置了 `StartUsn`。
- **后果**：若 Windows 重置了 Journal ID，线程由于未重新执行 `FSCTL_QUERY_USN_JOURNAL` 获取新 ID，会导致后续读取持续失败（ERROR_INVALID_PARAMETER），线程陷入无效循环。

### 2.4 拦截点 D：动态库创建后的缓存冷处理 (逻辑盲区)
- **现状**：在 UI 侧动态创建托管文件夹后，仅启动了物理监控。
- **后果**：`AutoImportManager` 的 `m_managedFrnCache` 白名单未实时更新。新库内的变动在程序重启前无法被识别。

## 3. 建议优化路径
1. **统一位宽**：废除 48 位截断，全系统链路回归 64 位完整 FRN 判定。
2. **洪流补救**：`AutoImportManager` 订阅 `dataChanged(-1)` 信号，并在此类情况下触发增量对账。
3. **自愈增强**：在 USN 报错分支中强制刷新 Journal ID 句柄。
4. **实时注入**：在库创建成功后，由 UI 显式向 `AutoImportManager` 注入新库根 FRN。
