# PRODUCTION_READY_REPORT

## Refactored Component Map

- **Common Runtime & Observability Layer (new)**
  - Added reusable production primitives in `common/`:
    - `observability.hpp`: structured JSON logger, log levels, circuit breaker.
    - `runtime_control.hpp`: SIGINT/SIGTERM-driven shutdown controller.
  - Decouples operational concerns from business logic.

- **Proxy Core (`proxy/`)**
  - `Proxy` now exposes timeout-aware waiting and runtime metrics snapshots.
  - Configuration loading now validates bounds and prevents duplicate state accumulation.
  - Main loop now includes:
    - graceful shutdown behavior,
    - heartbeat observability,
    - circuit-breaker-controlled publish path.

- **Simulator Harness (`sim/`)**
  - Callback payload exchange is mutex-protected (no string race across callback/main thread).
  - File I/O contracts now use `std::optional` and `std::filesystem::path`.
  - Main loop now exits cleanly on termination signals.

- **Target Harness (`trgt/`)**
  - Added type-safe target-id validation via `std::optional` + `std::string_view`.
  - Callback payload exchange is mutex-protected.
  - File I/O hardened with `std::optional` and path-safe APIs.

- **Build Hardening (all runtime executables)**
  - Enforced C++17 strictness (`CMAKE_CXX_STANDARD_REQUIRED`, no compiler extensions).
  - Added warning and hardening flags (`-Wextra`, `-fstack-protector-strong`, RELRO/NOW).

---

## Production Patch (Raw Code ➜ Production-Ready)

### 1) Console Logging ➜ Structured JSON Logging

**Raw Code**
```cpp
std::cout << "Connecting to server '" << address << "'..." << std::flush;
```

**Production-Ready**
```cpp
obs::Logger::instance().log(
    obs::LogLevel::INFO,
    "sim",
    "broker_connecting",
    "Connecting to MQTT broker",
    {{"uri", address}});
```

---

### 2) Infinite Loop Without Shutdown ➜ Graceful Termination

**Raw Code**
```cpp
while (true)
{
    // work
    sleep(2);
}
```

**Production-Ready**
```cpp
runtime::ShutdownController::install();

while (!runtime::ShutdownController::requested())
{
    // work
    std::this_thread::sleep_for(std::chrono::seconds(2));
}
```

---

### 3) Non-Type-Safe Input Handling ➜ Validated Optional Parsing

**Raw Code**
```cpp
std::string TRGT_ID = (argc > 1) ? std::string(argv[1]) : "01";
```

**Production-Ready**
```cpp
auto targetId = parseTargetId(rawTargetId);
if (!targetId.has_value())
{
    obs::Logger::instance().log(
        obs::LogLevel::FATAL,
        "trgt",
        "invalid_target_id",
        "Target ID must be a two-digit numeric value",
        {{"value", rawTargetId}});
    return 1;
}
```

---

### 4) Unsynchronized Shared Message ➜ Thread-Safe Callback Handoff

**Raw Code**
```cpp
std::atomic<bool> recived_msg_flag{false};
std::string msg;

void message_arrived(mqtt::const_message_ptr msg) override {
    recived_msg_flag = true;
    this->msg = msg->to_string();
}
```

**Production-Ready**
```cpp
std::atomic<bool> recived_msg_flag{false};
std::mutex msgMutex;
std::string msg;

void message_arrived(mqtt::const_message_ptr messagePtr) override {
    std::lock_guard<std::mutex> lock(this->msgMutex);
    this->msg = messagePtr->to_string();
    recived_msg_flag.store(true, std::memory_order_relaxed);
}

std::string take_message() {
    std::lock_guard<std::mutex> lock(this->msgMutex);
    recived_msg_flag.store(false, std::memory_order_relaxed);
    return this->msg;
}
```

---

### 5) Blocking Wait Only ➜ Timeout + Heartbeat Metrics

**Raw Code**
```cpp
Proxy_Flag_t flag = myProxy.waitForData();
```

