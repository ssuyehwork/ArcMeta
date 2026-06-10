#pragma once

#include <QString>

namespace ArcMeta {

/**
 * @brief 2026-06-xx 按照分析计划 #8：全局指令抽象基类
 */
class ActionCommand {
public:
    virtual ~ActionCommand() = default;

    virtual void execute() = 0;
    virtual void undo() = 0;
    virtual void redo() { execute(); }

    virtual QString description() const = 0;
};

} // namespace ArcMeta
