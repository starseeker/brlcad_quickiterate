/*                     R E S O U R C E . H
 * BRL-CAD
 *
 * Copyright (c) 1993-2026 United States Government as represented by
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
/** @addtogroup rt_resource
 * @brief
 * Per-CPU statistics and resources.
 */
/** @{ */
/** @file resource.h */

#ifndef RT_RESOURCE_H
#define RT_RESOURCE_H

#include "common.h"
#include "vmath.h"
#include "bu/list.h"
#include "bu/ptbl.h"
#include "rt/defines.h"
#include "rt/global.h" // for rt_uniresource
#include "rt/tree.h"
#include "rt/directory.h"

__BEGIN_DECLS

/**
 * One of these structures is needed per thread of execution, usually
 * with calling applications creating an array with at least MAX_PSW
 * elements.  To prevent excessive competition for free structures,
 * memory is now allocated on a per-processor basis.  The application
 * structure a_resource element specifies the resource structure to be
 * used; if uniprocessing, a null a_resource pointer results in using
 * the internal global structure (&rt_uniresource), making initial
 * application development simpler.
 *
 * Note that if multiple models are being used, the partition and bitv
 * structures (which are variable length) will require there to be
 * ncpus * nmodels resource structures, the selection of which will be
 * the responsibility of the application.
 *
 * Applications are responsible for calling rt_init_resource() on each
 * resource structure before letting LIBRT use them.
 *
 * Ray-shooting statistics are now accumulated directly on rt_i->stats
 * using C11 atomic operations; the former re_* stat fields have been
 * removed.  rt_add_res_stats() and rt_zero_res_stats() are deprecated
 * no-ops retained for source compatibility.
 *
 * Phase 5 removals: re_solid_bitv, re_region_ptbl, re_nmgfree,
 * re_tree_hd/get/malloc/free - these intermediate freelists are now
 * replaced with direct bu_malloc/bu_free calls.
 *
 * Phase 7 removals: re_seg, re_seg_blocks, re_seglen, re_segget,
 * re_segfree, re_parthead, re_partlen, re_partget, re_partfree -
 * the seg and partition freelists are replaced with direct
 * bu_malloc/bu_free; macros RT_GET_SEG/RT_FREE_SEG and GET_PT/FREE_PT
 * now accept but ignore the resource pointer.
 */
struct resource {
    uint32_t            re_magic;       /**< @brief  Magic number */
    int                 re_cpu;         /**< @brief  processor number, for ID */
    /* Former fields removed in earlier phases:
     *  Phase 4: re_randptr, re_boolstack, re_boolslen → a_randptr, a_boolstack, a_boolslen on struct application
     *  Phase 5: re_solid_bitv, re_region_ptbl, re_nmgfree, re_tree_hd/get/malloc/free → direct bu_malloc/bu_free
     *  Phase 6: re_ray_seqno, re_pieces, re_pieces_pending → struct rt_piecestate_set * a_pieces on struct application
     *  Phase 7: re_seg, re_seg_blocks, re_seglen/get/free,
     *           re_parthead, re_partlen/get/free → direct bu_malloc/bu_free
     * Statistics are accumulated on rt_i->stats (see rt_instance.h) using C11 atomics.
     */
    struct directory *  re_directory_hd;
    struct bu_ptbl      re_directory_blocks;    /**< @brief  Table of malloc'ed blocks */
};

#define RESOURCE_NULL   ((struct resource *)0)
#define RT_CK_RESOURCE(_p) BU_CKMAG(_p, RESOURCE_MAGIC, "struct resource")
#define RT_RESOURCE_INIT_ZERO { RESOURCE_MAGIC, 0, NULL, BU_PTBL_INIT_ZERO }

/**
 * Definition of global parallel-processing semaphores.
 *
 * res_syscall is now   BU_SEM_SYSCALL
 */
RT_EXPORT extern int RT_SEM_WORKER;
RT_EXPORT extern int RT_SEM_MODEL;
RT_EXPORT extern int RT_SEM_RESULTS;
RT_EXPORT extern int RT_SEM_TREE0;
RT_EXPORT extern int RT_SEM_TREE1;
RT_EXPORT extern int RT_SEM_TREE2;
RT_EXPORT extern int RT_SEM_TREE3;


__END_DECLS

#endif /* RT_RESOURCE_H */
/** @} */
/*
 * Local Variables:
 * tab-width: 8
 * mode: C
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
