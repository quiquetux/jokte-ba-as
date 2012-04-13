/* $Id: misc.c $ */
/** @file
 * NAT - helpers.
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

#define WANT_SYS_IOCTL_H
#include <slirp.h>

#ifndef HAVE_INET_ATON
int
inet_aton(const char *cp, struct in_addr *ia)
{
    u_int32_t addr = inet_addr(cp);
    if (addr == 0xffffffff)
        return 0;
    ia->s_addr = addr;
    return 1;
}
#endif

/*
 * Get our IP address and put it in our_addr
 */
void
getouraddr(PNATState pData)
{
    our_addr.s_addr = loopback_addr.s_addr;
}

struct quehead
{
    struct quehead *qh_link;
    struct quehead *qh_rlink;
};

void
insque(PNATState pData, void *a, void *b)
{
    register struct quehead *element = (struct quehead *) a;
    register struct quehead *head = (struct quehead *) b;
    element->qh_link = head->qh_link;
    head->qh_link = (struct quehead *)element;
    element->qh_rlink = (struct quehead *)head;
    ((struct quehead *)(element->qh_link))->qh_rlink = (struct quehead *)element;
}

void
remque(PNATState pData, void *a)
{
    register struct quehead *element = (struct quehead *) a;
    ((struct quehead *)(element->qh_link))->qh_rlink = element->qh_rlink;
    ((struct quehead *)(element->qh_rlink))->qh_link = element->qh_link;
    element->qh_rlink = NULL;
    /*  element->qh_link = NULL;  TCP FIN1 crashes if you do this.  Why ? */
}


/*
 * Set fd blocking and non-blocking
 */
void
fd_nonblock(int fd)
{
#ifdef FIONBIO
    int opt = 1;

    ioctlsocket(fd, FIONBIO, &opt);
#else
    int opt;

    opt = fcntl(fd, F_GETFL, 0);
    opt |= O_NONBLOCK;
    fcntl(fd, F_SETFL, opt);
#endif
}


#define ITEM_MAGIC 0xdead0001
struct item
{
    uint32_t magic;
    uma_zone_t zone;
    uint32_t ref_count;
    LIST_ENTRY(item) list;
};

#define ZONE_MAGIC 0xdead0002
struct uma_zone
{
    uint32_t magic;
    PNATState pData; /* to minimize changes in the rest of UMA emulation code */
    RTCRITSECT csZone;
    const char *name;
    size_t size; /* item size */
    ctor_t pfCtor;
    dtor_t pfDtor;
    zinit_t pfInit;
    zfini_t pfFini;
    uma_alloc_t pfAlloc;
    uma_free_t pfFree;
    int max_items;
    int cur_items;
    LIST_HEAD(RT_NOTHING, item) used_items;
    LIST_HEAD(RT_NOTHING, item) free_items;
    uma_zone_t master_zone;
    void *area;
    /** Needs call pfnXmitPending when memory becomes available if @c true.
     * @remarks Only applies to the master zone (master_zone == NULL) */
    bool fDoXmitPending;
};


/**
 * Called when memory becomes available, works pfnXmitPending.
 *
 * @note    This will LEAVE the critical section of the zone and RE-ENTER it
 *          again.  Changes to the zone data should be expected across calls to
 *          this function!
 *
 * @param   zone        The zone.
 */
DECLINLINE(void) slirp_zone_check_and_send_pending(uma_zone_t zone)
{
    if (   zone->fDoXmitPending
        && zone->master_zone == NULL)
    {
        int rc2;
        zone->fDoXmitPending = false;
        rc2 = RTCritSectLeave(&zone->csZone); AssertRC(rc2);

        slirp_output_pending(zone->pData->pvUser);

        rc2 = RTCritSectEnter(&zone->csZone); AssertRC(rc2);
    }
}

