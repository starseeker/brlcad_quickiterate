
/********************************************************************************
  pbrt source code is Copyright(c) 1998-2015
  Matt Pharr, Greg Humphreys, and Wenzel Jakob.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are
  met:
  - Redistributions of source code must retain the above copyright notice, this
    list of conditions and the following disclaimer.
  - Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.  THIS SOFTWARE IS
    PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS
    OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
    LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
    CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
    SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
    CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
    ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.

  Derived from src/accelerators/bvh.cpp
  within the pbrt-v3 project, https://github.com/mmp/pbrt-v3/

  Direct browse link:
  https://github.com/mmp/pbrt-v3/blob/master/src/accelerators/bvh.cpp

  Implements the HLBVH construction algorithm as in:
  "Simpler and Faster HLBVH with Work Queues" by
  Kirill Garanzha, Jacopo Pantaleoni, David McAllister.
  (Proc. High Performance Graphics 2011, pg 59)

*********************************************************************************/

  #include "common.h"

#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "bio.h"

#include "bu/parallel.h"
#include "bu/malloc.h"
#include "bu/sort.h"
#include "vmath.h"
#include "raytrace.h"
#include "bg/plane.h"
#include "bv/plot3.h"

#define HLBVH_IMPLEMENTATION
#include "cut_hlbvh.h"

#define HLBVH_STACK_SIZE 256




static void
bvh_bounds_union(fastf_t a[6], const fastf_t b[6], const fastf_t c[6])
{
    VMOVE(&a[0], &b[0]);
    VMIN(&a[0], &c[0]);
    VMOVE(&a[3], &b[3]);
    VMAX(&a[3], &c[3]);
}

static void
init_leaf(struct bvh_build_node *node, long first, long n, const fastf_t b[6])
{
    node->first_prim_offset = first;
    node->n_primitives = n;
    VMOVE(&node->bounds[0], &b[0]);
    VMOVE(&node->bounds[3], &b[3]);
    node->children[0] = node->children[1] = NULL;
}

static void
init_interior(struct bvh_build_node *node, uint8_t axis, struct bvh_build_node *c0, struct bvh_build_node *c1)
{
    node->children[0] = c0;
    node->children[1] = c1;
    bvh_bounds_union(node->bounds, c0->bounds, c1->bounds);
    node->split_axis = axis;
    node->n_primitives = 0;
}

struct bu_pool *
hlbvh_init_pool(size_t n_primitives) {
    /*
     * The pool must be large enough for the entire build tree; it stores
     * internal pointers and a realloc would invalidate them.
     *
     * A binary SAH-BVH over N primitives has at most 2*N - 1 nodes.
     */
    return bu_pool_create(sizeof(struct bvh_build_node) * (2 * n_primitives + 1));
}


static inline fastf_t
surface_area(const fastf_t b[6])
{
    vect_t d;
    VSUB2(d, &b[3], &b[0]);
    return (2.0 * (d[X] * d[Y] + d[X] * d[Z] + d[Y] * d[Z]));
}

static inline uint8_t
maximum_extent(const fastf_t b[6])
{
    vect_t d;
    VSUB2(d, &b[3], &b[0]);
    if (d[X] > d[Y] && d[X] > d[Z])
	return 0;
    else if (d[Y] > d[Z])
	return 1;
    else
	return 2;
}


/* Number of candidate split planes evaluated per axis by the SAH-BVH build.
 * The N split planes divide the centroid AABB into N+1 equal-width buckets.
 */
#define SAH_N_BINS 32

/*
 * Recursive binned-SAH BVH builder.
 *
 * pindices[0..n-1] are indices into the global centroids_prims / bounds_prims
 * arrays and are partitioned in-place.  On return the pindices order matches
 * the order primitives are written into ordered_prims (left sub-tree first,
 * then right sub-tree, depth-first).
 */
