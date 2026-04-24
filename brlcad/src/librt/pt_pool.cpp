/*                    P T _ P O O L . C P P
 * BRL-CAD
 *
 * Copyright (c) 1988-2026 United States Government as represented by
 * the U.S. Army Research Laboratory.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this file; see the file named COPYING for more
 * information.
 */
/** @addtogroup rt_partition
 * @brief Per-cpu partition freelist pool, owned by rt_i.
 *
 * Phase 7C of struct resource removal: the partition freelist that
 * was replaced with direct bu_malloc/bu_free in Phase 7 now lives
 * here, stored as a std::unordered_map<int,rt_pt_pool *> keyed by
 * CPU number and hung off rt_i_internal::rti_pt_pools (a void * for
 * C compatibility).  Users never see or manage pools directly; they
 * call GET_PT / FREE_PT / RT_FREE_PT_LIST which dispatch through
 * rt_pt_alloc / rt_pt_free / rt_pt_free_list.
 *
 * Unlike the segment pool there is no slab allocator: each struct
 * partition embeds a bu_ptbl (pt_seglist) whose backing store is
 * preserved across round-trips via bu_ptbl_reset(), so the key
 * allocation savings come from reusing both the partition struct and
 * its pt_seglist buffer.
 *
 * Lifetime rule: never switch ap->a_rt_i while a ray is in flight on
 * that application — the pool is tied to the rt_i, not the application.
 */

#include "common.h"

#include <unordered_map>

#include "vmath.h"
#include "raytrace.h"
#include "librt_private.h"


/* Convenience alias used throughout this file. */
typedef std::unordered_map<int, struct rt_pt_pool *> PtPoolMap;


/**
 * Create an empty pool map for a newly-created rt_i.
 * Called from rt_i_internal_create() in prep.cpp.
 */
void *
rt_pt_pool_map_create(void)
{
    return new PtPoolMap();
}


/**
 * Destroy all pools in the map and the map itself.
 * Called from rt_i_internal_destroy() in prep.cpp.
 *
 * Any partition still in flight (not yet returned to the pool) is
 * abandoned here.  Callers must ensure all rays have finished before
 * destroying the owning rt_i.
 */
void
rt_pt_pool_map_destroy(void *map_void)
{
    if (!map_void)
	return;

    PtPoolMap *m = static_cast<PtPoolMap *>(map_void);

    for (auto &kv : *m) {
	struct rt_pt_pool *pool = kv.second;
	if (!pool)
	    continue;

	/* Free every partition still sitting in the freelist. */
	while (!BU_LIST_IS_EMPTY(&pool->re_pt)) {
	    struct partition *pp = BU_LIST_FIRST(partition, &pool->re_pt);
	    BU_LIST_DEQUEUE((struct bu_list *)pp);
	    RT_CK_PARTITION(pp);
	    /* pt_overlap_reg was freed on return to pool; pt_seglist
	     * was reset (not freed) on return — free the backing store
	     * now. */
	    bu_ptbl_free(&pp->pt_seglist);
	    bu_free((void *)pp, "rt_pt_pool partition");
	}

	delete pool;
    }

    delete m;
}


/**
 * Look up (or lazily create) the partition pool for the given cpu in
 * the rt_i identified by rtip.
 */
static struct rt_pt_pool *
pt_pool_for_cpu(struct rt_i *rtip, int cpu)
{
    RT_CK_RTI(rtip);
    PtPoolMap *m = static_cast<PtPoolMap *>(rtip->i->rti_pt_pools);
    BU_ASSERT(m != nullptr);

    auto it = m->find(cpu);
    if (it != m->end())
	return it->second;

    /* First use for this cpu — allocate the pool. */
    struct rt_pt_pool *pool = new rt_pt_pool;
    BU_LIST_INIT(&pool->re_pt);
    pool->re_ptlen = pool->re_ptget = pool->re_ptfree = 0;
    (*m)[cpu] = pool;
    return pool;
}


/**
 * Pre-warm the pool for a specific cpu on rtip.  Called from
 * rt_init_resource() so the pool slot is registered before any
 * shooting begins.
 */
