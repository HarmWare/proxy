# SYSTEM_UNDERSTANDING

## Executive Summary
This codebase implements an MQTT-based synchronization proxy for a vehicle platooning workflow, bridging simulator sensor streams to multiple target controllers and aggregating their actions back to the simulator. The `proxy` process is the coordination core, while `sim` and `trgt` are integration harnesses that emulate simulator and target endpoints using file-backed I/O loops. The architecture is intentionally minimal and operational, with strong runtime coupling to Mosquitto/Paho and a bitmask-driven synchronization model that gates action publication on complete upstream data.

## Phase 1: Structural Discovery

### Dependency Graph

```mermaid
flowchart LR
    subgraph External
        MQTTBroker[Mosquitto MQTT Broker]
        Paho[Paho MQTT C++ lib]
        BoostPT[Boost PropertyTree]
        GTest[GoogleTest / GoogleMock]
    end

    subgraph Runtime Apps
        Sim[sim executable]
        Proxy[proxy executable]
        Trgt[trgt executable(s)]
    end

    subgraph Internal Modules
        SimIO[sim::MyCallBack + FileHandling]
        ProxyCore[Proxy]
        ProxyCfg[ConfigHandler]
        TrgtIO[trgt::MyCallBack + FileHandling]
        ProxyUT[proxy/unitTest/proxyTest]
        ConfigUT[proxy/unitTest/configTest]
    end

    Sim -->|publish sim/sensors| MQTTBroker
    MQTTBroker -->|deliver sim/sensors| Proxy
    Proxy -->|publish trgt/XX/sensors| MQTTBroker
    MQTTBroker -->|deliver trgt/XX/sensors| Trgt
    Trgt -->|publish trgt/XX/actions| MQTTBroker
    MQTTBroker -->|deliver trgt/XX/actions| Proxy
    Proxy -->|publish sim/actions| MQTTBroker
    MQTTBroker -->|deliver sim/actions| Sim

    ProxyCore --> Paho
    ProxyCfg --> BoostPT
    SimIO --> Paho
    TrgtIO --> Paho
    ProxyUT --> GTest
    ConfigUT --> GTest
    ProxyUT --> Paho
```

### Internal/External Dependencies (Integration Method)
- Build system: CMake (`find_package(...)`, direct `target_link_libraries(...)`) plus manual `g++` invocations in `setup.sh` and CI.
- MQTT client runtime: Eclipse Paho C++ (`mqtt/async_client.h`, `mqtt::topic`, async callbacks).
- Config/JSON parser: Boost PropertyTree (`ini_parser`, `json_parser`).
- Tests: GoogleTest and GoogleMock (`GTest::GTest`, `GTest::Main`, plus `-lgmock`).
- Broker/runtime dependency: Mosquitto (local broker required by proxy runtime and integration/proxy tests).
- Package manager usage in repo scripts: Debian/Ubuntu `apt` packages (`libpaho-mqttpp-dev`, `libboost-all-dev`, `libgtest-dev`, `libgmock-dev`, etc.).
- Conan/Vcpkg: **Not detected**.

### Component Mapping

| Module Name | Responsibility | Key Files | Complexity (Low/Med/High) |
|---|---|---|---|
| Proxy Core | Orchestrates MQTT subscriptions/publications, parses simulator payloads, composes target actions, synchronizes loop with bitmask and condition variable (Owner: `Proxy`) | `proxy/proxy.hpp`, `proxy/proxy.cpp`, `proxy/main.cpp` | High |
| Configuration | Loads INI config into strongly-typed runtime settings and topic lists (Owner: `ConfigHandler`) | `proxy/config.hpp`, `proxy/config.cpp`, `proxy/config.ini` | Medium |
| Simulator Harness | Emulates simulator endpoint by reading sensor file input, publishing to MQTT, and logging received actions (Owner: `MyCallBack`, `FileHandling`) | `sim/main.cpp`, `sim/sim.hpp`, `sim/sim.cpp`, `sim/filehandling.*` | Medium |
| Target Harness | Emulates target endpoint(s) by reading action file input, publishing to MQTT, and logging received sensor slices (Owner: `MyCallBack`, `FileHandling`) | `trgt/main.cpp`, `trgt/trgt.hpp`, `trgt/trgt.cpp`, `trgt/filehandling.*` | Medium |
| Config Unit Tests | Verifies `ConfigHandler` field extraction and topic mapping from INI (Owner: `configTest` fixture) | `proxy/unitTest/configTest/*` | Low |
| Proxy Unit Tests | Validates proxy construction and broker-connected scenarios with mocked configuration and live MQTT dependency (Owner: `ProxyTest` fixture) | `proxy/unitTest/proxyTest/*` | Medium |

## Phase 2: Technical Design Analysis

### Design Patterns Observed
- RAII (partial): stack-based ownership for clients/topics/mutexes; however lifecycle shutdown is mostly manual and loops are infinite.
- Callback/Event-Driven: MQTT async handlers (`set_message_callback`, connection handlers) drive state transitions.
- Producer/Consumer Synchronization: callback thread(s) produce messages, main loop consumes after `waitForData()` signaling.
- Facade-like role: `Proxy` centralizes connect/subscribe/parse/compose/publish operations behind one class.
- Bitmask State Machine (custom): `Rx` + `maskRx` encode readiness from CARLA and all RPIs.
- Test doubles: mocked `ConfigHandler` in proxy unit tests (`mockedConfig.hpp`).
- Not detected: PIMPL, CRTP, Observer framework abstraction, SEXT (explicit pattern implementation not visible).

