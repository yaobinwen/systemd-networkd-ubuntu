/* SPDX-License-Identifier: LGPL-2.1+ */
/***
  This file is part of systemd.

  Copyright 2013 Tom Gundersen <teg@jklm.no>

  systemd is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or
  (at your option) any later version.

  systemd is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with systemd; If not, see <http://www.gnu.org/licenses/>.
***/

#include <net/if.h>

#include "alloc-util.h"
#include "conf-files.h"
#include "conf-parser.h"
#include "fd-util.h"
#include "list.h"
#include "netlink-util.h"
#include "network-internal.h"
#include "netdev/netdev.h"
#include "networkd-manager.h"
#include "networkd-link.h"
#include "siphash24.h"
#include "stat-util.h"
#include "string-table.h"
#include "string-util.h"

#include "netdev/bridge.h"
#include "netdev/bond.h"
#include "netdev/geneve.h"
#include "netdev/vlan.h"
#include "netdev/macvlan.h"
#include "netdev/ipvlan.h"
#include "netdev/vxlan.h"
#include "netdev/tunnel.h"
#include "netdev/tuntap.h"
#include "netdev/veth.h"
#include "netdev/dummy.h"
#include "netdev/vrf.h"
#include "netdev/vcan.h"
#include "netdev/vxcan.h"
#include "netdev/wireguard.h"

const NetDevVTable *const netdev_vtable[_NETDEV_KIND_MAX] = {
    [NETDEV_KIND_BRIDGE] = &bridge_vtable,
    [NETDEV_KIND_BOND] = &bond_vtable,
    [NETDEV_KIND_VLAN] = &vlan_vtable,
    [NETDEV_KIND_MACVLAN] = &macvlan_vtable,
    [NETDEV_KIND_MACVTAP] = &macvtap_vtable,
    [NETDEV_KIND_IPVLAN] = &ipvlan_vtable,
    [NETDEV_KIND_VXLAN] = &vxlan_vtable,
    [NETDEV_KIND_IPIP] = &ipip_vtable,
    [NETDEV_KIND_GRE] = &gre_vtable,
    [NETDEV_KIND_GRETAP] = &gretap_vtable,
    [NETDEV_KIND_IP6GRE] = &ip6gre_vtable,
    [NETDEV_KIND_IP6GRETAP] = &ip6gretap_vtable,
    [NETDEV_KIND_SIT] = &sit_vtable,
    [NETDEV_KIND_VTI] = &vti_vtable,
    [NETDEV_KIND_VTI6] = &vti6_vtable,
    [NETDEV_KIND_VETH] = &veth_vtable,
    [NETDEV_KIND_DUMMY] = &dummy_vtable,
    [NETDEV_KIND_TUN] = &tun_vtable,
    [NETDEV_KIND_TAP] = &tap_vtable,
    [NETDEV_KIND_IP6TNL] = &ip6tnl_vtable,
    [NETDEV_KIND_VRF] = &vrf_vtable,
    [NETDEV_KIND_VCAN] = &vcan_vtable,
    [NETDEV_KIND_GENEVE] = &geneve_vtable,
    [NETDEV_KIND_VXCAN] = &vxcan_vtable,
    [NETDEV_KIND_WIREGUARD] = &wireguard_vtable,
};