static struct bvh_build_node *
sahbvh_recursive(long max_prims_in_node,
		 struct bu_pool *pool,
		 const fastf_t *centroids_prims, /* n_prims*3 */
		 const fastf_t *bounds_prims,    /* n_prims*6 */
		 long *pindices,                 /* [0..n-1] */
		 long n,
		 long *total_nodes,
		 long *ordered_prims,
		 long *ord_offset)
{
    struct bvh_build_node *node;
    fastf_t nb[6];   /* node AABB */
    fastf_t cb[6];   /* centroid AABB */
    long i;
    uint8_t best_axis;
    long best_split_bin;
    fastf_t best_cost;
    fastf_t node_sa;
    uint8_t axis;
    long mid;

    /* ---- compute node AABB ------------------------------------------ */
    nb[0] = nb[1] = nb[2] =  MAX_FASTF;
    nb[3] = nb[4] = nb[5] = -MAX_FASTF;
    for (i = 0; i < n; i++) {
	long idx = pindices[i];
	VMIN(&nb[0], &bounds_prims[idx*6+0]);
	VMAX(&nb[3], &bounds_prims[idx*6+3]);
    }

    ++*total_nodes;
    node = (struct bvh_build_node *)bu_pool_alloc(pool, 1, sizeof(*node));

    /* ---- leaf? -------------------------------------------------------- */
    if (n <= max_prims_in_node) {
	long first = *ord_offset;
	*ord_offset += n;
	for (i = 0; i < n; i++)
	    ordered_prims[first + i] = pindices[i];
	init_leaf(node, first, n, nb);
	return node;
    }

    /* ---- centroid AABB ----------------------------------------------- */
    cb[0] = cb[1] = cb[2] =  MAX_FASTF;
    cb[3] = cb[4] = cb[5] = -MAX_FASTF;
    for (i = 0; i < n; i++) {
	long idx = pindices[i];
	VMIN(&cb[0], &centroids_prims[idx*3]);
	VMAX(&cb[3], &centroids_prims[idx*3]);
    }

    /* ---- binned SAH over all 3 axes ----------------------------------- */
    best_axis = maximum_extent(cb);
    best_split_bin = -1;
    best_cost = (fastf_t)n;  /* leaf cost: n * 1.0 (intersection cost = 1) */
    node_sa = surface_area(nb);

    for (axis = 0; axis < 3; axis++) {
	fastf_t ext = cb[3+axis] - cb[0+axis];
	long k;

	if (ZERO(ext)) continue;

	/*
	 * Bbox-overlap SAH: for each of SAH_N_BINS-1 candidate split planes,
	 * classify every primitive by whether its bounding box overlaps the
	 * left half (bbox_min[axis] < split_pos) or the right half
	 * (bbox_max[axis] > split_pos).  Straddling primitives are counted in
	 * both children and contribute to both tight bboxes.
	 *
	 * This correctly penalises splits where many primitives straddle the
	 * plane — the resulting left_cnt and right_cnt are both inflated,
	 * pushing the SAH cost well above the leaf cost and causing the
	 * algorithm to reject pathological splits.  Centroid-only counting
	 * assigns each primitive to exactly one bin and misses the straddling
	 * cost entirely, leading to catastrophic trees for scenes with large
	 * overlapping primitives.
	 *
	 * Complexity: O(N * SAH_N_BINS) per axis per node.
	 */
	for (k = 0; k < SAH_N_BINS - 1; k++) {
	    fastf_t split_pos = cb[0+axis] + (k + 1) * ext / SAH_N_BINS;
	    fastf_t lb[6], rb[6];
	    long lc = 0, rc = 0;
	    fastf_t cost;

	    lb[0] = lb[1] = lb[2] =  MAX_FASTF;
	    lb[3] = lb[4] = lb[5] = -MAX_FASTF;
	    rb[0] = rb[1] = rb[2] =  MAX_FASTF;
	    rb[3] = rb[4] = rb[5] = -MAX_FASTF;

	    for (i = 0; i < n; i++) {
		long idx = pindices[i];
		fastf_t p_min = bounds_prims[idx*6 + axis];
		fastf_t p_max = bounds_prims[idx*6 + 3 + axis];
		if (p_min < split_pos) {
		    VMIN(&lb[0], &bounds_prims[idx*6 + 0]);
		    VMAX(&lb[3], &bounds_prims[idx*6 + 3]);
		    lc++;
		}
		if (p_max > split_pos) {
		    VMIN(&rb[0], &bounds_prims[idx*6 + 0]);
		    VMAX(&rb[3], &bounds_prims[idx*6 + 3]);
		    rc++;
		}
	    }

	    if (!lc || !rc) continue;
	    cost = 0.125 + (lc * surface_area(lb) + rc * surface_area(rb)) / node_sa;
	    if (cost < best_cost) {
		best_cost      = cost;
		best_axis      = axis;
		best_split_bin = k;
	    }
	}
    }

    /* ---- no split found? (all centroids coincide on every axis) ------ */
    if (best_split_bin < 0) {
	if (n <= 2 * max_prims_in_node) {
	    /* make an oversized leaf rather than an infinite recursion */
	    long first = *ord_offset;
	    *ord_offset += n;
	    for (i = 0; i < n; i++)
		ordered_prims[first + i] = pindices[i];
	    init_leaf(node, first, n, nb);
	    return node;
	}
	/* force equal split so recursion terminates */
	mid = n / 2;
	{
	    struct bvh_build_node *lc_node, *rc_node;
	    lc_node = sahbvh_recursive(max_prims_in_node, pool,
		    centroids_prims, bounds_prims,
		    pindices, mid,
		    total_nodes, ordered_prims, ord_offset);
	    rc_node = sahbvh_recursive(max_prims_in_node, pool,
		    centroids_prims, bounds_prims,
		    pindices + mid, n - mid,
		    total_nodes, ordered_prims, ord_offset);
	    init_interior(node, best_axis, lc_node, rc_node);
	}
	return node;
    }

    /* ---- in-place partition ------------------------------------------ */
    {
	fastf_t ext = cb[3+best_axis] - cb[0+best_axis];
	fastf_t inv_ext = 1.0 / ext;
	long lo = 0, hi = n - 1;
	while (lo <= hi) {
	    long idx = pindices[lo];
	    long b = (long)(SAH_N_BINS * (centroids_prims[idx*3+best_axis] - cb[0+best_axis]) * inv_ext);
	    if (b >= SAH_N_BINS) b = SAH_N_BINS - 1;
	    if (b <= best_split_bin) {
		lo++;
	    } else {
		long tmp = pindices[lo];
		pindices[lo] = pindices[hi];
		pindices[hi] = tmp;
		hi--;
	    }
	}
	mid = lo;
	/* guard against degenerate partition (fp rounding edge case) */
	if (mid == 0 || mid == n)
	    mid = n / 2;
    }

    /* ---- recurse ----------------------------------------------------- */
    {
	struct bvh_build_node *lc_node, *rc_node;
	lc_node = sahbvh_recursive(max_prims_in_node, pool,
		centroids_prims, bounds_prims,
		pindices, mid,
		total_nodes, ordered_prims, ord_offset);
	rc_node = sahbvh_recursive(max_prims_in_node, pool,
		centroids_prims, bounds_prims,
		pindices + mid, n - mid,
		total_nodes, ordered_prims, ord_offset);
	init_interior(node, best_axis, lc_node, rc_node);
    }
    return node;
}


