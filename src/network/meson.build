# SPDX-License-Identifier: LGPL-2.1+
#
# Copyright 2017 Zbigniew Jędrzejewski-Szmek
#
# systemd is free software; you can redistribute it and/or modify it
# under the terms of the GNU Lesser General Public License as published by
# the Free Software Foundation; either version 2.1 of the License, or
# (at your option) any later version.
#
# systemd is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with systemd; If not, see <http://www.gnu.org/licenses/>.

sources = files('''
        netdev/bond.c
        netdev/bond.h
        netdev/bridge.c
        netdev/bridge.h
        netdev/dummy.c
        netdev/dummy.h
        netdev/ipvlan.c
        netdev/ipvlan.h
        netdev/macvlan.c
        netdev/macvlan.h
        netdev/netdev.c
        netdev/netdev.h
        netdev/tunnel.c
        netdev/tunnel.h
        netdev/tuntap.c
        netdev/tuntap.h
        netdev/vcan.c
        netdev/vcan.h
        netdev/veth.c
        netdev/veth.h
        netdev/vlan.c
        netdev/vlan.h
        netdev/vrf.c
        netdev/vrf.h
        netdev/vxlan.c
        netdev/vxlan.h
        netdev/geneve.c
        netdev/geneve.h
        netdev/vxcan.c
        netdev/vxcan.h
        netdev/wireguard.c
        netdev/wireguard.h
        networkd-address-label.c
        networkd-address-label.h
        networkd-address-pool.c
        networkd-address-pool.h
        networkd-address.c
        networkd-address.h
        networkd-brvlan.c
        networkd-brvlan.h
        networkd-conf.c
        networkd-conf.h
        networkd-dhcp4.c
        networkd-dhcp6.c
        networkd-fdb.c
        networkd-fdb.h
        networkd-ipv4ll.c
        networkd-ipv6-proxy-ndp.c
        networkd-ipv6-proxy-ndp.h
        networkd-link-bus.c
        networkd-link.c
        networkd-link.h
        networkd-lldp-tx.c
        networkd-lldp-tx.h
        networkd-manager-bus.c
        networkd-manager.c
        networkd-manager.h
        networkd-ndisc.c
        networkd-ndisc.h
        networkd-radv.c
        networkd-radv.h
        networkd-network-bus.c
        networkd-network.c
        networkd-network.h
        networkd-route.c
        networkd-route.h
        networkd-routing-policy-rule.c
        networkd-routing-policy-rule.h
        networkd-util.c
        networkd-util.h
'''.split())

systemd_networkd_sources = files('networkd.c')

network_include_dir = include_directories('.')

if conf.get('ENABLE_NETWORKD') == 1
        networkd_gperf_c = custom_target(
                'networkd-gperf.c',
                input : 'networkd-gperf.gperf',
                output : 'networkd-gperf.c',
                command : [gperf, '@INPUT@', '--output-file', '@OUTPUT@'])

        networkd_network_gperf_c = custom_target(
                'networkd-network-gperf.c',
                input : 'networkd-network-gperf.gperf',
                output : 'networkd-network-gperf.c',
                command : [gperf, '@INPUT@', '--output-file', '@OUTPUT@'])

        netdev_gperf_c = custom_target(
                'netdev-gperf.c',
                input : 'netdev/netdev-gperf.gperf',
                output : 'netdev-gperf.c',
                command : [gperf, '@INPUT@', '--output-file', '@OUTPUT@'])

        libnetworkd_core = static_library(
                'networkd-core',
                sources,
                network_internal_h,
                networkd_gperf_c,
                networkd_network_gperf_c,
                netdev_gperf_c,
                include_directories : includes,
                link_with : [libshared])

        install_data('org.freedesktop.network1.conf',
                     install_dir : dbuspolicydir)
        install_data('org.freedesktop.network1.service',
                     install_dir : dbussystemservicedir)
        if install_polkit
                install_data('systemd-networkd.rules',
                             install_dir : polkitrulesdir)
        endif
        if install_polkit_pkla
                install_data('systemd-networkd.pkla',
                             install_dir : polkitpkladir)
        endif

        tests += [
    [['src/network/test-networkd-conf.c'],
     [libnetworkd_core,
      libsystemd_network,
      libudev],
     []],

    [['src/network/test-network.c'],
     [libnetworkd_core,
      libudev_static,
      libsystemd_network,
      libshared],
     [threads]],

    [['src/network/test-routing-policy-rule.c'],
     [libnetworkd_core,
      libsystemd_network,
      libudev],
     []],

    [['src/network/test-network-tables.c',
      'src/network/test-network-tables.c',
      test_tables_h],
     [libnetworkd_core,
      libudev_static,
      libudev_core,
      libsystemd_network,
      libshared],
     [threads],
     '', '', [],
     [network_include_dir] + libudev_core_includes],
  ]
endif
