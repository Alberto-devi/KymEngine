#pragma once

#include <memory>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

namespace Kyumist {

struct LogEntry {
    spdlog::level::level_enum level;
    std::string loggerName;
    std::string message;
};

class Log {
public:
    static void Init();

    static std::shared_ptr<spdlog::logger>& GetCoreLogger() { return s_CoreLogger; }
    static std::shared_ptr<spdlog::logger>& GetClientLogger() { return s_ClientLogger; }

    static std::vector<LogEntry> GetLogEntries();
    static void ClearLogEntries();

private:
    static std::shared_ptr<spdlog::logger> s_CoreLogger;
    static std::shared_ptr<spdlog::logger> s_ClientLogger;
};

} // namespace Kyumist

#define KM_CORE_TRACE(...)    ::Kyumist::Log::GetCoreLogger()->trace(__VA_ARGS__)
#define KM_CORE_INFO(...)     ::Kyumist::Log::GetCoreLogger()->info(__VA_ARGS__)
#define KM_CORE_WARN(...)     ::Kyumist::Log::GetCoreLogger()->warn(__VA_ARGS__)
#define KM_CORE_ERROR(...)    ::Kyumist::Log::GetCoreLogger()->error(__VA_ARGS__)
#define KM_CORE_CRITICAL(...) ::Kyumist::Log::GetCoreLogger()->critical(__VA_ARGS__)

#define KM_TRACE(...)         ::Kyumist::Log::GetClientLogger()->trace(__VA_ARGS__)
#define KM_INFO(...)          ::Kyumist::Log::GetClientLogger()->info(__VA_ARGS__)
#define KM_WARN(...)          ::Kyumist::Log::GetClientLogger()->warn(__VA_ARGS__)
#define KM_ERROR(...)         ::Kyumist::Log::GetClientLogger()->error(__VA_ARGS__)
#define KM_CRITICAL(...)      ::Kyumist::Log::GetClientLogger()->critical(__VA_ARGS__)
