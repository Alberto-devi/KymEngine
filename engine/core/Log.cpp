#include "core/Log.hpp"

#include <spdlog/sinks/base_sink.h>
#ifdef __ANDROID__
#include <spdlog/sinks/android_sink.h>
#else
#include <spdlog/sinks/stdout_color_sinks.h>
#endif

#include <deque>
#include <mutex>

namespace Kyumist {

namespace {

class EditorConsoleSink final : public spdlog::sinks::base_sink<std::mutex> {
public:
    static constexpr size_t MAX_ENTRIES = 500;

    std::vector<LogEntry> CopyEntries() {
        std::lock_guard<std::mutex> lock(mutex_);
        return {m_Entries.begin(), m_Entries.end()};
    }

    void ClearEntries() {
        std::lock_guard<std::mutex> lock(mutex_);
        m_Entries.clear();
    }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        spdlog::memory_buf_t formatted;
        base_sink<std::mutex>::formatter_->format(msg, formatted);

        std::string text(formatted.data(), formatted.size());
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
            text.pop_back();
        }

        if (m_Entries.size() >= MAX_ENTRIES) {
            m_Entries.pop_front();
        }

        m_Entries.push_back(LogEntry{
            msg.level,
            std::string(msg.logger_name.data(), msg.logger_name.size()),
            std::move(text)
        });
    }

    void flush_() override {}

private:
    std::deque<LogEntry> m_Entries;
};

std::shared_ptr<EditorConsoleSink> s_ConsoleSink;

} // namespace

std::shared_ptr<spdlog::logger> Log::s_CoreLogger;
std::shared_ptr<spdlog::logger> Log::s_ClientLogger;

void Log::Init() {
    if (s_CoreLogger && s_ClientLogger) {
        return;
    }

#ifdef __ANDROID__
    auto consoleOutSink = std::make_shared<spdlog::sinks::android_sink_mt>("KymEngine");
    consoleOutSink->set_pattern("[%n] %v");
#else
    auto consoleOutSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    consoleOutSink->set_pattern("%^[%T] [%n] [%l]: %v%$");
#endif

    s_ConsoleSink = std::make_shared<EditorConsoleSink>();
    s_ConsoleSink->set_pattern("[%T] %v");

    std::vector<spdlog::sink_ptr> sinks{consoleOutSink, s_ConsoleSink};

    s_CoreLogger = std::make_shared<spdlog::logger>("KYUMIST", sinks.begin(), sinks.end());
    s_CoreLogger->set_level(spdlog::level::trace);
    spdlog::register_logger(s_CoreLogger);

    s_ClientLogger = std::make_shared<spdlog::logger>("EDITOR", sinks.begin(), sinks.end());
    s_ClientLogger->set_level(spdlog::level::trace);
    spdlog::register_logger(s_ClientLogger);
}

std::vector<LogEntry> Log::GetLogEntries() {
    if (!s_ConsoleSink) {
        return {};
    }
    return s_ConsoleSink->CopyEntries();
}

void Log::ClearLogEntries() {
    if (s_ConsoleSink) {
        s_ConsoleSink->ClearEntries();
    }
}

} // namespace Kyumist
