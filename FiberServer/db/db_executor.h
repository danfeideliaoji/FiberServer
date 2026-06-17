#pragma once

#include "FiberServer/base/singleton.h"
#include "FiberServer/fiber.h"
#include "FiberServer/iomanager.h"

#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>

namespace FiberServer {

// 数据库任务执行器。
// 业务协程调用 submit() 后，实际数据库操作会被投递到独立的 db IOManager，
// 当前协程挂起等待结果，避免主业务调度线程被同步数据库调用长时间占住。
class DbExecutor {
public:
    DbExecutor();
    ~DbExecutor();

    template<class Fn>
    auto submit(Fn&& fn) -> typename std::invoke_result<Fn>::type {
        using Result = typename std::invoke_result<Fn>::type;
        static_assert(!std::is_void<Result>::value, "DbExecutor::submit requires a non-void result");

        // 如果已经在 db 执行器线程里，直接执行，避免递归投递后自己等自己。
        if (!m_iom || Scheduler::GetThis() == m_iom.get()) {
            return fn();
        }

        Scheduler* caller = Scheduler::GetThis();
        Fiber::ptr fiber = Fiber::GetThis();
        // 非协程环境下没有可挂起/唤醒的 fiber，只能同步执行。
        if (!caller || !fiber) {
            return fn();
        }

        // 调用协程和 db 工作线程共享这个状态对象传递结果或异常。
        struct TaskState {
            std::mutex mutex;
            std::optional<Result> result;
            std::exception_ptr exception;
        };
        auto state = std::make_shared<TaskState>();

        // 把数据库函数投递给 db IOManager。执行完成后重新调度原来的业务协程。
        m_iom->schedule([state, caller, fiber, fn = std::forward<Fn>(fn)]() mutable {
            try {
                auto value = fn();
                {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    state->result.emplace(std::move(value));
                }
            } catch (...) {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->exception = std::current_exception();
            }
            caller->schedule(fiber);
        });

        // 当前业务协程让出执行权，等待 db 线程完成后 schedule 回来。
        Fiber::YieldToHold();

        std::lock_guard<std::mutex> lock(state->mutex);
        // 保持调用方语义：db 线程抛出的异常在原调用协程里重新抛出。
        if (state->exception) {
            std::rethrow_exception(state->exception);
        }
        return std::move(*state->result);
    }

private:
    // 专门跑数据库任务的调度器，线程数由 db.worker_threads 或环境变量控制。
    std::unique_ptr<IOManager> m_iom;
};

typedef Singleton<DbExecutor> DbExecutorMgr;

}
