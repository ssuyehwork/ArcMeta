#include "AutoImportManager.h"
#include "../mft/MftReader.h"
#include "../meta/MetadataManager.h"
#include "../meta/DatabaseManager.h"
#include "../util/ImportHelper.h"
#include "AppConfig.h"
#include <QDebug>
#include <QCoreApplication>
#include <QDir>
#include <QMetaObject>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace ArcMeta {

AutoImportManager& AutoImportManager::instance() {
    static AutoImportManager inst;
    return inst;
}

AutoImportManager::AutoImportManager(QObject* parent) : QObject(parent) {
    m_debounceTimer = new QTimer(this);
    m_debounceTimer->setInterval(3000); 
    m_debounceTimer->setSingleShot(true);
    connect(m_debounceTimer, &QTimer::timeout, this, &AutoImportManager::processImportQueue);

    connect(&m_taskWatcher, &QFutureWatcher<void>::finished, this, &AutoImportManager::scheduleNextTask);
}

AutoImportManager::~AutoImportManager() {
    stopListening();
}

void AutoImportManager::startListening() {
    if (m_isListening) return;
    connect(&MftReader::instance(), &MftReader::entryAdded, this, &AutoImportManager::onEntryAdded, Qt::QueuedConnection);
    connect(&MftReader::instance(), &MftReader::entryRemoved, this, &AutoImportManager::onEntryRemoved, Qt::QueuedConnection);
    m_isListening = true;
}

void AutoImportManager::stopListening() {
    if (!m_isListening) return;
    disconnect(&MftReader::instance(), &MftReader::entryAdded, this, &AutoImportManager::onEntryAdded);
    disconnect(&MftReader::instance(), &MftReader::entryRemoved, this, &AutoImportManager::onEntryRemoved);
    m_isListening = false;
}

void AutoImportManager::onEntryAdded(uint64_t key) {
    int idx = MftReader::instance().getIndexByKey(key);
    if (idx < 0) return;

    QString qPath = MftReader::instance().getFullPath(idx);
    std::wstring fullPath = qPath.toStdWString();
    std::wstring managedFolder;
    
    if (checkAndGetManagedPath(fullPath, managedFolder)) {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_pendingPaths.push_back(fullPath);
        
        QMetaObject::invokeMethod(m_debounceTimer, "start", Qt::QueuedConnection);
    }
}

void AutoImportManager::onEntryRemoved(uint64_t key) {
    int idx = MftReader::instance().getIndexByKey(key);
    if (idx < 0) return;

    QString qPath = MftReader::instance().getFullPath(idx);
    std::wstring fullPath = qPath.toStdWString();
    std::wstring managedFolder;

    if (checkAndGetManagedPath(fullPath, managedFolder)) {
        // 2026-07-xx 按照 Plan-97：联动执行出库/失效逻辑
        MetadataManager::instance().deletePermanently(fullPath);
    }
}

bool AutoImportManager::checkAndGetManagedPath(const std::wstring& path, std::wstring& outManagedFolder) {
    // 逻辑一：匹配 ArcMeta.FERREX (Plan-97)
    // 判定路径是否位于 \ArcMeta.FERREX 内部
    // 适配 C:\ArcMeta.FERREX (pos=2) 或 \\?\C:\ArcMeta.FERREX (pos=6)
    // 注意：如果是 ArcMeta.FERREX 本身（pos + 15 == path.length()），则不作为托管内容处理，
    // 只有其内部子项才触发自动入库。
    size_t pos = path.find(L"\\ArcMeta.FERREX");
    if (pos != std::string::npos) {
        bool isRoot = false;
        if (pos == 2 && path[1] == L':') isRoot = true; // C:\...
        else if (pos == 6 && path.find(L"\\\\?\\") == 0) isRoot = true; // \\?\C:\...
        
        if (isRoot && path.length() > pos + 15) {
            outManagedFolder = path.substr(0, pos + 15);
            return true;
        }
    }

    // 逻辑二：匹配 AppConfig 托管路径 (Plan-68)
    std::wstring volSerial = MetadataManager::getVolumeSerialNumber(path);
    if (volSerial.empty()) return false;

    std::wstring managedAbs = getManagedFolderAbsolutePath(volSerial);
    if (managedAbs.empty()) return false;

    if (path.size() >= managedAbs.size() && _wcsnicmp(path.c_str(), managedAbs.c_str(), managedAbs.size()) == 0) {
        outManagedFolder = managedAbs;
        return true;
    }
    return false;
}

