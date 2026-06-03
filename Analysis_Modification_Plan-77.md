# Analysis and Modification Plan - 77

## 1. 编译错误根因分析
用户在本地删除了核心数据库类文件后，出现了 `CategoryRepo.h: No such file or directory` 的编译错误。经过排查，主要原因如下：

- **僵尸文件残留**: `src/db/SyncEngine.cpp` 和 `src/db/FolderRepo.h` 依然存在。它们内部硬编码引用了已删除的 `Database.h`、`ItemRepo.h` 和 `FolderRepo.h`。
- **编译依赖链断裂**: 当编译器处理 `SyncEngine.cpp` 时，由于找不到其依赖的头文件，整个构建过程会崩溃。在某些 IDE 环境下，错误可能会向上级目录传播或因缓存问题显示为同目录下的其他文件（如 `CategoryRepo.h`）丢失。
- **构建系统（CMake）未同步**: `CMakeLists.txt` 中依然列出了已删除的文件，导致构建系统尝试编译不存在的源文件。

## 2. 彻底清理方案

### 2.1 物理删除清单 (必须执行)
为了彻底解决编译错误，必须删除以下残留文件：
1. `src/db/SyncEngine.cpp`
2. `src/db/SyncEngine.h`
3. `src/db/FolderRepo.h`
4. `src/db/FavoritesRepo.h` (如果仍存在)

### 2.2 修复引用残留
- **src/db/CategoryRepo.cpp**: 检查是否还存在对已删除 DB 类的间接调用（目前看主要是 `MetadataManager` 间的引用）。
- **src/ui/CategoryModel.cpp**: 移除对 `FavoritesRepo.h` 的旧引用，或将其指向迁移后的新位置。

### 2.3 更新构建配置 (`CMakeLists.txt`)
- 在 `set(SOURCES ...)` 块中，移除以下行：
  - `src/db/FavoritesRepo.h`
  - `src/db/FolderRepo.cpp` (已删除)
  - `src/db/FolderRepo.h`
  - `src/db/ItemRepo.cpp` (已删除)
  - `src/db/ItemRepo.h` (已删除)
  - `src/db/SyncEngine.cpp`
  - `src/db/SyncEngine.h`

## 3. 架构建议：去 DB 化收尾
- **目录迁移**: 将 `src/db/` 中剩余的唯一有效文件 `CategoryRepo.cpp/h` 和 `FavoritesRepo.cpp` 移动到 `src/meta/`。
- **删除目录**: 物理删除 `src/db/` 文件夹。
- **刷新缓存**: 在删除文件后，务必清理 CMake 缓存（Delete Cache）并重新生成项目文件，以确保头文件搜索索引（Include Path）完全刷新。

## 4. 总结
当前的报错是由于“不彻底的删除”导致的。残留的同步引擎试图引用已不存在的数据库接口。只要完成上述物理删除和 CMake 同步，报错即可消失。