struct bvh_build_node *
hlbvh_create(long max_prims_in_node, struct bu_pool *pool, const fastf_t *centroids_prims,
	const fastf_t *bounds_prims, long *total_nodes,
	const long n_primitives, long **ordered_prims)
{
    long i;
    long *pindices;
    struct bvh_build_node *root;
    long ord_offset = 0;

    if (n_primitives == 0) {
	*ordered_prims = NULL;
	*total_nodes = 0;
	return NULL;
    }

    /* Working index array: pindices[i] = original index of the i-th prim. */
    pindices = (long *)bu_malloc((size_t)n_primitives * sizeof(long), "sah prim indices");
    for (i = 0; i < n_primitives; i++)
	pindices[i] = i;

    *ordered_prims = (long *)bu_calloc((size_t)n_primitives, sizeof(long), "sah ordered prims");
    *total_nodes = 0;

    root = sahbvh_recursive(max_prims_in_node, pool,
	    centroids_prims, bounds_prims,
	    pindices, n_primitives,
	    total_nodes, *ordered_prims, &ord_offset);

    bu_free(pindices, "sah prim indices");
    return root;
}


struct bvh_flat_node *
flatten_bvh_tree_recursive(int *next_unused, struct bvh_flat_node *flat_nodes, long total_nodes,
	const struct bvh_build_node *node, long depth)
{
    int my_offset = *next_unused;
    struct bvh_flat_node *linear_node;

    BU_ASSERT(my_offset < total_nodes);
    ++*next_unused;
    linear_node = &flat_nodes[my_offset];

    VMOVE(&linear_node->bounds[0], &node->bounds[0]);
    VMOVE(&linear_node->bounds[3], &node->bounds[3]);
    if (node->n_primitives > 0) {
	BU_ASSERT(!node->children[0] && !node->children[1]);
	BU_ASSERT(node->n_primitives < 65536);
	linear_node->data.first_prim_offset = node->first_prim_offset;
	linear_node->n_primitives = node->n_primitives;
    } else {
	/* Create interior flattened BVH node */
	// We don't copy the axis because that isn't used in the traversal.
	// If it is used in the traversal, then bvh_flat_node.n_primitives
	// should be resized to a ushort and the axis put in the remaining
	// space
	linear_node->n_primitives = 0;
	flatten_bvh_tree_recursive(next_unused, flat_nodes, total_nodes, node->children[0], depth + 1);
	linear_node->data.other_child =
	    flatten_bvh_tree_recursive(next_unused, flat_nodes, total_nodes, node->children[1], depth + 1);
    }
    return linear_node;
}