static void *slirp_uma_alloc(uma_zone_t zone,
                             int size, uint8_t *pflags, int fWait)
{
    struct item *it;
    uint8_t *sub_area;
    void *ret = NULL;
    int rc;

    RTCritSectEnter(&zone->csZone);
    for (;;)
    {
        if (!LIST_EMPTY(&zone->free_items))
        {
            it = LIST_FIRST(&zone->free_items);
            Assert(it->magic == ITEM_MAGIC);
            rc = 0;
            if (zone->pfInit)
                rc = zone->pfInit(zone->pData, (void *)&it[1], zone->size, M_DONTWAIT);
            if (rc == 0)
            {
                zone->cur_items++;
                LIST_REMOVE(it, list);
                LIST_INSERT_HEAD(&zone->used_items, it, list);
                slirp_zone_check_and_send_pending(zone); /* may exit+enter the cs! */
                ret = (void *)&it[1];
            }
            else
            {
                AssertMsgFailed(("NAT: item initialization failed for zone %s\n", zone->name));
                ret = NULL;
            }
            break;
        }

        if (!zone->master_zone)
        {
            /* We're on the master zone and we can't allocate more. */
            Log2(("NAT: no room on %s zone\n", zone->name));
            /* AssertMsgFailed(("NAT: OOM!")); */
            zone->fDoXmitPending = true;
            break;
        }

        /* we're on a sub-zone, we need get a chunk from the master zone and split
         * it into sub-zone conforming chunks.
         */
        sub_area = slirp_uma_alloc(zone->master_zone, zone->master_zone->size, NULL, 0);
        if (!sub_area)
        {
            /* No room on master */
            Log2(("NAT: no room on %s zone for %s zone\n", zone->master_zone->name, zone->name));
            break;
        }
        zone->max_items++;
        it = &((struct item *)sub_area)[-1];
        /* It's the chunk descriptor of the master zone, we should remove it
         * from the master list first.
         */
        Assert((it->zone && it->zone->magic == ZONE_MAGIC));
        RTCritSectEnter(&it->zone->csZone);
        /** @todo should we alter count of master counters? */
        LIST_REMOVE(it, list);
        RTCritSectLeave(&it->zone->csZone);

        /** @todo '+ zone->size' should be depend on flag */
        memset(it, 0, sizeof(struct item));
        it->zone = zone;
        it->magic = ITEM_MAGIC;
        LIST_INSERT_HEAD(&zone->free_items, it, list);
        if (zone->cur_items >= zone->max_items)
            LogRel(("NAT: zone(%s) has reached it maximum\n", zone->name));
    }
    RTCritSectLeave(&zone->csZone);
    return ret;
}

static void slirp_uma_free(void *item, int size, uint8_t flags)
{
    struct item *it;
    uma_zone_t zone;
    uma_zone_t master_zone;

    Assert(item);
    it = &((struct item *)item)[-1];
    Assert(it->magic == ITEM_MAGIC);
    zone = it->zone;
    /* check border magic */
    Assert((*(uint32_t *)(((uint8_t *)&it[1]) + zone->size) == 0xabadbabe));

    RTCritSectEnter(&zone->csZone);
    Assert(zone->magic == ZONE_MAGIC);
    LIST_REMOVE(it, list);
    if (zone->pfFini)
    {
        zone->pfFini(zone->pData, item, zone->size);
    }
    if (zone->pfDtor)
    {
        zone->pfDtor(zone->pData, item, zone->size, NULL);
    }
    LIST_INSERT_HEAD(&zone->free_items, it, list);
    zone->cur_items--;
    slirp_zone_check_and_send_pending(zone); /* may exit+enter the cs! */
    RTCritSectLeave(&zone->csZone);
}

