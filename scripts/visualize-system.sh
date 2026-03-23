#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

LOGS_DIR="${ROOT_DIR}/.logs"
TARGET_IDS="01,02,03"
INTERVAL_MS="1000"
WINDOW="180"

usage() {
  cat <<'USAGE'
Usage: scripts/visualize-system.sh [options]

Launch realtime plotting of sensors/actions from .logs files.

Options:
  --logs-dir <path>       Logs root directory (default: ./.logs)
  --target-ids <ids>      Comma-separated target ids (default: 01,02,03)
  --interval-ms <num>     Refresh interval ms (default: 1000)
  --window <num>          Points retained per channel (default: 180)
  -h, --help              Show this help

Examples:
  ./scripts/visualize-system.sh
  ./scripts/visualize-system.sh --target-ids 01,02 --interval-ms 500 --window 300
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --logs-dir)
      LOGS_DIR="$2"; shift 2 ;;
    --target-ids)
      TARGET_IDS="$2"; shift 2 ;;
    --interval-ms)
      INTERVAL_MS="$2"; shift 2 ;;
    --window)
      WINDOW="$2"; shift 2 ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage
      exit 1
      ;;
  esac
done

if ! command -v python3 >/dev/null 2>&1; then
  echo "python3 is required" >&2
  exit 1
fi

python3 "${ROOT_DIR}/scripts/realtime_visualizer.py" \
  --logs-dir "$LOGS_DIR" \
  --target-ids "$TARGET_IDS" \
  --interval-ms "$INTERVAL_MS" \
  --window "$WINDOW"
