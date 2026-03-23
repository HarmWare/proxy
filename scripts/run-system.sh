#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUN_DIR="${ROOT_DIR}/.run"
PID_DIR="${RUN_DIR}/pids"
LOG_DIR="${ROOT_DIR}/.logs"

BROKER_URI="tcp://localhost:1883"
PROXY_CONFIG="${ROOT_DIR}/proxy/config.ini"
SIM_INPUT="${LOG_DIR}/sim/sensors.csv"
SIM_OUTPUT="${LOG_DIR}/sim/actions.csv"
TARGET_IDS="01,02"
START_BROKER="false"
DETACH="false"
STOP_ONLY="false"
ONLY_COMPONENT="all"

print_usage() {
  cat <<'USAGE'
Usage: scripts/run-system.sh [options]

Run proxy, sim, and trgt processes with configurable runtime flags.

Options:
      --broker-uri <uri>       Broker URI (default: tcp://localhost:1883)
      --proxy-config <path>    Base proxy config.ini path
      --target-ids <ids>       Comma-separated target IDs (default: 01,02)
      --logs-dir <path>        Root logs directory (default: ./.logs)
      --sim-input <path>       sim input sensors CSV path
      --sim-output <path>      sim output actions CSV path
      --start-broker           Start mosquitto before launching components
      --detach                 Run all processes in background and write pid files
      --component <name>       Run only one component: proxy|sim|trgt|all
      --stop                   Stop previously detached processes and exit
  -h, --help                   Show this help message

Examples:
  scripts/run-system.sh --start-broker --broker-uri tcp://localhost:1883 --detach
  scripts/run-system.sh --target-ids 01,02,03 --component all
  scripts/run-system.sh --stop
USAGE
}

split_csv() {
  local csv="$1"
  IFS=',' read -r -a out <<< "$csv"
  printf '%s\n' "${out[@]}"
}

normalize_target_id() {
  local raw="$1"
  if [[ "$raw" =~ ^[0-9]{1,2}$ ]]; then
    printf "%02d" "$raw"
  else
    echo "Invalid target id: $raw (must be numeric, 1-2 digits)" >&2
    exit 1
  fi
}

ensure_dirs_and_files() {
  mkdir -p "$PID_DIR"
  mkdir -p "${LOG_DIR}/sim"
  touch "$SIM_INPUT" "$SIM_OUTPUT"

  if [[ ! -s "$SIM_INPUT" ]]; then
    echo '{"veh01":[0.0,0.0,0.0],"veh02":[0.0,0.0,0.0],"veh03":[0.0,0.0,0.0]}' > "$SIM_INPUT"
  fi

  mapfile -t TARGET_ARRAY < <(split_csv "$TARGET_IDS")
  for id in "${TARGET_ARRAY[@]}"; do
    local normalized
    normalized="$(normalize_target_id "$id")"
    mkdir -p "${LOG_DIR}/trgt${normalized}"
    touch "${LOG_DIR}/trgt${normalized}/actions.csv" "${LOG_DIR}/trgt${normalized}/sensors.csv"
    if [[ ! -s "${LOG_DIR}/trgt${normalized}/actions.csv" ]]; then
      echo '[0.0,0.0]' > "${LOG_DIR}/trgt${normalized}/actions.csv"
    fi
  done
}

write_runtime_proxy_config() {
  local runtime_cfg="${RUN_DIR}/proxy.runtime.ini"
  mkdir -p "$RUN_DIR"
  cp "$PROXY_CONFIG" "$runtime_cfg"

  sed -i "s#^address\s*=.*#address = ${BROKER_URI}#" "$runtime_cfg"

  mapfile -t TARGET_ARRAY < <(split_csv "$TARGET_IDS")
  local target_count="${#TARGET_ARRAY[@]}"
  sed -i "s#^numberOfRpis\s*=.*#numberOfRpis = ${target_count}#" "$runtime_cfg"

  for index in "${!TARGET_ARRAY[@]}"; do
    local id
    id="$(normalize_target_id "${TARGET_ARRAY[$index]}")"
    local slot=$((index + 1))
    local key_suffix
    key_suffix=$(printf "%02d" "$slot")

    if grep -q "^trgt${key_suffix}SensorsTopic\s*=.*" "$runtime_cfg"; then
      sed -i "s#^trgt${key_suffix}SensorsTopic\s*=.*#trgt${key_suffix}SensorsTopic = trgt/${id}/sensors#" "$runtime_cfg"
      sed -i "s#^trgt${key_suffix}ActionsTopic\s*=.*#trgt${key_suffix}ActionsTopic = trgt/${id}/actions#" "$runtime_cfg"
    else
      {
        echo "trgt${key_suffix}SensorsTopic = trgt/${id}/sensors"
        echo "trgt${key_suffix}ActionsTopic = trgt/${id}/actions"
      } >> "$runtime_cfg"
    fi
  done

  echo "$runtime_cfg"
}

start_mosquitto() {
  if pgrep -x mosquitto >/dev/null 2>&1; then
    echo "[run] mosquitto is already running"
    return
  fi

  echo "[run] starting mosquitto broker"
  if command -v systemctl >/dev/null 2>&1; then
    sudo systemctl start mosquitto || true
  fi

  if ! pgrep -x mosquitto >/dev/null 2>&1; then
    mosquitto -d -p 1883
  fi
}

run_process() {
  local name="$1"
  shift

  if [[ "$DETACH" == "true" ]]; then
    local stdout_log="${RUN_DIR}/${name}.out.log"
    local stderr_log="${RUN_DIR}/${name}.err.log"
    nohup "$@" >"$stdout_log" 2>"$stderr_log" &
    local pid="$!"
    echo "$pid" > "${PID_DIR}/${name}.pid"
    echo "[run] started ${name} (pid=${pid})"
  else
    echo "[run] starting ${name}: $*"
    "$@"
  fi
}

stop_detached() {
  if [[ ! -d "$PID_DIR" ]]; then
    echo "[run] no pid directory found, nothing to stop"
    return
  fi

  shopt -s nullglob
  local pid_files=("${PID_DIR}"/*.pid)
  shopt -u nullglob

  if [[ ${#pid_files[@]} -eq 0 ]]; then
    echo "[run] no pid files found, nothing to stop"
    return
  fi

  for pid_file in "${pid_files[@]}"; do
    local name
    name="$(basename "$pid_file" .pid)"
    local pid
    pid="$(cat "$pid_file")"
    if kill -0 "$pid" >/dev/null 2>&1; then
      echo "[run] stopping ${name} (pid=${pid})"
      kill "$pid" || true
    fi
    rm -f "$pid_file"
  done
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --broker-uri)
      BROKER_URI="$2"; shift 2 ;;
    --proxy-config)
      PROXY_CONFIG="$2"; shift 2 ;;
    --target-ids)
      TARGET_IDS="$2"; shift 2 ;;
    --logs-dir)
      LOG_DIR="$2"; shift 2 ;;
    --sim-input)
      SIM_INPUT="$2"; shift 2 ;;
    --sim-output)
      SIM_OUTPUT="$2"; shift 2 ;;
    --start-broker)
      START_BROKER="true"; shift ;;
    --detach)
      DETACH="true"; shift ;;
    --stop)
      STOP_ONLY="true"; shift ;;
    --component)
      ONLY_COMPONENT="$2"; shift 2 ;;
    -h|--help)
      print_usage
      exit 0 ;;
    *)
      echo "Unknown option: $1" >&2
      print_usage
      exit 1 ;;
  esac
done

if [[ "$STOP_ONLY" == "true" ]]; then
  stop_detached
  exit 0
fi

if [[ ! -f "${ROOT_DIR}/proxy/build/proxy" || ! -f "${ROOT_DIR}/sim/build/sim" || ! -f "${ROOT_DIR}/trgt/build/trgt" ]]; then
  echo "[run] one or more binaries are missing. Run scripts/build-system.sh first." >&2
  exit 1
fi

if [[ "$START_BROKER" == "true" ]]; then
  start_mosquitto
fi

ensure_dirs_and_files
RUNTIME_PROXY_CFG="$(write_runtime_proxy_config)"

run_proxy() {
  run_process "proxy" "${ROOT_DIR}/proxy/build/proxy" "$RUNTIME_PROXY_CFG"
}

run_sim() {
  run_process "sim" "${ROOT_DIR}/sim/build/sim" "$BROKER_URI" "$SIM_INPUT" "$SIM_OUTPUT"
}

run_targets() {
  mapfile -t TARGET_ARRAY < <(split_csv "$TARGET_IDS")
  for id in "${TARGET_ARRAY[@]}"; do
    local normalized
    normalized="$(normalize_target_id "$id")"
    local trgt_in="${LOG_DIR}/trgt${normalized}/actions.csv"
    local trgt_out="${LOG_DIR}/trgt${normalized}/sensors.csv"
    run_process "trgt${normalized}" "${ROOT_DIR}/trgt/build/trgt" "$normalized" "$trgt_in" "$trgt_out" "$BROKER_URI"
  done
}

case "$ONLY_COMPONENT" in
  proxy)
    run_proxy
    ;;
  sim)
    run_sim
    ;;
  trgt)
    run_targets
    ;;
  all)
    if [[ "$DETACH" == "true" ]]; then
      run_proxy
      run_sim
      run_targets
      echo "[run] all components started in background"
    else
      echo "[run] foreground mode requires --component to run a single process"
      echo "[run] use --detach for full system orchestration"
      exit 1
    fi
    ;;
  *)
    echo "Invalid --component value: ${ONLY_COMPONENT}" >&2
    exit 1
    ;;
esac
