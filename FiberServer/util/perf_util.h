#pragma once

#include "FiberServer/base/log.h"

#include <chrono>
#include <stdint.h>
#include <string>

namespace FiberServer {

class PerfTimer {
public:
    PerfTimer()
        : m_start(Clock::now()) {
    }

    int64_t elapsedMs() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now() - m_start).count();
    }

private:
    typedef std::chrono::steady_clock Clock;
    Clock::time_point m_start;
};

class ScopedPerfLog {
public:
    explicit ScopedPerfLog(const std::string& route)
        : m_route(route)
        , m_logger(FIBER_LOG_NAME("perf")) {
    }

    ~ScopedPerfLog() noexcept {
        try {
            FIBER_LOG_INFO(m_logger) << "perf route=" << m_route
                << " status=" << m_status
                << " total_ms=" << m_total.elapsedMs()
                << " db_ms=" << m_dbMs
                << " fastdfs_ms=" << m_fastdfsMs
                << " file_io_ms=" << m_fileIoMs
                << m_extra;
        } catch (...) {
        }
    }

    void setStatus(const std::string& status) {
        m_status = status;
    }

    void addDbMs(int64_t ms) {
        m_dbMs += ms;
    }

    void addFastDfsMs(int64_t ms) {
        m_fastdfsMs += ms;
    }

    void addFileIoMs(int64_t ms) {
        m_fileIoMs += ms;
    }

    void appendExtra(const std::string& extra) {
        m_extra += extra;
    }

private:
    std::string m_route;
    std::string m_status = "error";
    Logger::ptr m_logger;
    PerfTimer m_total;
    int64_t m_dbMs = 0;
    int64_t m_fastdfsMs = 0;
    int64_t m_fileIoMs = 0;
    std::string m_extra;
};

}