struct bvh_flat_node *
hlbvh_flatten(const struct bvh_build_node *root, long nodes_created)
{
    struct bvh_flat_node *new_root = (struct bvh_flat_node *) bu_malloc(nodes_created * sizeof(struct bvh_flat_node), "bvh flat nodes");
    int next_unused = 0;
    return flatten_bvh_tree_recursive(&next_unused, new_root, nodes_created, root, 0);
}


struct prim_list {
    struct bu_list l;
    long first_prim_offset, n_primitives;
};

void
while_populate_leaf_list_raw(struct bvh_build_node *root, struct xray* rp, struct prim_list* leafs, size_t* prims_so_far)
{
    // For maximum speed, move this code out and specialize
    // An example can be seen in src/librt/primitives/bot/bot.c:bot_shot_hlbvh()
    struct bvh_build_node *stack_node[HLBVH_STACK_SIZE];
    unsigned char stack_child_index[HLBVH_STACK_SIZE];
    int stack_ind = 0;
    stack_node[stack_ind] = root;
    stack_child_index[stack_ind] = 0;
    vect_t inverse_r_dir;
    VINVDIR(inverse_r_dir, rp->r_dir);

    while (stack_ind >= 0) {
	if (UNLIKELY(stack_ind >= HLBVH_STACK_SIZE)) {
	    // This should only ever happen if the BVH tree that was
	    // built had a depth greater than the defined stack size.
	    // Even if a BVH is built degenerately and has an average
	    // splitting factor of 1.2 (we expect this to be close to
	    // 2), then this stack should support >10^20 triangles
	    // There is a recursive function in cut_hlbvh that we use
	    // to flatten the tree, it has a depth parameter, and it
	    // does a similar traversal to the one here. (It would
	    // probably be good for debugging) - Apr 2024
	    bu_bomb("Stack size exceeded in hlbvh shot");
	}
	if (stack_child_index[stack_ind] >= 2) {
	    stack_ind--;
	    continue;
	}
	struct bvh_build_node* node = stack_node[stack_ind];
	// check bounds if it's the first time in this node
	if (!stack_child_index[stack_ind]) {
	    // TODO: do we want to handle NaNs correctly?
	    point_t lows_t, highs_t, low_ts, high_ts;

	    VSUB2( lows_t, &node->bounds[0], rp->r_pt);
	    VSUB2(highs_t, &node->bounds[3], rp->r_pt);

	    VELMUL( lows_t,  lows_t, inverse_r_dir);
	    VELMUL(highs_t, highs_t, inverse_r_dir);

	    VMOVE( low_ts, lows_t);
	    VMOVE(high_ts, lows_t);
	    VMINMAX(low_ts, high_ts, highs_t);

	    fastf_t high_t = FMIN(high_ts[0], FMIN(high_ts[1], high_ts[2]));
	    fastf_t  low_t = FMAX( low_ts[0], FMAX( low_ts[1],  low_ts[2]));
	    if ((high_t < -1.0) || (low_t > high_t)) {
		stack_ind--;
		continue;
	    }
	}
	if (node->n_primitives > 0) {
	    BU_ASSERT(node->children[0] == NULL && node->children[1] == NULL);
	    // add the leaf values into a list
	    struct prim_list* entry;
	    BU_GET(entry, struct prim_list);
	    entry->n_primitives = node->n_primitives;
	    entry->first_prim_offset = node->first_prim_offset;
	    BU_LIST_PUSH(&(leafs->l), &(entry->l));
	    *prims_so_far += node->n_primitives;
	    stack_ind--;
	    continue;
	}
	// we hit the bounds and are not in a leaf
	// so we do the next child of this node
	stack_node[stack_ind+1] = node->children[stack_child_index[stack_ind]];
	stack_child_index[stack_ind] += 1;
	stack_child_index[stack_ind+1] = 0;
	stack_ind++;
    }
}


