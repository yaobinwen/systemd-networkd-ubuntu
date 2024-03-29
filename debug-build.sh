#!/bin/sh

debug_env_error() {
    local DEBUG_FILE
    DEBUG_FILE="$1"

    echo "Debugging environment is not found (missing '$DEBUG_FILE')."
    echo "Have you set up the debugging environment (using 'debug-env.sh')?"
}

DEBUG_FILE_1="/etc/systemd/system/systemd-networkd.service.d/10-debug-log.conf"
test -f "$DEBUG_FILE_1" || {
    debug_env_error "$DEBUG_FILE_1"
    exit 1
}

DEBUG_FILE_2="/etc/systemd/network/j111.netdev"
test -f "$DEBUG_FILE_2" || {
    debug_env_error "$DEBUG_FILE_2"
    exit 1
}

## `CFLAGS` is used by "configure".
export CFLAGS=-rdynamic

## `NINJA_VERBOSE` is used by "Makefile".
export NINJA_VERBOSE=1

set -ex

sudo -v

./configure
make

sudo systemctl stop systemd-networkd

sudo rsync -rva --info=progress2 ./build/ /opt/systemd-networkd-ubuntu/build

# If `j111` is not found, it's fine and we can move on.
sudo ip link del dev j111 || true

sudo systemctl restart systemd-networkd

echo ">>> $0 succeeded <<<"
