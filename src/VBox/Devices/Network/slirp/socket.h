/* $Id: socket.h $ */
/** @file
 * NAT - socket handling (declarations/defines).
 */

/*
 * Copyright (C) 2006-2010 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

/*
 * This code is based on:
 *
 * Copyright (c) 1995 Danny Gasparovski.
 *
 * Please read the file COPYRIGHT for the
 * terms and conditions of the copyright.
 */

/* MINE */

#ifndef _SLIRP_SOCKET_H_
#define _SLIRP_SOCKET_H_
#ifdef VBOX_WITH_SLIRP_MT
#include <iprt/critsect.h>
#endif

#define SO_EXPIRE 240000
#define SO_EXPIREFAST 10000

/*
 * Our socket structure
 */

struct socket
{
    struct socket   *so_next;
    struct socket   *so_prev;    /* For a linked list of sockets */

#if !defined(RT_OS_WINDOWS)
    int s;                       /* The actual socket */
#else
    union {
            int s;
            HANDLE sh;
    };
    uint64_t so_icmp_id; /* XXX: hack */
    uint64_t so_icmp_seq; /* XXX: hack */
#endif

    /* XXX union these with not-yet-used sbuf params */
    struct mbuf     *so_m;       /* Pointer to the original SYN packet,
                                  * for non-blocking connect()'s, and
                                  * PING reply's */
    struct tcpiphdr *so_ti;      /* Pointer to the original ti within
                                  * so_mconn, for non-blocking connections */
    int             so_urgc;
    struct in_addr  so_faddr;    /* foreign host table entry */
    struct in_addr  so_laddr;    /* local host table entry */
    u_int16_t       so_fport;    /* foreign port */
    u_int16_t       so_lport;    /* local port */
    u_int16_t       so_hlport; /* host local port */
    struct in_addr  so_hladdr;    /* local host addr */

    u_int8_t        so_iptos;    /* Type of service */

    u_char          so_type;     /* Type of socket, UDP or TCP */
    int             so_state;    /* internal state flags SS_*, below */

    struct tcpcb    *so_tcpcb;   /* pointer to TCP protocol control block */
    u_int           so_expire;   /* When the socket will expire */

    int             so_queued;   /* Number of packets queued from this socket */
    int             so_nqueued;  /* Number of packets queued in a row
                                  * Used to determine when to "downgrade" a session
                                  * from fastq to batchq */

    struct sbuf     so_rcv;      /* Receive buffer */
    struct sbuf     so_snd;      /* Send buffer */
#ifdef VBOX_WITH_SLIRP_MT
    RTCRITSECT      so_mutex;
    int             so_deleted;
#endif
#ifndef RT_OS_WINDOWS
    int so_poll_index;
#endif /* !RT_OS_WINDOWS */
    /*
     * FD_CLOSE/POLLHUP event has been occurred on socket
     */
    int so_close;

    void (* so_timeout)(PNATState pData, struct socket *so, void *arg);
    void *so_timeout_arg;

#ifdef VBOX_WITH_NAT_SERVICE
    /* storage of source ether address */
    unsigned char so_ethaddr[6];
#endif
    /* required for port-forwarding */
    struct libalias *so_la;
};

#ifdef VBOX_WITH_SLIRP_MT
# define SOCKET_LOCK(so)                                                \
    do {                                                                \
        int rc;                                                         \
        /* Assert(strcmp(RTThreadSelfName(), "EMT") != 0); */           \
        Log2(("lock:%s:%d L on %R[natsock]\n", __FUNCTION__, __LINE__, (so))); \
        Assert(!RTCritSectIsOwner(&(so)->so_mutex));                    \
        rc = RTCritSectEnter(&(so)->so_mutex);                          \
        AssertRC(rc);                                                   \
    } while (0)
# define SOCKET_UNLOCK(so)                                              \
    do {                                                                \
        int rc;                                                         \
        if ((so) != NULL) Log2(("lock:%s:%d U on %R[natsock]\n", __FUNCTION__, __LINE__, (so))); \
        rc = RTCritSectLeave(&(so)->so_mutex);                          \
        Assert(rc);                                                     \
    } while (0)
# define SOCKET_LOCK_CREATE(so)                                         \
    do {                                                                \
        int rc;                                                         \
        rc = RTCritSectInit(&(so)->so_mutex);                           \
        AssertRC(rc);                                                   \
    } while (0)
# define SOCKET_LOCK_DESTROY(so)                                        \
    do {                                                                \
        int rc = RTCritSectDelete(&(so)->so_mutex);                     \
        AssertRC(rc);                                                   \
    } while (0)
#else
# define SOCKET_LOCK(so) do {} while (0)
# define SOCKET_UNLOCK(so) do {} while (0)
# define SOCKET_LOCK_CREATE(so) do {} while (0)
# define SOCKET_LOCK_DESTROY(so) do {} while (0)
#endif
/*
 * Socket state bits. (peer means the host on the Internet,
 * local host means the host on the other end of the modem)
 */
#define SS_NOFDREF              0x001   /* No fd reference */

#define SS_ISFCONNECTING        0x002   /* Socket is connecting to peer (non-blocking connect()'s) */
#define SS_ISFCONNECTED         0x004   /* Socket is connected to peer */
#define SS_FCANTRCVMORE         0x008   /* Socket can't receive more from peer (for half-closes) */
#define SS_FCANTSENDMORE        0x010   /* Socket can't send more to peer (for half-closes) */
/* #define SS_ISFDISCONNECTED   0x020*/ /* Socket has disconnected from peer, in 2MSL state */
#define SS_FWDRAIN              0x040   /* We received a FIN, drain data and set SS_FCANTSENDMORE */

/* #define SS_CTL               0x080 */
#define SS_FACCEPTCONN          0x100   /* Socket is accepting connections from a host on the internet */
#define SS_FACCEPTONCE          0x200   /* If set, the SS_FACCEPTCONN socket will die after one accept */

extern struct socket tcb;

#if defined(DECLARE_IOVEC) && !defined(HAVE_READV)
struct iovec
{
    char *iov_base;
    size_t iov_len;
};
#endif

void so_init (void);
struct socket * solookup (struct socket *, struct in_addr, u_int, struct in_addr, u_int);
struct socket * socreate (void);
void sofree (PNATState, struct socket *);
#ifdef VBOX_WITH_SLIRP_MT
void soread_queue (PNATState, struct socket *, int *);
#endif
int soread (PNATState, struct socket *);
void sorecvoob (PNATState, struct socket *);
int sosendoob (struct socket *);
int sowrite (PNATState, struct socket *);
void sorecvfrom (PNATState, struct socket *);
int sosendto (PNATState, struct socket *, struct mbuf *);
struct socket * solisten (PNATState, u_int32_t, u_int, u_int32_t, u_int, int);
void sorwakeup (struct socket *);
void sowwakeup (struct socket *);
void soisfconnecting (register struct socket *);
void soisfconnected (register struct socket *);
void sofcantrcvmore (struct  socket *);
void sofcantsendmore (struct socket *);
void soisfdisconnected (struct socket *);
void sofwdrain (struct socket *);

#endif /* _SOCKET_H_ */
