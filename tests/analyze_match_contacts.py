#!/usr/bin/env python3
"""Classify effective kicks from an evaluation-only Webots contact trace."""
import argparse
import csv
import math
from pathlib import Path


def distance(a, first, second):
    return math.sqrt(sum((a[f"{first}_{axis}"] - a[f"{second}_{axis}"]) ** 2
                         for axis in ("x", "y", "z")))


parser = argparse.ArgumentParser()
parser.add_argument("trace", type=Path)
args = parser.parse_args()
rows = []
with args.trace.open(newline="") as stream:
    for raw in csv.DictReader(stream):
        try:
            rows.append({key: float(value) for key, value in raw.items()})
        except (TypeError, ValueError):
            pass  # Ignore a final partial line if the simulator was interrupted.

launches = []
last_launch = -100.0
for index, row in enumerate(rows):
    speed = math.hypot(row["ball_vx"], row["ball_vz"])
    if speed > 0.25 and distance(row, "ball", "red") < 0.55 \
            and row["sim_time"] - last_launch > 3.0:
        launches.append(index)
        last_launch = row["sim_time"]

blocked_count = 0
for number, start in enumerate(launches, 1):
    row = rows[start]
    stop = launches[number] if number < len(launches) else len(rows)
    window = [sample for sample in rows[start:stop]
              if sample["sim_time"] <= row["sim_time"] + 8.0]
    closest = min(distance(sample, "ball", "blue") for sample in window)
    # Webots' PROTO hierarchy does not reliably expose the colliding ancestor.
    # Contact count plus exact 3-D separation is deterministic for this fixture.
    blocked = any(distance(sample, "ball", "blue") < 0.42 and
                  sample["ball_contacts"] > 1 for sample in window)
    blocked_count += blocked
    end = window[-1]
    label = "守门员阻挡" if blocked else "正常推进/进球"
    print(f"{number:2d}  t={row['sim_time']:7.2f}s  "
          f"({row['ball_x']:+.2f},{row['ball_z']:+.2f}) -> "
          f"({end['ball_x']:+.2f},{end['ball_z']:+.2f})  "
          f"最近守门员={closest:.3f}m  {label}")
print(f"有效出脚 {len(launches)}，判定被挡 {blocked_count}")
