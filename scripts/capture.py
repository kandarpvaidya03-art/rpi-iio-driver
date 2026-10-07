#!/usr/bin/env python3
"""Capture buffered samples from the adxl345_learn IIO driver and report timing."""
import argparse
import glob
import os
import statistics
import struct
import sys

NAME = "adxl345_learn"
RECORD = struct.Struct("<3h2xq")  # X, Y, Z, 2 padding bytes, timestamp in ns
CHANNELS = ["in_accel_x", "in_accel_y", "in_accel_z", "in_timestamp"]


def find_device():
    for path in glob.glob("/sys/bus/iio/devices/iio:device*"):
        try:
            with open(os.path.join(path, "name")) as f:
                if f.read().strip() == NAME:
                    return path
        except OSError:
            pass
    sys.exit(f"No IIO device named {NAME}. Is the driver loaded?")


def write(path, value):
    with open(path, "w") as f:
        f.write(str(value))


def read(path):
    with open(path) as f:
        return f.read().strip()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--rate", default="100", help="sampling frequency in Hz")
    ap.add_argument("--samples", type=int, default=1000)
    ap.add_argument("--buflen", type=int, default=1024,
                    help="kernel buffer length in samples")
    ap.add_argument("--skip", type=int, default=5, help="startup samples left out of the statistics")
    ap.add_argument("--csv", help="write samples to this CSV file")
    args = ap.parse_args()

    dev = find_device()
    node = "/dev/" + os.path.basename(dev)

    write(f"{dev}/buffer/enable", 0)
    write(f"{dev}/sampling_frequency", args.rate)
    for ch in CHANNELS:
        write(f"{dev}/scan_elements/{ch}_en", 1)
    write(f"{dev}/buffer/length", args.buflen)
    scale = float(read(f"{dev}/in_accel_scale"))

    wanted = args.samples + args.skip
    records = []
    pending = b""

    write(f"{dev}/buffer/enable", 1)
    try:
        with open(node, "rb", buffering=0) as f:
            while len(records) < wanted:
                chunk = f.read(RECORD.size * 64)
                if not chunk:
                    break
                pending += chunk
                while len(pending) >= RECORD.size and len(records) < wanted:
                    records.append(RECORD.unpack_from(pending))
                    pending = pending[RECORD.size:]
    finally:
        write(f"{dev}/buffer/enable", 0)

    records = records[args.skip:]
    if len(records) < 3:
        sys.exit("Not enough samples captured.")

    ts = [r[3] for r in records]
    gaps = [b - a for a, b in zip(ts, ts[1:])]
    median = statistics.median(gaps)
    long_gaps = [g for g in gaps if g > 1.5 * median]
    duration = (ts[-1] - ts[0]) / 1e9

    print(f"requested rate    : {args.rate} Hz")
    print(f"samples captured  : {len(records)}")
    print(f"startup skipped   : {args.skip}")
    print(f"duration          : {duration:.3f} s")
    print(f"measured rate     : {(len(records) - 1) / duration:.3f} Hz")
    print(f"interval mean     : {statistics.mean(gaps) / 1e6:.4f} ms")
    print(f"interval median   : {median / 1e6:.4f} ms")
    print(f"interval min/max  : {min(gaps) / 1e6:.4f} / {max(gaps) / 1e6:.4f} ms")
    print(f"min/max at index  : {gaps.index(min(gaps))} / {gaps.index(max(gaps))}")
    print(f"interval std dev  : {statistics.stdev(gaps) / 1e6:.4f} ms")
    print(f"gaps > 1.5 median : {len(long_gaps)}")
    for axis, name in enumerate("XYZ"):
        mean = statistics.mean(r[axis] for r in records)
        print(f"mean {name}            : {mean:8.2f} counts  {mean * scale:8.3f} m/s^2")

    if args.csv:
        with open(args.csv, "w") as f:
            f.write("timestamp_ns,x,y,z\n")
            for x, y, z, t in records:
                f.write(f"{t},{x},{y},{z}\n")
        print(f"wrote {len(records)} samples to {args.csv}")


if __name__ == "__main__":
    main()