void
rt_pt_pool_init_cpu(struct rt_i *rtip, int cpu)
{
    if (!rtip)
	return;
    (void)pt_pool_for_cpu(rtip, cpu);
}


/* ------------------------------------------------------------------ */
/* Public API — declared in include/rt/ray_partition.h                */
/* ------------------------------------------------------------------ */

/**
 * Allocate one struct partition from the per-cpu pool owned by
 * ap->a_rt_i.  The partition's pt_magic is set to PT_MAGIC.
 * When taken from the pool, pt_overlap_reg is NULL and pt_seglist
 * has been reset (count=0, backing store preserved).
 */
struct partition *
rt_pt_alloc(struct application *ap)
{
    struct partition *pp;
    int cpu;
    struct rt_pt_pool *pool;

    RT_AP_CHECK(ap);
    RT_CK_RTI(ap->a_rt_i);

    cpu = ap->a_resource ? ap->a_cpu : 0;
    pool = pt_pool_for_cpu(ap->a_rt_i, cpu);

    if (!BU_LIST_IS_EMPTY(&pool->re_pt)) {
	pp = BU_LIST_FIRST(partition, &pool->re_pt);
	BU_LIST_DEQUEUE((struct bu_list *)pp);
	/* pt_overlap_reg freed and pt_seglist reset on return to pool. */
    } else {
	BU_ALLOC(pp, struct partition);
	bu_ptbl_init(&pp->pt_seglist, 42, "pt_seglist ptbl");
	pool->re_ptlen++;
    }

    pp->pt_magic = PT_MAGIC;
    pp->pt_forw  = pp->pt_back = NULL;
    pool->re_ptget++;
    return pp;
}


/**
 * Return one struct partition to the per-cpu pool owned by
 * ap->a_rt_i.  Frees pt_overlap_reg if non-NULL; resets pt_seglist
 * without freeing its backing store (for reuse on the next alloc).
 */
void
rt_pt_free(struct partition *pp, struct application *ap)
{
    int cpu;
    struct rt_pt_pool *pool;

    RT_CK_PARTITION(pp);
    RT_AP_CHECK(ap);
    RT_CK_RTI(ap->a_rt_i);

    cpu = ap->a_resource ? ap->a_cpu : 0;
    pool = pt_pool_for_cpu(ap->a_rt_i, cpu);

    if (pp->pt_overlap_reg) {
	bu_free((void *)pp->pt_overlap_reg, "pt_overlap_reg");
	pp->pt_overlap_reg = NULL;
    }
    bu_ptbl_reset(&pp->pt_seglist);

    BU_LIST_INSERT(&pool->re_pt, (struct bu_list *)pp);
    pool->re_ptfree++;
}


/**
 * Return all partitions on the list headed by headp to the pool owned
 * by ap->a_rt_i.  headp is the sentinel list head, not a real
 * partition node; it is left pointing to itself (empty list) on
 * return.
 */
void
rt_pt_free_list(struct partition *headp, struct application *ap)
{
    struct partition *pp, *zap;
    int cpu;
    struct rt_pt_pool *pool;

    RT_AP_CHECK(ap);
    RT_CK_RTI(ap->a_rt_i);

    cpu = ap->a_resource ? ap->a_cpu : 0;
    pool = pt_pool_for_cpu(ap->a_rt_i, cpu);

    for (pp = headp->pt_forw; pp != headp;) {
	zap = pp;
	pp = pp->pt_forw;
	BU_LIST_DEQUEUE((struct bu_list *)zap);
	RT_CK_PARTITION(zap);
	if (zap->pt_overlap_reg) {
	    bu_free((void *)zap->pt_overlap_reg, "pt_overlap_reg");
	    zap->pt_overlap_reg = NULL;
	}
	bu_ptbl_reset(&zap->pt_seglist);
	BU_LIST_INSERT(&pool->re_pt, (struct bu_list *)zap);
	pool->re_ptfree++;
    }
    headp->pt_forw = headp->pt_back = headp;
}


/*
 * Local Variables:
 * tab-width: 8
 * mode: C++
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
