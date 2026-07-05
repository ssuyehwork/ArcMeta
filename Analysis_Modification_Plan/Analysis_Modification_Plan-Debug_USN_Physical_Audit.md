# 实施方案 - UsnWatcher 最上游物理审计埋点

## 1. 需求背景
为了验证 `UsnWatcher` 线程是否真的启动并进入工作状态，需要绕过 Qt 的信号槽和 GUI 限制，在磁盘驱动器调用层（`UsnWatcher::run`）插入最原始的文件追加日志记录，作为“物理级对账”的证据。

## 2. 逻辑埋点位置

### 2.1 函数入口审计 (确认线程启动)
- **位置**：`UsnWatcher::run()` 第一行。
- **目的**：确认 `QThread::start()` 是否成功触发了操作系统的线程调度。

### 2.2 驱动通信审计 (确认权限与句柄)
- **位置**：`DeviceIoControl(FSCTL_QUERY_USN_JOURNAL)` 调用之后。
- **目的**：记录返回值及 `GetLastError()`，确认为管理员权限下的句柄访问是否成功。

### 2.3 循环活跃度审计 (确认监听状态)
- **位置**：`while` 循环体入口及 `FSCTL_READ_USN_JOURNAL` 报错分支。
- **目的**：确认线程是否由于异常报错而提前退出或陷入无效死循环。

## 3. 技术实现 (Git Merge Diff 预览)

**文件：** `src/mft/UsnWatcher.cpp`

```cpp
#include <fstream>
#include <chrono>
#include <ctime>

// 物理级审计宏：直接写盘，杜绝脑补
inline void PHYSICAL_AUDIT(const std::string& msg) {
    std::ofstream logFile("C:\\ArcMeta_Debug.log", std::ios::app);
    if (logFile.is_open()) {
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        char buf[20];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
        logFile << "[" << buf << "] " << msg << std::endl;
    }
}

void UsnWatcher::run() {
    std::string vol = QString::fromStdWString(m_volume).toStdString();
    PHYSICAL_AUDIT(">>> [UsnWatcher] run() ENTERED for: " + vol);

    // 1. 获取 Journal ID
    if (!DeviceIoControl(...)) {
        PHYSICAL_AUDIT("!!! [UsnWatcher] FAILED: Query Journal (LastError: " + std::to_string(GetLastError()) + ")");
        return;
    }
    PHYSICAL_AUDIT("SUCCESS: Journal queried for " + vol);

    while (!m_stopRequested.load()) {
        // 循环审计...
    }
}
```

## 4. 风险控制与资深程序员预判
- **预判结论**：在“无缓存盘符”场景下，`MftReader` 根本没有 `new` 出对应的 `UsnWatcher` 对象。
- **证据支持**：若 `C:\ArcMeta_Debug.log` 中完全没有特定盘符的记录，则证实为“启动点火逻辑缺失”而非信号丢失。
- **安全性**：使用 `std::ios::app` 确保多线程安全追加。