static const char *const netdev_kind_table[_NETDEV_KIND_MAX] = {
    [NETDEV_KIND_BRIDGE] = "bridge",
    [NETDEV_KIND_BOND] = "bond",
    [NETDEV_KIND_VLAN] = "vlan",
    [NETDEV_KIND_MACVLAN] = "macvlan",
    [NETDEV_KIND_MACVTAP] = "macvtap",
    [NETDEV_KIND_IPVLAN] = "ipvlan",
    [NETDEV_KIND_VXLAN] = "vxlan",
    [NETDEV_KIND_IPIP] = "ipip",
    [NETDEV_KIND_GRE] = "gre",
    [NETDEV_KIND_GRETAP] = "gretap",
    [NETDEV_KIND_IP6GRE] = "ip6gre",
    [NETDEV_KIND_IP6GRETAP] = "ip6gretap",
    [NETDEV_KIND_SIT] = "sit",
    [NETDEV_KIND_VETH] = "veth",
    [NETDEV_KIND_VTI] = "vti",
    [NETDEV_KIND_VTI6] = "vti6",
    [NETDEV_KIND_DUMMY] = "dummy",
    [NETDEV_KIND_TUN] = "tun",
    [NETDEV_KIND_TAP] = "tap",
    [NETDEV_KIND_IP6TNL] = "ip6tnl",
    [NETDEV_KIND_VRF] = "vrf",
    [NETDEV_KIND_VCAN] = "vcan",
    [NETDEV_KIND_GENEVE] = "geneve",
    [NETDEV_KIND_VXCAN] = "vxcan",
    [NETDEV_KIND_WIREGUARD] = "wireguard",
};

DEFINE_STRING_TABLE_LOOKUP(netdev_kind, NetDevKind);
DEFINE_CONFIG_PARSE_ENUM(config_parse_netdev_kind, netdev_kind, NetDevKind, "Failed to parse netdev kind");

static void netdev_cancel_callbacks(NetDev *netdev)
{
        _cleanup_(sd_netlink_message_unrefp) sd_netlink_message *m = NULL;
        netdev_join_callback *callback;

        if (!netdev || !netdev->manager)
                return;

        rtnl_message_new_synthetic_error(netdev->manager->rtnl, -ENODEV, 0, &m);

        while ((callback = netdev->callbacks))
        {
                if (m)
                {
                        assert(callback->link);
                        assert(callback->callback);
                        assert(netdev->manager);
                        assert(netdev->manager->rtnl);

                        callback->callback(netdev->manager->rtnl, m, callback->link);
                }

                LIST_REMOVE(callbacks, netdev->callbacks, callback);
                link_unref(callback->link);
                free(callback);
        }
}

static void netdev_free(NetDev *netdev)
{
        if (!netdev)
                return;

        netdev_cancel_callbacks(netdev);

        if (netdev->ifname && netdev->manager)
                hashmap_remove(netdev->manager->netdevs, netdev->ifname);

        free(netdev->filename);

        free(netdev->description);
        free(netdev->ifname);
        free(netdev->mac);

        condition_free_list(netdev->match_host);
        condition_free_list(netdev->match_virt);
        condition_free_list(netdev->match_kernel_cmdline);
        condition_free_list(netdev->match_kernel_version);
        condition_free_list(netdev->match_arch);

        /* Invoke the per-kind done() destructor, but only if the state field is initialized. We conditionalize that
         * because we parse .netdev files twice: once to determine the kind (with a short, minimal NetDev structure
         * allocation, with no room for per-kind fields), and once to read the kind's properties (with a full,
         * comprehensive NetDev structure allocation with enough space for whatever the specific kind needs). Now, in
         * the first case we shouldn't try to destruct the per-kind NetDev fields on destruction, in the second case we
         * should. We use the state field to discern the two cases: it's _NETDEV_STATE_INVALID on the first "raw"
         * call. */
        if (netdev->state != _NETDEV_STATE_INVALID &&
            NETDEV_VTABLE(netdev) &&
            NETDEV_VTABLE(netdev)->done)
                NETDEV_VTABLE(netdev)->done(netdev);

        free(netdev);
}

NetDev *netdev_unref(NetDev *netdev)
{
        if (netdev && (--netdev->n_ref <= 0))
                netdev_free(netdev);

        return NULL;
}

NetDev *netdev_ref(NetDev *netdev)
{
        if (netdev)
                assert_se(++netdev->n_ref >= 2);

        return netdev;
}

void netdev_drop(NetDev *netdev)
{
        if (!netdev || netdev->state == NETDEV_STATE_LINGER)
                return;

        netdev->state = NETDEV_STATE_LINGER;

        log_netdev_debug(netdev, "netdev removed");

        netdev_cancel_callbacks(netdev);

        netdev_unref(netdev);

        return;
}

