#!/bin/bash
# Load/unload stress test for adxl345_learn.
# Run from the repository root: sudo bash scripts/stress.sh [cycles]
set -e

CYCLES=${1:-100}
KO=driver/adxl345_learn.ko
PATTERN='warn|bug|oops|leak|error|fail'

dmesg -C

for i in $(seq 1 "$CYCLES"); do
    insmod "$KO"
    dtoverlay adxl345-learn
    if ! grep -qs adxl345_learn /sys/bus/iio/devices/iio:device*/name; then
        echo "cycle $i: IIO device did not appear"
        exit 1
    fi
    dtoverlay -r adxl345-learn
    rmmod adxl345_learn
done

echo "cycles completed : $CYCLES"
echo "probe messages   : $(dmesg | grep -c 'ADXL345 found')"
echo "problem lines    : $(dmesg | grep -ciE "$PATTERN")"
dmesg | grep -iE "$PATTERN" | head -n 10