void
while_populate_leaf_list_flat(struct bvh_flat_node *root, struct xray* rp, struct prim_list* leafs, size_t* prims_so_far)
{
    // For maximum speed, move this code out and specialize
    // An example can be seen in src/librt/primitives/bot/bot.c:bot_shot_hlbvh()
    struct bvh_flat_node *stack_node[HLBVH_STACK_SIZE];
    unsigned char stack_child_index[HLBVH_STACK_SIZE];
    int stack_ind = 0;
    stack_node[stack_ind] = root;
    stack_child_index[stack_ind] = 0;
    vect_t inverse_r_dir;
    VINVDIR(inverse_r_dir, rp->r_dir);

    while (stack_ind >= 0) {
	if (UNLIKELY(stack_ind >= HLBVH_STACK_SIZE)) {
	    // This should only ever happen if the BVH tree that was
	    // built had a depth greater than the defined stack size.
	    // Even if a BVH is built degenerately and has an average
	    // splitting factor of 1.2 (we expect this to be close to
	    // 2), then this stack should support >10^20 triangles
	    // There is a recursive function in cut_hlbvh that we use
	    // to flatten the tree, it has a depth parameter, and it
	    // does a similar traversal to the one here. (It would
	    // probably be good for debugging) - Apr 2024
	    bu_bomb("Stack size exceeded in hlbvh shot");
	}
	if (stack_child_index[stack_ind] >= 2) {
	    stack_ind--;
	    continue;
	}
	struct bvh_flat_node* node = stack_node[stack_ind];
	// check bounds if it's the first time in this node
	if (!stack_child_index[stack_ind]) {
	    // TODO: do we want to handle NaNs correctly?
	    point_t lows_t, highs_t, low_ts, high_ts;

	    VSUB2( lows_t, &node->bounds[0], rp->r_pt);
	    VSUB2(highs_t, &node->bounds[3], rp->r_pt);

	    VELMUL( lows_t,  lows_t, inverse_r_dir);
	    VELMUL(highs_t, highs_t, inverse_r_dir);

	    VMOVE( low_ts, lows_t);
	    VMOVE(high_ts, lows_t);
	    VMINMAX(low_ts, high_ts, highs_t);

	    fastf_t high_t = FMIN(high_ts[0], FMIN(high_ts[1], high_ts[2]));
	    fastf_t  low_t = FMAX( low_ts[0], FMAX( low_ts[1],  low_ts[2]));
	    if ((high_t < -1.0) || (low_t > high_t)) {
		stack_ind--;
		continue;
	    }
	}
	if (node->n_primitives > 0) {
	    // add the leaf values into a list
	    struct prim_list* entry;
	    BU_GET(entry, struct prim_list);
	    entry->n_primitives = node->n_primitives;
	    entry->first_prim_offset = node->data.first_prim_offset;
	    BU_LIST_PUSH(&(leafs->l), &(entry->l));
	    *prims_so_far += node->n_primitives;
	    stack_ind--;
	    continue;
	}
	// we hit the bounds and are not in a leaf
	// so we do the next child of this node

	// stack_child_index[stack_ind] either == 0 or == 1
	// because of the guard at the top of the loop
	stack_node[stack_ind+1] = (stack_child_index[stack_ind]) ? (node->data.other_child) : (node +1);
	stack_child_index[stack_ind] += 1;
	stack_child_index[stack_ind+1] = 0;
	stack_ind++;
    }
}


