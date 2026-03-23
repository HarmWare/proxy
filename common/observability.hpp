#ifndef COMMON_OBSERVABILITY_HPP
#define COMMON_OBSERVABILITY_HPP

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace obs
{

enum class LogLevel : uint8_t
{
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARN = 3,
    ERROR = 4,
    FATAL = 5
};

inline const char *to_string(LogLevel level)
{
    switch (level)
    {
    case LogLevel::TRACE:
        return "TRACE";
    case LogLevel::DEBUG:
        return "DEBUG";
    case LogLevel::INFO:
        return "INFO";
    case LogLevel::WARN:
        return "WARN";
    case LogLevel::ERROR:
        return "ERROR";
    case LogLevel::FATAL:
        return "FATAL";
    default:
        return "UNKNOWN";
    }
}

inline std::string escape_json(std::string_view input)
{
    std::string out;
    out.reserve(input.size() + 8);
    for (char ch : input)
    {
        switch (ch)
        {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out += ch;
            break;
        }
    }
    return out;
}

inline std::string utc_timestamp_iso8601()
{
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto seconds_part = time_point_cast<seconds>(now);
    const auto millis = duration_cast<milliseconds>(now - seconds_part).count();

    const std::time_t tt = system_clock::to_time_t(now);
    std::tm tm_utc{};
    gmtime_r(&tt, &tm_utc);

    std::ostringstream oss;
    oss << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%S")
        << '.' << std::setw(3) << std::setfill('0') << millis << 'Z';
    return oss.str();
}

using LogFields = std::vector<std::pair<std::string, std::string>>;

class Logger
{
public:
    static Logger &instance()
    {
        static Logger logger;
        return logger;
    }

    void set_min_level(LogLevel level)
    {
        this->minLevel.store(static_cast<uint8_t>(level), std::memory_order_relaxed);
    }

    void log(LogLevel level,
             std::string_view component,
             std::string_view event,
             std::string_view message,
             const LogFields &fields = {})
    {
        if (static_cast<uint8_t>(level) < this->minLevel.load(std::memory_order_relaxed))
        {
            return;
        }

        std::ostringstream line;
        line << '{'
             << "\"timestamp\":\"" << escape_json(utc_timestamp_iso8601()) << "\","
             << "\"level\":\"" << to_string(level) << "\","
             << "\"component\":\"" << escape_json(component) << "\","
             << "\"event\":\"" << escape_json(event) << "\","
             << "\"message\":\"" << escape_json(message) << "\","
             << "\"fields\":{";

        for (size_t index = 0; index < fields.size(); ++index)
        {
            if (index > 0)
            {
                line << ',';
            }
            line << '"' << escape_json(fields[index].first) << '"'
                 << ":\"" << escape_json(fields[index].second) << '"';
        }
        line << "}}";

        std::lock_guard<std::mutex> lock(this->sinkMutex);
        std::cerr << line.str() << std::endl;
    }

private:
    Logger() = default;
    std::atomic<uint8_t> minLevel{static_cast<uint8_t>(LogLevel::INFO)};
    std::mutex sinkMutex;
};

class CircuitBreaker
{
public:
    CircuitBreaker(uint32_t failureThreshold,
                   std::chrono::milliseconds openWindow)
        : threshold(failureThreshold),
          coolDown(openWindow)
    {
    }

    bool allow()
    {
        const auto current = this->state.load(std::memory_order_relaxed);
        if (current == State::Closed)
        {
            return true;
        }

        const auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(this->stateMutex);
        if (this->state.load(std::memory_order_relaxed) == State::Open &&
            now - this->openedAt >= this->coolDown)
        {
            this->state.store(State::HalfOpen, std::memory_order_relaxed);
            return true;
        }

        return this->state.load(std::memory_order_relaxed) == State::HalfOpen;
    }

    void on_success()
    {
        this->failureCount.store(0, std::memory_order_relaxed);
        this->state.store(State::Closed, std::memory_order_relaxed);
    }

    void on_failure()
    {
        const auto failures = this->failureCount.fetch_add(1, std::memory_order_relaxed) + 1;
        if (failures >= this->threshold)
        {
            std::lock_guard<std::mutex> lock(this->stateMutex);
            this->state.store(State::Open, std::memory_order_relaxed);
            this->openedAt = std::chrono::steady_clock::now();
        }
    }

    std::string state_string() const
    {
        switch (this->state.load(std::memory_order_relaxed))
        {
        case State::Closed:
            return "closed";
        case State::Open:
            return "open";
        case State::HalfOpen:
            return "half_open";
        default:
            return "unknown";
        }
    }

private:
    enum class State : uint8_t
    {
        Closed,
        Open,
        HalfOpen
    };

    uint32_t threshold{3};
    std::chrono::milliseconds coolDown{5000};
    std::atomic<uint32_t> failureCount{0};
    std::atomic<State> state{State::Closed};
    std::chrono::steady_clock::time_point openedAt{};
    mutable std::mutex stateMutex;
};

inline std::string to_field_value(std::string_view value)
{
    return std::string(value);
}

inline std::string to_field_value(const std::string &value)
{
    return value;
}

template <typename Numeric>
inline std::string to_field_value(Numeric value)
{
    return std::to_string(value);
}

} // namespace obs

#endif // COMMON_OBSERVABILITY_HPP