### C++17 Usage Audit
- Explicit standard set to C++17 in all main CMake targets.
- Used idioms/features: `enum class`, lambdas, `std::chrono`, `std::atomic<bool>`, mutexes/condition variable, range-based loops, smart API composition (`connect_options_builder`).
- C++17 features explicitly requested in audit but **not observed** in production code: structured bindings, `std::optional`, `std::variant`, `if constexpr`, `std::filesystem`.
- Modernity assessment: mostly C++11/14-style with C++17 compile target; functional but not strongly idiomatic “modern C++17-first” design.

### Concurrency & Memory Management
- Threading model:
  - Paho async callbacks mutate shared state (`Rx`, `sensorsMsgs`, `actionsMsgs`).
  - Main thread waits on `std::condition_variable` (`waitForData`) and runs parse/compose/publish pipeline.
- Synchronization primitives:
  - `flagMutex` protects bitmask readiness flag (`Rx`).
  - `sensorsMutex` and `actionsMutex` protect message vectors during parse/compose and callback writes.
  - `std::atomic<bool>` in `sim`/`trgt` callback classes signals message availability.
- Memory management strategy:
  - No manual `new/delete` in production path; containers and value members dominate.
  - Heap allocation appears only in tests (`new Proxy`), released in `TearDown()`.
  - No custom allocators or PMR usage detected.

## Phase 3: System Logic & Data Flow

### Entry Point to Exit (Primary Execution Path)
1. `proxy/main.cpp` creates `ConfigHandler`, resolves config path, and calls `loadConfiguaration()`.
2. `Proxy` is constructed with loaded config: builds MQTT client, topics, callbacks, and synchronization mask.
3. Runtime setup: connect to broker, subscribe to all inbound topics (`sim/sensors` and each `trgt/XX/actions`).
4. Infinite control loop:
   - `waitForData()` blocks until either simulator bit is set or all target bits are set.
   - CARLA path: `parse()` simulator JSON -> split per-target payloads -> publish to `trgt/XX/sensors` -> clear CARLA bit.
   - RPI path: `compose()` target action slices -> one JSON array -> publish to `sim/actions` -> clear RPI bits.
5. Process exits only on exception or external termination (no graceful stop path in main loop).

### State Management
- Persistent runtime state is concentrated inside `Proxy`:
  - configuration-derived topic vectors and connection options,
  - message buffers (`sensorsMsgs`, `actionsMsgs`),
  - synchronization flags (`Rx`, `maskRx`, `numberOfRpis`).
- Global mutable state is minimal (const globals for defaults in each `main.cpp`).
- File-backed pseudo-state in `sim`/`trgt` (`.logs/.../*.csv`) supports integration playback/logging.

## Version & Tech Stack
- Language standard: C++17 (declared in CMake files).
- Compiler (workspace build artifacts): GNU C++ compiler, version `13.3.0` (detected in generated `CMakeCXXCompiler.cmake`).
- Build generator (workspace build artifacts): Unix Makefiles.
- CMake:
  - Minimum required in source: `3.5`.
  - Detected in generated cache: `3.28.3`.
- MQTT library: Paho MQTT C++ (`PahoMqttCpp::paho-mqttpp3`) — **exact library version Unknown**.
- MQTT C library linkage in tests: `-lpaho-mqtt3a` — **version Unknown**.
- Broker: Mosquitto (installed in scripts/CI) — **exact version Unknown**.
- Boost: `libboost-all-dev` + `boost::property_tree` — **exact version Unknown**.
- Test frameworks: GoogleTest/GoogleMock (`libgtest-dev`, `libgmock-dev`, `googletest`) — **exact version Unknown**.
- Package/dependency delivery: apt (Ubuntu/Debian), GitHub Actions on `ubuntu-22.04`.
- Conan/Vcpkg integration: Not detected.

## Architectural Risks
- Callback/data race risk in harnesses: `sim`/`trgt` share `std::string msg` across callback and main loop with only an atomic flag, no mutex around string data.
- Error handling resilience: `Proxy::connect()`/`disconnect()` swallow exceptions after logging; calling code cannot reliably react.
- Config reload bug risk: `ConfigHandler::loadConfiguaration()` appends topic vectors via `push_back` without clearing old contents; repeated calls can duplicate topics.
- Startup fallback mismatch: main logs “loading default configuration” on failure but does not reassign default path and retry load.
- Protocol/transport consistency risk: both `mqtt://` and `tcp://` URI schemes appear across modules/scripts; behavior depends on broker/client compatibility.
- Infinite-loop operational debt: no termination signal handling, no shutdown hooks, and limited backpressure/error recovery strategy.
- Test realism gap: proxy tests labeled unit tests still depend on live broker and timing sleeps, reducing determinism and CI robustness.

## Notes on Evidence Confidence
- Items marked **Unknown** are not explicitly version-pinned in repository source/build metadata.
- Items marked as detected from generated build artifacts are environment-specific observations (Inferred for this workspace state), not guaranteed across all developer machines.