/**
 * This is a naive shot function that returns an allocated
 * list of primitive indexes that correspond with the indexes
 * returned from ordered_prims in hlbvh_create().
 * It is not fast, but we're keeping it around to facilitate
 * prototyping code for other primitives.
 */
void
hlbvh_shot_internal(void* root, struct xray* rp, long** check_prims, size_t* num_check_prims,
		    long **reuse_buf, size_t *reuse_len,
		    int is_flat)
{
    size_t prim_accum = 0;
    struct prim_list *leafs = NULL;
    BU_GET(leafs, struct prim_list);
    BU_LIST_INIT(&(leafs->l));
    leafs->first_prim_offset = -1;
    leafs->n_primitives = -1;
    if (is_flat) {
	while_populate_leaf_list_flat((struct bvh_flat_node *)root, rp, leafs, &prim_accum);
    } else {
	while_populate_leaf_list_raw((struct bvh_build_node *)root, rp, leafs, &prim_accum);
    }
    *num_check_prims = prim_accum;
    if (prim_accum == 0) {
	BU_PUT(leafs, struct prim_list);
	return;
    }
    if (reuse_buf && reuse_len) {
	/* Grow the thread-local buffer only when needed; never shrink. */
	if (*reuse_len < prim_accum) {
	    *reuse_buf = (long *)bu_realloc(*reuse_buf, prim_accum * sizeof(long), "hlbvh prim buf");
	    *reuse_len = prim_accum;
	}
	*check_prims = *reuse_buf;
    } else {
	*check_prims = (long*)bu_calloc(prim_accum, sizeof(long), "hlbvh primitive list");
    }
    size_t index = 0;
    struct prim_list *entry;
    while (BU_LIST_WHILE(entry, prim_list, &(leafs->l))) {
	for (int i = 0; i < entry->n_primitives; i++) {
	    (*check_prims)[index] = entry->first_prim_offset + i;
	    index++;
	}

	BU_LIST_DEQUEUE(&(entry->l));
	BU_PUT(entry, struct prim_list);
    }
    BU_PUT(leafs, struct prim_list);
    BU_ASSERT(index == prim_accum);
}

void
hlbvh_shot_raw(struct bvh_build_node* root, struct xray* rp, long** check_prims, size_t* num_check_prims)
{
    hlbvh_shot_internal(root, rp, check_prims, num_check_prims, NULL, NULL, 0 /*false*/);
}

void
hlbvh_shot_flat(struct bvh_flat_node* root, struct xray* rp, long** check_prims, size_t* num_check_prims)
{
    hlbvh_shot_internal(root, rp, check_prims, num_check_prims, NULL, NULL, 1 /*true*/);
}