int netdev_get(Manager *manager, const char *name, NetDev **ret)
{
        NetDev *netdev;

        assert(manager);
        assert(name);
        assert(ret);

        netdev = hashmap_get(manager->netdevs, name);
        if (!netdev)
        {
                *ret = NULL;
                return -ENOENT;
        }

        *ret = netdev;

        return 0;
}

static int netdev_enter_failed(NetDev *netdev)
{
        netdev->state = NETDEV_STATE_FAILED;

        netdev_cancel_callbacks(netdev);

        return 0;
}

static int netdev_enslave_ready(NetDev *netdev, Link *link, sd_netlink_message_handler_t callback)
{
        _cleanup_(sd_netlink_message_unrefp) sd_netlink_message *req = NULL;
        int r;

        assert(netdev);
        assert(netdev->state == NETDEV_STATE_READY);
        assert(netdev->manager);
        assert(netdev->manager->rtnl);
        assert(IN_SET(netdev->kind, NETDEV_KIND_BRIDGE, NETDEV_KIND_BOND, NETDEV_KIND_VRF));
        assert(link);
        assert(callback);

        if (link->flags & IFF_UP)
        {
                log_netdev_debug(netdev, "Link '%s' was up when attempting to enslave it. Bringing link down.", link->ifname);
                r = link_down(link);
                if (r < 0)
                        return log_netdev_error_errno(netdev, r, "Could not bring link down: %m");
        }

        r = sd_rtnl_message_new_link(netdev->manager->rtnl, &req, RTM_SETLINK, link->ifindex);
        if (r < 0)
                return log_netdev_error_errno(netdev, r, "Could not allocate RTM_SETLINK message: %m");

        r = sd_netlink_message_append_u32(req, IFLA_MASTER, netdev->ifindex);
        if (r < 0)
                return log_netdev_error_errno(netdev, r, "Could not append IFLA_MASTER attribute: %m");

        r = sd_netlink_call_async(netdev->manager->rtnl, req, callback, link, 0, NULL);
        if (r < 0)
                return log_netdev_error(netdev, "Could not send rtnetlink message: %m");

        link_ref(link);

        log_netdev_debug(netdev, "Enslaving link '%s'", link->ifname);

        return 0;
}

static int netdev_enter_ready(NetDev *netdev)
{
        netdev_join_callback *callback, *callback_next;
        int r;

        assert(netdev);
        assert(netdev->ifname);

        if (netdev->state != NETDEV_STATE_CREATING)
                return 0;

        netdev->state = NETDEV_STATE_READY;

        log_netdev_info(netdev, "netdev ready");

        LIST_FOREACH_SAFE(callbacks, callback, callback_next, netdev->callbacks)
        {
                /* enslave the links that were attempted to be enslaved before the
                 * link was ready */
                r = netdev_enslave_ready(netdev, callback->link, callback->callback);
                if (r < 0)
                        return r;

                LIST_REMOVE(callbacks, netdev->callbacks, callback);
                link_unref(callback->link);
                free(callback);
        }

        if (NETDEV_VTABLE(netdev)->post_create)
                NETDEV_VTABLE(netdev)->post_create(netdev, NULL, NULL);

        return 0;
}

/* callback for netdev's created without a backing Link */
static int netdev_create_handler(sd_netlink *rtnl, sd_netlink_message *m, void *userdata)
{
        _cleanup_netdev_unref_ NetDev *netdev = userdata;
        int r;

        assert(netdev->state != _NETDEV_STATE_INVALID);

        r = sd_netlink_message_get_errno(m);
        if (r == -EEXIST)
                log_netdev_info(netdev, "netdev exists, using existing without changing its parameters");
        else if (r < 0)
        {
                log_netdev_warning_errno(netdev, r, "netdev could not be created: %m");
                netdev_drop(netdev);

                return 1;
        }

        log_netdev_debug(netdev, "Created");

        return 1;
}

