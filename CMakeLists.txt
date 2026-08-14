cmake_minimum_required(VERSION 3.20)
project(ArcMeta)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 修复：优先探测 QT_DIR 环境变量，若无则使用文档红线默认路径
if(DEFINED ENV{QT_DIR})
    set(CMAKE_PREFIX_PATH "$ENV{QT_DIR}")
else()
    set(CMAKE_PREFIX_PATH "C:/Qt/6.10.2/msvc2022_64")
endif()

# 强制 MSVC 编译器及选项 (文档红线: /W4 /O2)
if(MSVC)
    add_compile_options(/W4 /O2 /utf-8 /Zm1000)
endif()

# 全局屏蔽 MSVC 针对标准 C 函数 (如 strcpy 等) 抛出的 C4996 安全警告
add_definitions(-D_CRT_SECURE_NO_WARNINGS)

# Qt 6 基础组件
# 2026-05-10 修复：添加缺失的Core和Gui组件
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg Concurrent)

# 2026-03-xx 按照用户要求：禁用 Vulkan 探测以消除日志报错，并清理构建路径矛盾
set(QT_NO_VULKAN ON)

# 路径严谨性核查：若检测到 VS 默认的 out 路径，主动输出警告提示
if(CMAKE_BINARY_DIR MATCHES "/out/")
    message(WARNING "⚠️ 检测到非法构建路径 [out]！请在 VS 工具栏切换至 [x64-release] 预设以符合《源码纯净》规范。")
endif()

# 2026-03-xx 按照用户要求：将可执行文件直接输出到指定项目子目录 ArcMeta 下，解决 \out 文件夹查找不便的问题
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/ArcMeta")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/ArcMeta")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/ArcMeta")

