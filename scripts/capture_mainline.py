#!/usr/bin/env python3
"""Capture from the mainline adxl345 IIO driver (hardware FIFO, no timestamps)."""
import argparse
import glob
import os
import statistics
import struct
import sys
import time

from capture import print_cpu, runtime_ns, write

NAME = "adxl345"
RECORD = struct.Struct("<3h")  # X, Y, Z only


def find_device():
    for path in glob.glob("/sys/bus/iio/devices/iio:device*"):
        try:
            with open(os.path.join(path, "name")) as f:
                if f.read().strip() == NAME:
                    return path
        except OSError:
            pass
    sys.exit(f"No IIO device named {NAME}. Is the mainline driver loaded?")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--rate", default="100", help="sampling frequency in Hz")
    ap.add_argument("--samples", type=int, default=1000)
    ap.add_argument("--watermark", type=int, default=1,
                    help="FIFO watermark in samples")
    ap.add_argument("--buflen", type=int, default=1024,
                    help="kernel buffer length in samples")
    args = ap.parse_args()

    dev = find_device()
    node = "/dev/" + os.path.basename(dev)

    write(f"{dev}/buffer/enable", 0)
    write(f"{dev}/in_accel_sampling_frequency", args.rate)
    for axis in "xyz":
        write(f"{dev}/scan_elements/in_accel_{axis}_en", 1)
    write(f"{dev}/buffer/length", args.buflen)
    write(f"{dev}/buffer/watermark", args.watermark)

    records = []
    pending = b""
    first_time = None
    first_count = 0

    write(f"{dev}/buffer/enable", 1)
    cpu_before = runtime_ns()
    wall_before = time.monotonic()
    try:
        with open(node, "rb", buffering=0) as f:
            while len(records) < args.samples:
                chunk = f.read(RECORD.size * 64)
                if not chunk:
                    break
                pending += chunk
                while len(pending) >= RECORD.size:
                    records.append(RECORD.unpack_from(pending))
                    pending = pending[RECORD.size:]
                if first_time is None:
                    first_time = time.monotonic()
                    first_count = len(records)
    finally:
        wall_after = time.monotonic()
        cpu_after = runtime_ns()
        write(f"{dev}/buffer/enable", 0)

    if first_time is None or wall_after <= first_time:
        sys.exit("Not enough samples captured.")

    rate = (len(records) - first_count) / (wall_after - first_time)

    print(f"requested rate    : {args.rate} Hz (mainline, watermark {args.watermark})")
    print(f"samples captured  : {len(records)}")
    print(f"measured rate     : {rate:.3f} Hz (from arrival times in userspace)")
    print_cpu(cpu_before, cpu_after, wall_after - wall_before, len(records))
    for axis, name in enumerate("XYZ"):
        mean = statistics.mean(r[axis] for r in records)
        print(f"mean {name}            : {mean:8.2f} counts")


if __name__ == "__main__":
    main()