int netdev_enslave(NetDev *netdev, Link *link, sd_netlink_message_handler_t callback)
{
        int r;

        assert(netdev);
        assert(netdev->manager);
        assert(netdev->manager->rtnl);
        assert(IN_SET(netdev->kind, NETDEV_KIND_BRIDGE, NETDEV_KIND_BOND, NETDEV_KIND_VRF));

        if (netdev->state == NETDEV_STATE_READY)
        {
                r = netdev_enslave_ready(netdev, link, callback);
                if (r < 0)
                        return r;
        }
        else if (IN_SET(netdev->state, NETDEV_STATE_LINGER, NETDEV_STATE_FAILED))
        {
                _cleanup_(sd_netlink_message_unrefp) sd_netlink_message *m = NULL;

                r = rtnl_message_new_synthetic_error(netdev->manager->rtnl, -ENODEV, 0, &m);
                if (r >= 0)
                        callback(netdev->manager->rtnl, m, link);
        }
        else
        {
                /* the netdev is not yet read, save this request for when it is */
                netdev_join_callback *cb;

                cb = new0(netdev_join_callback, 1);
                if (!cb)
                        return log_oom();

                cb->callback = callback;
                cb->link = link;
                link_ref(link);

                LIST_PREPEND(callbacks, netdev->callbacks, cb);

                log_netdev_debug(netdev, "Will enslave '%s', when ready", link->ifname);
        }

        return 0;
}

int netdev_set_ifindex(NetDev *netdev, sd_netlink_message *message)
{
        uint16_t type;
        const char *kind;
        const char *received_kind;
        const char *received_name;
        int r, ifindex;

        assert(netdev);
        assert(message);

        r = sd_netlink_message_get_type(message, &type);
        if (r < 0)
                return log_netdev_error_errno(netdev, r, "Could not get rtnl message type: %m");

        if (type != RTM_NEWLINK)
        {
                log_netdev_error(netdev, "Cannot set ifindex from unexpected rtnl message type.");
                return -EINVAL;
        }

        r = sd_rtnl_message_link_get_ifindex(message, &ifindex);
        if (r < 0)
        {
                log_netdev_error_errno(netdev, r, "Could not get ifindex: %m");
                netdev_enter_failed(netdev);
                return r;
        }
        else if (ifindex <= 0)
        {
                log_netdev_error(netdev, "Got invalid ifindex: %d", ifindex);
                netdev_enter_failed(netdev);
                return -EINVAL;
        }

        if (netdev->ifindex > 0)
        {
                if (netdev->ifindex != ifindex)
                {
                        log_netdev_error(netdev, "Could not set ifindex to %d, already set to %d",
                                         ifindex, netdev->ifindex);
                        netdev_enter_failed(netdev);
                        return -EEXIST;
                }
                else
                        /* ifindex already set to the same for this netdev */
                        return 0;
        }

        r = sd_netlink_message_read_string(message, IFLA_IFNAME, &received_name);
        if (r < 0)
                return log_netdev_error_errno(netdev, r, "Could not get IFNAME: %m");

        if (!streq(netdev->ifname, received_name))
        {
                log_netdev_error(netdev, "Received newlink with wrong IFNAME %s", received_name);
                netdev_enter_failed(netdev);
                return r;
        }

        r = sd_netlink_message_enter_container(message, IFLA_LINKINFO);
        if (r < 0)
                return log_netdev_error_errno(netdev, r, "Could not get LINKINFO: %m");

        r = sd_netlink_message_read_string(message, IFLA_INFO_KIND, &received_kind);
        if (r < 0)
                return log_netdev_error_errno(netdev, r, "Could not get KIND: %m");

        r = sd_netlink_message_exit_container(message);
        if (r < 0)
                return log_netdev_error_errno(netdev, r, "Could not exit container: %m");

        if (netdev->kind == NETDEV_KIND_TAP)
                /* the kernel does not distinguish between tun and tap */
                kind = "tun";
        else
        {
                kind = netdev_kind_to_string(netdev->kind);
                if (!kind)
                {
                        log_netdev_error(netdev, "Could not get kind");
                        netdev_enter_failed(netdev);
                        return -EINVAL;
                }
        }

        if (!streq(kind, received_kind))
        {
                log_netdev_error(netdev,
                                 "Received newlink with wrong KIND %s, "
                                 "expected %s",
                                 received_kind, kind);
                netdev_enter_failed(netdev);
                return r;
        }

        netdev->ifindex = ifindex;

        log_netdev_debug(netdev, "netdev has index %d", netdev->ifindex);

        netdev_enter_ready(netdev);

        return 0;
}