uma_zone_t uma_zcreate(PNATState pData, char *name, size_t size,
                       ctor_t ctor, dtor_t dtor, zinit_t init, zfini_t fini, int flags1, int flags2)
{
    uma_zone_t zone = RTMemAllocZ(sizeof(struct uma_zone));
    Assert((pData));
    zone->magic = ZONE_MAGIC;
    zone->pData = pData;
    zone->name = name;
    zone->size = size;
    zone->pfCtor = ctor;
    zone->pfDtor = dtor;
    zone->pfInit = init;
    zone->pfFini = fini;
    zone->pfAlloc = slirp_uma_alloc;
    zone->pfFree = slirp_uma_free;
    RTCritSectInit(&zone->csZone);
    return zone;

}
uma_zone_t uma_zsecond_create(char *name, ctor_t ctor,
    dtor_t dtor, zinit_t init, zfini_t fini, uma_zone_t master)
{
    uma_zone_t zone;
    Assert(master);
    zone = RTMemAllocZ(sizeof(struct uma_zone));
    if (zone == NULL)
        return NULL;

    Assert((master && master->pData));
    zone->magic = ZONE_MAGIC;
    zone->pData = master->pData;
    zone->name = name;
    zone->pfCtor = ctor;
    zone->pfDtor = dtor;
    zone->pfInit = init;
    zone->pfFini = fini;
    zone->pfAlloc = slirp_uma_alloc;
    zone->pfFree = slirp_uma_free;
    zone->size = master->size;
    zone->master_zone = master;
    RTCritSectInit(&zone->csZone);
    return zone;
}

void uma_zone_set_max(uma_zone_t zone, int max)
{
    int i = 0;
    struct item *it;
    zone->max_items = max;
    zone->area = RTMemAllocZ(max * (sizeof(struct item) + zone->size + sizeof(uint32_t)));
    for (; i < max; ++i)
    {
        it = (struct item *)(((uint8_t *)zone->area) + i*(sizeof(struct item) + zone->size + sizeof(uint32_t)));
        it->magic = ITEM_MAGIC;
        it->zone = zone;
        *(uint32_t *)(((uint8_t *)&it[1]) + zone->size) = 0xabadbabe;
        LIST_INSERT_HEAD(&zone->free_items, it, list);
    }

}

void uma_zone_set_allocf(uma_zone_t zone, uma_alloc_t pfAlloc)
{
   zone->pfAlloc = pfAlloc;
}

void uma_zone_set_freef(uma_zone_t zone, uma_free_t pfFree)
{
   zone->pfFree = pfFree;
}

uint32_t *uma_find_refcnt(uma_zone_t zone, void *mem)
{
    /** @todo (vvl) this function supposed to work with special zone storing
    reference counters */
    struct item *it = (struct item *)mem; /* 1st element */
    Assert(mem != NULL);
    Assert(zone->magic == ZONE_MAGIC);
    /* for returning pointer to counter we need get 0 elemnt */
    Assert(it[-1].magic == ITEM_MAGIC);
    return &it[-1].ref_count;
}

void *uma_zalloc_arg(uma_zone_t zone, void *args, int how)
{
    void *mem;
    Assert(zone->magic == ZONE_MAGIC);
    if (zone->pfAlloc == NULL)
        return NULL;
    RTCritSectEnter(&zone->csZone);
    mem = zone->pfAlloc(zone, zone->size, NULL, 0);
    if (mem != NULL)
    {
        if (zone->pfCtor)
            zone->pfCtor(zone->pData, mem, zone->size, args, M_DONTWAIT);
    }
    RTCritSectLeave(&zone->csZone);
    return mem;
}

void uma_zfree(uma_zone_t zone, void *item)
{
    uma_zfree_arg(zone, item, NULL);
}

void uma_zfree_arg(uma_zone_t zone, void *mem, void *flags)
{
    struct item *it;
    Assert(zone->magic == ZONE_MAGIC);
    Assert((zone->pfFree));
    Assert((mem));

    RTCritSectEnter(&zone->csZone);
    it = &((struct item *)mem)[-1];
    Assert((it->magic == ITEM_MAGIC));
    Assert((zone->magic == ZONE_MAGIC && zone == it->zone));

    zone->pfFree(mem,  0, 0);
    RTCritSectLeave(&zone->csZone);
}