# 自动生成 MOC/UIC/RCC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
# 2026-06-xx 按照用户要求：彻底废除递归搜集 (GLOB)，改为显式列出所有有效源文件。
# 此举可确保构建系统在彻底移除历史冗余模块后不再包含冗余代码，符合《源码纯净》规范。
set(SOURCES
    src/main.cpp
    src/meta/sqlite3.c
    src/meta/sqlite3.h
    src/meta/AmMetaJson.cpp
    src/meta/AmMetaJson.h
    src/meta/MetaCacheDecorator.cpp
    src/meta/MetaCacheDecorator.h
    src/core/CoreController.cpp
    src/core/CoreController.h
    src/core/PhysicalDiskSearchExtractor.cpp
    src/core/PhysicalDiskSearchExtractor.h
    src/core/NativeFolderWatcher.cpp
    src/core/NativeFolderWatcher.h
    src/core/CacheManager.cpp
    src/core/CacheManager.h
    src/core/IndexedEntry.cpp
    src/core/IndexedEntry.h
    src/core/ItemRecord.cpp
    src/core/ItemRecord.h
    src/core/DatabaseSynchronizer.cpp
    src/core/DatabaseSynchronizer.h
    src/core/DiskScanService.cpp
    src/core/DiskScanService.h
    src/core/FileFilterService.cpp
    src/core/FileFilterService.h
    src/core/CategoryLoadService.cpp
    src/core/CategoryLoadService.h
    src/core/LibraryMaintenanceService.cpp
    src/core/LibraryMaintenanceService.h
    src/core/DiskTrashService.cpp
    src/core/DiskTrashService.h
    src/core/SyncStatusService.cpp
    src/core/SyncStatusService.h
    src/core/OperationSnapshotEngine.h
    src/core/OperationSnapshotEngine.cpp
    src/core/CategoryDropProcessor.h
    src/core/CategoryDropProcessor.cpp
    src/core/AutoImportManager.cpp
    src/core/AutoImportManager.h
    src/core/AppConfig.h
    src/core/NavigationHistoryService.cpp
    src/core/NavigationHistoryService.h
    src/core/SearchHistoryService.cpp
    src/core/SearchHistoryService.h
    src/crypto/EncryptionManager.cpp
    src/crypto/EncryptionManager.h
    src/meta/CategoryRepo.cpp
    src/meta/CategoryRepo.h
    src/meta/DiskTrashRepo.cpp
    src/meta/DiskTrashRepo.h
    src/meta/TagRepository.cpp
    src/meta/TagRepository.h
    src/meta/DatabaseManager.cpp
    src/meta/DatabaseManager.h
    src/meta/DatabaseMigrator.h
    src/meta/BatchRenameEngine.cpp
    src/meta/BatchRenameEngine.h
    src/meta/DuplicateDetectorService.h
    src/meta/DuplicateDetectorService.cpp
    src/meta/MetadataDefs.h
    src/meta/MetadataManager.cpp
    src/meta/MetadataManager.h
    src/meta/MediaExtractorPipeline.cpp
    src/meta/MediaExtractorPipeline.h
    src/mft/MftReader.cpp
    src/mft/MftReader.h
    src/mft/ScchCache.cpp
    src/mft/ScchCache.h
    src/ui/BatchRenameDialog.cpp
    src/ui/BatchRenameDialog.h
    src/ui/BatchRenamePreviewDialog.cpp
    src/ui/BatchRenamePreviewDialog.h
    src/ui/DuplicateConflictDialog.h
    src/ui/DuplicateConflictDialog.cpp
    src/ui/MemoryBatchRenameService.cpp
    src/ui/MemoryBatchRenameService.h
    src/ui/PresetManager.cpp
    src/ui/PresetManager.h
    src/ui/DiskBatchRenameService.cpp
    src/ui/DiskBatchRenameService.h
    src/ui/UndoToastOverlay.cpp
    src/ui/UndoToastOverlay.h
    src/ui/CategoryFilterProxyModel.h
    src/ui/BreadcrumbBar.cpp
    src/ui/BreadcrumbBar.h
    src/ui/CategoryDelegate.h
    src/ui/CategoryLockDialog.cpp
    src/ui/CategoryLockDialog.h
    src/ui/CategoryModel.cpp
    src/ui/CategoryModel.h
    src/ui/CategoryPanel.cpp
    src/ui/CategoryPanel.h
    src/ui/CategorySetPasswordDialog.cpp
    src/ui/CategorySetPasswordDialog.h
    src/ui/CategoryLockWidget.cpp
    src/ui/CategoryLockWidget.h
    src/ui/ColorPicker.cpp
    src/ui/ColorPicker.h
    src/ui/ContentPanel.cpp
    src/ui/ContentPanel.h
    src/ui/DriveButton.cpp
    src/ui/DriveButton.h
    src/ui/DropJustifiedView.cpp
    src/ui/DropJustifiedView.h
    src/ui/DropListView.cpp
    src/ui/DropListView.h
    src/ui/DropTreeView.cpp
    src/ui/DropTreeView.h
    src/ui/IScanResultView.h
    src/ui/ListResultView.cpp
    src/ui/ListResultView.h
    src/ui/GridResultView.cpp
    src/ui/GridResultView.h
    src/ui/JustifiedResultView.cpp
    src/ui/JustifiedResultView.h
    src/ui/FilterPanel.cpp
    src/ui/FilterPanel.h
    src/ui/FramelessDialog.cpp
    src/ui/FramelessDialog.h
    src/ui/FramelessFileDialog.cpp
    src/ui/FramelessFileDialog.h
    src/ui/JustifiedView.cpp
    src/ui/JustifiedView.h
    src/ui/ElidedTextUtility.h
    src/ui/CardPainterHelper.cpp
    src/ui/CardPainterHelper.h
    src/ui/ThumbnailDelegate.cpp
    src/ui/ThumbnailDelegate.h
    src/ui/LoadingWindow.cpp
    src/ui/LoadingWindow.h
    src/ui/Logger.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/TrayController.cpp
    src/ui/TrayController.h
    src/ui/HoverEventFilter.cpp
    src/ui/HoverEventFilter.h
    src/ui/ResizeEventFilter.cpp
    src/ui/ResizeEventFilter.h
    src/ui/AddressBar.cpp
    src/ui/AddressBar.h
    src/ui/MetaPanel.cpp
    src/ui/MetaPanel.h
    src/ui/TagManagerDialog.h
    src/ui/PresetTagsDialog.cpp
    src/ui/TagSelectorOverlay.h
    src/ui/TagSelectorOverlay.cpp
    src/ui/TagManagerDialog.cpp
    src/ui/TaskProgressToolBar.h
    src/ui/TaskProgressToolBar.cpp
    src/ui/components/ElasticEdit.h
    src/ui/components/ElasticEdit.cpp
    src/ui/components/TagPill.h
    src/ui/components/TagPill.cpp
    src/ui/components/FlowLayout.h
    src/ui/components/FlowLayout.cpp
    src/ui/components/ColorPill.h
    src/ui/components/ColorPill.cpp
    src/ui/NavPanel.cpp
    src/ui/NavPanel.h
    src/ui/BatchProgressDialog.h
    src/ui/QuickLookWindow.cpp
    src/ui/QuickLookWindow.h
    src/ui/QuickLookMinimap.cpp
    src/ui/QuickLookMinimap.h
    src/ui/RuleRow.cpp
    src/ui/RuleRow.h
    src/ui/CreateRuleRow.cpp
    src/ui/CreateRuleRow.h
    src/ui/BatchCreateDialog.cpp
    src/ui/BatchCreateDialog.h
    src/ui/SearchHistoryPanel.cpp
    src/ui/SearchHistoryPanel.h
    src/ui/AddressHistoryPanel.cpp
    src/ui/AddressHistoryPanel.h
    src/ui/TagManagerView.cpp
    src/ui/TagManagerView.h
    src/ui/TagManagerController.cpp
    src/ui/TagManagerController.h
    src/core/ActionCommand.h
    src/core/UndoManager.h
    src/core/BasicCommands.h
    src/ui/ToolTipOverlay.cpp
    src/ui/ToolTipOverlay.h
    src/ui/TreeItemDelegate.h
    src/ui/models/ItemModelBase.h
    src/ui/models/DiskItemModel.h
    src/ui/models/DiskItemModel.cpp
    src/ui/models/LibraryAssetModel.h
    src/ui/models/LibraryAssetModel.cpp
    src/ui/UiHelper.h
    src/ui/ShellIconManager.h
    src/ui/SvgIconRenderer.h
    src/ui/SvgIconRenderer.cpp
    src/ui/WindowsShellThumbnailProvider.h
    src/ui/WindowsShellThumbnailProvider.cpp
    src/ui/MediaColorExtractor.h
    src/ui/MediaColorExtractor.cpp
    src/ui/FormatDecoders.h
    src/ui/FormatDecoders.cpp
    src/ui/ImageDecoderFacade.h
    src/ui/ImageDecoderFacade.cpp
    src/ui/ColorAlgorithmEngine.h
    src/ui/ColorAlgorithmEngine.cpp
    src/util/DiskMediaExtractor.h
    src/util/DiskMediaExtractor.cpp
    src/meta/CapsuleMediaExtractor.h
    src/meta/CapsuleMediaExtractor.cpp
    src/util/ShellHelper.cpp
    src/util/ShellHelper.h
    src/util/ImportHelper.cpp
    src/util/ImportHelper.h
    src/util/AssetImporter.cpp
    src/util/AssetImporter.h
    ArcMeta.rc
    resources/resources.qrc
)

