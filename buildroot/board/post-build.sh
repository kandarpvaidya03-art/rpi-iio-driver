#!/bin/sh
# Install an SSH public key for root, if one has been placed next to this script.
set -eu

BOARD_DIR="$(dirname "$0")"
KEY="${BOARD_DIR}/authorized_keys"

if [ -f "${KEY}" ]; then
    install -d -m 700 "${TARGET_DIR}/root/.ssh"
    install -m 600 "${KEY}" "${TARGET_DIR}/root/.ssh/authorized_keys"
fi
