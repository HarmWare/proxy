#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

BUILD_TYPE="Release"
JOBS="$(nproc)"
COMPONENTS=()
CLEAN_FIRST="false"
VERBOSE="false"
GENERATOR=""
EXTRA_CMAKE_ARGS=()

print_usage() {
  cat <<'USAGE'
Usage: scripts/build-system.sh [options]

Build the C++ system with configurable flags.

Options:
  -c, --component <name>       Component to build: proxy|sim|trgt|all (repeatable)
  -t, --build-type <type>      CMake build type: Debug|Release|RelWithDebInfo|MinSizeRel
  -j, --jobs <num>             Parallel build jobs (default: nproc)
      --clean                  Remove existing build directories before configuring
  -g, --generator <name>       CMake generator (e.g., "Unix Makefiles", "Ninja")
      --cmake-arg <arg>        Extra argument forwarded to CMake configure (repeatable)
  -v, --verbose                Enable verbose build output
  -h, --help                   Show this help message

Examples:
  scripts/build-system.sh --component all --build-type Release --jobs 8
  scripts/build-system.sh -c proxy -c sim -t Debug --cmake-arg=-DENABLE_TESTS=ON
  scripts/build-system.sh --clean -g Ninja --verbose
USAGE
}

component_valid() {
  [[ "$1" == "proxy" || "$1" == "sim" || "$1" == "trgt" || "$1" == "all" ]]
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -c|--component)
      [[ $# -ge 2 ]] || { echo "Missing value for $1" >&2; exit 1; }
      if ! component_valid "$2"; then
        echo "Invalid component: $2" >&2
        exit 1
      fi
      if [[ "$2" == "all" ]]; then
        COMPONENTS=("proxy" "sim" "trgt")
      else
        COMPONENTS+=("$2")
      fi
      shift 2
      ;;
    -t|--build-type)
      [[ $# -ge 2 ]] || { echo "Missing value for $1" >&2; exit 1; }
      BUILD_TYPE="$2"
      shift 2
      ;;
    -j|--jobs)
      [[ $# -ge 2 ]] || { echo "Missing value for $1" >&2; exit 1; }
      JOBS="$2"
      shift 2
      ;;
    --clean)
      CLEAN_FIRST="true"
      shift
      ;;
    -g|--generator)
      [[ $# -ge 2 ]] || { echo "Missing value for $1" >&2; exit 1; }
      GENERATOR="$2"
      shift 2
      ;;
    --cmake-arg)
      [[ $# -ge 2 ]] || { echo "Missing value for $1" >&2; exit 1; }
      EXTRA_CMAKE_ARGS+=("$2")
      shift 2
      ;;
    -v|--verbose)
      VERBOSE="true"
      shift
      ;;
    -h|--help)
      print_usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      print_usage
      exit 1
      ;;
  esac
done

# De-duplicate components while preserving first appearance
if [[ ${#COMPONENTS[@]} -gt 0 ]]; then
  mapfile -t COMPONENTS < <(printf '%s\n' "${COMPONENTS[@]}" | awk '!seen[$0]++')
fi

if [[ ${#COMPONENTS[@]} -eq 0 ]]; then
  COMPONENTS=("proxy" "sim" "trgt")
fi

configure_and_build() {
  local component="$1"
  local src_dir="${ROOT_DIR}/${component}"
  local build_dir="${src_dir}/build"

  [[ -d "$src_dir" ]] || { echo "Component directory not found: $src_dir" >&2; exit 1; }

  if [[ "$CLEAN_FIRST" == "true" ]]; then
    rm -rf "$build_dir"
  fi

  mkdir -p "$build_dir"

  local cmake_cmd=(cmake -S "$src_dir" -B "$build_dir" -D "CMAKE_BUILD_TYPE=${BUILD_TYPE}")
  if [[ -n "$GENERATOR" ]]; then
    cmake_cmd+=(-G "$GENERATOR")
  fi
  if [[ ${#EXTRA_CMAKE_ARGS[@]} -gt 0 ]]; then
    cmake_cmd+=("${EXTRA_CMAKE_ARGS[@]}")
  fi

  echo "[build] Configuring ${component} (type=${BUILD_TYPE})"
  "${cmake_cmd[@]}"

  local build_cmd=(cmake --build "$build_dir" -j "$JOBS")
  if [[ "$VERBOSE" == "true" ]]; then
    build_cmd+=(--verbose)
  fi

  echo "[build] Building ${component} with ${JOBS} job(s)"
  "${build_cmd[@]}"
}

for component in "${COMPONENTS[@]}"; do
  configure_and_build "$component"
done

echo "[build] Completed: ${COMPONENTS[*]}"