void
hlbvh_shot_flat_reuse(struct bvh_flat_node *root, struct xray *rp,
		      long **check_prims, size_t *num_check_prims,
		      long **reuse_buf, size_t *reuse_len)
{
    /* Single-pass specialization: traverse the flat BVH and write prim indices
     * directly into the reuse buffer, growing it only when necessary.
     * This avoids the prim_list linked-list intermediary (per-leaf BU_GET/BU_PUT
     * and a second copy pass) used by the generic hlbvh_shot_internal path.
     */
    struct bvh_flat_node *stack_node[HLBVH_STACK_SIZE];
    unsigned char stack_child_index[HLBVH_STACK_SIZE];
    int stack_ind = 0;
    size_t index = 0;
    vect_t inverse_r_dir;
    VINVDIR(inverse_r_dir, rp->r_dir);

    stack_node[stack_ind] = root;
    stack_child_index[stack_ind] = 0;

    while (stack_ind >= 0) {
	if (UNLIKELY(stack_ind >= HLBVH_STACK_SIZE)) {
	    bu_bomb("Stack size exceeded in hlbvh shot");
	}
	if (stack_child_index[stack_ind] >= 2) {
	    stack_ind--;
	    continue;
	}
	struct bvh_flat_node *node = stack_node[stack_ind];
	if (!stack_child_index[stack_ind]) {
	    point_t lows_t, highs_t, low_ts, high_ts;

	    VSUB2( lows_t, &node->bounds[0], rp->r_pt);
	    VSUB2(highs_t, &node->bounds[3], rp->r_pt);

	    VELMUL( lows_t,  lows_t, inverse_r_dir);
	    VELMUL(highs_t, highs_t, inverse_r_dir);

	    VMOVE( low_ts, lows_t);
	    VMOVE(high_ts, lows_t);
	    VMINMAX(low_ts, high_ts, highs_t);

	    fastf_t high_t = FMIN(high_ts[0], FMIN(high_ts[1], high_ts[2]));
	    fastf_t  low_t = FMAX( low_ts[0], FMAX( low_ts[1],  low_ts[2]));
	    if ((high_t < -1.0) || (low_t > high_t)) {
		stack_ind--;
		continue;
	    }
	}
	if (node->n_primitives > 0) {
	    size_t need = index + (size_t)node->n_primitives;
	    if (need > *reuse_len) {
		*reuse_buf = (long *)bu_realloc(*reuse_buf, need * sizeof(long), "hlbvh prim buf");
		*reuse_len = need;
	    }
	    long base = node->data.first_prim_offset;
	    for (long i = 0; i < node->n_primitives; i++)
		(*reuse_buf)[index++] = base + i;
	    stack_ind--;
	    continue;
	}
	stack_node[stack_ind+1] = (stack_child_index[stack_ind]) ? (node->data.other_child) : (node + 1);
	stack_child_index[stack_ind] += 1;
	stack_child_index[stack_ind+1] = 0;
	stack_ind++;
    }

    *num_check_prims = index;
    *check_prims = (index > 0) ? *reuse_buf : NULL;
}

/*
 * hlbvh_shot_flat_reuse - BVH flat-tree traversal with per-resource reuse buffer.
 *
 * Like hlbvh_shot_flat() but writes results into a caller-managed buffer
 * (*reuse_buf / *reuse_len) that is grown with bu_realloc as needed.
 * This eliminates the per-ray bu_calloc / bu_free in the hot path.
 * On return, *check_prims points into *reuse_buf; the caller must NOT free it.
 */
void
hlbvh_shot_flat_reuse(struct bvh_flat_node *root, struct xray *rp,
		      long **check_prims, size_t *num_check_prims,
		      long **reuse_buf, size_t *reuse_len)
{
    size_t prim_accum = 0;
    size_t index = 0;
    struct prim_list *leafs = NULL;
    struct prim_list *entry;

    BU_GET(leafs, struct prim_list);
    BU_LIST_INIT(&(leafs->l));
    leafs->first_prim_offset = -1;
    leafs->n_primitives = -1;

    while_populate_leaf_list_flat(root, rp, leafs, &prim_accum);

    *num_check_prims = prim_accum;
    *check_prims = NULL;

    if (prim_accum == 0) {
	BU_PUT(leafs, struct prim_list);
	return;
    }

    /* Grow the reuse buffer if needed */
    if (*reuse_len < prim_accum) {
	*reuse_buf = (long *)bu_realloc(*reuse_buf, prim_accum * sizeof(long),
					"hlbvh reuse prim indices");
	*reuse_len = prim_accum;
    }

    while (BU_LIST_WHILE(entry, prim_list, &(leafs->l))) {
	long i;
	for (i = 0; i < entry->n_primitives; i++)
	    (*reuse_buf)[index++] = entry->first_prim_offset + i;
	BU_LIST_DEQUEUE(&(entry->l));
	BU_PUT(entry, struct prim_list);
    }
    BU_PUT(leafs, struct prim_list);
    BU_ASSERT(index == prim_accum);
    *check_prims = *reuse_buf;
}


