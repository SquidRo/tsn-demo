//
// Created on 2022/5/12.
//

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <unistd.h>

#include <linux/rtnetlink.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <linux/if.h>

#include <netlink/netlink.h>
#include <netlink/route/link.h>
#include <netlink/route/link/vlan.h>
#include <netlink/route/link/bridge.h>
#include <sysrepo.h>

#include "log.h"
#include "dynamic-string.h"
#include "sset.h"
#include "utils.h"

#include <pthread.h>

#include "interface.h"
#include "shash.h"


//TODO: monitor interface link up/down by netlink messages
//
pthread_t tid_inf_lm;

void parse_rtmsg(struct nlmsghdr *nlh, void *arg)
{
    struct nlattr       *attrs[IFLA_MAX+1] ={0};
    struct ifinfomsg    *ifi;
    struct shash        *interfaces_p = (struct shash *) arg;
    struct interface    *inf_p;
    int                 i;
    char                *if_name_p = NULL;


    if (nlmsg_parse(nlh, sizeof(*ifi), attrs, IFLA_MAX, NULL) < 0) {
        printf("parse_rtmsg error \n");
        return;
    }

    for (i =0; i < IFLA_MAX; i++) {
        if (attrs[i]) {
            printf("id:type - %02x/%02x\n", i, nla_type(attrs[i]));
        }
    }

    /* TODO: ignore message without IFLA_CARRIER ???
     */
    ifi = nlmsg_data(nlh);

    if (attrs[IFLA_IFNAME]) {  // validation
        if_name_p = nla_data(attrs[IFLA_IFNAME]);
        printf(" DATA- %s", if_name_p); // get network interface name
    }

    if (attrs[IFLA_OPERSTATE]) {
        printf(" OPER(%d) -%d", IFLA_OPERSTATE, nla_get_u8(attrs[IFLA_OPERSTATE]));
    }

    if (attrs[IFLA_PROTINFO]) {
        printf(" PROTINFO");
    }

    if (attrs[IFLA_CARRIER]) {
        printf(" CARR(%d)", IFLA_CARRIER);
    }

    if (attrs[IFLA_PROTO_DOWN]) {
        printf(" DOWN RSN(%d)", IFLA_PROTO_DOWN);
    }

    if (ifi->ifi_flags & IFF_UP) { // get UP flag of the network interface
        printf(" UP");
    } else {
        printf(" DOWN");
    }

    if (ifi->ifi_flags & IFF_RUNNING) { // get RUNNING flag of the network interface
        printf(" RUNNING");
    } else {
        printf(" NOT RUNNING");
    }

    printf("\n");

    if (NULL != (inf_p = shash_find_data(interfaces_p, if_name_p)))
    {
        printf("inf FOUND\n");

        if ((inf_p->flags & IFF_UP) != (ifi->ifi_flags & IFF_UP))
        {
            printf("FLAG CHANGED !!!\n");
            inf_p->flags ^= IFF_UP;
        }
    }
}

int parse_nlmsg(struct nl_msg *nlmsg, void *arg)
{
    printf("[+%s]\n", __FUNCTION__);
//  nl_msg_dump(nlmsg, stdout);

    struct nlmsghdr *nlhdr;
    struct rtmsg *rtm;
    int len;
    nlhdr = nlmsg_hdr(nlmsg);
    len = nlhdr->nlmsg_len; // + NLMSG_HDRLEN;

    for (nlhdr; NLMSG_OK(nlhdr, len); nlhdr = NLMSG_NEXT(nlhdr, len)) {
        switch (nlhdr->nlmsg_type) {
        case RTM_NEWLINK:
            printf("RTM_NEWLINK\n");
            parse_rtmsg(nlhdr, arg);
            break;
        case RTM_DELLINK:
            printf("RTM_DELLINK\n");
            parse_rtmsg(nlhdr, arg);
            break;
        default:
            printf("nlmsg_type:%d\n\n", nlhdr->nlmsg_type);
            break;
        }
    }
    return 0;
}

void *inf_lm_thd (void *data_p)
{
	struct nl_sock *sock;

	sock = nl_socket_alloc();

	nl_join_groups(sock, RTMGRP_LINK);

	nl_connect(sock, NETLINK_ROUTE);

	nl_socket_modify_cb(sock, NL_CB_MSG_IN,
                            NL_CB_CUSTOM,
                            //NL_CB_DEBUG,
                            //NL_CB_DEFAULT,
                            //NL_CB_VERBOSE,
                            parse_nlmsg, data_p);

	while (tid_inf_lm != 0)
		nl_recvmsgs_default(sock);

    pthread_exit(NULL);
}

