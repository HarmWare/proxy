# Proxy Software Component

## Overview

This repository contains the source code for the **proxy** software component, a crucial part of the graduation project *"Vehicle Platooning with V2V using Embedded Linux"*. The main objective of this project is to implement a truck platooning system, which involves realistic simulations and embedded Linux target units to represent the brains of vehicles responsible for communication and control algorithms.

## Project Components

The system consists of:

- **Simulation Environment (CARLA)**: Provides a realistic environment for testing the system.
- **Embedded Linux Target Units**: Each unit represents a vehicle's brain, handling communication and control algorithms.
- **Proxy**: Bridges the simulation and target units over MQTT, synchronizing data flow.

## Problem and Solution

The project faced a challenge with unsynchronized data exchange between the simulation environment and the target units. The proxy component was introduced to address this issue by synchronizing data transmission and reception between the simulation environment (CARLA) and the embedded Linux target units.

## Proxy Component

The proxy component's primary role is to:

1. **Receive Sensor Messages**: Collect sensor data from the CARLA simulation environment via MQTT.
2. **Parse and Route Messages**: Parse the sensor data and route the parsed messages to the corresponding target embedded Linux units.
3. **Synchronize Actions**: Receive control signals from all target units, compose them into a single message, and send it back to CARLA to synchronize the arrival of actions.

### Architecture

```plaintext
CARLA (sim)  ──MQTT──►  Proxy  ──MQTT──►  Target 1..N
                          ▲                    │
                          └────────MQTT────────┘
```

- The proxy subscribes to `sim/sensors` and `trgt/XX/actions`.
- The proxy publishes to `sim/actions` and `trgt/XX/sensors`.
- A bitmask tracks which sources have delivered data; a condition variable wakes the main loop when all expected data arrives.

## Communication Protocol

Communication between the simulation environment, proxy, and target units is based on the **MQTT protocol** using a Mosquitto broker and Paho C++ clients.

## Repository Structure

```plaintext
.
├── setup.sh                 # Build automation script
├── README.md
├── proxy/                   # Core proxy component
│   ├── CMakeLists.txt
│   ├── config.cpp / .hpp    # INI configuration loader
│   ├── config.ini           # Default configuration file
│   ├── main.cpp             # Entry point
│   ├── proxy.cpp / .hpp     # MQTT orchestration, parse/compose, sync
│   └── unitTest/
│       ├── configTest/      # Config unit tests (GoogleTest)
│       └── proxyTest/       # Proxy unit tests (GoogleTest + GMock)
├── sim/                     # Simulation-side integration harness
│   ├── CMakeLists.txt
│   ├── filehandling.cpp / .hpp
│   ├── main.cpp
│   └── sim.cpp / .hpp
├── trgt/                    # Target-side integration harness
│   ├── CMakeLists.txt
│   ├── filehandling.cpp / .hpp
│   ├── main.cpp
│   └── trgt.cpp / .hpp
└── .github/
    └── workflows/
        └── ci.yml           # GitHub Actions CI pipeline
```

### Directories Description

| Directory | Description |
|-----------|-------------|
| `proxy/` | Source code for the proxy component |
| `sim/` | Standalone app emulating the simulation environment (integration testing) |
| `trgt/` | Standalone app emulating a target unit (integration testing) |
| `proxy/unitTest/` | Unit tests using GoogleTest / GMock |
| `.github/` | CI/CD pipeline configurations (GitHub Actions) |

## Quick Start

A `setup.sh` script is provided to automate the entire workflow:

```bash
# Clone the repository
git clone https://github.com/harmware/proxy.git
cd proxy

# Install dependencies, build everything, and run all tests
./setup.sh all

# Or run individual steps:
./setup.sh deps        # Install system dependencies
./setup.sh build       # Build proxy, sim, and trgt
./setup.sh test        # Build and run all unit tests
./setup.sh integration # Build sim & trgt, create log directories
./setup.sh clean       # Remove all build directories
./setup.sh help        # Show usage information
```

## Scripted Build & Run (Config Flags)

Two production-oriented scripts are available under `scripts/`:

```bash
# Build with flags
./scripts/build-system.sh --component all --build-type Release --jobs 8

# Build only proxy in Debug with extra CMake arg
./scripts/build-system.sh --component proxy --build-type Debug --cmake-arg=-DENABLE_TESTS=ON

# Start full system in background with runtime flags
./scripts/run-system.sh --start-broker --detach --broker-uri tcp://localhost:1883 --target-ids 01,02,03

# Stop all detached processes started by run-system.sh
./scripts/run-system.sh --stop
```

Common run flags:

- `--broker-uri <uri>`: broker endpoint used by proxy/sim/trgt
- `--proxy-config <path>`: source config file for proxy (runtime copy is generated)
- `--target-ids <ids>`: comma-separated target IDs, e.g. `01,02,03`
- `--logs-dir <path>`: base folder for sim/trgt CSV files
- `--component proxy|sim|trgt|all`: run selected component(s)
- `--detach`: run in background and store PID files in `.run/pids/`

## Realtime Visualization

You can visualize sensor/action readings in real time from `.logs` using a matplotlib-based dashboard:

