/*                    S E G _ P O O L . C P P
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
/** @addtogroup rt_seg
 * @brief Per-cpu segment slab-allocator pool, owned by rt_i.
 *
 * Phase 7B of struct resource removal: the segment freelist that
 * previously lived in struct resource (re_seg / re_seg_blocks / ...)
 * now lives here, stored as a std::unordered_map<int,rt_seg_pool *>
 * keyed by CPU number and hung off rt_i_internal::rti_seg_pools (a
 * void * for C compatibility).  Users never see or manage pools
 * directly; they call RT_GET_SEG / RT_FREE_SEG / RT_FREE_SEG_LIST
 * which dispatch through rt_seg_alloc / rt_seg_free / rt_seg_free_list.
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
typedef std::unordered_map<int, struct rt_seg_pool *> SegPoolMap;


/**
 * Create an empty pool map for a newly-created rt_i.
 * Called from rt_i_internal_create() in prep.cpp.
 */
void *
rt_seg_pool_map_create(void)
{
    return new SegPoolMap();
}


/**
 * Destroy all pools in the map and the map itself.
 * Called from rt_i_internal_destroy() in prep.cpp.
 */
void
rt_seg_pool_map_destroy(void *map_void)
{
    if (!map_void)
	return;

    SegPoolMap *m = static_cast<SegPoolMap *>(map_void);

    for (auto &kv : *m) {
	struct rt_seg_pool *pool = kv.second;
	if (!pool)
	    continue;

	/* Free all slab blocks.  The individual struct seg nodes inside
	 * the slabs are abandoned (re_seg list is not walked); the blocks
	 * are the allocation unit. */
	BU_LIST_INIT(&pool->re_seg);   /* abandon individuals */
	if (BU_LIST_IS_INITIALIZED(&pool->re_seg_blocks.l)) {
	    struct seg **spp;
	    BU_CK_PTBL(&pool->re_seg_blocks);
	    for (BU_PTBL_FOR(spp, (struct seg **), &pool->re_seg_blocks)) {
		RT_CK_SEG(*spp);
		bu_free((void *)(*spp), "rt_seg_pool slab");
	    }
	    bu_ptbl_free(&pool->re_seg_blocks);
	}
	delete pool;
    }

    delete m;
}


/**
 * Look up (or lazily create) the seg pool for the given cpu in the
 * rt_i identified by rtip.  The fast path (pool already created) is
 * lock-free; the first-init path is serialized with RT_SEM_WORKER to
 * prevent concurrent map insertions.
 */
static struct rt_seg_pool *
seg_pool_for_cpu(struct rt_i *rtip, int cpu)
{
    RT_CK_RTI(rtip);
    SegPoolMap *m = static_cast<SegPoolMap *>(rtip->i->rti_seg_pools);
    BU_ASSERT(m != nullptr);

    /* Fast path: pool already initialized (no lock needed for read
     * since no concurrent writers once the entry exists). */
    auto it = m->find(cpu);
    if (it != m->end())
	return it->second;

    /* First use for this cpu — serialize pool creation. */
    bu_semaphore_acquire(RT_SEM_WORKER);
    /* Re-check under lock in case another thread raced us here. */
    it = m->find(cpu);
    if (it != m->end()) {
	bu_semaphore_release(RT_SEM_WORKER);
	return it->second;
    }
    struct rt_seg_pool *pool = new rt_seg_pool;
    BU_LIST_INIT(&pool->re_seg);
    bu_ptbl_init(&pool->re_seg_blocks, 64, "rt_seg_pool blocks");
    pool->re_seglen = pool->re_segget = pool->re_segfree = 0;
    (*m)[cpu] = pool;
    bu_semaphore_release(RT_SEM_WORKER);
    return pool;
}


/**
 * Pre-warm the pool for a specific cpu on rtip.  Called from
 * rt_init_resource() so the pool is ready before any ft_shot fires.
 */
void
rt_seg_pool_init_cpu(struct rt_i *rtip, int cpu)
{
    if (!rtip)
	return;
    (void)seg_pool_for_cpu(rtip, cpu);
}


/**
 * Fill pool->re_seg with a fresh slab of struct seg nodes.
 * Private to this file; exposed only for use from seg_pool_for_cpu.
 */
static void
alloc_seg_block(struct rt_seg_pool *pool)
{
    struct seg *sp;
    size_t bytes;

    BU_ASSERT(pool != nullptr);

    bytes = bu_malloc_len_roundup(64 * sizeof(struct seg));
    sp = (struct seg *)bu_malloc(bytes, "rt_seg_pool slab");
    bu_ptbl_ins(&pool->re_seg_blocks, (long *)sp);
    while (bytes >= sizeof(struct seg)) {
	sp->l.magic = RT_SEG_MAGIC;
	BU_LIST_INSERT(&pool->re_seg, &sp->l);
	pool->re_seglen++;
	sp++;
	bytes -= sizeof(struct seg);
    }
}


/* ------------------------------------------------------------------ */
/* Public API — declared in include/rt/seg.h                          */
/* ------------------------------------------------------------------ */

/**
 * Allocate one struct seg from the per-cpu pool owned by ap->a_rt_i.
 */
struct seg *
rt_seg_alloc(struct application *ap)
{
    struct seg *segp;
    int cpu;
    struct rt_seg_pool *pool;

    RT_AP_CHECK(ap);
    RT_CK_RTI(ap->a_rt_i);

    cpu = ap->a_cpu;
    pool = seg_pool_for_cpu(ap->a_rt_i, cpu);

    while (BU_LIST_IS_EMPTY(&pool->re_seg))
	alloc_seg_block(pool);

    segp = BU_LIST_FIRST(seg, &pool->re_seg);
    BU_LIST_DEQUEUE(&segp->l);
    segp->l.forw = segp->l.back = BU_LIST_NULL;
    segp->seg_in.hit_magic = segp->seg_out.hit_magic = RT_HIT_MAGIC;
    pool->re_segget++;
    return segp;
}


/**
 * Return one struct seg to the per-cpu pool owned by ap->a_rt_i.
 */
void
rt_seg_free(struct seg *segp, struct application *ap)
{
    int cpu;
    struct rt_seg_pool *pool;

    RT_CK_SEG(segp);
    RT_AP_CHECK(ap);
    RT_CK_RTI(ap->a_rt_i);

    cpu = ap->a_cpu;
    pool = seg_pool_for_cpu(ap->a_rt_i, cpu);

    BU_LIST_INSERT(&pool->re_seg, &segp->l);
    pool->re_segfree++;
}


/**
 * Return all segs on the list headed by seghead to ap->a_rt_i's pool.
 * seghead is the sentinel list head, not a real segment node.
 */
void
rt_seg_free_list(struct seg *seghead, struct application *ap)
{
    struct seg *segp;
    int cpu;
    struct rt_seg_pool *pool;

    RT_AP_CHECK(ap);
    RT_CK_RTI(ap->a_rt_i);

    cpu = ap->a_cpu;
    pool = seg_pool_for_cpu(ap->a_rt_i, cpu);

    while (BU_LIST_WHILE(segp, seg, &seghead->l)) {
	BU_LIST_DEQUEUE(&segp->l);
	RT_CK_SEG(segp);
	BU_LIST_INSERT(&pool->re_seg, &segp->l);
	pool->re_segfree++;
    }
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
