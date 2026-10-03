#!/usr/bin/env bash
set -euo pipefail
# No actual input devices or /dev/uinput are opened by this test. Give Qt's
# device enumeration a private directory containing only simulated FIFOs.
exec unshare --user --map-root-user --mount -- sh -eu -c '
    mount -t tmpfs -o mode=755 tmpfs /dev/input
    mkfifo /dev/input/event0 /dev/input/event1 /dev/input/event2
    exec "$1"
' sh "$1"