std::wstring AutoImportManager::getManagedFolderAbsolutePath(const std::wstring& volSerial) {
    // 根据序列号反查当前盘符 (Plan-68 4.1)
    QString drive;
    const auto drives = QDir::drives();
    for (const QFileInfo& d : drives) {
        if (MetadataManager::getVolumeSerialNumber(d.absolutePath().toStdWString()) == volSerial) {
            drive = d.absolutePath();
            break;
        }
    }
    if (drive.isEmpty()) return L"";

    QString key = QString("ManagedFolder/Volume_%1").arg(QString::fromStdWString(volSerial));
    QString relPath = AppConfig::instance().getValue(key, "").toString();
    if (relPath.isEmpty()) return L"";

    return MetadataManager::normalizePath((drive.toStdWString() + relPath.toStdWString()));
}

void AutoImportManager::processImportQueue() {
    std::vector<std::wstring> pathsToProcess;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        pathsToProcess = std::move(m_pendingPaths);
        m_pendingPaths.clear();
    }

    if (pathsToProcess.empty()) return;

    // 按照盘符归类任务
    std::lock_guard<std::recursive_mutex> lock(m_schedulerMutex);
    for (const auto& p : pathsToProcess) {
        // 提取盘符 (统一为 "C:" 格式以对齐 UI Map)
        size_t colonPos = p.find(L":");
        if (colonPos == std::string::npos || colonPos == 0) continue;
        QString letter = QString::fromWCharArray(&p[colonPos - 1], 2).toUpper();

        auto it = std::find_if(m_taskQueue.begin(), m_taskQueue.end(), [&](const DriveTask& t) { return t.letter == letter; });
        if (it != m_taskQueue.end()) {
            it->paths.push_back(p);
        } else {
            m_taskQueue.push_back({letter, {p}});
        }
    }

    if (m_activeTask.letter.isEmpty()) {
        scheduleNextTask();
    }
}

void AutoImportManager::setPriorityDrive(const QString& letter) {
    std::lock_guard<std::recursive_mutex> lock(m_schedulerMutex);
    auto it = std::find_if(m_taskQueue.begin(), m_taskQueue.end(), [&](const DriveTask& t) { return t.letter == letter; });
    if (it != m_taskQueue.end()) {
        DriveTask task = *it;
        m_taskQueue.erase(it);
        m_taskQueue.push_front(task);
    } else if (m_activeTask.letter != letter) {
        // 如果队列里没有，且当前不在处理，则创建一个空路径任务置顶（仅用于 UI 排序展示优先级，实际 USN 任务会随之后到来）
        m_taskQueue.push_front({letter, {}});
    }
    
    // 如果当前空闲，立即开始
    if (m_activeTask.letter.isEmpty()) {
        scheduleNextTask();
    }
}

void AutoImportManager::scheduleNextTask() {
    std::lock_guard<std::recursive_mutex> lock(m_schedulerMutex);
    
    // 1. 如果当前有正在执行的任务，说明是刚结束，触发结束信号并清理
    if (!m_activeTask.letter.isEmpty()) {
        QString finishedLetter = m_activeTask.letter;
        m_activeTask = DriveTask();
        emit taskFinished(finishedLetter);
    }

    // 2. 如果队列也空了，触发全部完成信号
    if (m_taskQueue.empty()) {
        emit allTasksCompleted();
        return;
    }

    // 3. 取出下一个任务
    m_activeTask = m_taskQueue.front();
    m_taskQueue.pop_front();

    // 4. 触发开始信号（驱动 UI 转圈）
    emit taskStarted(m_activeTask.letter);

    // 5. 执行任务
    QStringList qPaths;
    for (const auto& wp : m_activeTask.paths) {
        qPaths << QString::fromStdWString(wp);
    }

    if (!qPaths.isEmpty()) {
        // 串行执行导入任务
        m_taskWatcher.setFuture(ImportHelper::importPaths(qPaths, 0, nullptr));
    } else {
        // 如果是空路径任务（仅为了置顶展示），延迟触发下一步以确保信号已被 UI 处理
        QTimer::singleShot(500, this, &AutoImportManager::scheduleNextTask);
    }
}

} // namespace ArcMeta