#define HASH_KEY SD_ID128_MAKE(52, e1, 45, bd, 00, 6f, 29, 96, 21, c6, 30, 6d, 83, 71, 04, 48)

int netdev_get_mac(const char *ifname, struct ether_addr **ret)
{
        _cleanup_free_ struct ether_addr *mac = NULL;
        uint64_t result;
        size_t l, sz;
        uint8_t *v;
        int r;

        assert(ifname);
        assert(ret);

        mac = new0(struct ether_addr, 1);
        if (!mac)
                return -ENOMEM;

        l = strlen(ifname);
        sz = sizeof(sd_id128_t) + l;
        v = alloca(sz);

        /* fetch some persistent data unique to the machine */
        r = sd_id128_get_machine((sd_id128_t *)v);
        if (r < 0)
                return r;

        /* combine with some data unique (on this machine) to this
         * netdev */
        memcpy(v + sizeof(sd_id128_t), ifname, l);

        /* Let's hash the host machine ID plus the container name. We
         * use a fixed, but originally randomly created hash key here. */
        result = siphash24(v, sz, HASH_KEY.bytes);

        assert_cc(ETH_ALEN <= sizeof(result));
        memcpy(mac->ether_addr_octet, &result, ETH_ALEN);

        /* see eth_random_addr in the kernel */
        mac->ether_addr_octet[0] &= 0xfe; /* clear multicast bit */
        mac->ether_addr_octet[0] |= 0x02; /* set local assignment bit (IEEE802) */

        *ret = mac;
        mac = NULL;

        return 0;
}

static int netdev_create(NetDev *netdev, Link *link,
                         sd_netlink_message_handler_t callback)
{
        int r;

        assert(netdev);
        assert(!link || callback);

        /* create netdev */
        if (NETDEV_VTABLE(netdev)->create)
        {
                assert(!link);

                r = NETDEV_VTABLE(netdev)->create(netdev);
                if (r < 0)
                        return r;

                log_netdev_debug(netdev, "Created");
        }
        else
        {
                _cleanup_(sd_netlink_message_unrefp) sd_netlink_message *m = NULL;

                r = sd_rtnl_message_new_link(netdev->manager->rtnl, &m, RTM_NEWLINK, 0);
                if (r < 0)
                        return log_netdev_error_errno(netdev, r, "Could not allocate RTM_NEWLINK message: %m");

                r = sd_netlink_message_append_string(m, IFLA_IFNAME, netdev->ifname);
                if (r < 0)
                        return log_netdev_error_errno(netdev, r, "Could not append IFLA_IFNAME, attribute: %m");

                if (netdev->mac)
                {
                        r = sd_netlink_message_append_ether_addr(m, IFLA_ADDRESS, netdev->mac);
                        if (r < 0)
                                return log_netdev_error_errno(netdev, r, "Could not append IFLA_ADDRESS attribute: %m");
                }

                if (netdev->mtu)
                {
                        r = sd_netlink_message_append_u32(m, IFLA_MTU, netdev->mtu);
                        if (r < 0)
                                return log_netdev_error_errno(netdev, r, "Could not append IFLA_MTU attribute: %m");
                }

                if (link)
                {
                        r = sd_netlink_message_append_u32(m, IFLA_LINK, link->ifindex);
                        if (r < 0)
                                return log_netdev_error_errno(netdev, r, "Could not append IFLA_LINK attribute: %m");
                }

                r = sd_netlink_message_open_container(m, IFLA_LINKINFO);
                if (r < 0)
                        return log_netdev_error_errno(netdev, r, "Could not append IFLA_LINKINFO attribute: %m");

                r = sd_netlink_message_open_container_union(m, IFLA_INFO_DATA, netdev_kind_to_string(netdev->kind));
                if (r < 0)
                        return log_netdev_error_errno(netdev, r, "Could not append IFLA_INFO_DATA attribute: %m");

                if (NETDEV_VTABLE(netdev)->fill_message_create)
                {
                        r = NETDEV_VTABLE(netdev)->fill_message_create(netdev, link, m);
                        if (r < 0)
                                return r;
                }

                r = sd_netlink_message_close_container(m);
                if (r < 0)
                        return log_netdev_error_errno(netdev, r, "Could not append IFLA_INFO_DATA attribute: %m");

                r = sd_netlink_message_close_container(m);
                if (r < 0)
                        return log_netdev_error_errno(netdev, r, "Could not append IFLA_LINKINFO attribute: %m");

                if (link)
                {
                        r = sd_netlink_call_async(netdev->manager->rtnl, m, callback, link, 0, NULL);
                        if (r < 0)
                                return log_netdev_error_errno(netdev, r, "Could not send rtnetlink message: %m");

                        link_ref(link);
                }
                else
                {
                        r = sd_netlink_call_async(netdev->manager->rtnl, m, netdev_create_handler, netdev, 0, NULL);
                        if (r < 0)
                                return log_netdev_error_errno(netdev, r, "Could not send rtnetlink message: %m");

                        netdev_ref(netdev);
                }

                netdev->state = NETDEV_STATE_CREATING;

                log_netdev_debug(netdev, "Creating");
        }

        return 0;
}