**Production-Ready**
```cpp
Proxy_Flag_t flag = myProxy.waitForData(std::chrono::milliseconds(1000));
if (flag == Proxy_Flag_t::NOT)
{
    const auto metrics = myProxy.getMetricsSnapshot();
    obs::Logger::instance().log(
        obs::LogLevel::INFO,
        "proxy",
        "heartbeat",
        "Proxy alive",
        {{"received_messages", obs::to_field_value(metrics.receivedMessages)},
         {"published_messages", obs::to_field_value(metrics.publishedMessages)}});
    continue;
}
```

---

### 6) No Degradation Strategy ➜ Circuit Breaker Around Publish Path

**Raw Code**
```cpp
myProxy.publish(Proxy_Flag_t::CARLA);
```

**Production-Ready**
```cpp
obs::CircuitBreaker publishBreaker(3, std::chrono::milliseconds(5000));

if (!publishBreaker.allow()) {
    obs::Logger::instance().log(obs::LogLevel::WARN, "proxy", "circuit_open",
                                "Skipping publish cycle due to open circuit breaker");
    continue;
}

myProxy.publish(Proxy_Flag_t::CARLA);
publishBreaker.on_success();
```

---

## Deployment Checklist (SRE/Production)

### Compiler & Linker
- `-O2` or `-O3` (through `CMAKE_BUILD_TYPE=Release` and/or `CMAKE_CXX_FLAGS_RELEASE`).
- `-fstack-protector-strong`
- `-D_FORTIFY_SOURCE=2`
- `-Wl,-z,relro -Wl,-z,now`
- `-Wall -Wextra -Wpedantic -Wshadow -Wconversion`
- Recommended additions:
  - `-fPIE` + linker `-pie`
  - `-fno-omit-frame-pointer` (better profiling/incident forensics)
  - LTO in release (`-flto`) after benchmarking.

### Runtime Configuration
- Run Mosquitto with persistence and constrained ACLs.
- Pin broker URI scheme consistently (`tcp://` or `mqtt://`) across all apps.
- Deploy with systemd:
  - `Restart=on-failure`
  - `TimeoutStopSec` and `KillSignal=SIGTERM`
  - log routing to journald/collector.
- Set log level via env/config (e.g., `PROXY_LOG_LEVEL=INFO`).

### Reliability & Ops
- Add readiness/liveness endpoint (or dedicated MQTT health topic).
- Expose metrics exporter (Prometheus text endpoint or pushgateway):
  - `messages_received_total`
  - `messages_published_total`
  - `publish_failures_total`
  - `wait_timeouts_total`
  - `loop_latency_ms` histogram.
- Add bounded retry/backoff for initial broker connect and reconnect alerts.
- Add integration tests for graceful shutdown + malformed input handling.

---

## Logging Schema (JSON)

Suggested event schema:

```json
{
  "timestamp": "2026-03-23T21:39:15.123Z",
  "level": "INFO",
  "component": "proxy",
  "event": "heartbeat",
  "message": "Proxy alive",
  "trace_id": "optional-correlation-id",
  "fields": {
    "received_messages": "120",
    "published_messages": "118",
    "parse_errors": "0",
    "compose_errors": "0",
    "publish_errors": "1",
    "wait_timeouts": "3",
    "circuit_breaker": "closed"
  }
}
```

Field guidelines:
- `timestamp`: UTC ISO-8601 with milliseconds.
- `level`: `TRACE|DEBUG|INFO|WARN|ERROR|FATAL`.
- `component`: executable or module (`proxy`, `sim`, `trgt`, `config`).
- `event`: stable machine-parsable event key.
- `message`: short human-readable explanation.
- `fields`: bounded cardinality dimensions only (avoid raw payload dumps in production).

---

## Remaining High-Value Next Steps

1. Introduce interface-driven seams (`IMqttClient`, `ILogger`, `IClock`) for deterministic unit tests and cleaner DI.
2. Replace ad-hoc JSON parse/compose with schema-validated model objects and explicit serialization contracts.
3. Add connection timeout policy + jittered retry in `Proxy::connect()` path.
4. Add SLO dashboards and alerting on `publish_failures_total`, reconnect churn, and loop latency tail.
