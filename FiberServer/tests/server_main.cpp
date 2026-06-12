#include <csignal>
#include "FiberServer/my/application.h"
#include "FiberServer/base/log.h"

using namespace FiberServer;

int main() {
    Thread::SetName("main");
    // 故意泄漏，不 delete
    Application app;
    app.run();

    FIBER_LOG_INFO(FIBER_LOG_ROOT()) << "exit";
    return 0;
}
