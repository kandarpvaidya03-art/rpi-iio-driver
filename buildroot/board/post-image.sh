#!/bin/bash
# Derived from Buildroot's board/raspberrypi/post-image.sh (GPL-2.0-or-later).
# Adds the adxl345-learn overlay to the boot partition and uses a larger one.
set -e

BOARD_DIR="$(dirname "$0")"
REPO_DIR="$(realpath "${BOARD_DIR}/../..")"
GENIMAGE_CFG="${BINARIES_DIR}/genimage.cfg"
GENIMAGE_TMP="${BUILD_DIR}/genimage.tmp"

mkdir -p "${BINARIES_DIR}/rpi-firmware/overlays"
"${HOST_DIR}/bin/dtc" -@ -I dts -O dtb \
    -o "${BINARIES_DIR}/rpi-firmware/overlays/adxl345-learn.dtbo" \
    "${REPO_DIR}/overlay/adxl345-learn-overlay.dts"

FILES=()
for i in "${BINARIES_DIR}"/*.dtb "${BINARIES_DIR}"/rpi-firmware/*; do
    FILES+=( "${i#${BINARIES_DIR}/}" )
done

KERNEL=$(sed -n 's/^kernel=//p' "${BINARIES_DIR}/rpi-firmware/config.txt")
FILES+=( "${KERNEL}" )

BOOT_FILES=$(printf '\\t\\t\\t"%s",\\n' "${FILES[@]}")
sed "s|#BOOT_FILES#|${BOOT_FILES}|" "${BOARD_DIR}/genimage.cfg.in" > "${GENIMAGE_CFG}"

trap 'rm -rf "${ROOTPATH_TMP}"' EXIT
ROOTPATH_TMP="$(mktemp -d)"
rm -rf "${GENIMAGE_TMP}"

genimage \
    --rootpath "${ROOTPATH_TMP}" \
    --tmppath "${GENIMAGE_TMP}" \
    --inputpath "${BINARIES_DIR}" \
    --outputpath "${BINARIES_DIR}" \
    --config "${GENIMAGE_CFG}"