int uma_zone_exhausted_nolock(uma_zone_t zone)
{
    int fExhausted;
    RTCritSectEnter(&zone->csZone);
    fExhausted = (zone->cur_items == zone->max_items);
    RTCritSectLeave(&zone->csZone);
    return fExhausted;
}

void zone_drain(uma_zone_t zone)
{
    struct item *it;
    uma_zone_t master_zone;

    /* vvl: Huh? What to do with zone which hasn't got backstore ? */
    Assert((zone->master_zone));
    master_zone = zone->master_zone;
    while (!LIST_EMPTY(&zone->free_items))
    {
        it = LIST_FIRST(&zone->free_items);
        Assert((it->magic == ITEM_MAGIC));

        RTCritSectEnter(&zone->csZone);
        LIST_REMOVE(it, list);
        zone->max_items--;
        RTCritSectLeave(&zone->csZone);

        it->zone = master_zone;

        RTCritSectEnter(&master_zone->csZone);
        LIST_INSERT_HEAD(&master_zone->free_items, it, list);
        master_zone->cur_items--;
        slirp_zone_check_and_send_pending(master_zone); /* may exit+enter the cs! */
        RTCritSectLeave(&master_zone->csZone);
    }
}

void slirp_null_arg_free(void *mem, void *arg)
{
    /** @todo (vvl) make it wiser  */
    Assert(mem);
    RTMemFree(mem);
}

void *uma_zalloc(uma_zone_t zone, int len)
{
    return NULL;
}

struct mbuf *slirp_ext_m_get(PNATState pData, size_t cbMin, void **ppvBuf, size_t *pcbBuf)
{
    struct mbuf *m;
    size_t size = MCLBYTES;
    if (cbMin < MSIZE)
        size = MCLBYTES;
    else if (cbMin < MCLBYTES)
        size = MCLBYTES;
    else if (cbMin < MJUM9BYTES)
        size = MJUM9BYTES;
    else if (cbMin < MJUM16BYTES)
        size = MJUM16BYTES;
    else
        AssertMsgFailed(("Unsupported size"));

    m = m_getjcl(pData, M_NOWAIT, MT_HEADER, M_PKTHDR, size);
    if (m == NULL)
    {
        *ppvBuf = NULL;
        *pcbBuf = 0;
        return NULL;
    }
    m->m_len = size;
    *ppvBuf = mtod(m, void *);
    *pcbBuf = size;
    return m;
}

void slirp_ext_m_free(PNATState pData, struct mbuf *m, uint8_t *pu8Buf)
{

    if (   !pu8Buf
        && pu8Buf != mtod(m, uint8_t *))
        RTMemFree(pu8Buf); /* This buffer was allocated on heap */
    m_freem(pData, m);
}

static void zone_destroy(uma_zone_t zone)
{
    RTCritSectEnter(&zone->csZone);
    LogRel(("NAT: zone(nm:%s, used:%d)\n", zone->name, zone->cur_items));
    if (zone->master_zone)
        RTMemFree(zone->area);
    RTCritSectLeave(&zone->csZone);
    RTCritSectDelete(&zone->csZone);
    RTMemFree(zone);
}

void m_fini(PNATState pData)
{
    zone_destroy(pData->zone_mbuf);
    zone_destroy(pData->zone_clust);
    zone_destroy(pData->zone_pack);
    zone_destroy(pData->zone_jumbop);
    zone_destroy(pData->zone_jumbo9);
    zone_destroy(pData->zone_jumbo16);
    /** @todo do finalize here.*/
}

void
if_init(PNATState pData)
{
    /* 14 for ethernet */
    if_maxlinkhdr = 14;
    if_comp = IF_AUTOCOMP;
    if_mtu = 1500;
    if_mru = 1500;
}
