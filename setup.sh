#!/usr/bin/env bash
# ============================================================================
# setup.sh — Build automation script for the Proxy project
#
# Usage:
#   ./setup.sh [COMMAND]
#
# Commands:
#   deps        Install all required dependencies
#   build       Build the proxy, sim, and trgt binaries
#   test        Build and run all unit tests
#   integration Build sim and trgt for integration testing
#   clean       Remove all build directories
#   all         deps + build + test (default)
#   help        Show this help message
# ============================================================================

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

info()    { echo -e "${CYAN}[INFO]${NC}  $*"; }
success() { echo -e "${GREEN}[OK]${NC}    $*"; }
warn()    { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error()   { echo -e "${RED}[ERROR]${NC} $*"; }

# --------------------------------------------------------------------------
# Install dependencies
# --------------------------------------------------------------------------
cmd_deps() {
    info "Installing dependencies..."
    sudo apt-get update
    sudo apt-get install -y \
        cmake \
        libssl-dev \
        libboost-all-dev \
        libpaho-mqtt-dev \
        libpaho-mqttpp-dev \
        "libpaho-mqtt*" \
        libgtest-dev \
        libgmock-dev \
        mosquitto
    success "Dependencies installed."
}

# --------------------------------------------------------------------------
# Build a CMake target (helper)
#   $1 = source directory  $2 = display name
# --------------------------------------------------------------------------
build_cmake_target() {
    local src_dir="$1"
    local name="$2"
    info "Building ${name}..."
    mkdir -p "${src_dir}/build"
    cmake -S "${src_dir}" -B "${src_dir}/build"
    cmake --build "${src_dir}/build" -j "$(nproc)"
    success "${name} built  →  ${src_dir}/build/${name}"
}

# --------------------------------------------------------------------------
# Build all main binaries
# --------------------------------------------------------------------------
cmd_build() {
    info "=== Building all targets ==="
    build_cmake_target "${ROOT_DIR}/proxy" "proxy"
    build_cmake_target "${ROOT_DIR}/sim"   "sim"
    build_cmake_target "${ROOT_DIR}/trgt"  "trgt"
    success "=== All targets built ==="
}

# --------------------------------------------------------------------------
# Build & run unit tests
# --------------------------------------------------------------------------
cmd_test() {
    info "=== Running unit tests ==="

    # ---- configTest ----
    local cfg_dir="${ROOT_DIR}/proxy/unitTest/configTest"
    info "Building configTest..."
    mkdir -p "${cfg_dir}/build"
    g++ "${cfg_dir}/config.cpp" \
        "${cfg_dir}/configTest_main.cpp" \
        "${cfg_dir}/configTest_testCases.cpp" \
        -o "${cfg_dir}/build/configTest" \
        -lgtest -lgtest_main -lgmock -pthread
    success "configTest built."

    info "Running configTest..."
    (cd "${cfg_dir}" && ./build/configTest)
    success "configTest passed."

    # ---- proxyTest (needs MQTT broker) ----
    local prx_dir="${ROOT_DIR}/proxy/unitTest/proxyTest"
    info "Building proxyTest..."
    mkdir -p "${prx_dir}/build"
    g++ "${prx_dir}/proxy.cpp" \
        "${prx_dir}/proxyTest_main.cpp" \
        "${prx_dir}/proxyTest_testCases.cpp" \
        -o "${prx_dir}/build/proxyTest" \
        -lgtest -lgtest_main -lgmock -pthread \
        -lpaho-mqttpp3 -lpaho-mqtt3a
    success "proxyTest built."

    # Ensure Mosquitto broker is running
    if ! pgrep -x mosquitto > /dev/null 2>&1; then
        warn "Mosquitto broker not running — attempting to start..."
        sudo systemctl start mosquitto 2>/dev/null || mosquitto -d -p 1883
        sleep 1
    fi

    info "Running proxyTest..."
    (cd "${prx_dir}" && ./build/proxyTest)
    success "proxyTest passed."

    success "=== All unit tests passed ==="
}

# --------------------------------------------------------------------------
# Build integration test helpers (sim + trgt)
# --------------------------------------------------------------------------
cmd_integration() {
    info "=== Building integration targets ==="
    build_cmake_target "${ROOT_DIR}/sim"  "sim"
    build_cmake_target "${ROOT_DIR}/trgt" "trgt"

    # Create log directories
    mkdir -p "${ROOT_DIR}/.logs/sim"
    touch "${ROOT_DIR}/.logs/sim/sensors.csv" "${ROOT_DIR}/.logs/sim/actions.csv"
    success "Log directory created: .logs/sim/"

    info "To spin up targets, create log dirs per target, e.g.:"
    echo "  mkdir -p .logs/trgt01 && touch .logs/trgt01/{actions,sensors}.csv"
    echo ""
    info "Then run:"
    echo "  ./sim/build/sim  [broker_address]"
    echo "  ./trgt/build/trgt <id> [actions_log] [sensors_log]"
    success "=== Integration targets ready ==="
}

# --------------------------------------------------------------------------
# Clean all build artifacts
# --------------------------------------------------------------------------
cmd_clean() {
    info "Cleaning build directories..."
    rm -rf "${ROOT_DIR}/proxy/build"
    rm -rf "${ROOT_DIR}/sim/build"
    rm -rf "${ROOT_DIR}/trgt/build"
    rm -rf "${ROOT_DIR}/proxy/unitTest/configTest/build"
    rm -rf "${ROOT_DIR}/proxy/unitTest/proxyTest/build"
    success "All build directories removed."
}

# --------------------------------------------------------------------------
# Show help
# --------------------------------------------------------------------------
cmd_help() {
    sed -n '2,/^# ====.*$/p' "${BASH_SOURCE[0]}" | grep '^#' | sed 's/^# \?//'
}

# --------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------
COMMAND="${1:-all}"

case "${COMMAND}" in
    deps)        cmd_deps ;;
    build)       cmd_build ;;
    test)        cmd_test ;;
    integration) cmd_integration ;;
    clean)       cmd_clean ;;
    all)         cmd_deps && cmd_build && cmd_test ;;
    help|--help|-h) cmd_help ;;
    *)
        error "Unknown command: ${COMMAND}"
        cmd_help
        exit 1
        ;;
esac
