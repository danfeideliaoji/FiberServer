#include "status_servlet.h"
#include "FiberServer/scheduler.h"
#include "FiberServer/util/json_util.h"

namespace FiberServer {
namespace http {

StatusServlet::StatusServlet()
    :Servlet("StatusServlet") {
}

int32_t StatusServlet::handle(http::HttpRequest::ptr request
                              ,http::HttpResponse::ptr response
                              ,http::HttpSession::ptr session) {
    response->setHeader("Content-Type", "application/json charset=utf-8");

    Scheduler* scheduler = Scheduler::GetThis();
    Json::Value body;
    body["code"] = 0;

    if(!scheduler) {
        // 如果该接口不是在 Scheduler 工作线程上下文中被调用，可能拿不到调度器。
        // 这里返回稳定的 JSON 结构，而不是让接口失败。
        body["scheduler"] = Json::Value(Json::objectValue);
        body["scheduler"]["available"] = false;
        response->setBody(JsonUtil::ToString(body));
        return 0;
    }

    Scheduler::SchedulerStats stats = scheduler->getStats();
    Json::Value scheduler_json;
    // 这个接口刻意保持轻量：只采样计数器和队列长度。
    // 它不会暂停调度器，因此返回的是观测快照，不是严格一致的运行时状态。
    scheduler_json["available"] = true;
    scheduler_json["name"] = stats.name;
    scheduler_json["global_queue_size"] = (Json::UInt64)stats.global_queue_size;
    scheduler_json["global_schedule_count"] = (Json::UInt64)stats.global_schedule_count;
    scheduler_json["active_thread_count"] = (Json::UInt64)stats.active_thread_count;
    scheduler_json["idle_thread_count"] = (Json::UInt64)stats.idle_thread_count;

    // 暴露每个 P 的队列长度和执行来源统计。
    // 压测后可以据此判断任务主要命中了本地队列、全局队列还是任务窃取路径。
    Json::Value processors(Json::arrayValue);
    for(const auto& processor : stats.processors) {
        Json::Value item;
        // queue_size 表示当前积压，execute/source 计数表示已完成任务来自哪里。
        // 两者结合可以看出 GMP 队列是否真正被使用，还是全部退回全局队列。
        item["id"] = processor.id;
        item["queue_size"] = (Json::UInt64)processor.queue_size;
        item["schedule_count"] = (Json::UInt64)processor.schedule_count;
        item["execute_count"] = (Json::UInt64)processor.execute_count;
        item["local_execute_count"] = (Json::UInt64)processor.local_execute_count;
        item["global_execute_count"] = (Json::UInt64)processor.global_execute_count;
        item["steal_execute_count"] = (Json::UInt64)processor.steal_execute_count;
        item["global_pull_count"] = (Json::UInt64)processor.global_pull_count;
        item["global_batch_count"] = (Json::UInt64)processor.global_batch_count;
        item["steal_count"] = (Json::UInt64)processor.steal_count;
        item["steal_batch_count"] = (Json::UInt64)processor.steal_batch_count;
        item["steal_attempt_count"] = (Json::UInt64)processor.steal_attempt_count;
        item["steal_fail_count"] = (Json::UInt64)processor.steal_fail_count;
        processors.append(item);
    }
    scheduler_json["processors"] = processors;
    body["scheduler"] = scheduler_json;

    response->setBody(JsonUtil::ToString(body));
    return 0;
}

}
}
