#!/bin/sh

set -e

ACTION_INSTALL="install"
ACTION_REMOVE="remove"

test $# -eq 1 || {
    cat << __END__
USAGE: $0 <ACTION>

    ACTION: Install/remove the debug environment (choices: [$ACTION_INSTALL, $ACTION_REMOVE]).
__END__

    exit 1
}

ACTION="$1"
case "$ACTION" in
    "$ACTION_INSTALL")
        sudo -v
        sudo mkdir -p /opt/systemd-networkd-ubuntu
        sudo rsync -r -v -a --progress ./debug-env/systemd-networkd.service.d/ /etc/systemd/system/systemd-networkd.service.d
        sudo systemctl daemon-reload
        sudo mkdir -p /etc/systemd/network
        sudo rsync -r -v -a --progress ./debug-env/etc/systemd/network/j111.* /etc/systemd/network
        ;;
    "$ACTION_REMOVE")
        sudo -v
        sudo rm -vf /etc/systemd/network/j111.*
        sudo rm -vrf /etc/systemd/system/systemd-networkd.service.d
        sudo systemctl daemon-reload
        sudo rm -rf /opt/systemd-networkd-ubuntu
        ;;
    *)
        echo "Invalid action: expected '$ACTION_INSTALL' or '$ACTION_REMOVE' but got '$ACTION'"
        exit 1
        ;;
esac

echo ">>> $ACTION succeeded <<<"