/* the callback must be called, possibly after a timeout, as otherwise the Link will hang */
int netdev_join(NetDev *netdev, Link *link, sd_netlink_message_handler_t callback)
{
        int r;

        assert(netdev);
        assert(netdev->manager);
        assert(netdev->manager->rtnl);
        assert(NETDEV_VTABLE(netdev));

        switch (NETDEV_VTABLE(netdev)->create_type)
        {
        case NETDEV_CREATE_MASTER:
                r = netdev_enslave(netdev, link, callback);
                if (r < 0)
                        return r;

                break;
        case NETDEV_CREATE_STACKED:
                r = netdev_create(netdev, link, callback);
                if (r < 0)
                        return r;

                break;
        default:
                assert_not_reached("Can not join independent netdev");
        }

        return 0;
}

static int netdev_load_one(Manager *manager, const char *filename)
{
        _cleanup_netdev_unref_ NetDev *netdev_raw = NULL, *netdev = NULL;
        _cleanup_fclose_ FILE *file = NULL;
        const char *dropin_dirname;
        bool independent = false;
        int r;

        assert(manager);
        assert(filename);

        file = fopen(filename, "re");
        if (!file)
        {
                if (errno == ENOENT)
                        return 0;

                return -errno;
        }

        if (null_or_empty_fd(fileno(file)))
        {
                log_debug("Skipping empty file: %s", filename);
                return 0;
        }

        netdev_raw = new0(NetDev, 1);
        if (!netdev_raw)
                return log_oom();

        netdev_raw->n_ref = 1;
        netdev_raw->kind = _NETDEV_KIND_INVALID;
        netdev_raw->state = _NETDEV_STATE_INVALID; /* an invalid state means done() of the implementation won't be called on destruction */

        dropin_dirname = strjoina(basename(filename), ".d");
        r = config_parse_many(filename, network_dirs, dropin_dirname,
                              "Match\0NetDev\0",
                              config_item_perf_lookup, network_netdev_gperf_lookup,
                              CONFIG_PARSE_WARN | CONFIG_PARSE_RELAXED, netdev_raw);
        if (r < 0)
                return r;

        /* skip out early if configuration does not match the environment */
        if (net_match_config(NULL, NULL, NULL, NULL, NULL,
                             netdev_raw->match_host, netdev_raw->match_virt,
                             netdev_raw->match_kernel_cmdline, netdev_raw->match_kernel_version,
                             netdev_raw->match_arch,
                             NULL, NULL, NULL, NULL, NULL, NULL) <= 0)
                return 0;

        if (netdev_raw->kind == _NETDEV_KIND_INVALID)
        {
                log_warning("NetDev has no Kind configured in %s. Ignoring", filename);
                return 0;
        }

        if (!netdev_raw->ifname)
        {
                log_warning("NetDev without Name configured in %s. Ignoring", filename);
                return 0;
        }

        r = fseek(file, 0, SEEK_SET);
        if (r < 0)
                return -errno;

        netdev = malloc0(NETDEV_VTABLE(netdev_raw)->object_size);
        if (!netdev)
                return log_oom();

        netdev->n_ref = 1;
        netdev->manager = manager;
        netdev->kind = netdev_raw->kind;
        netdev->state = NETDEV_STATE_LOADING; /* we initialize the state here for the first time, so that done() will be called on destruction */

        if (NETDEV_VTABLE(netdev)->init)
                NETDEV_VTABLE(netdev)->init(netdev);

        r = config_parse(NULL, filename, file,
                         NETDEV_VTABLE(netdev)->sections,
                         config_item_perf_lookup, network_netdev_gperf_lookup,
                         CONFIG_PARSE_WARN, netdev);
        if (r < 0)
                return r;

        /* verify configuration */
        if (NETDEV_VTABLE(netdev)->config_verify)
        {
                r = NETDEV_VTABLE(netdev)->config_verify(netdev, filename);
                if (r < 0)
                        return 0;
        }

        netdev->filename = strdup(filename);
        if (!netdev->filename)
                return log_oom();

        if (!netdev->mac && netdev->kind != NETDEV_KIND_VLAN)
        {
                r = netdev_get_mac(netdev->ifname, &netdev->mac);
                if (r < 0)
                        return log_error_errno(r, "Failed to generate predictable MAC address for %s: %m", netdev->ifname);
        }

        r = hashmap_put(netdev->manager->netdevs, netdev->ifname, netdev);
        if (r < 0)
                return r;

        LIST_HEAD_INIT(netdev->callbacks);

        log_netdev_debug(netdev, "loaded %s", netdev_kind_to_string(netdev->kind));

        switch (NETDEV_VTABLE(netdev)->create_type)
        {
        case NETDEV_CREATE_MASTER:
        case NETDEV_CREATE_INDEPENDENT:
                r = netdev_create(netdev, NULL, NULL);
                if (r < 0)
                        return r;

                break;
        default:
                break;
        }

        switch (netdev->kind)
        {
        case NETDEV_KIND_IPIP:
                independent = IPIP(netdev)->independent;
                break;
        case NETDEV_KIND_GRE:
                independent = GRE(netdev)->independent;
                break;
        case NETDEV_KIND_GRETAP:
                independent = GRETAP(netdev)->independent;
                break;
        case NETDEV_KIND_IP6GRE:
                independent = IP6GRE(netdev)->independent;
                break;
        case NETDEV_KIND_IP6GRETAP:
                independent = IP6GRETAP(netdev)->independent;
                break;
        case NETDEV_KIND_SIT:
                independent = SIT(netdev)->independent;
                break;
        case NETDEV_KIND_VTI:
                independent = VTI(netdev)->independent;
                break;
        case NETDEV_KIND_VTI6:
                independent = VTI6(netdev)->independent;
                break;
        case NETDEV_KIND_IP6TNL:
                independent = IP6TNL(netdev)->independent;
                break;
        default:
                break;
        }

        if (independent)
        {
                r = netdev_create(netdev, NULL, NULL);
                if (r < 0)
                        return r;
        }

        netdev = NULL;

        return 0;
}

int netdev_load(Manager *manager)
{
        _cleanup_strv_free_ char **files = NULL;
        NetDev *netdev;
        char **f;
        int r;

        assert(manager);

        while ((netdev = hashmap_first(manager->netdevs)))
                netdev_unref(netdev);

        r = conf_files_list_strv(&files, ".netdev", NULL, 0, network_dirs);
        if (r < 0)
                return log_error_errno(r, "Failed to enumerate netdev files: %m");

        STRV_FOREACH_BACKWARDS(f, files)
        {
                r = netdev_load_one(manager, *f);
                if (r < 0)
                        return r;
        }

        return 0;
}
