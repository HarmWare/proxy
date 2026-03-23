#!/usr/bin/env python3
"""
Realtime visualizer for proxy system sensor/action readings.

Reads latest lines from:
- .logs/sim/sensors.csv
- .logs/sim/actions.csv
- .logs/trgtXX/sensors.csv
- .logs/trgtXX/actions.csv

Parses payloads as JSON when possible; falls back to numeric extraction regex for malformed rows.
Plots each numeric channel as a time series using matplotlib.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import time
from collections import defaultdict, deque
from dataclasses import dataclass
from pathlib import Path
from typing import Deque, Dict, Iterable, List, Optional, Tuple

import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation

NUMBER_REGEX = re.compile(r"[-+]?\d*\.?\d+(?:[eE][-+]?\d+)?")


@dataclass(frozen=True)
class Source:
    name: str
    kind: str  # sensors | actions
    path: Path


class ChannelStore:
    def __init__(self, max_points: int) -> None:
        self.max_points = max_points
        self.data: Dict[str, Deque[Tuple[float, float]]] = defaultdict(lambda: deque(maxlen=max_points))

    def append(self, channel: str, timestamp: float, value: float) -> None:
        self.data[channel].append((timestamp, value))

    def channels(self) -> Iterable[str]:
        return self.data.keys()

    def get(self, channel: str) -> Deque[Tuple[float, float]]:
        return self.data[channel]


def parse_target_ids(target_ids_csv: str) -> List[str]:
    out: List[str] = []
    for raw in target_ids_csv.split(","):
        raw = raw.strip()
        if not raw:
            continue
        if not raw.isdigit() or len(raw) > 2:
            raise ValueError(f"Invalid target id '{raw}'. Expected 1-2 digits.")
        out.append(f"{int(raw):02d}")
    if not out:
        raise ValueError("No target IDs provided.")
    return out


def build_sources(logs_dir: Path, target_ids: List[str]) -> List[Source]:
    sources: List[Source] = [
        Source("sim.sensors", "sensors", logs_dir / "sim" / "sensors.csv"),
        Source("sim.actions", "actions", logs_dir / "sim" / "actions.csv"),
    ]

    for target_id in target_ids:
        sources.append(Source(f"trgt{target_id}.sensors", "sensors", logs_dir / f"trgt{target_id}" / "sensors.csv"))
        sources.append(Source(f"trgt{target_id}.actions", "actions", logs_dir / f"trgt{target_id}" / "actions.csv"))

    return sources


def read_last_nonempty_line(path: Path) -> Optional[str]:
    if not path.exists() or not path.is_file():
        return None

    try:
        with path.open("r", encoding="utf-8", errors="ignore") as handle:
            lines = handle.readlines()
    except OSError:
        return None

    for line in reversed(lines):
        candidate = line.strip()
        if candidate:
            return candidate
    return None


def extract_numbers(payload) -> List[float]:
    values: List[float] = []

    if isinstance(payload, dict):
        for key in sorted(payload.keys()):
            values.extend(extract_numbers(payload[key]))
    elif isinstance(payload, list):
        for item in payload:
            values.extend(extract_numbers(item))
    elif isinstance(payload, (int, float)):
        values.append(float(payload))

    return values


def parse_payload_to_numbers(raw: str) -> List[float]:
    raw = raw.strip()
    if not raw:
        return []

    # Fast JSON path
    try:
        parsed = json.loads(raw)
        values = extract_numbers(parsed)
        if values:
            return values
    except json.JSONDecodeError:
        pass

    # Fallback for malformed rows (legacy content)
    fallback = [float(match.group(0)) for match in NUMBER_REGEX.finditer(raw)]
    return fallback


def update_channels(store: ChannelStore, source: Source, timestamp: float, values: List[float]) -> None:
    if not values:
        return

    for index, value in enumerate(values):
        channel_name = f"{source.name}[{index}]"
        store.append(channel_name, timestamp, value)


def plot_group(axis, store: ChannelStore, group_keyword: str, title: str) -> None:
    axis.clear()
    axis.set_title(title)
    axis.set_xlabel("time (s)")
    axis.set_ylabel("value")
    axis.grid(True, alpha=0.25)

    channel_names = sorted([name for name in store.channels() if group_keyword in name])
    if not channel_names:
        axis.text(0.5, 0.5, "No data yet", ha="center", va="center", transform=axis.transAxes)
        return

    for channel_name in channel_names:
        points = list(store.get(channel_name))
        if not points:
            continue

        x_values = [point[0] for point in points]
        y_values = [point[1] for point in points]
        axis.plot(x_values, y_values, label=channel_name)

    if channel_names:
        axis.legend(loc="upper left", fontsize=8)


def make_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Realtime sensor/action visualizer for the proxy system.")
    parser.add_argument("--logs-dir", default=".logs", help="Root logs directory (default: .logs)")
    parser.add_argument("--target-ids", default="01,02,03", help="Comma-separated target IDs (default: 01,02,03)")
    parser.add_argument("--interval-ms", type=int, default=1000, help="Refresh interval in milliseconds (default: 1000)")
    parser.add_argument("--window", type=int, default=180, help="Number of points to keep per channel (default: 180)")
    return parser


def main() -> None:
    parser = make_argument_parser()
    args = parser.parse_args()

    if args.interval_ms <= 0:
        raise ValueError("--interval-ms must be > 0")
    if args.window <= 0:
        raise ValueError("--window must be > 0")

    logs_dir = Path(args.logs_dir).resolve()
    target_ids = parse_target_ids(args.target_ids)
    sources = build_sources(logs_dir, target_ids)

    store = ChannelStore(max_points=args.window)
    start_time = time.time()

    figure, (ax_sensors, ax_actions) = plt.subplots(2, 1, figsize=(14, 8), sharex=True)
    figure.suptitle("Realtime Sensor / Action Readings")

    def animate(_frame_index: int) -> None:
        timestamp = time.time() - start_time

        for source in sources:
            line = read_last_nonempty_line(source.path)
            if line is None:
                continue
            values = parse_payload_to_numbers(line)
            update_channels(store, source, timestamp, values)

        plot_group(ax_sensors, store, ".sensors", "Sensors")
        plot_group(ax_actions, store, ".actions", "Actions")

    anim = FuncAnimation(figure, animate, interval=args.interval_ms, cache_frame_data=False)
    # Keep a strong reference for the full GUI lifetime to avoid animation GC warning.
    figure._anim = anim  # type: ignore[attr-defined]
    plt.tight_layout()
    plt.show()


if __name__ == "__main__":
    main()
