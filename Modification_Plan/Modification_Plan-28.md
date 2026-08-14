# 编译配置引入 CategoryLockWidget 源文件 —— Modification_Plan-28.md

> 状态：已批准，执行中 / 已执行完成

## 1. 任务背景
本方案承接自 Modification_Plan-27.md。为了让新创建的 `CategoryLockWidget.h` 和 `CategoryLockWidget.cpp` 源文件被编译系统感知并顺畅构建，不造成 unresolved external 链接报错，需要在 `CMakeLists.txt` 中将两个文件物理地追加到静态源文件列表。

## 2. 问题定位
CMake 在没有文件追加的情况下无法识别新增文件，构建将直接失败。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 将新文件加入编译系统 | 在 CMakeLists.txt 的 SOURCES 列表中追加 CategoryLockWidget.h 和 CategoryLockWidget.cpp 的物理路径。 | ✅ |

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换。

### 4.1 修改 `CMakeLists.txt`

```diff
<<<<<<< SEARCH
    src/ui/CategoryModel.cpp
    src/ui/CategoryModel.h
    src/ui/CategoryPanel.cpp
    src/ui/CategoryPanel.h
    src/ui/CategorySetPasswordDialog.cpp
    src/ui/CategorySetPasswordDialog.h
    src/ui/ColorPicker.cpp
=======
    src/ui/CategoryModel.cpp
    src/ui/CategoryModel.h
    src/ui/CategoryPanel.cpp
    src/ui/CategoryPanel.h
    src/ui/CategorySetPasswordDialog.cpp
    src/ui/CategorySetPasswordDialog.h
    src/ui/CategoryLockWidget.cpp
    src/ui/CategoryLockWidget.h
    src/ui/ColorPicker.cpp
>>>>>>> REPLACE
```

## 5. 修改边界声明【范围】
- [x] 修改 `CMakeLists.txt`

## 6. 实现准则与预警【核心】
（无）

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 源码纯净与显式文件列表 | 彻底废除 GLOB 递归，改为显式列出有效源文件。 | ✅ 符合。 |