#ifdef USE_OPENCL
static cl_int
flatten_bvh_tree(cl_int *offset, struct clt_linear_bvh_node *nodes, long total_nodes,
	const struct bvh_build_node *node, long depth)
{
    cl_int my_offset = *offset;
    struct clt_linear_bvh_node *linear_node;

    BU_ASSERT(my_offset < total_nodes);
    ++*offset;
    linear_node = &nodes[my_offset];

    VMOVE(linear_node->bounds.p_min, &node->bounds[0]);
    VMOVE(linear_node->bounds.p_max, &node->bounds[3]);
    if (node->n_primitives > 0) {
	BU_ASSERT(!node->children[0] && !node->children[1]);
	BU_ASSERT(node->n_primitives < 65536);
	linear_node->u.primitives_offset = node->first_prim_offset;
	linear_node->n_primitives = node->n_primitives;
    } else {
	/* Create interior flattened BVH node */
	linear_node->axis = node->split_axis;
	linear_node->n_primitives = 0;
	flatten_bvh_tree(offset, nodes, total_nodes, node->children[0], depth + 1);
	linear_node->u.second_child_offset =
	    flatten_bvh_tree(offset, nodes, total_nodes, node->children[1], depth + 1);
    }
    return my_offset;
}

void
clt_linear_bvh_create(long n_primitives, struct clt_linear_bvh_node **nodes_p,
	long **ordered_prims, const fastf_t *centroids_prims,
	const fastf_t *bounds_prims, cl_int *total_nodes)
{
    struct clt_linear_bvh_node *nodes;
    cl_int lnodes_created = 0;

    nodes = NULL;
    if (n_primitives != 0) {
	/* Build BVH tree for primitives */
	struct bu_pool *pool;
	long nodes_created = 0;
	struct bvh_build_node *root;

	pool = hlbvh_init_pool(n_primitives);
	root = hlbvh_create(4, pool, centroids_prims, bounds_prims, &nodes_created,
		n_primitives, ordered_prims);

	/* Compute representation of depth-first traversal of BVH tree */
	nodes = (struct clt_linear_bvh_node*)bu_calloc(nodes_created, sizeof(*nodes),
		"bvh create");
	flatten_bvh_tree(&lnodes_created, nodes, nodes_created, root, 0);
	bu_pool_delete(pool);

	if (RT_G_DEBUG&RT_DEBUG_CUT) {
	    bu_log("HLBVH: %ld nodes, %ld primitives (%.2f KB)\n",
		    nodes_created, n_primitives,
		    (double)(sizeof(*nodes) * nodes_created) / (1024.0));
	}

	if (RT_G_DEBUG&RT_DEBUG_CUT) {
	    int i;
	    long j;
	    for (i=0; i<lnodes_created; i++) {
		if (nodes[i].n_primitives != 0) {
		    bu_log("#%d: %d\n", i, nodes[i].n_primitives);
		    for (j=0; j<nodes[i].n_primitives; j++) {
			bu_log("  %ld\n", (*ordered_prims)[nodes[i].u.primitives_offset+j]);
		    }
		} else {
		    bu_log("#%d> #%d\n", i, nodes[i].u.second_child_offset);

		}
	    }

	    for (i=0; i<n_primitives; i++) {
		bu_log(":%ld\n", (*ordered_prims)[i]);
	    }
	}
    }
    *nodes_p = nodes;
    *total_nodes = lnodes_created;
}
#endif


/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
