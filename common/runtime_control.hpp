#ifndef COMMON_RUNTIME_CONTROL_HPP
#define COMMON_RUNTIME_CONTROL_HPP

#include <atomic>
#include <csignal>

namespace runtime
{

class ShutdownController
{
public:
    static void install()
    {
        std::signal(SIGINT, &ShutdownController::handle_signal);
        std::signal(SIGTERM, &ShutdownController::handle_signal);
    }

    static bool requested()
    {
        return flag().load(std::memory_order_relaxed);
    }

    static void reset()
    {
        flag().store(false, std::memory_order_relaxed);
    }

private:
    static std::atomic<bool> &flag()
    {
        static std::atomic<bool> stopRequested{false};
        return stopRequested;
    }

    static void handle_signal(int)
    {
        flag().store(true, std::memory_order_relaxed);
    }
};

} // namespace runtime

#endif // COMMON_RUNTIME_CONTROL_HPP