```bash
# Install plotting dependency (once)
python3 -m pip install matplotlib

# Start the system in background
./scripts/run-system.sh --start-broker --detach --target-ids 01,02,03

# Launch realtime visualizer
./scripts/visualize-system.sh --target-ids 01,02,03 --interval-ms 1000 --window 180

# Stop the system when done
./scripts/run-system.sh --stop
```

Visualization flags:

- `--logs-dir <path>`: source log directory (default: `.logs`)
- `--target-ids <ids>`: comma-separated targets (e.g. `01,02,03`)
- `--interval-ms <num>`: refresh rate in milliseconds
- `--window <num>`: points retained per plotted channel

## Manual Building and Running

### Dependencies

| Dependency | Purpose |
|------------|---------|
| `cmake` | Build system |
| `libpaho-mqtt*` | MQTT C/C++ client libraries |
| `libboost-all-dev` | INI parser (`boost::property_tree`) |
| `libgtest-dev`, `libgmock-dev` | Unit testing framework |
| `mosquitto` | MQTT broker (required at runtime and for proxy tests) |
| `libssl-dev` | TLS support for Paho |

Install all at once:

```bash
sudo apt-get update && sudo apt-get install -y \
    cmake libssl-dev libboost-all-dev libpaho-mqtt* \
    libgtest-dev libgmock-dev mosquitto
```

### Build the Proxy

```bash
cd proxy
# Optional: edit config.ini to configure proxy behaviour
mkdir -p build && cd build
cmake ..
make
```

### Run the Proxy

The proxy requires a running MQTT broker (Mosquitto):

```bash
# Start the broker if not already running
sudo systemctl start mosquitto

# Run the proxy (optionally pass a config file)
./proxy                             # uses default ../config.ini
./proxy /path/to/custom/config.ini  # uses custom config
```

### Configuration

Edit `proxy/config.ini` to set MQTT broker address, client options, and topic mappings:

```ini
[client]
address = mqtt://localhost:1883
clientId = proxy
maxBufMsgs = 1000
cleanSession = true
autoReconnect = true
keepAliveTime = 600

[topics]
qualityOfService = 1
retainedFlag = true
numberOfRpis = 3
simSensorsTopic = sim/sensors
simActionsTopic = sim/actions
trgt01SensorsTopic = trgt/01/sensors
trgt01ActionsTopic = trgt/01/actions
trgt02SensorsTopic = trgt/02/sensors
trgt02ActionsTopic = trgt/02/actions
trgt03SensorsTopic = trgt/03/sensors
trgt03ActionsTopic = trgt/03/actions
```

To add more targets, increase `numberOfRpis` and add matching `trgtXXSensorsTopic` / `trgtXXActionsTopic` entries.

## Unit Testing

Unit tests use the **GoogleTest** and **GMock** frameworks.

```bash
# Fastest way — run both test suites at once:
./setup.sh test

# Or manually:

# Config tests (no broker needed)
cd proxy/unitTest/configTest
mkdir -p build && cd build
g++ ../config.cpp ../configTest_main.cpp ../configTest_testCases.cpp \
    -o configTest -lgtest -lgtest_main -lgmock -pthread
cd .. && ./build/configTest

# Proxy tests (needs Mosquitto running on localhost:1883)
cd proxy/unitTest/proxyTest
mkdir -p build && cd build
g++ ../proxy.cpp ../proxyTest_main.cpp ../proxyTest_testCases.cpp \
    -o proxyTest -lgtest -lgtest_main -lgmock -pthread \
    -lpaho-mqttpp3 -lpaho-mqtt3a
cd .. && ./build/proxyTest
```

> **Note**: The proxy tests (`publishSenario`, `subscibeSenario`, `getDatafromCarla`, `getDatafromRPIS`) require a live MQTT broker at `mqtt://localhost:1883`. Start Mosquitto before running them.

## Integration Testing

Standalone applications emulate the simulation and target sides to verify end-to-end communication through the proxy.

```bash
# Build both (or use ./setup.sh integration)
cd sim   && mkdir -p build && cd build && cmake .. && make && cd ../..
cd trgt  && mkdir -p build && cd build && cmake .. && make && cd ../..

# Create log directories
mkdir -p .logs/sim .logs/trgt01 .logs/trgt02
touch .logs/sim/{sensors,actions}.csv
touch .logs/trgt01/{sensors,actions}.csv
touch .logs/trgt02/{sensors,actions}.csv

# Start the broker
sudo systemctl start mosquitto

# In separate terminals:
./proxy/build/proxy                                     # Start proxy
./sim/build/sim mqtt://localhost:1883                   # Start sim
./trgt/build/trgt 01 .logs/trgt01/actions.csv .logs/trgt01/sensors.csv  # Target 1
./trgt/build/trgt 02 .logs/trgt02/actions.csv .logs/trgt02/sensors.csv  # Target 2
```

Make sure the number of targets matches `numberOfRpis` in `proxy/config.ini`. Each target instance must have a unique ID.

## CI/CD

The repository uses **GitHub Actions** for continuous integration. The pipeline (`.github/workflows/ci.yml`) performs:

1. **Install dependencies** — system packages for MQTT, Boost, GoogleTest
2. **Build the proxy** — CMake + Make
3. **Run config unit tests** — builds and executes `configTest`
4. **Start MQTT broker** — installs and starts Mosquitto
5. **Run proxy unit tests** — builds and executes `proxyTest`

The pipeline runs automatically on every push to the `main` branch.