# 新增：libtiff 核心源码（排除 JPEG/JBIG/LZMA/WebP/zstd/LERC 等需要额外第三方库的部分，
# 排除 Unix 平台文件，排除 C++ 封装层，排除工具/构建脚本）
set(LIBTIFF_SOURCES
    src/third_party/libtiff/tif_aux.c
    src/third_party/libtiff/tif_close.c
    src/third_party/libtiff/tif_codec.c
    src/third_party/libtiff/tif_color.c
    src/third_party/libtiff/tif_compress.c
    src/third_party/libtiff/tif_dir.c
    src/third_party/libtiff/tif_dirinfo.c
    src/third_party/libtiff/tif_dirread.c
    src/third_party/libtiff/tif_dirwrite.c
    src/third_party/libtiff/tif_dumpmode.c
    src/third_party/libtiff/tif_error.c
    src/third_party/libtiff/tif_extension.c
    src/third_party/libtiff/tif_fax3.c
    src/third_party/libtiff/tif_fax3sm.c
    src/third_party/libtiff/tif_flush.c
    src/third_party/libtiff/tif_getimage.c
    src/third_party/libtiff/tif_hash_set.c
    src/third_party/libtiff/tif_luv.c
    src/third_party/libtiff/tif_lzw.c
    src/third_party/libtiff/tif_next.c
    src/third_party/libtiff/tif_ojpeg.c
    src/third_party/libtiff/tif_open.c
    src/third_party/libtiff/tif_packbits.c
    src/third_party/libtiff/tif_pixarlog.c
    src/third_party/libtiff/tif_predict.c
    src/third_party/libtiff/tif_print.c
    src/third_party/libtiff/tif_read.c
    src/third_party/libtiff/tif_strip.c
    src/third_party/libtiff/tif_swab.c
    src/third_party/libtiff/tif_thunder.c
    src/third_party/libtiff/tif_tile.c
    src/third_party/libtiff/tif_version.c
    src/third_party/libtiff/tif_warning.c
    src/third_party/libtiff/tif_win32.c
    src/third_party/libtiff/tif_write.c
    src/third_party/libtiff/tif_zip.c
)
list(APPEND SOURCES ${LIBTIFF_SOURCES})

# 可执行文件 (添加 WIN32 属性以消除控制台黑框)
# 修复：移除直接引用的 "ArcMeta.manifest"，改由 ArcMeta.rc 引入以杜绝 mt.exe 冲突
add_executable(ArcMeta WIN32 ${SOURCES})

# 链接库 (文档红线列表: ole32, bcrypt)
# 2026-04-14 按照用户要求：恢复 MFT 逻辑，重新引入 ntdll 依赖
# 2026-05-10 修复：添加缺失的Qt6::Core和Qt6::Gui库
target_link_libraries(ArcMeta PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Svg
    Qt6::Concurrent
    ntdll
    ole32
    bcrypt
    psapi
)

target_include_directories(ArcMeta PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/meta
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/models
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mft
    ${CMAKE_CURRENT_SOURCE_DIR}/src/crypto
    ${CMAKE_CURRENT_SOURCE_DIR}/src/third_party/libtiff
)

# 2026-07-20 按照用户最新要求：
# 彻底去除强制管理员权限，改为普通权限 (asInvoker)
if(MSVC)
    target_link_options(ArcMeta PRIVATE "/MANIFESTUAC:level='asInvoker' uiAccess='false'")
endif()
# Force Refresh CMake: 2026-06-xx 11:30 (Removed AmMetaJson & SyncQueue, switched to DB-only)