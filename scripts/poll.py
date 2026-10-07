#!/usr/bin/env python3
"""Polled baseline: read the adxl345_learn raw sysfs attributes at a fixed rate."""
import argparse
import os
import statistics
import time

from capture import cpu_busy, cpu_ticks, find_device, write


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--rate", default="100", help="polling rate in Hz")
    ap.add_argument("--samples", type=int, default=1000)
    ap.add_argument("--skip", type=int, default=5,
                    help="startup samples left out of the statistics")
    args = ap.parse_args()

    dev = find_device()
    write(f"{dev}/buffer/enable", 0)
    write(f"{dev}/sampling_frequency", args.rate)

    fds = [os.open(f"{dev}/in_accel_{axis}_raw", os.O_RDONLY) for axis in "xyz"]
    period = 1.0 / float(args.rate)
    wanted = args.samples + args.skip
    ts = []
    values = []
    late = 0

    cpu_before = cpu_ticks()
    deadline = time.monotonic()
    for i in range(wanted):
        now = time.monotonic()
        if deadline > now:
            time.sleep(deadline - now)
        elif i >= args.skip:
            late += 1
        ts.append(time.monotonic_ns())
        values.append([int(os.pread(fd, 32, 0)) for fd in fds])
        deadline += period
    cpu_after = cpu_ticks()

    for fd in fds:
        os.close(fd)

    ts = ts[args.skip:]
    values = values[args.skip:]
    gaps = [b - a for a, b in zip(ts, ts[1:])]
    duration = (ts[-1] - ts[0]) / 1e9

    print(f"requested rate    : {args.rate} Hz (polled)")
    print(f"samples read      : {len(ts)}")
    print(f"startup skipped   : {args.skip}")
    print(f"duration          : {duration:.3f} s")
    print(f"measured rate     : {(len(ts) - 1) / duration:.3f} Hz")
    print(f"interval mean     : {statistics.mean(gaps) / 1e6:.4f} ms")
    print(f"interval min/max  : {min(gaps) / 1e6:.4f} / {max(gaps) / 1e6:.4f} ms")
    print(f"interval std dev  : {statistics.stdev(gaps) / 1e6:.4f} ms")
    print(f"missed deadlines  : {late}")
    print(f"CPU busy          : {cpu_busy(cpu_before, cpu_after):.2f} % of one core")
    for axis, name in enumerate("XYZ"):
        mean = statistics.mean(v[axis] for v in values)
        print(f"mean {name}            : {mean:8.2f} counts")


if __name__ == "__main__":
    main()
