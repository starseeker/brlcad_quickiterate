/*                D B I _ S T A T E . C P P
 * BRL-CAD
 *
 * Copyright (c) 1990-2026 United States Government as represented by
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
/** @addtogroup ged_db*/
/** @{ */
/** @file libged/dbi_state.cpp
 *
 * Maintain and manage an in-memory representation of the database hierarchy.
 * Main utility of this is to make commonly needed hierarchy/attribute
 * information available to applications without having to crack combs from
 * disk during a standard tree walk.
 */

#include "common.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <string>
#include <unordered_map>
#include <vector>

#include "./alphanum.h"
#include "./bot/concurrentqueue.h"

#include "vmath.h"
#include "bu/app.h"
#include "bu/cache.h"
#include "bu/color.h"
#include "bu/hash.h"
#include "bu/path.h"
#include "bu/opt.h"
#include "bu/sort.h"
#include "bv/lod.h"
#include "raytrace.h"
#include "ged/defines.h"
#include "ged/view.h"
#include "ged/bsg_ged_draw.h"
#include "./ged_private.h"
#include "bsg/selection.h"
#include "bsg/util.h"
#include "bsg/draw_set.h"
#include "bsg/lod_ops.h"
#include "bsg/settings.h"

#include "./dbi.h"

/* Forward declaration for drawing helper defined in draw.cpp */
extern "C" void draw_scene(struct bv_scene_obj *s, struct bview *v);

// Subdirectory in BRL-CAD cache to Dbi state data
#define DBI_CACHEDIR ".Dbi"

/* On-disk format version.  Increment whenever the binary layout of any cached
 * payload changes (new component type added, struct layout changed, etc.).
 * DbiState::DbiState() reads BU_DIR_CACHE/.Dbi/format; a mismatch clears the
 * entire .Dbi tree via bu_dirclear() before new per-file caches are opened.
 *
 * Version history:
 *   1 — initial (OBB stored as center + 3 extent vectors, 4x3 doubles)
 *   2 — Phase 3.5: OBB stored as 8 corner points (8x point_t = 24 fastf_t)
 *                  from ft_oriented_bbox; attr/AABB/LoD now cached by pipeline */
#define DBI_CACHE_FORMAT_VERSION 2

/* Cache component names for per-object attribute data.
 * Keys are plain "hash:component" strings.
 * Changing any of these requires incrementing DBI_CACHE_FORMAT_VERSION. */
#define CACHE_OBJ_BOUNDS   "bb"
#define CACHE_OBJ_OBB      "obb"
#define CACHE_REGION_ID    "rid"
#define CACHE_REGION_FLAG  "rf"
#define CACHE_INHERIT_FLAG "if"
#define CACHE_COLOR        "c"

// Build a cache lookup key from an object hash and component name.
static inline std::string
dbi_cache_key(unsigned long long hash, const char *component)
{
    return std::to_string(hash) + ":" + std::string(component);
}


// alphanum sort
bool alphanum_cmp(const std::string &a, const std::string &b)
{
    return alphanum_impl(a.c_str(), b.c_str(), NULL) < 0;
}

static void
_bsg_draw_request_copy_if_changed(struct bv_scene_obj *s, const struct bsg_draw_request *vs, std::unordered_set<struct bv_scene_obj *> &objs)
{
    if (!s || !vs)
	return;

    struct bsg_draw_request curr;
    bsg_node_draw_request_get((const bsg_node *)s, &curr);
    int changed = 0;

    if (curr.appearance.draw_mode != vs->appearance.draw_mode) { curr.appearance.draw_mode = vs->appearance.draw_mode; changed = 1; }
    if (curr.policy.mixed_modes != vs->policy.mixed_modes) { curr.policy.mixed_modes = vs->policy.mixed_modes; changed = 1; }
    if (!NEAR_EQUAL(curr.material.transparency, vs->material.transparency, SMALL_FASTF)) { curr.material.transparency = vs->material.transparency; curr.material.rgba[3] = vs->material.rgba[3]; changed = 1; }
    if (curr.material.use_override_color != vs->material.use_override_color) { curr.material.use_override_color = vs->material.use_override_color; changed = 1; }
    if (curr.material.override_rgb[0] != vs->material.override_rgb[0] || curr.material.override_rgb[1] != vs->material.override_rgb[1] || curr.material.override_rgb[2] != vs->material.override_rgb[2]) {
	curr.material.override_rgb[0] = vs->material.override_rgb[0];
	curr.material.override_rgb[1] = vs->material.override_rgb[1];
	curr.material.override_rgb[2] = vs->material.override_rgb[2];
	changed = 1;
    }
    if (curr.appearance.line_width != vs->appearance.line_width) { curr.appearance.line_width = vs->appearance.line_width; changed = 1; }
    if (!NEAR_EQUAL(curr.appearance.arrow_tip_length, vs->appearance.arrow_tip_length, SMALL_FASTF)) { curr.appearance.arrow_tip_length = vs->appearance.arrow_tip_length; changed = 1; }
    if (!NEAR_EQUAL(curr.appearance.arrow_tip_width, vs->appearance.arrow_tip_width, SMALL_FASTF)) { curr.appearance.arrow_tip_width = vs->appearance.arrow_tip_width; changed = 1; }
    if (curr.appearance.draw_solid_lines_only != vs->appearance.draw_solid_lines_only) { curr.appearance.draw_solid_lines_only = vs->appearance.draw_solid_lines_only; changed = 1; }
    if (curr.appearance.draw_non_subtract_only != vs->appearance.draw_non_subtract_only) { curr.appearance.draw_non_subtract_only = vs->appearance.draw_non_subtract_only; changed = 1; }

    if (changed) {
	bsg_node_draw_request_set((bsg_node *)s, &curr);
	objs.insert(s);
    }
}

struct walk_data {
    DbiState *dbis = NULL;
    std::unordered_map<unsigned long long, unsigned long long> i_count;
    mat_t *curr_mat = NULL;
    unsigned long long phash = 0;
};

static void
populate_leaf(void *client_data, const char *name, matp_t c_m, int op)
{
    struct walk_data *d = (struct walk_data *)client_data;
    struct db_i *dbip = d->dbis->dbip;
    RT_CHECK_DBI(dbip);

    std::unordered_map<unsigned long long, unsigned long long> &i_count = d->i_count;
    struct directory *dp = db_lookup(dbip, name, LOOKUP_QUIET);
    unsigned long long chash = bu_data_hash(name, strlen(name)*sizeof(char));
    i_count[chash] += 1;
    if (i_count[chash] > 1) {
	// If we've got multiple instances of the same object in the tree,
	// hash the string labeling the instance and map it to the correct
	// parent comb so we can associate it with the tree contents
	struct bu_vls iname = BU_VLS_INIT_ZERO;
	bu_vls_sprintf(&iname, "%s@%llu", name, i_count[chash] - 1);
	unsigned long long ihash = bu_data_hash(bu_vls_cstr(&iname), bu_vls_strlen(&iname)*sizeof(char));
	d->dbis->i_map[ihash] = chash;
	d->dbis->i_str[ihash] = std::string(bu_vls_cstr(&iname));
	d->dbis->p_c[d->phash].insert(ihash);
	d->dbis->p_v[d->phash].push_back(ihash);
	if (dp == RT_DIR_NULL) {
	    // Invalid comb reference - goes into map
	    d->dbis->invalid_entry_map[ihash] = std::string(bu_vls_cstr(&iname));
	} else {
	    // In case this was previously invalid, remove
	    d->dbis->invalid_entry_map.erase(ihash);
	}
	bu_vls_free(&iname);

	// For the next stages, if we have an ihash use it
	chash = ihash;

    } else {

	d->dbis->p_v[d->phash].push_back(chash);
	d->dbis->p_c[d->phash].insert(chash);
	if (dp == RT_DIR_NULL) {
	    // Invalid comb reference - goes into map
	    d->dbis->invalid_entry_map[chash] = std::string(name);
	} else {
	    // In case this was previously invalid, remove
	    d->dbis->invalid_entry_map.erase(chash);
	}

    }

    // If we have a non-IDN matrix, store it
    if (c_m) {
	for (int i = 0; i < 16; i++)
	    d->dbis->matrices[d->phash][chash].push_back(c_m[i]);
    }

    // If we have a non-UNION op, store it
    d->dbis->i_bool[d->phash][chash] = op;
}

static void
populate_walk_tree(union tree *tp, void *d, int subtract_skip, int p_op,
	void (*leaf_func)(void *, const char *, matp_t, int)
	)
{
    if (!tp)
	return;

    RT_CK_TREE(tp);

    int op = p_op;
    switch (tp->tr_op) {
	case OP_SUBTRACT:
	    op = OP_SUBTRACT;
	    break;
	case OP_INTERSECT:
	    op = OP_INTERSECT;
	    break;
    };


    switch (tp->tr_op) {
	case OP_SUBTRACT:
	    if (subtract_skip)
		return;
	    /* fall through */
	case OP_UNION:
	case OP_INTERSECT:
	case OP_XOR:
	    populate_walk_tree(tp->tr_b.tb_right, d, subtract_skip, op, leaf_func);
	    /* fall through */
	case OP_NOT:
	case OP_GUARD:
	case OP_XNOP:
	    populate_walk_tree(tp->tr_b.tb_left, d, subtract_skip, OP_UNION, leaf_func);
	    break;
	case OP_DB_LEAF:
	    (*leaf_func)(d, tp->tr_l.tl_name, tp->tr_l.tl_mat, op);
	    break;
	default:
	    bu_log("unrecognized operator %d\n", tp->tr_op);
	    bu_bomb("draw walk\n");
    }
}


/* ---- Phase 3.5: DrawPipeline — 5-stage concurrent pipeline ------------ *
 *
 * Replaces the Phase 3-C single-threaded GeomLoader with a concurrentqueue-
 * based design matching qgedobol's cache_drawing.cpp pipeline.
 *
 * Stages:
 *   q_init  → attr_worker  → q_aabb → aabb_worker → q_obb
 *   q_obb   → obb_worker   → q_lod  → lod_worker
 *   q_write → write_worker (serialises all bu_cache writes)
 *
 * result_q is drained by the main thread via DrawPipeline::drain().
 * -------------------------------------------------------------------- */

/* Debug delay helpers (same env vars as qgedobol cache_drawing.cpp). */
static int g_dp_delay_attr_ms = 0;
static int g_dp_delay_aabb_ms = 0;
static int g_dp_delay_obb_ms  = 0;
static int g_dp_delay_lod_ms  = 0;

static void
dp_init_debug_delays(void)
{
    static bool done = false;
    if (done) return;
    done = true;
    auto getenv_int = [](const char *name) -> int {
	const char *v = getenv(name);
	if (!v) return 0;
	int n = atoi(v);
	return (n > 0) ? n : 0;
    };
    g_dp_delay_attr_ms = getenv_int("BRLCAD_CACHE_ATTR_DELAY_MS");
    g_dp_delay_aabb_ms = getenv_int("BRLCAD_CACHE_AABB_DELAY_MS");
    g_dp_delay_obb_ms  = getenv_int("BRLCAD_CACHE_OBB_DELAY_MS");
    g_dp_delay_lod_ms  = getenv_int("BRLCAD_CACHE_LOD_DELAY_MS");
}

/* Single item queued for writing to the drawing cache. */
struct DrawCacheWriteItem {
    char   key[256] = {0};
    bool   erase_op = false;
    size_t data_len = 0;
    void  *data     = nullptr;

    DrawCacheWriteItem() = default;
    DrawCacheWriteItem(const char *k, const void *d, size_t len)
    {
	snprintf(key, sizeof(key), "%s", k);
	erase_op = (!d || !len);
	data_len  = len;
	if (d && len) {
	    data = bu_malloc(len, "DrawCacheWriteItem");
	    memcpy(data, d, len);
	}
    }
    DrawCacheWriteItem(const DrawCacheWriteItem &o)
	: erase_op(o.erase_op), data_len(o.data_len), data(nullptr)
    {
	snprintf(key, sizeof(key), "%s", o.key);
	if (o.data && o.data_len) {
	    data = bu_malloc(o.data_len, "DrawCacheWriteItem copy");
	    memcpy(data, o.data, o.data_len);
	}
    }
    DrawCacheWriteItem &operator=(const DrawCacheWriteItem &o)
    {
	if (this == &o) return *this;
	if (data) bu_free(data, "DrawCacheWriteItem free");
	data = nullptr;
	erase_op = o.erase_op;
	data_len  = o.data_len;
	snprintf(key, sizeof(key), "%s", o.key);
	if (o.data && o.data_len) {
	    data = bu_malloc(o.data_len, "DrawCacheWriteItem assign");
	    memcpy(data, o.data, o.data_len);
	}
	return *this;
    }
    ~DrawCacheWriteItem()
    {
	if (data) bu_free(data, "DrawCacheWriteItem dtor");
	data = nullptr;
    }
};

/* Result posted to the main thread result queue. */
struct DrawInternalResult {
    int                type;      /* 0=AABB, 1=OBB, 2=LOD */
    unsigned long long hash;
    char               dp_name[256];
    /* AABB */
    point_t bmin;
    point_t bmax;
    /* OBB: 8 corner points from ft_oriented_bbox */
    int     obb_valid;
    point_t obb_pts[8];
    /* LOD */
    unsigned long long lod_key;
};

/* Per-database pipeline state (private to this TU). */
struct DrawPipelineState {
    std::atomic<bool> shutdown{false};
    std::atomic<int>  thread_cnt{0};

    /* Inter-stage lock-free queues. */
    moodycamel::ConcurrentQueue<std::string>              q_init;  /* names → attr */
    moodycamel::ConcurrentQueue<struct rt_db_internal *>  q_aabb;  /* internal → AABB */
    moodycamel::ConcurrentQueue<struct rt_db_internal *>  q_obb;   /* post-AABB → OBB */
    moodycamel::ConcurrentQueue<struct rt_db_internal *>  q_lod;   /* post-OBB  → LoD */
    moodycamel::ConcurrentQueue<DrawCacheWriteItem>       q_write; /* → write_worker */

    /* Main-thread result queue. */
    moodycamel::ConcurrentQueue<DrawInternalResult> results_q;

    /* Name map: maps each in-flight rt_db_internal* to its object name.
     * Protected by name_mu.  lod_worker is the LAST consumer and must erase. */
    std::mutex name_mu;
    std::unordered_map<struct rt_db_internal *, std::string> ip_names;

    struct db_i          *dbip    = nullptr;
    struct bu_cache      *dcache  = nullptr;
    struct bv_mesh_lod_context *lod_ctx = nullptr;

    std::vector<std::thread> threads;
};

/* Helper: build a cache key string */
static inline void
dp_make_key(char *buf, size_t bufsz, unsigned long long hash,
	    const char *component)
{
    snprintf(buf, bufsz, "%llu:%s", hash, component);
}

/* ---- Pipeline stage 1: attr_worker ---------------------------------- */
static void
dp_attr_worker(std::shared_ptr<DrawPipelineState> p)
{
    char ckey[256];
    struct resource bres;
    memset(&bres, 0, sizeof(bres));
    rt_init_resource(&bres, 1, NULL);
    dp_init_debug_delays();

    while (!p->shutdown) {
	if (p->q_init.size_approx() == 0) {
	    std::this_thread::sleep_for(std::chrono::milliseconds(10));
	    continue;
	}

	std::string name;
	if (!p->q_init.try_dequeue(name))
	    continue;

	struct directory *dp = db_lookup(p->dbip, name.c_str(), LOOKUP_QUIET);
	if (dp == RT_DIR_NULL)
	    continue;

	unsigned long long hash =
	    bu_data_hash(dp->d_namep, strlen(dp->d_namep) * sizeof(char));

	/* Read attributes */
	struct bu_attribute_value_set avs = BU_AVS_INIT_ZERO;
	if (db5_get_attributes(p->dbip, &avs, dp) < 0)
	    bu_avs_free(&avs);

	/* Region flag */
	int rflag = 0;
	{
	    const char *s = bu_avs_get(&avs, "region");
	    if (s && (BU_STR_EQUAL(s, "R") || BU_STR_EQUAL(s, "1")))
		rflag = 1;
	}
	dp_make_key(ckey, sizeof(ckey), hash, CACHE_REGION_FLAG);
	p->q_write.enqueue(DrawCacheWriteItem(ckey, &rflag, sizeof(int)));

	/* Region id */
	int region_id_v = -1;
	{
	    const char *s = bu_avs_get(&avs, "region_id");
	    if (s)
		bu_opt_int(NULL, 1, &s, (void *)&region_id_v);
	}
	dp_make_key(ckey, sizeof(ckey), hash, CACHE_REGION_ID);
	p->q_write.enqueue(DrawCacheWriteItem(ckey, &region_id_v, sizeof(int)));

	/* Inherit flag */
	int inherit = 0;
	{
	    const char *s = bu_avs_get(&avs, "inherit");
	    if (s && BU_STR_EQUAL(s, "1"))
		inherit = 1;
	}
	dp_make_key(ckey, sizeof(ckey), hash, CACHE_INHERIT_FLAG);
	p->q_write.enqueue(DrawCacheWriteItem(ckey, &inherit, sizeof(int)));

	/* Color */
	unsigned int colors = UINT_MAX;
	{
	    const char *s = bu_avs_get(&avs, "color");
	    if (!s) s = bu_avs_get(&avs, "rgb");
	    if (s) {
		struct bu_color col;
		bu_opt_color(NULL, 1, &s, (void *)&col);
		int r, g, b;
		bu_color_to_rgb_ints(&col, &r, &g, &b);
		colors = (unsigned int)(r + (g << 8) + (b << 16));
	    }
	}
	// Only cache a color value when one was actually found.  UINT_MAX is
	// used internally as a "no color" placeholder, but digest_path uses
	// INT_MAX as its sentinel; writing UINT_MAX here would corrupt the cache
	// and cause path_color to return white for every colorless solid on the
	// next run (UINT_MAX != INT_MAX, so digest_path would treat the value as
	// a valid packed RGB).
	if (colors != UINT_MAX) {
	    dp_make_key(ckey, sizeof(ckey), hash, CACHE_COLOR);
	    p->q_write.enqueue(DrawCacheWriteItem(ckey, &colors, sizeof(unsigned int)));
	}

	bu_avs_free(&avs);

	/* Crack the geometry and pass to q_aabb */
	struct rt_db_internal *ip;
	BU_GET(ip, struct rt_db_internal);
	RT_DB_INTERNAL_INIT(ip);
	if (rt_db_get_internal(ip, dp, p->dbip, NULL) < 0) {
	    BU_PUT(ip, struct rt_db_internal);
	    continue;
	}
	{
	    std::lock_guard<std::mutex> lk(p->name_mu);
	    p->ip_names[ip] = std::string(dp->d_namep);
	}
	if (g_dp_delay_attr_ms > 0)
	    std::this_thread::sleep_for(
		std::chrono::milliseconds(g_dp_delay_attr_ms));
	p->q_aabb.enqueue(ip);
    }

    rt_clean_resource_basic(NULL, &bres);
    p->thread_cnt--;
}

/* ---- Pipeline stage 2: aabb_worker ---------------------------------- */
static void
dp_aabb_worker(std::shared_ptr<DrawPipelineState> p)
{
    char ckey[256];
    const struct bn_tol btol = BN_TOL_INIT_TOL;

    while (!p->shutdown) {
	if (p->q_aabb.size_approx() == 0) {
	    std::this_thread::sleep_for(std::chrono::milliseconds(10));
	    continue;
	}

	struct rt_db_internal *ip = nullptr;
	if (!p->q_aabb.try_dequeue(ip))
	    continue;

	std::string ip_name;
	{
	    std::lock_guard<std::mutex> lk(p->name_mu);
	    auto it = p->ip_names.find(ip);
	    if (it == p->ip_names.end()) { p->q_obb.enqueue(ip); continue; }
	    ip_name = it->second;
	}
	const char *name = ip_name.c_str();
	unsigned long long hash =
	    bu_data_hash(name, strlen(name) * sizeof(char));

	dp_make_key(ckey, sizeof(ckey), hash, CACHE_OBJ_BOUNDS);

	DrawInternalResult dr{};
	dr.type = 0; /* AABB */
	dr.hash = hash;
	snprintf(dr.dp_name, sizeof(dr.dp_name), "%s", name);

	if (ip->idb_meth && ip->idb_meth->ft_bbox) {
	    point_t bmin, bmax;
	    VSETALL(bmin,  INFINITY);
	    VSETALL(bmax, -INFINITY);
	    if (ip->idb_meth->ft_bbox(ip, &bmin, &bmax, &btol) == 0) {
		point_t bb[2];
		VMOVE(bb[0], bmin);
		VMOVE(bb[1], bmax);
		p->q_write.enqueue(
		    DrawCacheWriteItem(ckey, &bb, 2 * sizeof(point_t)));
		VMOVE(dr.bmin, bmin);
		VMOVE(dr.bmax, bmax);
		p->results_q.enqueue(dr);
	    } else {
		p->q_write.enqueue(DrawCacheWriteItem(ckey, nullptr, 0));
	    }
	} else {
	    p->q_write.enqueue(DrawCacheWriteItem(ckey, nullptr, 0));
	}

	if (g_dp_delay_aabb_ms > 0)
	    std::this_thread::sleep_for(
		std::chrono::milliseconds(g_dp_delay_aabb_ms));
	p->q_obb.enqueue(ip);
    }

    p->thread_cnt--;
}

/* ---- Pipeline stage 3: obb_worker ----------------------------------- */
static void
dp_obb_worker(std::shared_ptr<DrawPipelineState> p)
{
    char ckey[256];

    while (!p->shutdown) {
	if (p->q_obb.size_approx() == 0) {
	    std::this_thread::sleep_for(std::chrono::milliseconds(10));
	    continue;
	}

	struct rt_db_internal *ip = nullptr;
	if (!p->q_obb.try_dequeue(ip))
	    continue;

	std::string ip_name;
	{
	    std::lock_guard<std::mutex> lk(p->name_mu);
	    auto it = p->ip_names.find(ip);
	    if (it == p->ip_names.end()) { p->q_lod.enqueue(ip); continue; }
	    ip_name = it->second;
	}
	const char *name = ip_name.c_str();
	unsigned long long hash =
	    bu_data_hash(name, strlen(name) * sizeof(char));

	dp_make_key(ckey, sizeof(ckey), hash, CACHE_OBJ_OBB);

	DrawInternalResult dr{};
	dr.type      = 1; /* OBB */
	dr.hash      = hash;
	dr.obb_valid = 0;
	snprintf(dr.dp_name, sizeof(dr.dp_name), "%s", name);

	if (ip->idb_meth && ip->idb_meth->ft_oriented_bbox) {
	    struct rt_arb_internal arb;
	    arb.magic = RT_ARB_INTERNAL_MAGIC;
	    double tol_dist = BN_TOL_DIST;
	    if (ip->idb_meth->ft_oriented_bbox(&arb, ip, tol_dist) == 0) {
		for (int k = 0; k < 8; k++)
		    VMOVE(dr.obb_pts[k], arb.pt[k]);
		dr.obb_valid = 1;
		p->q_write.enqueue(
		    DrawCacheWriteItem(ckey, arb.pt, 8 * sizeof(point_t)));
		p->results_q.enqueue(dr);
	    } else {
		p->q_write.enqueue(DrawCacheWriteItem(ckey, nullptr, 0));
	    }
	}
	/* No ft_oriented_bbox — skip OBB silently, still forward to lod */

	if (g_dp_delay_obb_ms > 0)
	    std::this_thread::sleep_for(
		std::chrono::milliseconds(g_dp_delay_obb_ms));
	p->q_lod.enqueue(ip);
    }

    p->thread_cnt--;
}

/* ---- Pipeline stage 4: lod_worker ----------------------------------- */
static void
dp_lod_worker(std::shared_ptr<DrawPipelineState> p)
{
    while (!p->shutdown) {
	if (p->q_lod.size_approx() == 0) {
	    std::this_thread::sleep_for(std::chrono::milliseconds(10));
	    continue;
	}

	struct rt_db_internal *ip = nullptr;
	if (!p->q_lod.try_dequeue(ip))
	    continue;

	std::string ip_name;
	{
	    std::lock_guard<std::mutex> lk(p->name_mu);
	    auto it = p->ip_names.find(ip);
	    if (it != p->ip_names.end())
		ip_name = it->second;
	    p->ip_names.erase(ip); /* last consumer — remove from map */
	}
	const char *name = ip_name.c_str();

	if (p->lod_ctx && !ip_name.empty() &&
	    ip->idb_minor_type == DB5_MINORTYPE_BRLCAD_BOT)
	{
	    struct rt_bot_internal *bot =
		(struct rt_bot_internal *)ip->idb_ptr;
	    if (bot && bot->magic == RT_BOT_INTERNAL_MAGIC
		&& bot->num_faces > 0 && bot->num_vertices > 0)
	    {
		unsigned long long hash =
		    bu_data_hash(name, strlen(name) * sizeof(char));

		unsigned long long key =
		    bv_mesh_lod_key_get(p->lod_ctx, name);
		if (!key) {
		    key = bv_mesh_lod_cache(
			p->lod_ctx,
			(const point_t *)bot->vertices, bot->num_vertices,
			NULL, bot->faces, bot->num_faces, 0, 0.66);
		    if (key)
			bv_mesh_lod_key_put(p->lod_ctx, name, key);
		}
		if (key) {
		    DrawInternalResult dr{};
		    dr.type    = 2; /* LOD */
		    dr.hash    = hash;
		    dr.lod_key = key;
		    snprintf(dr.dp_name, sizeof(dr.dp_name), "%s", name);
		    p->results_q.enqueue(dr);
		}
	    }
	}

	if (g_dp_delay_lod_ms > 0)
	    std::this_thread::sleep_for(
		std::chrono::milliseconds(g_dp_delay_lod_ms));
	rt_db_free_internal(ip);
	BU_PUT(ip, struct rt_db_internal);
    }

    p->thread_cnt--;
}

/* ---- Pipeline stage 5: write_worker --------------------------------- */
static void
dp_write_worker(std::shared_ptr<DrawPipelineState> p)
{
    while (!p->shutdown) {
	if (p->q_write.size_approx() == 0) {
	    std::this_thread::sleep_for(std::chrono::milliseconds(10));
	    continue;
	}

	DrawCacheWriteItem item;
	if (!p->q_write.try_dequeue(item))
	    continue;

	if (!p->dcache)
	    continue;

	if (item.erase_op || !item.data || !item.data_len) {
	    bu_cache_clear(item.key, p->dcache, NULL);
	} else {
	    int tries = 0;
	    while (tries < 5 &&
		   !bu_cache_write(item.data, item.data_len,
				   item.key, p->dcache, NULL))
	    {
		tries++;
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	    }
	}
    }

    p->thread_cnt--;
}

/* ---- DrawPipeline implementation ------------------------------------ */

DrawPipeline::DrawPipeline(struct ged *gedp, struct bu_cache *dcache)
{
    if (!gedp || !gedp->dbip)
	return;

    auto state = std::make_shared<DrawPipelineState>();
    state->dbip   = gedp->dbip;
    state->dcache = dcache;
    state->lod_ctx = gedp->ged_lod;

    /* Launch 5 worker threads */
    state->thread_cnt = 5;
    state->threads.emplace_back(dp_attr_worker,  state);
    state->threads.emplace_back(dp_aabb_worker,  state);
    state->threads.emplace_back(dp_obb_worker,   state);
    state->threads.emplace_back(dp_lod_worker,   state);
    state->threads.emplace_back(dp_write_worker, state);

    /* Detach all — they run until shutdown is signalled */
    for (auto &t : state->threads)
	t.detach();
    state->threads.clear();

    /* Store the shared_ptr heap-allocated as an opaque pointer.
     * ~DrawPipeline() will recover and release it. */
    state_ = reinterpret_cast<DrawPipelineState *>(
	new std::shared_ptr<DrawPipelineState>(state));
}

DrawPipeline::~DrawPipeline()
{
    if (!state_)
	return;
    auto *pp = reinterpret_cast<std::shared_ptr<DrawPipelineState> *>(state_);
    if (pp && *pp) {
	(*pp)->shutdown = true;
	int waited = 0;
	while ((*pp)->thread_cnt > 0 && waited < 200) {
	    std::this_thread::sleep_for(std::chrono::milliseconds(10));
	    waited++;
	}
    }
    delete pp;
    state_ = nullptr;
}

void
DrawPipeline::push(const std::vector<WorkItem> &items)
{
    if (items.empty() || !state_)
	return;
    auto *pp = reinterpret_cast<std::shared_ptr<DrawPipelineState> *>(state_);
    if (!pp || !*pp)
	return;
    for (const auto &item : items) {
	if (!item.dp || !item.hash)
	    continue;
	if (item.dp->d_flags & RT_DIR_COMB)
	    continue;
	if (item.dp->d_flags & RT_DIR_HIDDEN)
	    continue;
	(*pp)->q_init.enqueue(std::string(item.dp->d_namep));
    }
}

size_t
DrawPipeline::drain(std::vector<Result> &out)
{
    if (!state_)
	return 0;
    auto *pp = reinterpret_cast<std::shared_ptr<DrawPipelineState> *>(state_);
    if (!pp || !*pp)
	return 0;
    size_t n = 0;
    DrawInternalResult dr{};
    while ((*pp)->results_q.try_dequeue(dr)) {
	Result r;
	r.hash    = dr.hash;
	r.dp_name = dr.dp_name;
	if (dr.type == 0) { /* AABB */
	    r.type = Result::AABB;
	    VMOVE(r.bmin, dr.bmin);
	    VMOVE(r.bmax, dr.bmax);
	} else if (dr.type == 1) { /* OBB */
	    r.type      = Result::OBB;
	    r.obb_valid = (dr.obb_valid != 0);
	    for (int k = 0; k < 8; k++)
		VMOVE(r.obb_pts[k], dr.obb_pts[k]);
	} else { /* LOD */
	    r.type    = Result::LOD;
	    r.has_lod = (dr.lod_key != 0);
	    r.lod_key = dr.lod_key;
	}
	out.push_back(r);
	n++;
    }
    return n;
}

bool
DrawPipeline::settled() const
{
    if (!state_)
	return true;
    auto *pp = reinterpret_cast<std::shared_ptr<DrawPipelineState> *>(state_);
    if (!pp || !*pp)
	return true;
    auto &s = **pp;
    return (s.q_init.size_approx()  == 0 &&
	    s.q_aabb.size_approx()  == 0 &&
	    s.q_obb.size_approx()   == 0 &&
	    s.q_lod.size_approx()   == 0 &&
	    s.q_write.size_approx() == 0);
}

void
DrawPipeline::set_lod_ctx(struct bv_mesh_lod_context *lod_ctx)
{
    if (!state_)
	return;
    auto *pp = reinterpret_cast<std::shared_ptr<DrawPipelineState> *>(state_);
    if (pp && *pp)
	(*pp)->lod_ctx = lod_ctx;
}



DbiState::DbiState(struct ged *ged_p)
{
    bu_vls_init(&path_string);
    bu_vls_init(&hash_string);
    BU_GET(res, struct resource);
    rt_init_resource(res, 0, NULL);
    shared_vs = new BViewState(this);
    default_selected = new SelectionSet(this, "active");
    selected_sets[std::string("default")] = default_selected;
    gedp = ged_p;
    if (!gedp)
	return;
    dbip = gedp->dbip;
    if (!dbip)
	return;

    // Set up cache
    {
	// Check the on-disk format version before opening any per-file cache.
	// If it doesn't match DBI_CACHE_FORMAT_VERSION, nuke the entire .Dbi
	// tree so stale entries from old schema don't accumulate.
	{
	    char fpath[MAXPATHLEN];
	    bu_dir(fpath, MAXPATHLEN, BU_DIR_CACHE, DBI_CACHEDIR, "format", NULL);
	    long disk_fmt = -1;
	    {
		std::ifstream fmt_file(fpath);
		if (fmt_file.is_open())
		    fmt_file >> disk_fmt;
	    }
	    if (disk_fmt > 0 && disk_fmt != DBI_CACHE_FORMAT_VERSION) {
		char ddir[MAXPATHLEN];
		bu_dir(ddir, MAXPATHLEN, BU_DIR_CACHE, DBI_CACHEDIR, NULL);
		bu_log("Old dbi cache version (%ld) found at %s - clearing\n", disk_fmt, fpath);
		bu_dirclear((const char *)ddir);
		/* bu_dirclear removes the directory itself; recreate it so the
		 * format file write below succeeds. */
		bu_mkdir(ddir);
	    }
	    FILE *fp = fopen(fpath, "w");
	    if (fp) {
		fprintf(fp, "%d\n", DBI_CACHE_FORMAT_VERSION);
		fclose(fp);
	    }
	}

	struct bu_vls fname = BU_VLS_INIT_ZERO;
	bu_vls_sprintf(&fname, "%s", bu_path_normalize(dbip->dbi_filename));
	unsigned long long fhash = bu_data_hash(bu_vls_cstr(&fname), bu_vls_strlen(&fname)*sizeof(char));
	bu_path_component(&fname, bu_path_normalize(dbip->dbi_filename), BU_PATH_BASENAME_EXTLESS);
	bu_vls_printf(&fname, "_%llu", fhash);
	struct bu_vls cpath = BU_VLS_INIT_ZERO;
	bu_vls_sprintf(&cpath, "%s/%s", DBI_CACHEDIR, bu_vls_cstr(&fname));
	dcache = bu_cache_open(bu_vls_cstr(&cpath), 1, 0);
	bu_vls_free(&fname);
	bu_vls_free(&cpath);
    }

    // Phase 3.5: start the background geometry pipeline
    if (dcache)
	draw_pipeline_ = new DrawPipeline(gedp, dcache);

    struct directory *dp;
    FOR_ALL_DIRECTORY_START(dp, dbip)
	update_dp(dp, 0);
    FOR_ALL_DIRECTORY_END;

    // Queue all solid objects for background AABB/OBB/LoD processing.
    // Objects that already have a cached bbox in bboxes[] will still be
    // processed for OBB and LoD; the pipeline skips AABB cache writes if
    // the data hasn't changed.
    if (draw_pipeline_) {
	std::vector<DrawPipeline::WorkItem> items;
	struct directory *qdp;
	FOR_ALL_DIRECTORY_START(qdp, dbip)
	    if (qdp->d_flags & RT_DIR_COMB) continue;
	    if (qdp->d_flags & RT_DIR_HIDDEN) continue;
	    unsigned long long h =
		bu_data_hash(qdp->d_namep, strlen(qdp->d_namep)*sizeof(char));
	    items.push_back({h, qdp});
	FOR_ALL_DIRECTORY_END;
	if (!items.empty())
	    draw_pipeline_->push(items);
    }
}


DbiState::~DbiState()
{
    bu_vls_free(&path_string);
    bu_vls_free(&hash_string);
    std::unordered_map<std::string, SelectionSet *>::iterator ss_it;
    for (ss_it = selected_sets.begin(); ss_it != selected_sets.end(); ss_it++) {
	delete ss_it->second;
    }
    default_selected = NULL;
    // Phase 1-D: delete all GObj instances
    while (!gobjs.empty()) {
	delete gobjs.begin()->second; // dtor removes entry from gobjs
    }
    delete shared_vs;
    rt_clean_resource_basic(NULL, res);
    BU_PUT(res, struct resource);

    // Phase 3.5: stop the background drawing pipeline before closing the cache
    delete draw_pipeline_;
    draw_pipeline_ = NULL;

    if (dcache)
	bu_cache_close(dcache);
}


void
DbiState::populate_maps(struct directory *dp, unsigned long long phash, int reset)
{
    if (!(dp->d_flags & RT_DIR_COMB))
	return;
    std::unordered_map<unsigned long long, std::unordered_set<unsigned long long>>::iterator pc_it;
    std::unordered_map<unsigned long long, std::vector<unsigned long long>>::iterator pv_it;
    pc_it = p_c.find(phash);
    pv_it = p_v.find(phash);
    if (pc_it == p_c.end() || pv_it != p_v.end() || reset) {
	if (reset && pc_it != p_c.end()) {
	    pc_it->second.clear();
	}
	if (reset && pv_it != p_v.end()) {
	    pv_it->second.clear();
	}
	struct rt_db_internal in;
	if (rt_db_get_internal(&in, dp, dbip, NULL) < 0)
	    return;
	struct rt_comb_internal *comb = (struct rt_comb_internal *)in.idb_ptr;
	if (!comb->tree)
	    return;

	std::unordered_map<unsigned long long, unsigned long long> i_count;
	struct walk_data d;
	d.dbis = this;
	d.phash = phash;
	populate_walk_tree(comb->tree, (void *)&d, 0, OP_UNION, populate_leaf);
	rt_db_free_internal(&in);
    }
}

unsigned long long
DbiState::path_hash(std::vector<unsigned long long> &path, size_t max_len)
{
    size_t mlen = (max_len) ? max_len : path.size();
    return bu_data_hash(path.data(), mlen * sizeof(unsigned long long));
}

static void
fp_path_split(std::vector<std::string> &objs, const char *str)
{
    std::string s(str);
    while (s.length() && s.c_str()[0] == '/')
	s.erase(0, 1);  //Remove leading slashes

    std::string nstr;
    bool escaped = false;
    for (size_t i = 0; i < s.length(); i++) {
	if (s[i] == '\\') {
	    if (escaped) {
		nstr.push_back(s[i]);
		escaped = false;
		continue;
	    }
	    escaped = true;
	    continue;
	}
	if (s[i] == '/' && !escaped) {
	    if (nstr.length())
		objs.push_back(nstr);
	    nstr.clear();
	    continue;
	}
	nstr.push_back(s[i]);
	escaped = false;
    }
    if (nstr.length())
	objs.push_back(nstr);
}

static std::string
name_deescape(std::string &name)
{
    std::string s(name);
    std::string nstr;

    for (size_t i = 0; i < s.length(); i++) {
	if (s[i] == '\\') {
	    if ((i+1) < s.length())
		nstr.push_back(s[i+1]);
	    i++;
	} else {
	    nstr.push_back(s[i]);
	}
    }

    return nstr;
}

// This is a full (and more expensive) check to ensure
// a path has no cycles anywhere in it.
static bool
path_cyclic(std::vector<unsigned long long> &path)
{
    if (path.size() == 1)
	return false;
    int i = path.size() - 1;
    while (i > 0) {
	int j = i - 1;
	while (j >= 0) {
	    if (path[i] == path[j])
		return true;
	    j--;
	}
	i--;
    }
    return false;
}

// This version of the cyclic check assumes the path entries other than the
// last one are OK, and checks only against that last entry.
static bool
path_addition_cyclic(std::vector<unsigned long long> &path)
{
    if (path.size() == 1)
	return false;
    int new_entry = path.size() - 1;
    int i = new_entry - 1;
    while (i >= 0) {
	if (path[new_entry] == path[i])
	    return true;
	i--;
    }
    return false;
}


static size_t
path_elements(std::vector<std::string> &elements, const char *path)
{
    std::vector<std::string> substrs;
    fp_path_split(substrs, path);
    for (size_t i = 0; i < substrs.size(); i++) {
	std::string cleared = name_deescape(substrs[i]);
	elements.push_back(cleared);
    }
    return elements.size();
}

std::vector<unsigned long long>
DbiState::digest_path(const char *path)
{
    // If no path, nothing to process
    if (!path)
	return std::vector<unsigned long long>();

    // Digest the string into individual path elements
    std::vector<std::string> elements;
    path_elements(elements, path);

    // Convert the string elements into hash elements
    std::vector<unsigned long long> phe;
    struct bu_vls hname = BU_VLS_INIT_ZERO;
    for (size_t i = 0; i < elements.size(); i++) {
	bu_vls_sprintf(&hname, "%s", elements[i].c_str());
	phe.push_back(bu_data_hash(bu_vls_cstr(&hname), bu_vls_strlen(&hname)*sizeof(char)));
    }
    bu_vls_free(&hname);

    // If we're cyclic, path is invalid
    if (path_cyclic(phe))
	return std::vector<unsigned long long>();

    // parent/child relationship validate
    std::unordered_map<unsigned long long, std::unordered_set<unsigned long long>>::iterator pc_it;
    std::unordered_map<unsigned long long, unsigned long long>::iterator i_it;
    unsigned long long phash = phe[0];
    for (size_t i = 1; i < phe.size(); i++) {
	pc_it = p_c.find(phash);
	// The parent comb structure is stored only under its original name's hash - if
	// we have a numbered instance from a comb tree as a parent, we may be able to
	// map it to the correct entry with i_map.  If not, we have an invalid path.
	if (pc_it == p_c.end()) {
	    i_it = i_map.find(phash);
	    if (i_it == i_map.end())
		return std::vector<unsigned long long>();
	    phash = i_it->second;
	    pc_it = p_c.find(phash);
	    if (pc_it == p_c.end())
		return std::vector<unsigned long long>();
	}
	unsigned long long chash = phe[i];
	if (pc_it->second.find(chash) == pc_it->second.end()) {
	    bu_log("Invalid element path: %s\n", elements[i].c_str());
	    return std::vector<unsigned long long>();
	}
	phash = chash;
    }

    return phe;
}

bool
DbiState::valid_hash(unsigned long long phash)
{
    if (!phash)
	return false;

    // First, see if the hash is an instance string
    if (i_str.find(phash) != i_str.end())
	return true;

    // If we have potentially obsolete names, check those
    // before trying the dp (which may no longer be invalid)
    if (old_names.size() && old_names.find(phash) != old_names.end())
	return true;

    // If not, try the directory pointer
    if (d_map.find(phash) != d_map.end())
	return true;

    // Last option - invalid string
    if (invalid_entry_map.find(phash) != invalid_entry_map.end())
	return true;

    return false;
}

bool
DbiState::valid_hash_path(std::vector<unsigned long long> &phashes)
{
    for (size_t i = 0; i < phashes.size(); i++) {
	if (!valid_hash(phashes[i]))
	    return false;
    }
    return true;
}

bool
DbiState::print_hash(struct bu_vls *opath, unsigned long long phash)
{
    if (!phash)
	return false;

    // First, see if the hash is an instance string
    if (i_str.find(phash) != i_str.end()) {
	bu_vls_printf(opath, "%s", i_str[phash].c_str());
	return true;
    }

    // If we have potentially obsolete names, check those
    // before trying the dp (which may no longer be invalid)
    if (old_names.size() && old_names.find(phash) != old_names.end()) {
	bu_vls_printf(opath, "%s", old_names[phash].c_str());
	return true;
    }

    // If not, try the directory pointer
    if (d_map.find(phash) != d_map.end()) {
	bu_vls_printf(opath, "%s", d_map[phash]->d_namep);
	return true;
    }

    // Last option - invalid string
    if (invalid_entry_map.find(phash) != invalid_entry_map.end()) {
	bu_vls_printf(opath, "%s", invalid_entry_map[phash].c_str());
	return true;
    }

    bu_exit(EXIT_FAILURE, "DbiState::print_hash failure, dbi_state.cpp::%d - a hash not known to the database's DbiState was passed in.  This can happen when the dbip contents change and dbi_state->update() isn't called in the parent application after doing so.\n", __LINE__);
    bu_vls_printf(opath, "\nERROR!!!\n");
    return false;
}

void
DbiState::print_path(struct bu_vls *opath, std::vector<unsigned long long> &path, size_t pmax, int verbose)
{
    if (!opath || !path.size())
	return;

    bu_vls_trunc(opath, 0);
    for (size_t i = 0; i < path.size(); i++) {
	if (pmax && i == pmax)
	    break;
	if (i > 0 && verbose) {
	    std::unordered_map<unsigned long long, std::unordered_map<unsigned long long, std::vector<fastf_t>>>::iterator m_it;
	    m_it = matrices.find(path[i-1]);
	    if (m_it != matrices.end()) {
		std::unordered_map<unsigned long long, std::vector<fastf_t>>::iterator mv_it;
		mv_it = m_it->second.find(path[i]);
		if (mv_it == m_it->second.end()) {
		    bu_vls_printf(opath, "[M]");
		}
	    }
	}
	if (!print_hash(opath, path[i]))
	    continue;
	if (i < path.size() - 1 && (!pmax || i < pmax - 1))
	    bu_vls_printf(opath, "/");
    }
}

const char *
DbiState::pathstr(std::vector<unsigned long long> &path, size_t pmax)
{
    bu_vls_trunc(&path_string, 0);
    print_path(&path_string, path, pmax);
    return bu_vls_cstr(&path_string);
}


const char *
DbiState::hashstr(unsigned long long hash)
{
    bu_vls_trunc(&hash_string, 0);
    print_hash(&hash_string, hash);
    return bu_vls_cstr(&hash_string);
}

unsigned int
DbiState::color_int(struct bu_color *c)
{
    if (!c)
	return 0;
    int r, g, b;
    bu_color_to_rgb_ints(c, &r, &g, &b);
    unsigned int colors = r + (g << 8) + (b << 16);
    return colors;
}

int
DbiState::int_color(struct bu_color *c, unsigned int cval)
{
    if (!c)
	return 0;

    int r, g, b;
    r = cval & 0xFF;
    g = (cval >> 8) & 0xFF;
    b = (cval >> 16) & 0xFF;

    unsigned char lrgb[3];
    lrgb[0] = (unsigned char)r;
    lrgb[1] = (unsigned char)g;
    lrgb[2] = (unsigned char)b;

    return bu_color_from_rgb_chars(c, lrgb);
}

void
DbiState::clear_cache(struct directory *dp)
{
    if (!dp || !dcache)
	return;

    unsigned long long hash = bu_data_hash(dp->d_namep, strlen(dp->d_namep)*sizeof(char));

    {
	std::string k;
	k = dbi_cache_key(hash, CACHE_OBJ_BOUNDS);  bu_cache_clear(k.c_str(), dcache, NULL);
	k = dbi_cache_key(hash, CACHE_OBJ_OBB);     bu_cache_clear(k.c_str(), dcache, NULL);
	k = dbi_cache_key(hash, CACHE_REGION_ID);   bu_cache_clear(k.c_str(), dcache, NULL);
	k = dbi_cache_key(hash, CACHE_REGION_FLAG); bu_cache_clear(k.c_str(), dcache, NULL);
	k = dbi_cache_key(hash, CACHE_INHERIT_FLAG); bu_cache_clear(k.c_str(), dcache, NULL);
	k = dbi_cache_key(hash, CACHE_COLOR);       bu_cache_clear(k.c_str(), dcache, NULL);
    }

    bboxes.erase(hash);
    obbs.erase(hash);
    region_id.erase(hash);
    c_inherit.erase(hash);
    rgb.erase(hash);
}

unsigned long long
DbiState::update_dp(struct directory *dp, int reset)
{
    if (dp->d_flags & DB_LS_HIDDEN)
	return 0;

    // Set up to go from hash back to name
    unsigned long long hash = bu_data_hash(dp->d_namep, strlen(dp->d_namep)*sizeof(char));
    d_map[hash] = dp;

    // Clear any (possibly) state bbox.  bbox calculation
    // can be expensive, so defer it until it's needed
    bboxes.erase(hash);
    obbs.erase(hash);

    // Encode hierarchy info if this is a comb
    if (dp->d_flags & RT_DIR_COMB)
	populate_maps(dp, hash, reset);

    // Check for various drawing related attributes
    // Ideally, if we have enough info, we'd like to avoid loading
    // the avs.  See if we can get away with it using dcache
    struct bu_attribute_value_set c_avs = BU_AVS_INIT_ZERO;
    bool loaded_avs = false;
    region_id.erase(hash);
    c_inherit.erase(hash);
    rgb.erase(hash);

    // First, check the dcache for all remaining needed values
    void *bdata = NULL;
    size_t bsize = 0;

    bool need_region_id_avs = true;
    bool need_region_flag_avs = true;
    bool need_color_inherit_avs = true;
    bool need_cval_avs = true;

    int region_flag = 0;
    int attr_region_id = -1;
    int color_inherit = 0;
    unsigned int cval = INT_MAX;

    if (dcache) {
	{
	    struct bu_cache_txn *t = NULL;
	    std::string k = dbi_cache_key(hash, CACHE_REGION_ID);
	    bsize = bu_cache_get(&bdata, k.c_str(), dcache, &t);
	    if (bsize == sizeof(attr_region_id)) {
		memcpy(&attr_region_id, bdata, sizeof(attr_region_id));
		need_region_id_avs = false;
	    }
	    bu_cache_get_done(&t);
	}
	{
	    struct bu_cache_txn *t = NULL;
	    std::string k = dbi_cache_key(hash, CACHE_REGION_FLAG);
	    bsize = bu_cache_get(&bdata, k.c_str(), dcache, &t);
	    if (bsize == sizeof(region_flag)) {
		memcpy(&region_flag, bdata, sizeof(region_flag));
		need_region_flag_avs = false;
	    }
	    bu_cache_get_done(&t);
	}
	{
	    struct bu_cache_txn *t = NULL;
	    std::string k = dbi_cache_key(hash, CACHE_INHERIT_FLAG);
	    bsize = bu_cache_get(&bdata, k.c_str(), dcache, &t);
	    if (bsize == sizeof(color_inherit)) {
		memcpy(&color_inherit, bdata, sizeof(color_inherit));
		need_color_inherit_avs = false;
	    }
	    bu_cache_get_done(&t);
	}
	{
	    struct bu_cache_txn *t = NULL;
	    std::string k = dbi_cache_key(hash, CACHE_COLOR);
	    bsize = bu_cache_get(&bdata, k.c_str(), dcache, &t);
	    if (bsize == sizeof(cval)) {
		memcpy(&cval, bdata, sizeof(cval));
		need_cval_avs = false;
	    }
	    bu_cache_get_done(&t);
	}
    }


    if (need_region_flag_avs) {
	if (!loaded_avs) {
	    db5_get_attributes(dbip, &c_avs, dp);
	    loaded_avs = true;
	}
	// Check for region flag.
	const char *region_flag_str = bu_avs_get(&c_avs, "region");
	if (region_flag_str && (BU_STR_EQUAL(region_flag_str, "R") || BU_STR_EQUAL(region_flag_str, "1"))) {
	    region_flag = 1;
	}

	if (dcache) {
	    std::string k = dbi_cache_key(hash, CACHE_REGION_FLAG);
	    bu_cache_write(&region_flag, sizeof(region_flag), k.c_str(), dcache, NULL);
	}
    }


    if (need_region_id_avs) {
	if (!loaded_avs) {
	    db5_get_attributes(dbip, &c_avs, dp);
	    loaded_avs = true;
	}
	// Check for region id.  For drawing purposes this needs to be a number.
	const char *region_id_val = bu_avs_get(&c_avs, "region_id");
	if (region_id_val)
	    bu_opt_int(NULL, 1, &region_id_val, (void *)&attr_region_id);

	if (dcache) {
	    std::string k = dbi_cache_key(hash, CACHE_REGION_ID);
	    bu_cache_write(&attr_region_id, sizeof(attr_region_id), k.c_str(), dcache, NULL);
	}
    }

    if (need_color_inherit_avs) {
	if (!loaded_avs) {
	    db5_get_attributes(dbip, &c_avs, dp);
	    loaded_avs = true;
	}
	color_inherit = (BU_STR_EQUAL(bu_avs_get(&c_avs, "inherit"), "1")) ? 1 : 0;

	if (dcache) {
	    std::string k = dbi_cache_key(hash, CACHE_INHERIT_FLAG);
	    bu_cache_write(&color_inherit, sizeof(color_inherit), k.c_str(), dcache, NULL);
	}
    }

    if (need_cval_avs) {
	if (!loaded_avs) {
	    db5_get_attributes(dbip, &c_avs, dp);
	    loaded_avs = true;
	}
	// Color (note that the db_mater_head colors and a region_id may
	// override this, as might a parent comb with color and the inherit
	// flag both set.
	rgb.erase(hash);
	struct bu_color c = BU_COLOR_INIT_ZERO;
	const char *color_val = bu_avs_get(&c_avs, "color");
	if (!color_val)
	    color_val = bu_avs_get(&c_avs, "rgb");
	if (color_val){
	    bu_opt_color(NULL, 1, &color_val, (void *)&c);
	    cval = color_int(&c);
	}

	if (dcache) {
	    std::string k = dbi_cache_key(hash, CACHE_COLOR);
	    bu_cache_write(&cval, sizeof(cval), k.c_str(), dcache, NULL);
	}
    }

    // If a region flag is set but a region_id is not, there is an implicit
    // assumption that the region_id is to be regarded as 0.  Not sure this
    // will always be true, but right now region table based coloring works
    // that way in existing BRL-CAD code (see the example m35.g model's
    // all.g/component/power.train/r75 for an instance of this)
    if (region_flag && attr_region_id == -1)
	attr_region_id = 0;


    if (attr_region_id != -1)
	region_id[hash] = attr_region_id;
    if (color_inherit)
	c_inherit[hash] = color_inherit;
    if (cval != INT_MAX)
	rgb[hash] = cval;

    // Done with attributes
    if (loaded_avs) {
	bu_avs_free(&c_avs);
    }

    // Phase 1-D: create (or recreate) the GObj for this directory pointer.
    // GObj ctor reads from the flat maps we just populated and, for combs,
    // calls GenCombInstances() to build the CombInst child list.
    {
	auto g_it = gobjs.find(hash);
	if (g_it != gobjs.end())
	    delete g_it->second; // dtor deregisters the old GObj from gobjs
	new GObj(this, dp);      // ctor registers the new GObj in gobjs
    }

    return hash;
}

bool
DbiState::path_color(struct bu_color *c, std::vector<unsigned long long> &elements)
{
    // This may not be how we'll always want to do this, but at least for the
    // moment (to duplicate observed MGED behavior) the first region_id seen
    // along the path with an active color in the database material table trumps
    // all other color values set by any other means.
    if (dbip && db_mater_head(dbip) != MATER_NULL) {
	std::unordered_map<unsigned long long, int>::iterator r_it;
	int path_region_id;
	for (size_t i = 0; i < elements.size(); i++) {
	    r_it = region_id.find(elements[i]);
	    if (r_it == region_id.end())
		continue;
	    path_region_id = r_it->second;
	    const struct mater *mp;
	    for (mp = db_mater_head(dbip); mp != MATER_NULL; mp = mp->mt_forw) {
		if (path_region_id > mp->mt_high || path_region_id < mp->mt_low)
		    continue;
		unsigned char mt[3];
		mt[0] = mp->mt_r;
		mt[1] = mp->mt_g;
		mt[2] = mp->mt_b;
		bu_color_from_rgb_chars(c, mt);
		return true;
	    }
	}
    }

    // Next, check for an inherited color.  If we have one (the behavior seen in MGED
    // appears to require a comb with both inherit and a color value set to override
    // lower colors) then we are done.
    std::unordered_map<unsigned long long, int>::iterator ci_it;
    std::unordered_map<unsigned long long, unsigned int>::iterator rgb_it;
    for (size_t i = 0; i < elements.size(); i++) {
	ci_it = c_inherit.find(elements[i]);
	if (ci_it == c_inherit.end())
	    continue;
	rgb_it = rgb.find(elements[i]);
	if (rgb_it == rgb.end())
	    continue;
	int_color(c, rgb_it->second);
	return true;
    }

    // If we don't have an inherited color, it works the other way around - the
    // lowest set color wins.  Note that a region flag doesn't automatically
    // override a lower color level - i.e. there is no implicit inherit flag
    // in a region being set on a comb.
    std::vector<unsigned long long>::reverse_iterator v_it;
    for (v_it = elements.rbegin(); v_it != elements.rend(); v_it++) {
	rgb_it = rgb.find(*v_it);
	if (rgb_it == rgb.end())
	    continue;
	int_color(c, rgb_it->second);
	return true;
    }

    // If we don't have anything else, default to red
    unsigned char mt[3];
    mt[0] = 255;
    mt[1] = 0;
    mt[2] = 0;
    bu_color_from_rgb_chars(c, mt);
    return false;
}

bool
DbiState::path_is_subtraction(std::vector<unsigned long long> &elements)
{
    if (elements.size() < 2)
	return false;

    unsigned long long phash = elements[0];
    for (size_t i = 1; i < elements.size(); i++) {
	unsigned long long chash = elements[i];
	std::unordered_map<unsigned long long, std::unordered_map<unsigned long long, size_t>>::iterator i_it;
	i_it = i_bool.find(phash);
	if (i_it == i_bool.end())
	    return false;
	std::unordered_map<unsigned long long, size_t>::iterator ib_it;
	ib_it = i_it->second.find(chash);
	if (ib_it == i_it->second.end())
	    return false;

	if (ib_it->second == OP_SUBTRACT)
	    return true;

	phash = chash;
    }

    return false;
}

db_op_t
DbiState::bool_op(unsigned long long phash, unsigned long long chash)
{
    if (!phash)
	return DB_OP_UNION;
    size_t op = i_bool[phash][chash];
    if (op == OP_SUBTRACT) {
	return DB_OP_SUBTRACT;
    }
    if (op == OP_INTERSECT)
	return DB_OP_INTERSECT;
    return DB_OP_UNION;
}

struct directory *
DbiState::get_hdp(unsigned long long phash)
{
    if (!phash)
	return NULL;

    std::unordered_map<unsigned long long, struct directory *>::iterator d_it;
    d_it = d_map.find(phash);
    if (d_it != d_map.end()) {
	return d_it->second;
    }

    std::unordered_map<unsigned long long, unsigned long long>::iterator i_it;
    i_it = i_map.find(phash);

    if (i_it != i_map.end()) {
	d_it = d_map.find(i_it->second);
	if (d_it != d_map.end()) {
	    return d_it->second;
	}
    }

    return NULL;
}

bool
DbiState::get_matrix(matp_t m, unsigned long long p_key, unsigned long long i_key)
{
    if (UNLIKELY(!m || p_key == 0 || i_key == 0))
	return false;

    std::unordered_map<unsigned long long, std::unordered_map<unsigned long long, std::vector<fastf_t>>>::iterator m_it;
    std::unordered_map<unsigned long long, std::vector<fastf_t>>::iterator mv_it;
    m_it = matrices.find(p_key);
    if (m_it == matrices.end())
	return false;
    mv_it = m_it->second.find(i_key);
    if (mv_it == m_it->second.end())
	return false;

    // If we got this far, we have an index into the matrices vector.  Assign
    // the result to m
    std::vector<fastf_t> &mv = mv_it->second;
    for (size_t i = 0; i < 16; i++)
	m[i] = mv[i];

    return true;
}

bool
DbiState::get_path_matrix(matp_t m, std::vector<unsigned long long> &elements)
{
    bool have_mat = false;
    if (UNLIKELY(!m))
	return have_mat;

    MAT_IDN(m);
    if (elements.size() < 2)
	return have_mat;

    unsigned long long phash = elements[0];
    for (size_t i = 1; i < elements.size(); i++) {
	unsigned long long chash = elements[i];
	mat_t nm;
	MAT_IDN(nm);
	bool got_mat = get_matrix(nm, phash, chash);
	if (got_mat) {
	    mat_t cmat;
	    bn_mat_mul(cmat, m, nm);
	    MAT_COPY(m, cmat);
	    have_mat = true;
	}
	phash = chash;
    }

    return have_mat;
}

bool
DbiState::get_bbox(point_t *bbmin, point_t *bbmax, matp_t curr_mat, unsigned long long hash)
{

    if (UNLIKELY(!bbmin || !bbmax || hash == 0))
	return false;

    bool ret = false;
    std::unordered_map<unsigned long long, std::unordered_set<unsigned long long>>::iterator pc_it;
    std::unordered_set<unsigned long long>::iterator s_it;
    unsigned long long key = hash;
    // First, see if this is an instance we need to translate to its canonical
    // .g database name
    if (i_map.find(hash) != i_map.end())
	key = i_map[hash];

    // See if we have a direct bbox lookup available
    std::unordered_map<unsigned long long, std::vector<fastf_t>>::iterator b_it;
    b_it = bboxes.find(key);
    if (b_it != bboxes.end()) {
	point_t lbmin, lbmax;
	lbmin[X] = b_it->second[0];
	lbmin[Y] = b_it->second[1];
	lbmin[Z] = b_it->second[2];
	lbmax[X] = b_it->second[3];
	lbmax[Y] = b_it->second[4];
	lbmax[Z] = b_it->second[5];

	if (curr_mat) {
	    point_t tbmin, tbmax;
	    MAT4X3PNT(tbmin, curr_mat, lbmin);
	    VMOVE(lbmin, tbmin);
	    MAT4X3PNT(tbmax, curr_mat, lbmax);
	    VMOVE(lbmax, tbmax);
	}

	VMINMAX(*bbmin, *bbmax, lbmin);
	VMINMAX(*bbmin, *bbmax, lbmax);
	return true;
    }

    // We might have a comb.  If that's the case, we need to work
    // through the hierarchy to get the bboxes of the children.
    pc_it = p_c.find(key);
    if (pc_it != p_c.end()) {
	// Have comb children - incorporate each one
	for (s_it = pc_it->second.begin(); s_it != pc_it->second.end(); s_it++) {
	    unsigned long long child_hash = *s_it;
	    // See if we have a matrix for this case - if so, we need to
	    // incorporate it
	    mat_t nm;
	    MAT_IDN(nm);
	    bool have_mat = get_matrix(nm, key, child_hash);
	    if (have_mat) {
		// Construct new "current" matrix
		if (curr_mat) {
		    // If we already have a non-IDN matrix from parent
		    // path elements, we need to multiply the matrices
		    // to accumulate the position changes
		    mat_t om;
		    MAT_COPY(om, curr_mat);
		    bn_mat_mul(curr_mat, om, nm);
		    if (get_bbox(bbmin, bbmax, curr_mat, child_hash))
			ret = true;
		    MAT_COPY(curr_mat, om);
		} else {
		    // If this is the first non-IDN matrix, we don't
		    // need to combine it with parent matrices
		    if (get_bbox(bbmin, bbmax, nm, child_hash))
			ret = true;
		}
	    } else {
		if (get_bbox(bbmin, bbmax, curr_mat, child_hash))
		    ret = true;
	    }
	}
    }

    // When we have an object that is not a comb, look up its pre-calculated
    // box and incorporate it into bmin/bmax.
    point_t bmin, bmax;
    bool have_bbox = false;

    // First, check the dcache
    void *bbox_data = NULL;
    size_t bsize = 0;
    if (dcache) {
	struct bu_cache_txn *t = NULL;
	std::string k = dbi_cache_key(hash, CACHE_OBJ_BOUNDS);
	bsize = bu_cache_get(&bbox_data, k.c_str(), dcache, &t);
	if (bsize) {
	    if (bsize != (sizeof(bmin) + sizeof(bmax))) {
		bu_log("Incorrect data size found loading cached bounds data\n");
	    } else {
		const char *bp = (const char *)bbox_data;
		memcpy(&bmin, bp, sizeof(bmin));
		bp += sizeof(bmin);
		memcpy(&bmax, bp, sizeof(bmax));
		have_bbox = true;
	    }
	}
	bu_cache_get_done(&t);
    }


    // This calculation can be expensive.  If we've already
    // got it stashed as part of LoD processing, use that
    // version.
    struct directory *dp = get_hdp(hash);
    if (!dp)
	return false;
    if (!have_bbox) {
	if (dp->d_minor_type == DB5_MINORTYPE_BRLCAD_BOT && gedp->ged_lod) {
	    key = bv_mesh_lod_key_get(gedp->ged_lod, dp->d_namep);
	    if (key) {
		struct bv_mesh_lod *lod = bv_mesh_lod_create(gedp->ged_lod, key);
		if (lod) {
		    VMOVE(bmin, lod->bmin);
		    VMOVE(bmax, lod->bmax);
		    have_bbox = true;

		    if (dcache) {
			char buf[sizeof(bmin) + sizeof(bmax)];
			memcpy(buf, &bmin, sizeof(bmin));
			memcpy(buf + sizeof(bmin), &bmax, sizeof(bmax));
			std::string k = dbi_cache_key(hash, CACHE_OBJ_BOUNDS);
			bu_cache_write(buf, sizeof(buf), k.c_str(), dcache, NULL);
		    }
		}
	    }
	}
    }

    // No LoD - ask librt
    if (!have_bbox) {
	struct bg_tess_tol ttol = BG_TESS_TOL_INIT_ZERO;
	struct bn_tol tol = BN_TOL_INIT_TOL;
	mat_t m;
	MAT_IDN(m);
	int bret = rt_bound_instance(&bmin, &bmax, dp, dbip, &ttol, &tol, &m);
	if (bret != -1) {
	    have_bbox = true;

	    if (dcache) {
		char buf[sizeof(bmin) + sizeof(bmax)];
		memcpy(buf, &bmin, sizeof(bmin));
		memcpy(buf + sizeof(bmin), &bmax, sizeof(bmax));
		std::string k = dbi_cache_key(hash, CACHE_OBJ_BOUNDS);
		bu_cache_write(buf, sizeof(buf), k.c_str(), dcache, NULL);
	    }
	}
    }

    if (have_bbox) {
	for (size_t j = 0; j < 3; j++)
	    bboxes[hash].push_back(bmin[j]);
	for (size_t j = 0; j < 3; j++)
	    bboxes[hash].push_back(bmax[j]);

	VMINMAX(*bbmin, *bbmax, bmin);
	VMINMAX(*bbmin, *bbmax, bmax);
	ret = true;
    }

    return ret;
}

bool
DbiState::get_path_bbox(point_t *bbmin, point_t *bbmax, std::vector<unsigned long long> &elements)
{
    if (UNLIKELY(!bbmin || !bbmax || !elements.size()))
	return false;

    // Everything but the last element should be a comb - we only need to
    // assemble a matrix from the path (if there are any non-identity matrices)
    // and call get_bbox on the last element.
    bool have_mat = false;
    mat_t start_mat;
    MAT_IDN(start_mat);
    for (size_t i = 0; i < elements.size() - 1; i++) {
	mat_t nm;
	MAT_IDN(nm);
	bool got_mat = get_matrix(nm, elements[i], elements[i+1]);
	if (got_mat) {
	    mat_t cmat;
	    bn_mat_mul(cmat, start_mat, nm);
	    MAT_COPY(start_mat, cmat);
	    have_mat = true;
	}
    }
    if (have_mat) {
	return get_bbox(bbmin, bbmax, start_mat, elements[elements.size() - 1]);
    }

    return get_bbox(bbmin, bbmax, NULL, elements[elements.size() - 1]);
}

BViewState *
DbiState::get_view_state(struct bview *v)
{
    if (!bv_view_is_independent(v))
	return shared_vs;
    if (view_states.find(v) != view_states.end())
	return view_states[v];

    BViewState *nv = new BViewState(this);
    view_states[v] = nv;
    return nv;
}

std::vector<SelectionSet *>
DbiState::get_selected_states(const char *sname)
{
    std::vector<SelectionSet *> ret;
    std::unordered_map<std::string, SelectionSet *>::iterator ss_it;

    if (!sname || BU_STR_EQUIV(sname, "default")) {
	ret.push_back(default_selected);
	return ret;
    }

    std::string sn(sname);
    if (sn.find('*') != std::string::npos) {
	for (ss_it = selected_sets.begin(); ss_it != selected_sets.end(); ss_it++) {
	    if (bu_path_match(sname, ss_it->first.c_str(), 0)) {
		ret.push_back(ss_it->second);
	    }
	}
	return ret;
    }

    for (ss_it = selected_sets.begin(); ss_it != selected_sets.end(); ss_it++) {
	if (BU_STR_EQUIV(sname, ss_it->first.c_str())) {
	    ret.push_back(ss_it->second);
	}
    }
    if (ret.size())
	return ret;

    SelectionSet *ns = new SelectionSet(this, sn.c_str());
    selected_sets[sn] = ns;
    ret.push_back(ns);
    return ret;
}

SelectionSet *
DbiState::find_selected_state(const char *sname)
{
    if (!sname || BU_STR_EQUIV(sname, "default")) {
	return default_selected;
    }

    std::unordered_map<std::string, SelectionSet *>::iterator ss_it;
    for (ss_it = selected_sets.begin(); ss_it != selected_sets.end(); ss_it++) {
	if (BU_STR_EQUIV(sname, ss_it->first.c_str())) {
	    return ss_it->second;
	}
    }

    return NULL;
}

void
DbiState::put_selected_state(const char *sname)
{
    if (!sname || BU_STR_EQUIV(sname, "default")) {
	default_selected->clear();
	return;
    }

    std::unordered_map<std::string, SelectionSet *>::iterator ss_it;
    for (ss_it = selected_sets.begin(); ss_it != selected_sets.end(); ss_it++) {
	if (BU_STR_EQUIV(sname, ss_it->first.c_str())) {
	    ss_it->second->clear();
	    delete ss_it->second;
	    selected_sets.erase(ss_it);
	    return;
	}
    }
}

std::vector<std::string>
DbiState::list_selection_sets()
{
    std::vector<std::string> ret;
    std::unordered_map<std::string, SelectionSet *>::iterator ss_it;
    for (ss_it = selected_sets.begin(); ss_it != selected_sets.end(); ss_it++) {
	ret.push_back(ss_it->first);
    }
    std::sort(ret.begin(), ret.end(), &alphanum_cmp);
    return ret;
}

/* ---- Phase 1-G: new SelectionSet management -------------------------- */

SelectionSet *
DbiState::get_selection_set(const char *name)
{
    if (!name || !name[0] || BU_STR_EQUIV(name, "default"))
	return default_selected;

    std::string sn(name);
    std::unordered_map<std::string, SelectionSet *>::iterator ss_it = selected_sets.find(sn);
    if (ss_it != selected_sets.end())
	return ss_it->second;

    SelectionSet *ns = new SelectionSet(this, sn.c_str());
    selected_sets[sn] = ns;
    return ns;
}

std::vector<SelectionSet *>
DbiState::get_selection_sets(const char *pattern)
{
    return get_selected_states(pattern);
}

void
DbiState::add_selection_set(const char *name)
{
    if (!name || !name[0] || BU_STR_EQUIV(name, "default"))
	return;
    std::string sn(name);
    if (selected_sets.find(sn) == selected_sets.end())
	selected_sets[sn] = new SelectionSet(this, sn.c_str());
}

void
DbiState::remove_selection_set(const char *name)
{
    put_selected_state(name);
}

/* ---- Phase 1-C: observer dispatch ------------------------------------ */

void
DbiState::add_observer(IDbiObserver *obs)
{
    if (!obs) return;
    dbi_observers_.push_back(obs);
}

void
DbiState::remove_observer(IDbiObserver *obs)
{
    if (!obs) return;
    auto it = std::find(dbi_observers_.begin(), dbi_observers_.end(), obs);
    if (it != dbi_observers_.end())
	dbi_observers_.erase(it);
}

void
DbiState::add_scene_observer(ISceneObserver *obs)
{
    if (!obs) return;
    scene_observers_.push_back(obs);
}

void
DbiState::remove_scene_observer(ISceneObserver *obs)
{
    if (!obs) return;
    auto it = std::find(scene_observers_.begin(), scene_observers_.end(), obs);
    if (it != scene_observers_.end())
	scene_observers_.erase(it);
}

void
DbiState::notify_dbi_observers(const std::vector<DbiChangeEvent> &events)
{
    for (IDbiObserver *obs : dbi_observers_)
	obs->on_dbi_changed(events);
}

void
DbiState::notify_scene_observers(const std::vector<SceneChangeEvent> &events)
{
    for (ISceneObserver *obs : scene_observers_)
	obs->on_scene_changed(events);
}

void
DbiState::gather_cyclic(
	std::unordered_set<unsigned long long> &cyclic,
	unsigned long long c_hash,
	std::vector<unsigned long long> &path_hashes
	)
{
    std::unordered_map<unsigned long long, std::unordered_set<unsigned long long>>::iterator pc_it;
    pc_it = p_c.find(c_hash);

    path_hashes.push_back(c_hash);

    if (!path_addition_cyclic(path_hashes)) {
	/* Not cyclic - keep going */
	if (pc_it != p_c.end()) {
	    std::unordered_set<unsigned long long>::iterator c_it;
	    for (c_it = pc_it->second.begin(); c_it != pc_it->second.end(); c_it++)
		gather_cyclic(cyclic, *c_it, path_hashes);
	}
    } else {
	cyclic.insert(c_hash);
    }

    /* Done with branch - restore path */
    path_hashes.pop_back();
}

static int
alphanum_sort(const void *a, const void *b, void *UNUSED(data)) {
    struct directory *ga = *(struct directory **)a;
    struct directory *gb = *(struct directory **)b;
    return alphanum_impl(ga->d_namep, gb->d_namep, NULL);
}

std::vector<unsigned long long>
DbiState::tops(bool show_cyclic)
{
    std::vector<unsigned long long> ret;
    // First, get the standard tops results
    struct directory **all_paths = NULL;
    db_update_nref(gedp->dbip);
    int tops_cnt = db_ls(gedp->dbip, DB_LS_TOPS, NULL, &all_paths);
    if (all_paths) {
	bu_sort(all_paths, tops_cnt, sizeof(struct directory *), alphanum_sort, NULL);
	for (int i = 0; i < tops_cnt; i++) {
	    unsigned long long hash = bu_data_hash(all_paths[i]->d_namep, strlen(all_paths[i]->d_namep)*sizeof(char));
	    ret.push_back(hash);
	}
	bu_free(all_paths, "free db_ls output");
    }

    if (!show_cyclic)
	return ret;

    // If we also want cyclic paths, use DbiState to try and speed things up.
    // db_ls has that capability, but it has to unpack all the combs walking
    // the tree to find the answer and that results in a slow check for large
    // databases.
    std::unordered_set<unsigned long long> cyclic_paths;
    std::vector<unsigned long long> path_hashes;
    for (size_t i = 0; i < ret.size(); i++) {
	path_hashes.clear();
	gather_cyclic(cyclic_paths, ret[i], path_hashes);
    }
    std::unordered_set<unsigned long long>::iterator c_it;
    for (c_it = cyclic_paths.begin(); c_it != cyclic_paths.end(); c_it++) {
	ret.push_back(*c_it);
    }

    return ret;
}

unsigned long long
DbiState::update()
{
    unsigned long long ret = 0;

    if (!added.size() && !changed.size() && !removed.size()) {
	changed_hashes.clear();
	old_names.clear();
	return ret;
    }

    // If we got this far, SOMETHING changed
    ret |= GED_DBISTATE_DB_CHANGE;

    std::unordered_set<unsigned long long>::iterator s_it;
    std::unordered_set<struct directory *>::iterator g_it;

    if (need_update_nref) {
	db_update_nref(dbip);
	need_update_nref = false;
    }

    // dps -> hashes
    changed_hashes.clear();
    for(g_it = changed.begin(); g_it != changed.end(); g_it++) {
	struct directory *dp = *g_it;
	unsigned long long hash = bu_data_hash(dp->d_namep, strlen(dp->d_namep)*sizeof(char));
	changed_hashes.insert(hash);
    }

    // Update the primary data structures
    for(s_it = removed.begin(); s_it != removed.end(); s_it++) {
	// Combs with this key in their child set need to be updated to refer
	// to it as an invalid entry.
	std::unordered_map<unsigned long long, std::vector<unsigned long long>>::iterator pv_it;
	for (pv_it = p_v.begin(); pv_it != p_v.end(); pv_it++) {
	    for (size_t i = 0; i < pv_it->second.size(); i++) {
		if (i_map.find(pv_it->second[i]) != i_map.end()) {
		    invalid_entry_map[i_map[pv_it->second[i]]] = i_str[i_map[pv_it->second[i]]];
		} else {
		    invalid_entry_map[pv_it->second[i]] = old_names[*s_it];
		}
	    }
	}

	d_map.erase(*s_it);
	bboxes.erase(*s_it);
	obbs.erase(*s_it);
	c_inherit.erase(*s_it);
	rgb.erase(*s_it);
	region_id.erase(*s_it);
	matrices.erase(*s_it);
	i_bool.erase(*s_it);

	// Phase 1-D: remove corresponding GObj
	{
	    auto g_it2 = gobjs.find(*s_it);
	    if (g_it2 != gobjs.end())
		delete g_it2->second; // dtor removes entry from gobjs
	}

	// We do not clear the instance maps (i_map and i_str) since those containers do not
	// guarantee uniqueness to one child object.  To remove entries no longer
	// used anywhere in the database, we have to confirm they are no longer needed on a global
	// basis in a subsequent garbage-collect operation.

	// Entries with this hash as their key are erased.
	p_c.erase(*s_it);
	p_v.erase(*s_it);
    }

    for(g_it = added.begin(); g_it != added.end(); g_it++) {
	struct directory *dp = *g_it;
	unsigned long long hash = update_dp(dp, 0);

	// If this name was previously the source of an invalid reference,
	// it is no longer.
	invalid_entry_map.erase(hash);
    }

    for(g_it = changed.begin(); g_it != changed.end(); g_it++) {
	struct directory *dp = *g_it;
	// Properties need to be updated - comb children, colors, matrices,
	// bounding box for solids, etc.
	update_dp(dp, 1);
    }

    // Garbage collect i_map and i_str
    std::unordered_map<unsigned long long, std::vector<unsigned long long>>::iterator sk_it;
    std::unordered_set<unsigned long long> used;
    for (sk_it = p_v.begin(); sk_it != p_v.end(); sk_it++) {
	used.insert(sk_it->second.begin(), sk_it->second.end());
    }
    std::vector<unsigned long long> unused;
    std::unordered_map<unsigned long long, unsigned long long>::iterator im_it;
    for (im_it = i_map.begin(); im_it != i_map.end(); im_it++) {
	if (used.find(im_it->first) != used.end())
	    unused.push_back(im_it->first);
    }
    for (size_t i = 0; i < unused.size(); i++) {
	i_map.erase(unused[i]);
	i_str.erase(unused[i]);
    }

    // For all associated view states, execute any necessary changes to
    // view objects and lists
    std::unordered_map<BViewState *, std::unordered_set<struct bview *>> vmap;
    struct bu_ptbl *views = bv_set_views(&gedp->ged_views);
    for (size_t i = 0; i < BU_PTBL_LEN(views); i++) {
	struct bview *v = (struct bview *)BU_PTBL_GET(views, i);
	DbiState *dbis = (DbiState *)gedp->dbi_state;
	BViewState *bvs = dbis->get_view_state(v);
	if (!bvs)
	    continue;
	vmap[bvs].insert(v);
    }
    std::unordered_map<BViewState *, std::unordered_set<struct bview *>>::iterator bv_it;
    for (bv_it = vmap.begin(); bv_it != vmap.end(); bv_it++) {
	bv_it->first->redraw(NULL, bv_it->second, 1);
    }

    // Phase 1-C: build change events before clearing the sets
    std::vector<DbiChangeEvent> events_added, events_changed, events_removed;
    for (auto *dp : added) {
	DbiChangeEvent ev;
	ev.kind = DbiChangeKind::ObjectAdded;
	ev.object = GHash{bu_data_hash(dp->d_namep, strlen(dp->d_namep) * sizeof(char))};
	ev.batch = false;
	events_added.push_back(ev);
    }
    for (auto *dp : changed) {
	DbiChangeEvent ev;
	ev.kind = DbiChangeKind::ObjectModified;
	ev.object = GHash{bu_data_hash(dp->d_namep, strlen(dp->d_namep) * sizeof(char))};
	ev.batch = false;
	events_changed.push_back(ev);
    }
    for (auto h : removed) {
	DbiChangeEvent ev;
	ev.kind = DbiChangeKind::ObjectRemoved;
	ev.object = GHash{h};
	ev.batch = false;
	events_removed.push_back(ev);
    }

    // Updates done, clear items stored by callbacks
    added.clear();
    changed.clear();
    changed_hashes.clear();
    removed.clear();
    old_names.clear();

    // Phase 1-C: notify registered observers about the changes that occurred
    if (!events_added.empty() || !events_changed.empty() || !events_removed.empty()) {
	std::vector<DbiChangeEvent> events;
	events.insert(events.end(), events_added.begin(), events_added.end());
	events.insert(events.end(), events_changed.begin(), events_changed.end());
	events.insert(events.end(), events_removed.begin(), events_removed.end());
	notify_dbi_observers(events);
    }

    return ret;
}

void
DbiState::print_leaves(
	std::set<std::string> &leaves,
	unsigned long long c_hash,
	std::vector<unsigned long long> &path_hashes
	)
{
    std::unordered_map<unsigned long long, std::unordered_set<unsigned long long>>::iterator pc_it;
    path_hashes.push_back(c_hash);

    bool leaf = path_addition_cyclic(path_hashes);

    if (!leaf) {
	/* Not cyclic - keep going */
	pc_it = p_c.find(c_hash);
	if (pc_it == p_c.end()) {
	    leaf = true;
	}
    }

    if (!leaf) {
	std::unordered_set<unsigned long long>::iterator c_it;
	for (c_it = pc_it->second.begin(); c_it != pc_it->second.end(); c_it++)
	    print_leaves(leaves, *c_it, path_hashes);
    }

    // Print leaf
    if (leaf) {
	struct bu_vls p = BU_VLS_INIT_ZERO;
	print_path(&p, path_hashes, 0, 1);
	leaves.insert(std::string(bu_vls_cstr(&p)));
	bu_vls_free(&p);
    }

    /* Done with branch - restore path */
    path_hashes.pop_back();
}

void
DbiState::print_dbi_state(struct bu_vls *outvls, bool report_view_states)
{
    struct bu_vls *o = outvls;
    if (!o) {
	BU_GET(o, struct bu_vls);
	bu_vls_init(o);
    }

    std::vector<unsigned long long> top_objs = tops(true);
    std::set<std::string> leaves;
    // Report each path to its leaves (or to cyclic termination)
    std::vector<unsigned long long> path_hashes;
    for (size_t i = 0; i < top_objs.size(); i++) {
	path_hashes.clear();
	print_leaves(leaves, top_objs[i], path_hashes);
    }

    std::set<std::string>::iterator l_it;
    for (l_it = leaves.begin(); l_it != leaves.end(); l_it++)
	bu_vls_printf(o, "%s\n", l_it->c_str());

    if (report_view_states) {
	if (gedp->ged_gvp) {
	    BViewState *vs = get_view_state(gedp->ged_gvp);
	    bu_vls_printf(o, "\nDefault:\n");
	    vs->print_view_state(o);
	}
	if (view_states.size()) {
	    std::unordered_map<struct bview *, BViewState *>::iterator v_it;
	    std::map<std::string, std::set<BViewState *>> oviews;
	    for (v_it = view_states.begin(); v_it != view_states.end(); v_it++) {
		if (v_it->first == gedp->ged_gvp)
		    continue;
		std::string vname(bu_vls_cstr(&v_it->first->gv_name));
		oviews[vname].insert(v_it->second);
	    }
	    if (oviews.size()) {
		bu_vls_printf(o, "\nViews:\n");
		std::map<std::string, std::set<BViewState *>>::iterator o_it;
		for (o_it = oviews.begin(); o_it != oviews.end(); o_it++) {
		    std::set<BViewState *> &vset = o_it->second;
		    if (vset.size() > 1) {
			std::cout << "Warning:  " << vset.size() << " views with name " << o_it->first << "\n";
		    }
		    std::set<BViewState *>::iterator vs_it;
		    for (vs_it = vset.begin(); vs_it != vset.end(); vs_it++) {
			bu_vls_printf(o, "\n%s:\n", o_it->first.c_str());
			(*vs_it)->print_view_state(o);
		    }
		}
	    }
	}
    }

    if (!outvls) 
	std::cout << bu_vls_cstr(o) << "\n";

    if (o != outvls) {
	bu_vls_free(o);
	BU_PUT(o, struct bu_vls);
    }
}

BViewState::BViewState(DbiState *s)
{
    dbis = s;
}

/* ------------------------------------------------------------------ */
/* BSG integration helpers (Phase B short-term hot-fix).               */
/*                                                                    */
/* BViewState was historically depositing its leaf scene objects into  */
/* bv_view_objs(v, BV_DB_OBJS).  The BSG render path in dm_draw_objs   */
/* now reads from gd_draw_root only, so those leaves were invisible.   */
/* These helpers attach/detach BViewState-allocated leaves to/from the */
/* GED draw tree at gd_draw_root via the public bsg_view_obj_* API,    */
/* without changing BViewState's own s_map[phash][mode] bookkeeping.   */
/* ------------------------------------------------------------------ */
static void
_bview_state_attach_leaf(struct ged *gedp,
			 struct bv_scene_obj *sp,
			 const std::vector<unsigned long long> &path_hashes,
			 DbiState *dbis)
{
    if (!gedp || !sp || path_hashes.empty() || !dbis)
	return;
    if (!gedp->dbip)
	return;

    /* Build the leaf full-path string and parse it into a db_full_path.
     * print_path produces a slash-separated path with no leading '/',
     * which is what db_string_to_path expects. */
    struct bu_vls pstr = BU_VLS_INIT_ZERO;
    {
	std::vector<unsigned long long> &cp =
	    const_cast<std::vector<unsigned long long> &>(path_hashes);
	dbis->print_path(&pstr, cp);
    }
    if (!bu_vls_strlen(&pstr)) {
	bu_vls_free(&pstr);
	return;
    }

    struct db_full_path leaf_dfp;
    db_full_path_init(&leaf_dfp);
    if (db_string_to_path(&leaf_dfp, gedp->dbip, bu_vls_cstr(&pstr)) != 0) {
	/* Path parse failed (invalid entry) — leave sp un-attached.
	 * It will not render, which matches the prior behaviour. */
	db_free_full_path(&leaf_dfp);
	bu_vls_free(&pstr);
	return;
    }
    bu_vls_free(&pstr);

    /* Set up ged_bv_data so bsg_view_obj_append_solid_to_group can walk
     * the path components and so the per-solid free callback can clear
     * the illumination NodeSensor (Phase 7 Step 9). */
    struct ged_bv_data *bdata =
	(sp->s_u_data) ? (struct ged_bv_data *)sp->s_u_data : NULL;
    if (!bdata) {
	BU_GET(bdata, struct ged_bv_data);
	db_full_path_init(&bdata->s_fullpath);
	sp->s_u_data = (void *)bdata;
    } else {
	bdata->s_fullpath.fp_len = 0;
    }
    bdata->gedp = gedp;
    db_dup_full_path(&bdata->s_fullpath, &leaf_dfp);
    sp->s_free_callback = ged_bv_illum_free_cb;

    /* Build a single-component db_full_path for the top-level group
     * (the user-typed root, e.g. "all.g") and look it up / create it. */
    struct directory *top_dp = leaf_dfp.fp_names[0];
    struct db_full_path top_dfp;
    db_full_path_init(&top_dfp);
    db_add_node_to_full_path(&top_dfp, top_dp);

    struct bview *cv = gedp->ged_gvp;
    gedp->ged_gvp = sp->s_v;
    struct bv_scene_obj *gdlp =
	bsg_view_obj_lookup_or_add_dbpath(gedp, &top_dfp);
    db_free_full_path(&top_dfp);
    db_free_full_path(&leaf_dfp);

    if (!gdlp) {
	gedp->ged_gvp = cv;
	return;
    }

    /* Append the leaf to the BSG tree.  This walks down from the
     * top-level group, creating intermediate sub-group nodes as needed
     * based on bdata->s_fullpath. */
    bsg_view_obj_append_solid_to_group(gedp, gdlp, sp);
    gedp->ged_gvp = cv;
}

/*
 * Unlink @p sp from its BSG parent's children ptbl, bump the parent's
 * revision counter and invalidate cached aggregate bboxes.  Safe to
 * call on objects that were never attached (sp->bsg.bsg_parent == NULL).
 *
 * Must be called before bv_obj_put(sp), because bv_obj_put() does NOT
 * remove the object from any parent BSG node's children ptbl — only
 * from sp->otbl (the gv_objs ptbl it was minted into).
 */
static void
_bview_state_detach_leaf(struct bv_scene_obj *sp)
{
    if (!sp || !sp->bsg.bsg_parent)
	return;
    struct bv_scene_obj *p = (struct bv_scene_obj *)sp->bsg.bsg_parent;
    bu_ptbl_rm(&p->bsg.bsg_children, (const long *)sp);
    bsg_bump_rev_node((bsg_node *)p);
    bsg_node_bbox_invalidate((bsg_node *)p);
    sp->bsg.bsg_parent = NULL;
}


// 0 = valid, 3 = need re-eval
int
BViewState::leaf_check(
	unsigned long long c_hash,
	std::vector<unsigned long long> &path_hashes
	)
{
    if (!c_hash)
	return 0;

    bool is_removed = (dbis->removed.find(c_hash) != dbis->removed.end());
    if (is_removed)
	return 3;

    bool is_changed = (dbis->changed_hashes.find(c_hash) != dbis->changed_hashes.end());
    if (is_changed)
	return 3;

    std::unordered_map<unsigned long long, std::unordered_set<unsigned long long>>::iterator pc_it;
    pc_it = dbis->p_c.find(c_hash);
    path_hashes.push_back(c_hash);

    if (!path_addition_cyclic(path_hashes)) {
	/* Not cyclic - keep going */
	if (pc_it != dbis->p_c.end()) {
	    std::unordered_set<unsigned long long>::iterator c_it;
	    for (c_it = pc_it->second.begin(); c_it != pc_it->second.end(); c_it++)
		if (leaf_check(*c_it, path_hashes))
		    return 3;
	}
    }

    return 0;
}

// 0 = valid, 1 = invalid, 2 = invalid, remain "drawn", 3 == need re-eval
int
BViewState::check_status(
	std::unordered_set<unsigned long long> *invalid_paths,
	std::unordered_set<unsigned long long> *changed_paths,
	unsigned long long path_hash,
       	std::vector<unsigned long long> &cpath,
	bool leaf_expand
	)
{
    // If nothing was removed or changed, there's nothing to tell us anything
    // is invalid - just return
    if (dbis->removed.size() && !dbis->changed_hashes.size())
	return 0;

    bool parent_changed = false;
    for (size_t j = 0; j < cpath.size(); j++) {
	unsigned long long phash = (j > 0) ? cpath[j-1] : 0;
	unsigned long long hash = cpath[j];
	if (phash && parent_changed) {
	    // Need to see if this is still a parent of the new comb. This step
	    // is why the draw update has to come AFTER the above primitive
	    // update passes, so the comb can give us the correct, current
	    // answer.
	    bool is_parent = false;
	    if (dbis->p_c.find(phash) != dbis->p_c.end() && dbis->p_c[phash].find(hash) != dbis->p_c[phash].end())
		is_parent = true;
	    // If not we're done, whether or not the parent dp was
	    // removed from the database.
	    if (!is_parent) {
		if (invalid_paths)
		    (*invalid_paths).insert(path_hash);
		if (changed_paths)
		    (*changed_paths).erase(path_hash);
		return 1;
	    }
	    // If it's still in the comb tree, proceed with the evaluation.
	}

	bool is_removed = (dbis->removed.find(hash) != dbis->removed.end());
	bool is_changed = (dbis->changed_hashes.find(hash) != dbis->changed_hashes.end());
	if (is_removed) {
	    if (is_removed && !j) {
		// Top level removed - everything else is gone
		if (invalid_paths)
		    (*invalid_paths).insert(path_hash);
		if (changed_paths)
		    (*changed_paths).erase(path_hash);
		return 1;
	    }

	    if (is_removed && j != cpath.size()-1) {
		// If removed is first and not a leaf, erase - if we got here
		// the parent comb either wasn't changed at all or this
		// particular instance is still there; either way the state
		// here is not preservable, since the path is trying to refer
		// to a tree path which no longer exists in the hierarchy.
		if (invalid_paths)
		    (*invalid_paths).insert(path_hash);
		if (changed_paths)
		    (*changed_paths).erase(path_hash);
		return 1;
	    }
	    if (is_removed && j == cpath.size()-1) {
		// If removed is a leaf and the comb instance is intact,
		// leave "drawn" as invalid path.
		if (changed_paths)
		    (*changed_paths).insert(path_hash);
		return 2;
	    }
	}
	if (is_changed) {
	    bu_log("changed\n");
	    if (j == cpath.size()-1) {
		// Changed, but a leaf - stays drawn
		if (changed_paths)
		    (*changed_paths).insert(path_hash);
		return 0;
	    }
	    // Not a leaf - check child
	    parent_changed = true;
	    if (changed_paths)
		(*changed_paths).insert(path_hash);
	    continue;
	}

	// If we got here, reset the parent changed flag
	parent_changed = false;
    }

    // If we got through the whole path and leaf check is enabled, check if the
    // leaf of the path is a comb.  If it is, the presumption is that this path
    // is part of the active set because it is an evaluated solid, and we will
    // have to check its tree to see if anything below it changed.
    if (leaf_expand) {
	std::vector<unsigned long long> pitems = cpath;
	unsigned long long lhash = pitems[pitems.size() - 1];
	pitems.pop_back();
	int ret = leaf_check(lhash, pitems);
	if (ret == 3 && changed_paths) {
	    (*changed_paths).insert(path_hash);
	}
    }

    return 0;
}

void
BViewState::add_path(const char *path)
{
    if (!path)
	return;

    std::vector<unsigned long long> path_hashes = dbis->digest_path(path);
    add_hpath(path_hashes);
}

void
BViewState::add_hpath(std::vector<unsigned long long> &path_hashes)
{
    if (!path_hashes.size())
	return;
    staged.push_back(path_hashes);
}

void
BViewState::erase_path(int mode, int argc, const char **argv)
{
    if (!argc || !argv)
	return;

    std::unordered_map<unsigned long long, std::unordered_map<int, struct bv_scene_obj *>>::iterator sm_it;
    for (int i = 0; i < argc; i++) {
	std::vector<unsigned long long> path_hashes = dbis->digest_path(argv[i]);
	if (!path_hashes.size())
	    continue;
	unsigned long long c_hash = path_hashes[path_hashes.size() - 1];
	path_hashes.pop_back();
	erase_hpath(mode, c_hash, path_hashes, false);
    }

    // Update info AFTER all paths are fully drawn
    cache_collapsed();
}

void
BViewState::erase_hpath(int mode, unsigned long long c_hash, std::vector<unsigned long long> &path_hashes, bool cache_collapse)
{
    std::unordered_map<unsigned long long, std::unordered_map<int, struct bv_scene_obj *>>::iterator sm_it;
    std::unordered_map<unsigned long long, std::unordered_set<unsigned long long>>::iterator pc_it;
    std::unordered_map<int, std::unordered_set<unsigned long long>>::iterator m_it;
    pc_it = dbis->p_c.find(c_hash);

    path_hashes.push_back(c_hash);

    /* Cyclic - wasn't anything to draw */
    if (path_addition_cyclic(path_hashes))
	return;

    /* For some modes it's possible for combs to have evaluated objects, so
     * check regardless of whether c_hash is a solid */
    if (mode == 3 || mode == 5 || pc_it == dbis->p_c.end()) {
	unsigned long long phash = dbis->path_hash(path_hashes, 0);
	sm_it = s_map.find(phash);
	if (sm_it != s_map.end()) {
	    std::unordered_map<int, struct bv_scene_obj *>::iterator s_it;
	    if (mode < 0) {
		for (s_it = sm_it->second.begin(); s_it != sm_it->second.end(); s_it++) {
		    _bview_state_detach_leaf(s_it->second);
		    bv_obj_put(s_it->second);
		}
		for (m_it = drawn_paths.begin(); m_it != drawn_paths.end(); m_it++)
		    m_it->second.erase(phash);
		s_map.erase(phash);
	    } else {
		s_it = sm_it->second.find(mode);
		if (s_it != sm_it->second.end()) {
		    _bview_state_detach_leaf(s_it->second);
		    bv_obj_put(s_it->second);
		    sm_it->second.erase(s_it);
		    drawn_paths[mode].erase(phash);
		    s_map[phash].erase(mode);
		}
	    }

	    // In case phash is now gone from s_map check again
	    sm_it = s_map.find(phash);

	    // IFF we have removed all of the drawn elements for this path,
	    // clear it from the active sets
	    if (sm_it == s_map.end() || !sm_it->second.size()) {
		s_keys.erase(phash);
		all_drawn_paths.erase(phash);
	    }
	}
    }

    /* If we do have a comb, keep going */
    if (pc_it != dbis->p_c.end()) {
	std::unordered_set<unsigned long long>::iterator c_it;
	for (c_it = pc_it->second.begin(); c_it != pc_it->second.end(); c_it++)
	    erase_hpath(mode, *c_it, path_hashes, false);
    }

    /* Done with branch - restore path */
    path_hashes.pop_back();

    // Update info on drawn paths
    if (cache_collapse)
	cache_collapsed();
}

unsigned long long
BViewState::path_hash(std::vector<unsigned long long> &path, size_t max_len)
{
    return dbis->path_hash(path, max_len);
}

void
BViewState::depth_group_collapse(
	std::vector<std::vector<unsigned long long>> &collapsed,
	std::unordered_set<unsigned long long> &d_paths,
	std::unordered_set<unsigned long long> &p_d_paths,
	std::map<size_t, std::unordered_set<unsigned long long>> &depth_groups
	)
{
    // Whittle down the mode depth groups until we find not-fully-drawn
    // parents - when we find that, the children constitute non-collapsible
    // paths based on what's drawn in this mode
    while (depth_groups.size()) {
	size_t plen = depth_groups.rbegin()->first;
	if (plen == 1)
	    break;
	std::unordered_set<unsigned long long> &pckeys = depth_groups.rbegin()->second;

	// For a given depth, group the paths by parent path.  This results
	// in path sub-groups which will define for us how "fully drawn"
	// that particular parent comb instance is.
	std::unordered_map<unsigned long long, std::unordered_set<unsigned long long>> grouped_pckeys;
	std::unordered_map<unsigned long long, unsigned long long> pcomb;
	std::unordered_set<unsigned long long>::iterator s_it;
	for (s_it = pckeys.begin(); s_it != pckeys.end(); s_it++) {
	    std::vector<unsigned long long> &pc_path = s_keys[*s_it];
	    unsigned long long ppathhash = dbis->path_hash(pc_path, plen - 1);
	    grouped_pckeys[ppathhash].insert(*s_it);
	    pcomb[ppathhash] = pc_path[plen-2];
	}

	// For each parent/child grouping, compare it against the .g ground
	// truth set.  If they match, fully drawn and we promote the path to
	// the parent depth.  If not, the paths do not collapse further and are
	// added to drawn paths.
	std::unordered_map<unsigned long long, std::unordered_set<unsigned long long>>::iterator pg_it;
	for (pg_it = grouped_pckeys.begin(); pg_it != grouped_pckeys.end(); pg_it++) {

	    unsigned long long cpkey = *pg_it->second.begin();
	    std::vector<unsigned long long> check_path = s_keys[cpkey];
	    check_path.pop_back();

	    // As above, use the full path from the s_keys, but this time
	    // we're collecting the children.  This is the set we need to compare
	    // against the .g ground truth to determine fully or partially drawn.
	    std::unordered_set<unsigned long long> g_children;
	    std::unordered_set<unsigned long long> &g_pckeys = pg_it->second;
	    for (s_it = g_pckeys.begin(); s_it != g_pckeys.end(); s_it++) {
		std::vector<unsigned long long> &pc_path = s_keys[*s_it];
		g_children.insert(pc_path[plen-1]);
	    }

	    // Do the check against the .g comb children info - the "ground truth"
	    // that defines what must be present for a fully drawn comb
	    bool is_fully_drawn = true;
	    std::unordered_set<unsigned long long> &ground_truth = dbis->p_c[pcomb[pg_it->first]];
	    for (s_it = ground_truth.begin(); s_it != ground_truth.end(); s_it++) {
	    }
	    for (s_it = ground_truth.begin(); s_it != ground_truth.end(); s_it++) {
		if (g_children.find(*s_it) == g_children.end()) {
		    is_fully_drawn = false;
		    break;
		}
	    }

	    // All the sub-paths in this grouping are fully drawn, whether
	    // or not they define a fully drawn parent, so stash their
	    // hashes
	    for (s_it = g_pckeys.begin(); s_it != g_pckeys.end(); s_it++) {
		std::vector<unsigned long long> &path_hashes = s_keys[*s_it];
		unsigned long long thash = dbis->path_hash(path_hashes, plen);
		d_paths.insert(thash);
	    }

	    if (is_fully_drawn) {
		// If fully drawn, depth_groups[plen-1] gets the first path in
		// g_pckeys.  The path is longer than that depth, but contains
		// all the necessary information and using that approach avoids
		// the need to duplicate paths.
		depth_groups[plen - 1].insert(*g_pckeys.begin());
	    } else {
		// No further collapsing - add to final.  We must make trimmed
		// versions of the paths in case this depth holds promoted
		// paths from deeper levels, since we are duplicating the full
		// path contents.
		for (s_it = g_pckeys.begin(); s_it != g_pckeys.end(); s_it++) {
		    std::vector<unsigned long long> trimmed = s_keys[*s_it];
		    trimmed.resize(plen);
		    collapsed.push_back(trimmed);

		    // Because we're not collapsing further, any paths above this
		    // path can be considered partially drawn.
		    while (trimmed.size() - 1) {
			trimmed.pop_back();
			unsigned long long thash = dbis->path_hash(trimmed, 0);
			p_d_paths.insert(thash);
		    }
		}
	    }
	}

	// Done with this depth
	depth_groups.erase(plen);
    }

    // If we collapsed all the way to top level objects, make sure to add them
    // if they are still valid entries.  If a toplevel entry is invalid, there
    // is no parent comb to refer to it as an "invalid" object and it can no
    // longer be drawn.
    if (depth_groups.find(1) != depth_groups.end()) {
	std::unordered_set<unsigned long long> &pckeys = depth_groups[1];
	std::unordered_set<unsigned long long>::iterator s_it;
	for (s_it = pckeys.begin(); s_it != pckeys.end(); s_it++) {
	    std::vector<unsigned long long> trimmed = s_keys[*s_it];
	    trimmed.resize(1);
	    collapsed.push_back(trimmed);
	    unsigned long long thash = dbis->path_hash(trimmed, 0);
	    d_paths.insert(thash);
	}
    }
}

void
BViewState::cache_collapsed()
{
    // Group drawn paths by drawing mode type
    std::unordered_map<int, std::unordered_set<unsigned long long>> mode_map;
    std::unordered_map<unsigned long long, std::vector<unsigned long long>>::iterator sk_it;
    for (sk_it = s_keys.begin(); sk_it != s_keys.end(); sk_it++) {
	std::unordered_map<unsigned long long, std::unordered_map<int, struct bv_scene_obj *>>::iterator s_it;
	s_it = s_map.find(sk_it->first);
	if (s_it == s_map.end())
	    continue;
	std::unordered_map<int, struct bv_scene_obj *>::iterator sm_it;
	for (sm_it = s_it->second.begin(); sm_it != s_it->second.end(); sm_it++)
	    mode_map[sm_it->first].insert(sk_it->first);
    }

    // Collapse each drawing mode until the leaf is a changed dp or not fully
    // drawn.  We must do this before the comb p_c relationships are updated in
    // the context, since we want the answers for this collapse to be from the
    // prior state, not the current state.

    // Reset containers
    mode_collapsed.clear();
    drawn_paths.clear();
    partially_drawn_paths.clear();

    // Each mode is initially processed separately, so we maintain
    // awareness of the drawing state in various modes
    std::unordered_map<int, std::unordered_set<unsigned long long>>::iterator mm_it;
    for (mm_it = mode_map.begin(); mm_it != mode_map.end(); mm_it++) {

	std::map<size_t, std::unordered_set<unsigned long long>> depth_groups;
	std::unordered_set<unsigned long long> &mode_keys = mm_it->second;;
	std::unordered_set<unsigned long long>::iterator ms_it;

	// Group paths of the same depth.  Depth == 1 paths are already
	// top level objects and need no further processing.
	for (ms_it = mode_keys.begin(); ms_it != mode_keys.end(); ms_it++) {
	    std::unordered_map<unsigned long long, std::vector<unsigned long long>>::iterator k_it;
	    k_it = s_keys.find(*ms_it);
	    if (k_it->second.size() == 1) {
		mode_collapsed[mm_it->first].push_back(k_it->second);
	        unsigned long long dhash = dbis->path_hash(k_it->second, 0);
		drawn_paths[mm_it->first].insert(dhash);
	    } else {
		depth_groups[k_it->second.size()].insert(k_it->first);
	    }
	}

	depth_group_collapse(mode_collapsed[mm_it->first], drawn_paths[mm_it->first], partially_drawn_paths[mm_it->first], depth_groups);
    }


    // Having processed all the modes, we now do the same thing without regards to
    // drawing mode, to provide "who" with a mode-agnostic list of drawn paths.
    all_collapsed.clear();
    std::map<size_t, std::unordered_set<unsigned long long>> all_depth_groups;
    for (mm_it = mode_map.begin(); mm_it != mode_map.end(); mm_it++) {
	// Group paths of the same depth.  Depth == 1 paths are already
	// top level objects and need no further processing.
	std::unordered_set<unsigned long long> &mode_keys = mm_it->second;;
	std::unordered_set<unsigned long long>::iterator ms_it;
	for (ms_it = mode_keys.begin(); ms_it != mode_keys.end(); ms_it++) {
	    std::unordered_map<unsigned long long, std::vector<unsigned long long>>::iterator k_it;
	    k_it = s_keys.find(*ms_it);
	    if (k_it->second.size() == 1) {
		all_collapsed.push_back(k_it->second);
	        unsigned long long dhash = dbis->path_hash(k_it->second, 0);
		all_drawn_paths.insert(dhash);
	    } else {
		// Populate mode-agnostic container
		all_depth_groups[k_it->second.size()].insert(k_it->first);
	    }
	}
    }

    all_drawn_paths.clear();
    all_partially_drawn_paths.clear();
    depth_group_collapse(all_collapsed, all_drawn_paths, all_partially_drawn_paths, all_depth_groups);
}

struct bv_scene_obj *
BViewState::scene_obj(
	std::unordered_set<struct bv_scene_obj *> &objs,
	int curr_mode,
	const struct bsg_draw_request *vs,
	matp_t m,
       	std::vector<unsigned long long> &path_hashes,
	std::unordered_set<struct bview *> &views,
	struct bview *v
	)
{
    // Solid - scene object time
    unsigned long long phash = dbis->path_hash(path_hashes, 0);
    std::unordered_map<unsigned long long, std::unordered_map<int, struct bv_scene_obj *>>::iterator sm_it;
    sm_it = s_map.find(phash);
    struct bv_scene_obj *sp = NULL;
    if (sm_it != s_map.end()) {

	// If we have user supplied settings, we need to do some checking
	if (vs && !vs->policy.mixed_modes) {
	    // If we're not allowed to mix modes, we need to erase any modes
	    // that don't match the current mode
	    std::vector<unsigned long long> phashes = path_hashes;
	    if (phashes.size()) {
		unsigned long long c_hash = phashes[phashes.size() - 1];
		phashes.pop_back();
		std::unordered_set<int> erase_modes;
		std::unordered_map<int, struct bv_scene_obj *>::iterator s_it;
		for (s_it = sm_it->second.begin(); s_it != sm_it->second.end(); s_it++) {
		    if (s_it->first == curr_mode)
			continue;
		    erase_modes.insert(s_it->first);
		}
		std::unordered_set<int>::iterator e_it;
		for (e_it = erase_modes.begin(); e_it != erase_modes.end(); e_it++) {
		    erase_hpath(*e_it, c_hash, phashes, false);
		}
	    }
	}

	if (s_map[phash].find(curr_mode) != s_map[phash].end()) {
	    // Already have scene object - check it against vs
	    // settings to see if we need to update
	    sp = s_map[phash][curr_mode];
	    if (vs && vs->appearance.draw_mode == curr_mode) {
		if (sp->s_soldash && vs->appearance.draw_non_subtract_only) {
		    if (sp->bsg.bsg_flag != DOWN)
			sp->bsg.bsg_flag = DOWN;
		} else {
		    if (sp->bsg.bsg_flag != UP)
			sp->bsg.bsg_flag = UP;
		}
		_bsg_draw_request_copy_if_changed(sp, vs, objs);
	    }

	    // Most view setting changes won't alter geometry, and adaptive
	    // drawing updating is handled via callbacks.  However, adaptive
	    // plotting enablement/disablement must invalidate LoD state so
	    // the next bsg_lod_update pass regenerates level geometry.
	    struct bv_scene_obj *lod = NULL;
	    if (sp->bsg.bsg_kind & BSG_NODE_LOD) {
		lod = sp;
	    } else {
		/* Adaptive LoD leaves are wrapped by a direct BSG_NODE_LOD parent
		 * in BViewState::redraw (Phase L3 insertion). */
		struct bv_scene_obj *pp = reinterpret_cast<struct bv_scene_obj *>(sp->bsg.bsg_parent);
		if (pp && (pp->bsg.bsg_kind & BSG_NODE_LOD))
		    lod = pp;
	    }
	    if (lod) {
		std::unordered_set<struct bview *>::iterator v_it;
		for (v_it = views.begin(); v_it != views.end(); v_it++) {
		    int adaptive_on = 0;
		    if (sp->csg_obj)
			adaptive_on = ((*v_it)->gv_s->adaptive_plot_csg) ? 1 : 0;
		    if (sp->mesh_obj)
			adaptive_on = ((*v_it)->gv_s->adaptive_plot_mesh) ? 1 : 0;
		    if (ged_lod_adaptive_toggle_sync(lod, *v_it, adaptive_on)) {
			bv_obj_stale(sp);
			objs.insert(sp);
		    }
		}
	    }

	    // Refresh s_color from the current path color so that material
	    // changes (or a cache corruption that left stale values) are always
	    // reflected before draw_scene_obj is invoked.
	    {
		struct bu_color c;
		dbis->path_color(&c, path_hashes);
		bu_color_to_rgb_chars(&c, sp->s_color);
		if (vs && vs->material.use_override_color) {
		    sp->s_color[0] = vs->material.override_rgb[0];
		    sp->s_color[1] = vs->material.override_rgb[1];
		    sp->s_color[2] = vs->material.override_rgb[2];
		}
	    }

	    return NULL;
	}
    }

    // No pre-existing object - make a new one
    sp = bv_obj_get_unregistered(v, BV_DB_OBJS);

    // Find the leaf directory pointer
    struct directory *dp = dbis->get_hdp(path_hashes[path_hashes.size()-1]);
    if (!dp) {
	bu_log("dbi_state.cpp:%d - dp lookup failed!\n", __LINE__);
	return NULL;
    }

    // Prepare draw data
    struct rt_wdb *wdbp = wdb_dbopen(dbis->gedp->dbip, RT_WDB_TYPE_DB_DEFAULT);
    struct draw_update_data_t *ud;
    BU_GET(ud, struct draw_update_data_t);
    ud->dbip = dbis->gedp->dbip;
    ud->tol = &wdbp->wdb_tol;
    ud->ttol = &wdbp->wdb_ttol;
    ud->mesh_c = dbis->gedp->ged_lod;
    sp->dp = dp;
    sp->s_i_data = (void *)ud;

    // Get color from path, unless we're overridden
    struct bu_color c;
    dbis->path_color(&c, path_hashes);
    bu_color_to_rgb_chars(&c, sp->s_color);
    if (vs && vs->material.use_override_color) {
	// TODO - shouldn't be using s_color for the override...
	sp->s_color[0] = vs->material.override_rgb[0];
	sp->s_color[1] = vs->material.override_rgb[1];
	sp->s_color[2] = vs->material.override_rgb[2];
    }

    // Phase 4 / Phase 12: set drawing mode/style/material via split draw request.
    {
	struct bsg_draw_request sinfo;
	bsg_node_draw_request_get((const bsg_node *)sp, &sinfo);
	sinfo.appearance.draw_mode = curr_mode;
	if (vs && vs->appearance.line_width)
	    sinfo.appearance.line_width = vs->appearance.line_width;
	if (vs) {
	    sinfo.material.transparency = vs->material.transparency;
	    sinfo.material.rgba[3] = vs->material.rgba[3];
	    sinfo.material.use_override_color = vs->material.use_override_color;
	    sinfo.material.override_rgb[0] = vs->material.override_rgb[0];
	    sinfo.material.override_rgb[1] = vs->material.override_rgb[1];
	    sinfo.material.override_rgb[2] = vs->material.override_rgb[2];
	    sinfo.policy.mixed_modes = vs->policy.mixed_modes;
	}
	bsg_node_draw_request_set((bsg_node *)sp, &sinfo);
    }

    // Tell scene object what the current matrix is
    if (m) {
	MAT_COPY(sp->s_mat, m);
    } else {
	dbis->get_path_matrix(sp->s_mat, path_hashes);
    }

    // Assign the bounding box (needed for pre-adaptive-plot
    // autoview)
    dbis->get_path_bbox(&sp->bmin, &sp->bmax, path_hashes);

    // Adaptive also needs s_size and s_center to be set
    sp->s_center[X] = (sp->bmin[X] + sp->bmax[X]) * 0.5;
    sp->s_center[Y] = (sp->bmin[Y] + sp->bmax[Y]) * 0.5;
    sp->s_center[Z] = (sp->bmin[Z] + sp->bmax[Z]) * 0.5;
    sp->s_size = sp->bmax[X] - sp->bmin[X];
    V_MAX(sp->s_size, sp->bmax[Y] - sp->bmin[Y]);
    V_MAX(sp->s_size, sp->bmax[Z] - sp->bmin[Z]);
    sp->have_bbox = 1;

    // If we're drawing a subtraction and we're not overridden, set the
    // appropriate flag for dashed line drawing
    if (vs && !vs->appearance.draw_solid_lines_only) {
	bool is_subtract = dbis->path_is_subtraction(path_hashes);
	sp->s_soldash = (is_subtract) ? 1 : 0;
    }

    // Align with vs draw_non_subtract_only settings
    if (vs && vs->appearance.draw_mode == curr_mode) {
        if (sp->s_soldash && vs->appearance.draw_non_subtract_only) {
            if (sp->bsg.bsg_flag != DOWN)
                sp->bsg.bsg_flag = DOWN;
        } else {
            if (sp->bsg.bsg_flag != UP)
                sp->bsg.bsg_flag = UP;
        }
    }

    dbis->print_path(&sp->bsg.bsg_name, path_hashes);
    /* Phase 4 / Phase 12: read draw mode back from BSG appearance for the s_map key. */
    {
	struct bsg_appearance _kinfo;
	bsg_node_appearance_get((const bsg_node *)sp, &_kinfo);
	s_map[phash][_kinfo.draw_mode] = sp;
    }
    s_keys[phash] = path_hashes;

    /* Phase B-full: the leaf is allocated via bv_obj_get_unregistered so it
     * does NOT enter any gv_objs ptbl.  The BSG tree (gd_draw_root) is the
     * sole index for rendering and iteration (bv_view_objs_visit_db).
     * BViewState's s_map[phash][mode] continues to own the path-hash
     * → bv_scene_obj mapping lifetime. */
    if (dbis && dbis->gedp)
	_bview_state_attach_leaf(dbis->gedp, sp, path_hashes, dbis);

    // Final geometry generation is deferred - see draw_scene
    objs.insert(sp);

    return sp;
}


void
BViewState::walk_tree(
	std::unordered_set<struct bv_scene_obj *> &objs,
	unsigned long long chash,
	int curr_mode,
	struct bview *v,
	const struct bsg_draw_request *vs,
	matp_t m,
       	std::vector<unsigned long long> &path_hashes,
	std::unordered_set<struct bview *> &views,
	unsigned long long *ret
	)
{
    size_t op = OP_UNION;
    std::unordered_map<unsigned long long, std::unordered_map<unsigned long long, size_t>>::iterator b_it;
    b_it = dbis->i_bool.find(path_hashes[path_hashes.size() - 1]);
    if (b_it != dbis->i_bool.end()) {
	std::unordered_map<unsigned long long, size_t>::iterator bb_it;
	bb_it = b_it->second.find(chash);
	if (bb_it != b_it->second.end()) {
	    op = bb_it->second;
	}
    }

    if (op == OP_SUBTRACT && vs && vs->appearance.draw_solid_lines_only)
	return;

    mat_t lm;
    MAT_IDN(lm);
    unsigned long long phash = path_hashes[path_hashes.size() - 1];
    dbis->get_matrix(lm, phash, chash);

    gather_paths(objs, chash, curr_mode, v, vs, m, lm, path_hashes, views, ret);
}

// Note - by the time we are using gather_paths, any existing objects
// already defined are assumed to be updated/valid - only create
// missing objects.
void
BViewState::gather_paths(
	std::unordered_set<struct bv_scene_obj *> &objs,
	unsigned long long c_hash,
	int curr_mode,
	struct bview *v,
	const struct bsg_draw_request *vs,
	matp_t m,
       	matp_t lm,
	std::vector<unsigned long long> &path_hashes,
	std::unordered_set<struct bview *> &views,
	unsigned long long *ret
	)
{
    std::unordered_map<unsigned long long, std::unordered_set<unsigned long long>>::iterator pc_it;
    pc_it = dbis->p_c.find(c_hash);

    struct directory *dp = dbis->get_hdp(c_hash);
    path_hashes.push_back(c_hash);

    mat_t om, nm;
    /* Update current matrix state to reflect the new branch of
     * the tree. Either we have a local matrix, or we have an
     * implicit IDN matrix. */
    MAT_COPY(om, m);
    if (lm) {
	MAT_COPY(nm, lm);
    } else {
	MAT_IDN(nm);
    }
    bn_mat_mul(m, om, nm);

    if (pc_it != dbis->p_c.end()) {
	// Two things may prevent further processing of a comb - a hidden dp, or
	// a cyclic path.
	if (dp && !(dp->d_flags & RT_DIR_HIDDEN) && pc_it->second.size() && !path_addition_cyclic(path_hashes)) {
	    /* Keep going */
	    std::unordered_set<unsigned long long>::iterator c_it;
	    for (c_it = pc_it->second.begin(); c_it != pc_it->second.end(); c_it++) {
		walk_tree(objs, *c_it, curr_mode, v, vs, m, path_hashes, views, ret);
	    }
	} else {
	    // Comb without children - (empty) scene object time
	    scene_obj(objs, curr_mode, vs, m, path_hashes, views, v);
	}
    } else {
	// Solid - scene object time
	struct bv_scene_obj *nobj = scene_obj(objs, curr_mode, vs, m, path_hashes, views, v);
	if (nobj && ret)
	    (*ret) |= GED_DBISTATE_VIEW_CHANGE;
    }
    /* Done with branch - restore path, put back the old matrix state,
     * and restore previous color settings */
    path_hashes.pop_back();
    MAT_COPY(m, om);
}

void
BViewState::clear()
{
    /* Phase B-full: detach and free every leaf.  After B-full-1 the leaves are
     * allocated via bv_obj_get_unregistered, so they are NOT in any gv_objs
     * ptbl.  We must explicitly call bv_obj_put to return them to the free
     * list; relying on a subsequent bv_clear() would leak. */
    if (dbis && dbis->gedp) {
	for (auto &kv : s_map) {
	    for (auto &mkv : kv.second) {
		_bview_state_detach_leaf(mkv.second);
		bv_obj_put(mkv.second);
	    }
	}
    }

    s_map.clear();
    s_keys.clear();
    staged.clear();
    drawn_paths.clear();
    all_drawn_paths.clear();
    partially_drawn_paths.clear();
    mode_collapsed.clear();
    all_collapsed.clear();
}

size_t
BViewState::drain_geom_results()
{
    if (!dbis)
	return 0;
    return dbis->drain_geom_results();
}

std::vector<std::string>
BViewState::list_drawn_paths(int mode, bool list_collapsed)
{
    std::unordered_map<int, std::vector<std::vector<unsigned long long>>>::iterator m_it;
    std::vector<std::string> ret;
    if (mode == -1 && list_collapsed) {
	struct bu_vls vpath = BU_VLS_INIT_ZERO;
	for (size_t i = 0; i < all_collapsed.size(); i++) {
	    dbis->print_path(&vpath, all_collapsed[i]);
	    ret.push_back(std::string(bu_vls_cstr(&vpath)));
	}
    }
    if (mode != -1 && list_collapsed) {
	m_it = mode_collapsed.find(mode);
	if (m_it == mode_collapsed.end())
	    return ret;
    	struct bu_vls vpath = BU_VLS_INIT_ZERO;
	for (size_t i = 0; i < m_it->second.size(); i++) {
	    dbis->print_path(&vpath, m_it->second[i]);
	    ret.push_back(std::string(bu_vls_cstr(&vpath)));
	}
    }
    if (mode == -1 && !list_collapsed) {
	struct bu_vls vpath = BU_VLS_INIT_ZERO;
	std::unordered_map<unsigned long long, std::vector<unsigned long long>>::iterator k_it;
	for (k_it = s_keys.begin(); k_it != s_keys.end(); k_it++) {
	    dbis->print_path(&vpath, k_it->second);
	    ret.push_back(std::string(bu_vls_cstr(&vpath)));
	}
    }
    if (mode != -1 && !list_collapsed) {
	struct bu_vls vpath = BU_VLS_INIT_ZERO;
	std::unordered_map<unsigned long long, std::unordered_map<int, struct bv_scene_obj *>>::iterator sm_it;
	for (sm_it = s_map.begin(); sm_it != s_map.end(); sm_it++) {
	    if (sm_it->second.find(mode) == sm_it->second.end())
		continue;
	    dbis->print_path(&vpath, s_keys[sm_it->first]);
	    ret.push_back(std::string(bu_vls_cstr(&vpath)));
	}
    }

    std::sort(ret.begin(), ret.end(), &alphanum_cmp);

    return ret;
}

size_t
BViewState::count_drawn_paths(int mode, bool list_collapsed)
{
    std::unordered_map<int, std::vector<std::vector<unsigned long long>>>::iterator m_it;
    std::vector<std::string> ret;
    if (mode == -1 && list_collapsed)
	return all_collapsed.size();

    if (mode != -1 && list_collapsed) {
	m_it = mode_collapsed.find(mode);
	if (m_it == mode_collapsed.end())
	    return m_it->second.size();
	return 0;
    }

    if (mode == -1 && !list_collapsed)
	return s_keys.size();

    if (mode != -1 && !list_collapsed) {
	std::unordered_map<unsigned long long, std::unordered_map<int, struct bv_scene_obj *>>::iterator sm_it;
	sm_it = s_map.find(mode);
	if (sm_it != s_map.end())
	    return sm_it->second.size();
	return 0;
    }

    return 0;
}

int
BViewState::is_hdrawn(int mode, unsigned long long phash)
{
    if (mode == -1) {
	if (all_drawn_paths.find(phash) != all_drawn_paths.end())
	    return 1;
	if (all_partially_drawn_paths.find(phash) != all_partially_drawn_paths.end())
	    return 2;
	return 0;
    }

    if (drawn_paths.find(mode) == drawn_paths.end())
	return 0;

    if (drawn_paths[mode].find(phash) != drawn_paths[mode].end())
	return 1;
    if (partially_drawn_paths[mode].find(phash) != partially_drawn_paths[mode].end())
	return 2;
    return 0;
}

unsigned long long
BViewState::refresh(struct bview *v, int argc, const char **argv)
{
    if (!v)
	return 0;

    bv_log(1, "BViewState::refresh");
    // We (well, callers) need to be able to tell if the redraw pass actually
    // changed anything.
    unsigned long long ret = 0;

    // Make sure the view knows how to update the oriented bounding box
    v->gv_bounds_update = &bv_view_bounds;

    // If we have specific paths specified, the leaves of those paths
    // denote which paths need refreshing.  We need to process them
    // and turn them to hashes, so we can check the s_keys hash vectors
    // for the presence of "hashes of interest".
    //
    // Note - this is too aggressive, in that it will result in refreshing
    // of objects that have the leaf in their paths but don't match the
    // parent full path.  However, checking the full parent path is more
    // complicated without an n^2 order performance problem, so for the
    // moment we punt and use the more aggressive redraw solution.
    std::unordered_set<unsigned long long> active_hashes;
    for (int i = 0; i < argc; i++) {
	std::vector<unsigned long long> phashes = dbis->digest_path(argv[i]);
	active_hashes.insert(phashes[phashes.size() - 1]);
    }

    // Objects may be "drawn" in different ways - wireframes, shaded,
    // evaluated.  How they must be redrawn is mode dependent.
    std::unordered_map<int, std::unordered_set<unsigned long long>> mode_map;
    std::unordered_map<unsigned long long, std::vector<unsigned long long>>::iterator sk_it;
    for (sk_it = s_keys.begin(); sk_it != s_keys.end(); sk_it++) {
	std::unordered_map<unsigned long long, std::unordered_map<int, struct bv_scene_obj *>>::iterator s_it;
	s_it = s_map.find(sk_it->first);
	if (s_it == s_map.end())
	    continue;

	// If we have specified objects, we only refresh if the path
	// involves a hash of interest
	if (active_hashes.size()) {
	    int active = 0;
	    for (size_t i = 0; i < sk_it->second.size(); i++) {
		if (active_hashes.find(sk_it->second[i]) != active_hashes.end()) {
		    active = 1;
		    break;
		}
	    }
	    if (!active)
		continue;
	}

	std::unordered_map<int, struct bv_scene_obj *>::iterator sm_it;
	for (sm_it = s_it->second.begin(); sm_it != s_it->second.end(); sm_it++) {
	    mode_map[sm_it->first].insert(sk_it->first);
	}
    }

    // Redo drawing based on current db info - color, matrix, and geometry
    std::unordered_map<int, std::unordered_set<unsigned long long>>::iterator mm_it;
    for (mm_it = mode_map.begin(); mm_it != mode_map.end(); mm_it++) {
	std::unordered_set<unsigned long long> &mkeys = mm_it->second;
	std::unordered_set<unsigned long long>::iterator k_it;
	for (k_it = mkeys.begin(); k_it != mkeys.end(); k_it++) {
	    std::vector<unsigned long long> &cp = s_keys[*k_it];
	    struct bv_scene_obj *s = NULL;
	    if (s_map.find(*k_it) != s_map.end()) {
		if (s_map[*k_it].find(mm_it->first) != s_map[*k_it].end())
		    s = s_map[*k_it][mm_it->first];
	    }
	    if (!s)
		continue;
	    struct bv_scene_obj *nso = bv_obj_get_unregistered(v, BV_DB_OBJS);
	    bv_obj_sync(nso, s);
	    nso->s_i_data = s->s_i_data;
	    s->s_i_data = NULL;
	    s_map[*k_it].erase(mm_it->first);
	    ret = GED_DBISTATE_VIEW_CHANGE;

	    // print path name, set view - otherwise empty
	    dbis->print_path(&nso->bsg.bsg_name, cp);
	    nso->s_v = v;
	    nso->dp = s->dp;
	    s_map[*k_it][mm_it->first] = nso;

	    /* Phase B: replace the BSG-attached predecessor with the new
	     * synced object so the draw tree remains consistent. */
	    if (dbis && dbis->gedp) {
		_bview_state_detach_leaf(s);
		_bview_state_attach_leaf(dbis->gedp, nso, cp, dbis);
	    }

	    bv_log(3, "refresh %s[%s]", bu_vls_cstr(&(nso->bsg.bsg_name)), bu_vls_cstr(&(v->gv_name)));
	    draw_scene(nso, v);
	    bv_obj_put(s);
	}
    }

    // Do selection sync
    SelectionSet *ss = dbis->find_selected_state(NULL);
    if (ss) {
	if (ss->draw_sync())
	    ret |= GED_DBISTATE_VIEW_CHANGE;
    }

    return ret;
}

unsigned long long
BViewState::redraw(const struct bsg_settings *vs, std::unordered_set<struct bview *> &views, int no_autoview)
{
    bv_log(1, "BViewState::redraw");
    // We (well, callers) need to be able to tell if the redraw pass actually
    // changed anything.
    unsigned long long ret = 0;

    if (!views.size())
	return 0;

    struct bsg_draw_request draw_request;
    const struct bsg_draw_request *vr = NULL;
    if (vs) {
	bsg_draw_request_from_settings(&draw_request, vs);
	vr = &draw_request;
    }

    // Make sure the views know how to update the oriented bounding box
    std::unordered_set<struct bview *>::iterator v_it;
    for (v_it = views.begin(); v_it != views.end(); v_it++) {
	struct bview *v = *v_it;
	v->gv_bounds_update = &bv_view_bounds;
    }

    // For most operations on objects, we need only the current view (for
    // independent views) or a single instance of any representative view (for
    // shared state views).
    struct bview *v = NULL;
    if (views.size() == 1)
	v = (*(views.begin()));
    if (!v && views.size() > 1) {
	// If we have multiple views, we want a non-independent view
	for (v_it = views.begin(); v_it != views.end(); v_it++) {
	    struct bview *nv = *v_it;
	    if (bv_view_is_independent(nv))
		continue;
	    v = nv;
	    break;
	}
    }

    // The principle for redrawing will be that anything that was previously
    // fully drawn should stay fully drawn, even if its tree structure has
    // changed.
    //
    // In order to accommodate autoview requirements, final geometry
    // drawing has to be delayed until after the initial scene objects are
    // created.  Make a set to track which objects we need to draw in the
    // finalization stage.
    std::unordered_set<struct bv_scene_obj *> objs;


    // First order of business is to go through already drawn solids, if any,
    // and remove no-longer-valid paths. Keep still-valid paths to avoid the
    // work of re-generating the scene objects.
    std::unordered_set<unsigned long long> invalid_paths;
    std::unordered_set<unsigned long long> changed_paths;
    std::unordered_map<unsigned long long, std::vector<unsigned long long>>::iterator sk_it;
    for (sk_it = s_keys.begin(); sk_it != s_keys.end(); sk_it++) {
	// Work down from the root of each path looking for the first changed or
	// removed entry.
	std::vector<unsigned long long> &cpath = sk_it->second;
	check_status(&invalid_paths, &changed_paths, sk_it->first, cpath, true);
    }

    // Invalid path objects we remove completely
    std::unordered_set<unsigned long long>::iterator iv_it;
    for (iv_it = invalid_paths.begin(); iv_it != invalid_paths.end(); iv_it++) {
	std::vector<unsigned long long> &phashes = s_keys[*iv_it];
	if (!phashes.size())
	    continue;
	unsigned long long c_hash = phashes[phashes.size() - 1];
	phashes.pop_back();
	erase_hpath(-1, c_hash, phashes, false);
    }

    // Objects may be "drawn" in different ways - wireframes, shaded, evaluated.
    // How they must be redrawn in the event of a database change is mode dependent,
    // so after removing the invalid paths we categorize active paths according to
    // which modes they are being visualized with.
    std::unordered_map<int, std::unordered_set<unsigned long long>> mode_map;
    for (sk_it = s_keys.begin(); sk_it != s_keys.end(); sk_it++) {
	std::unordered_map<unsigned long long, std::unordered_map<int, struct bv_scene_obj *>>::iterator s_it;
	s_it = s_map.find(sk_it->first);
	if (s_it == s_map.end())
	    continue;
	std::unordered_map<int, struct bv_scene_obj *>::iterator sm_it;
	for (sm_it = s_it->second.begin(); sm_it != s_it->second.end(); sm_it++) {
	    mode_map[sm_it->first].insert(sk_it->first);
	}
    }

    // Changed paths we redo based on current db info - color, matrix, and
    // geometry if the entry isn't invalid.  This is the step that ensures any
    // surviving solid objects in the drawing state are current for subsequent
    // operations (and thus valid to reuse)
    std::unordered_map<int, std::unordered_set<unsigned long long>>::iterator mm_it;
    for (mm_it = mode_map.begin(); mm_it != mode_map.end(); mm_it++) {
	for (iv_it = changed_paths.begin(); iv_it != changed_paths.end(); iv_it++) {
	    if (mm_it->second.find(*iv_it) == mm_it->second.end())
		continue;
	    std::vector<unsigned long long> &cp = s_keys[*iv_it];
	    struct bv_scene_obj *s = NULL;
	    if (s_map.find(*iv_it) != s_map.end()) {
		if (s_map[*iv_it].find(mm_it->first) != s_map[*iv_it].end()) {
		    ret = GED_DBISTATE_VIEW_CHANGE;
		    s = s_map[*iv_it][mm_it->first];
		}
	    }
	    if (dbis->invalid_entry_map.find(cp[cp.size() - 1]) != dbis->invalid_entry_map.end()) {
		if (s) {
		    // Invalid - remove any scene object geometry
		    ret = GED_DBISTATE_VIEW_CHANGE;
		    bv_obj_reset(s);
		    s->s_v = v;
		} else {
		    s = bv_obj_get_unregistered(v, BV_DB_OBJS);
		    // print path name, set view - otherwise empty
		    dbis->print_path(&s->bsg.bsg_name, cp);
		    s->s_v = v;
		    s_map[*iv_it][mm_it->first] = s;
		}
		continue;
	    }
	    if (s) {
		// Geometry is suspect - clear to prepare for regeneration
		_bview_state_detach_leaf(s);
		bv_obj_put(s);
		s_map[*iv_it].erase(mm_it->first);
		ret = GED_DBISTATE_VIEW_CHANGE;
	    }
	}
    }

    // Evaluate prior collapsed paths according to the same validity criteria,
    // then re-expand them
    std::unordered_map<int, std::vector<std::vector<unsigned long long>>>::iterator ms_it;
    for (ms_it = mode_collapsed.begin(); ms_it != mode_collapsed.end(); ms_it++) {
	std::unordered_set<size_t> active_collapsed;
	std::unordered_set<size_t> draw_invalid_collapsed;
	for (size_t i = 0; i < ms_it->second.size(); i++) {
	    std::vector<unsigned long long> &cpath = ms_it->second[i];
	    int sret = check_status(NULL, NULL, 0, cpath, true);
	    if (sret == 2)
		draw_invalid_collapsed.insert(i);
	    if (sret == 0)
		active_collapsed.insert(i);
	}

	// Expand active collapsed paths to solids, creating any missing path objects
	//
	// NOTE:  We deliberately do NOT pass the supplied vs (if any) to these scene_obj/
	// gather_paths calls - user specified override settings should be applied only to
	// staged paths specified by the user.  Pre-existing geometry NOT specified by
	// those commands does not get those settings applied during redraw.
	std::unordered_set<size_t>::iterator sz_it;
	for (sz_it = active_collapsed.begin(); sz_it != active_collapsed.end(); sz_it++) {
	    std::vector<unsigned long long> cpath = ms_it->second[*sz_it];
	    mat_t m;
	    MAT_IDN(m);
	    dbis->get_path_matrix(m, cpath);
	    if (ms_it->first == 3 || ms_it->first == 5) {
		dbis->get_path_matrix(m, cpath);
		scene_obj(objs, ms_it->first, NULL, m, cpath, views, v);
		continue;
	    }
	    unsigned long long ihash = cpath[cpath.size() - 1];
	    cpath.pop_back();
	    gather_paths(objs, ihash, ms_it->first, v, NULL, m, NULL, cpath, views, &ret);
	}
	for (sz_it = draw_invalid_collapsed.begin(); sz_it != draw_invalid_collapsed.end(); sz_it++) {
	    std::vector<unsigned long long> cpath = ms_it->second[*sz_it];
	    struct bv_scene_obj *s = bv_obj_get_unregistered(v, BV_DB_OBJS);
	    // print path name, set view - otherwise empty
	    dbis->print_path(&s->bsg.bsg_name, cpath);
	    s->s_v = v;
	    s_map[ms_it->first][*iv_it] = s;

	    // NOTE: Because there is no geometry to update, these scene objs
	    // are not added to objs
	}
    }

    // Expand (or queue, depending on settings) any staged paths.
    if (vr) {
	for (size_t i = 0; i < staged.size(); i++) {
	    std::vector<unsigned long long> cpath = staged[i];
	    // Validate this path - if the user has specified an invalid
	    // path, there's nothing else to do
	    if (!dbis->valid_hash_path(cpath))
		continue;
	    unsigned long long phash = dbis->path_hash(cpath, 0);
	    if (check_status(NULL, NULL, phash, cpath, false))
		continue;
	    mat_t m;
	    MAT_IDN(m);
	    dbis->get_path_matrix(m, cpath);
	    if ((vr->appearance.draw_mode == 3 || vr->appearance.draw_mode == 5)) {
		dbis->get_path_matrix(m, cpath);
		scene_obj(objs, vr->appearance.draw_mode, vr, m, cpath, views, v);
		continue;
	    }
	    unsigned long long ihash = cpath[cpath.size() - 1];
	    cpath.pop_back();
	    gather_paths(objs, ihash, vr->appearance.draw_mode, v, vr, m, NULL, cpath, views, &ret);
	}
    }
    // Staged paths are now added (as long as settings were supplied) - clear the queue
    staged.clear();

    // Do a preliminary autoview, unless suppressed, so any adaptive plotting
    // routines have a rough idea of the correct dimensions to use
    if (!no_autoview) {
	for (v_it = views.begin(); v_it != views.end(); v_it++) {
	    bv_autoview(*v_it, BV_AUTOVIEW_SCALE_DEFAULT, 0);
	}
    }

    // Update geometry.  draw_scene will avoid repeat creation of geometry
    // when s is not adaptive, but if s IS adaptive we need unique geometry
    // for each view even though the BViewState is shared - camera settings,
    // which are unique to each bview, may differ and adaptive geometry must
    // reflect that.
    //
    // Note that this is the ONLY situation where we must care about each
    // view individually for shared state - the above uses of the first view
    // work for the "top level" object used for adaptive cases, since shared
    // views will be using a shared object pool for anything other than their
    // view specific geometry sub-objects.
    for (v_it = views.begin(); v_it != views.end(); v_it++) {
	std::unordered_set<struct bv_scene_obj *>::iterator o_it;
	for (o_it = objs.begin(); o_it != objs.end(); o_it++) {
	    bv_log(3, "redraw %s[%s]", bu_vls_cstr(&((*(*o_it)).bsg.bsg_name)), bu_vls_cstr(&((*(*v_it)).gv_name)));
	    draw_scene(*o_it, *v_it);
	}
    }

    struct bview *first_view = NULL;
    if (v) {
	first_view = v;
    } else if (!views.empty()) {
	first_view = *(views.begin());
    }
    if (first_view) {
	for (auto sp : objs) {
	    if (!sp)
		continue;
	    if (!(sp->mesh_obj || sp->csg_obj))
		continue;
	    struct bv_scene_obj *pp = (struct bv_scene_obj *)sp->bsg.bsg_parent;
	    if (pp && (pp->bsg.bsg_kind & BSG_NODE_LOD))
		continue;
	    struct bv_scene_obj *lod = (struct bv_scene_obj *)bsg_lod_node_insert_above((bsg_node *)sp, first_view);
	    if (!lod)
		continue;
	    /* mesh_obj takes precedence if both flags are set */
	    if (sp->mesh_obj)
		ged_lod_install_mesh_ops(lod, sp);
	    else if (sp->csg_obj)
		ged_lod_install_csg_ops(lod, sp);
	    for (auto lv : views)
		bsg_lod_node_get_cursor((bsg_node *)lod, lv);
	}
    }

    // Phase 3.5: Queue newly drawn solid objects into the DrawPipeline so
    // that OBB, AABB (if not yet cached) and LoD are computed in the
    // background.  The pipeline supplements the synchronous AABB already
    // stored in bboxes[] — it will not overwrite a synchronous bbox but
    // will add OBB data (new in 3.5) and trigger LoD caching.
    if (dbis->draw_pipeline_) {
	std::vector<DrawPipeline::WorkItem> items;
	std::unordered_map<unsigned long long, std::unordered_map<int, struct bv_scene_obj *>>::iterator obb_it;
	for (obb_it = s_map.begin(); obb_it != s_map.end(); obb_it++) {
	    unsigned long long phash = obb_it->first;
	    // Only queue objects that were part of this redraw pass
	    bool in_pass = false;
	    for (auto &mm : obb_it->second) {
		if (mm.second && objs.find(mm.second) != objs.end()) {
		    in_pass = true; break;
		}
	    }
	    if (!in_pass) continue;
	    // Only queue primitives (non-combs) not already in the obbs map
	    if (dbis->obbs.find(phash) != dbis->obbs.end())
		continue;
	    struct directory *dp = dbis->get_hdp(phash);
	    if (!dp || (dp->d_flags & RT_DIR_COMB))
		continue;
	    items.push_back({phash, dp});
	}
	if (!items.empty())
	    dbis->start_geom_load(items);
    }

    // We need to check if any drawn solids are selected.  If so, we need
    // to illuminate them.  This is what ensures that newly drawn solids
    // respect a previously selected set from the command line
    SelectionSet *ss = dbis->find_selected_state(NULL);
    if (ss) {
	if (invalid_paths.size() || changed_paths.size()) {
	    ss->refresh();
	    ss->collapse();
	}
	if (ss->draw_sync())
	    ret |= GED_DBISTATE_VIEW_CHANGE;
    }
    // Now that we have the finalized geometry, do a finishing autoview,
    // unless suppressed
    if (!no_autoview) {
	for (v_it = views.begin(); v_it != views.end(); v_it++) {
	    bv_autoview(*v_it, BV_AUTOVIEW_SCALE_DEFAULT, 0);
	}
    }

    // Now that all path manipulations are finalized, update the
    // sets of drawn paths
    cache_collapsed();

    // Phase F (drawing_stack_modernization): bsg_root is now an alias for
    // gv_draw_root; bsg_root->bsg.bsg_children is maintained live by the draw-tree
    // mutations above.  No per-view sync is needed here.

    return ret;
}

void
BViewState::print_view_state(struct bu_vls *outvls)
{
    struct bu_vls *o = outvls;
    if (!o) {
	BU_GET(o, struct bu_vls);
	bu_vls_init(o);
    }

    bu_vls_printf(o, "Object count: %zd\n", s_keys.size());
    std::unordered_map<unsigned long long, std::vector<unsigned long long>>::iterator k_it;
    for (k_it = s_keys.begin(); k_it != s_keys.end(); k_it++) {
	std::vector<unsigned long long> &path = k_it->second;
	struct bu_vls pstr = BU_VLS_INIT_ZERO;
	dbis->print_path(&pstr, path, 0, 1);
	bu_vls_printf(o, "%s\n", bu_vls_cstr(&pstr));
	bu_vls_free(&pstr);
    }

    if (o != outvls) {
	bu_vls_free(o);
	BU_PUT(o, struct bu_vls);
    }
}

// Added dps present their own challenge, in terms of whether or not to
// automatically draw them.  (I think this decision comes after the existing
// draw paths' removed/changed processing and the main .g reflecting maps are
// updated.)  The cases:
//
// 1.  Already part of a drawn invalid path - draw and expand, as path was
// drawn but is no longer invalid
//
// 2.  Already part of a non-drawn invalid path - do not draw (? - could see
// a case for either behavior here, if the user wants to see the instances
// of the newly enabled part... this may have to be a user option)
//
// 2.  Not part of any path, pre or post removed/changed draw states (i.e.
// a tops object) - draw




/* Handle selection status for various instances in the database */

namespace {
struct selection_sync_ctx {
    DbiState *dbis;
    std::unordered_map<unsigned long long, std::vector<unsigned long long>> *selected;
};

static int
_selset_sync_from_bsg_cb(const struct bsg_selection_entry *e, void *data)
{
    struct selection_sync_ctx *ctx = (struct selection_sync_ctx *)data;
    if (!ctx || !ctx->dbis || !ctx->selected || !e || !e->src_path)
	return 1;

    std::vector<unsigned long long> path_hashes = ctx->dbis->digest_path(e->src_path);
    if (path_hashes.empty()) {
	bu_log("%s: skipping invalid selection path %s\n", __func__, e->src_path);
	return 1;
    }

    unsigned long long phash = ctx->dbis->path_hash(path_hashes, 0);
    (*(ctx->selected))[phash] = path_hashes;
    return 1;
}
}

SelectionSet::SelectionSet(DbiState *s, const char *name)
{
    dbis = s;
    set_name = (name && name[0]) ? name : "active";
}

struct bsg_selection_set *
SelectionSet::bsg_set(bool create) const
{
    if (!dbis || !dbis->gedp)
	return NULL;

    struct bview *v = dbis->gedp->ged_gvp;
    if (!v) {
	struct bu_ptbl *views = bv_set_views(&dbis->gedp->ged_views);
	if (views && BU_PTBL_LEN(views) > 0)
	    v = (struct bview *)BU_PTBL_GET(views, 0);
    }
    if (!v)
	return NULL;

    if (!v->bsg_root)
	bsg_scene_root_create(v);
    if (!v->bsg_root)
	return NULL;

    return bsg_scene_selection_get((bsg_node *)v->bsg_root,
	    set_name.c_str(), create ? 1 : 0);
}

void
SelectionSet::sync_from_bsg()
{
    struct bsg_selection_set *ss = bsg_set(false);
    if (!ss)
	return;

    selected.clear();
    struct selection_sync_ctx ctx = {dbis, &selected};
    bsg_selection_visit(ss, _selset_sync_from_bsg_cb, &ctx);
}

void
SelectionSet::sync_to_bsg() const
{
    struct bsg_selection_set *ss = bsg_set(true);
    if (!ss || !dbis)
	return;

    bsg_selection_clear(ss);

    struct bu_vls vpath = BU_VLS_INIT_ZERO;
    for (auto &sel_entry : selected) {
	struct bsg_selection_entry e = {};
	std::vector<unsigned long long> path_hashes = sel_entry.second;
	dbis->print_path(&vpath, path_hashes);
	e.src_path = (char *)bu_vls_cstr(&vpath);
	e.kind = (path_hashes.size() > 1) ? BSG_SELECTION_INSTANCE : BSG_SELECTION_NODE;
	bsg_selection_add(ss, &e);
	bu_vls_trunc(&vpath, 0);
    }
    bu_vls_free(&vpath);
}

bool
SelectionSet::select_path(const char *path, bool update)
{
    if (!path)
	return false;

    std::vector<unsigned long long> path_hashes = dbis->digest_path(path);
    if (!path_hashes.size())
	return false;

    bool ret = select_hpath(path_hashes);
    if (update)
	characterize();
    return ret;
}

bool
SelectionSet::select_hpath(std::vector<unsigned long long> &hpath)
{
    if (!hpath.size())
	return false;

    sync_from_bsg();

    // If we're already selected, nothing to do
    unsigned long long shash = dbis->path_hash(hpath, 0);
    if (selected.find(shash) != selected.end())
	return true;

    // Validate that the specified path is current in the database.
    for (size_t i = 1; i < hpath.size(); i++) {
	unsigned long long phash = hpath[i-1];
	unsigned long long chash = hpath[i];
	if (dbis->p_c.find(phash) == dbis->p_c.end())
	    return false;
	if (dbis->p_c[phash].find(chash) == dbis->p_c[phash].end())
	    return false;
    }

    // If we're going to select this path, we need to clear out conflicting
    // paths.  We deliberately don't allow selection of multiple levels of
    // a single path, to avoid unexpected and unintuitive behaviors.  This
    // means we have to clear any selection that is either a superset of this
    // path or a child of it.
    std::vector<unsigned long long> pitems = hpath;
    pitems.pop_back();
    while (pitems.size()) {
	unsigned long long phash = dbis->path_hash(pitems, 0);
	selected.erase(phash);
	active_paths.erase(phash);
	pitems.pop_back();
    }
    // Clear any active children of the selected path
    pitems = hpath;
    std::unordered_map<unsigned long long, std::unordered_set<unsigned long long>>::iterator pc_it;
    pc_it = dbis->p_c.find(pitems[pitems.size() -1]);
    if (pc_it != dbis->p_c.end()) {
	std::unordered_set<unsigned long long>::iterator c_it;
	for (c_it = pc_it->second.begin(); c_it != pc_it->second.end(); c_it++)
	    clear_paths(*c_it, pitems);
    }

    // Add to selected set
    selected[shash] = hpath;
    sync_to_bsg();

    // Note - with this lower level function, it is the caller's responsibility
    // to call characterize to populate the path relationships - we deliberately
    // do not do it here, so an application can do the work once per cycle
    // rather than being forced to do it per path.

    return true;
}

bool
SelectionSet::deselect_path(const char *path, bool update)
{
    if (!path)
	return false;

    std::vector<unsigned long long> path_hashes = dbis->digest_path(path);
    if (!path_hashes.size())
	return false;

    bool ret = deselect_hpath(path_hashes);
    if (update)
	characterize();
    return ret;
}

bool
SelectionSet::deselect_hpath(std::vector<unsigned long long> &hpath)
{
    if (!hpath.size())
	return false;

    sync_from_bsg();

    // For higher level paths, need to clear the illuminated solids
    // below this path (if any)
    std::vector<unsigned long long> pitems = hpath;
    std::unordered_map<unsigned long long, std::unordered_set<unsigned long long>>::iterator pc_it;
    pc_it = dbis->p_c.find(pitems[pitems.size() -1]);
    if (pc_it != dbis->p_c.end()) {
	std::unordered_set<unsigned long long>::iterator c_it;
	for (c_it = pc_it->second.begin(); c_it != pc_it->second.end(); c_it++)
	    clear_paths(*c_it, pitems);
    }

    // Clear the selection itself
    unsigned long long phash = dbis->path_hash(hpath, 0);
    selected.erase(phash);
    active_paths.erase(phash);
    sync_to_bsg();
    return true;

    // Note - with this lower level function, it is the caller's responsibility
    // to call characterize to populate the path relationships - we deliberately
    // do not do it here, so an application can do the work once per cycle
    // rather than being forced to do it per path.
}

bool
SelectionSet::is_selected(unsigned long long hpath)
{
    if (!hpath)
	return false;

    if (selected.find(hpath) == selected.end())
	return false;

    return true;
}

bool
SelectionSet::is_active(unsigned long long phash)
{
    if (!phash)
	return false;

    if (active_paths.find(phash) == active_paths.end())
	return false;

    return true;
}

bool
SelectionSet::is_active_parent(unsigned long long phash)
{
    if (!phash)
	return false;

    if (active_parents.find(phash) == active_parents.end())
	return false;

    return true;
}

bool
SelectionSet::is_parent_obj(unsigned long long hash)
{
    if (is_immediate_parent_obj(hash) || is_grand_parent_obj(hash))
	return true;

    return false;
}

bool
SelectionSet::is_immediate_parent_obj(unsigned long long hash)
{
    if (!hash)
	return false;

    if (immediate_parents.find(hash) == immediate_parents.end())
	return false;

    return true;
}

bool
SelectionSet::is_grand_parent_obj(unsigned long long hash)
{

    if (!hash)
	return false;

    if (grand_parents.find(hash) == grand_parents.end())
	return false;

    return true;
}

void
SelectionSet::clear()
{
    sync_from_bsg();
    selected.clear();
    active_paths.clear();
    sync_to_bsg();
    characterize();
}

std::vector<std::string>
SelectionSet::list_selected_paths()
{
    sync_from_bsg();
    std::unordered_map<unsigned long long, std::vector<unsigned long long>>::iterator s_it;
    std::vector<std::string> ret;
    struct bu_vls vpath = BU_VLS_INIT_ZERO;
    for (s_it = selected.begin(); s_it != selected.end(); s_it++) {
	dbis->print_path(&vpath, s_it->second);
	ret.push_back(std::string(bu_vls_cstr(&vpath)));
    }
    bu_vls_free(&vpath);
    std::sort(ret.begin(), ret.end(), &alphanum_cmp);
    return ret;
}

void
SelectionSet::add_paths(
	unsigned long long c_hash,
	std::vector<unsigned long long> &path_hashes
	)
{
    std::unordered_map<unsigned long long, std::unordered_set<unsigned long long>>::iterator pc_it;
    pc_it = dbis->p_c.find(c_hash);

    path_hashes.push_back(c_hash);
    unsigned long long phash = dbis->path_hash(path_hashes, 0);
    active_paths.insert(phash);

    if (!path_addition_cyclic(path_hashes)) {
	/* Not cyclic - keep going */
	if (pc_it != dbis->p_c.end()) {
	    std::unordered_set<unsigned long long>::iterator c_it;
	    for (c_it = pc_it->second.begin(); c_it != pc_it->second.end(); c_it++)
		add_paths(*c_it, path_hashes);
	}
    }

    /* Done with branch - restore path */
    path_hashes.pop_back();
}

void
SelectionSet::clear_paths(
	unsigned long long c_hash,
	std::vector<unsigned long long> &path_hashes
	)
{
    std::unordered_map<unsigned long long, std::unordered_set<unsigned long long>>::iterator pc_it;
    pc_it = dbis->p_c.find(c_hash);
    path_hashes.push_back(c_hash);

    unsigned long long phash = dbis->path_hash(path_hashes, 0);
    selected.erase(phash);
    active_paths.erase(phash);

    if (!path_addition_cyclic(path_hashes)) {
	/* Not cyclic - keep going */
	if (pc_it != dbis->p_c.end()) {
	    std::unordered_set<unsigned long long>::iterator c_it;
	    for (c_it = pc_it->second.begin(); c_it != pc_it->second.end(); c_it++)
		clear_paths(*c_it, path_hashes);
	}
    }

    /* Done with branch - restore path */
    path_hashes.pop_back();
}

void
SelectionSet::expand_paths(
	std::vector<std::vector<unsigned long long>> &out_paths,
	unsigned long long c_hash,
	std::vector<unsigned long long> &path_hashes
	)
{
    std::unordered_map<unsigned long long, std::unordered_set<unsigned long long>>::iterator pc_it;
    pc_it = dbis->p_c.find(c_hash);

    path_hashes.push_back(c_hash);

    if (!path_addition_cyclic(path_hashes)) {
	/* Not cyclic - keep going */
	if (pc_it != dbis->p_c.end()) {
	    std::unordered_set<unsigned long long>::iterator c_it;
	    for (c_it = pc_it->second.begin(); c_it != pc_it->second.end(); c_it++)
		expand_paths(out_paths, *c_it, path_hashes);
	} else {
	    out_paths.push_back(path_hashes);
	}
    } else {
	out_paths.push_back(path_hashes);
    }

    /* Done with branch - restore path */
    path_hashes.pop_back();
}

void
SelectionSet::expand()
{
    sync_from_bsg();
    // Given the current selection set, expand all the paths to
    // their leaf solids and report those paths
    std::vector<std::vector<unsigned long long>> out_paths;
    std::unordered_map<unsigned long long, std::vector<unsigned long long>>::iterator s_it;
    for (s_it = selected.begin(); s_it != selected.end(); s_it++) {
	std::vector<unsigned long long> seed_hashes = s_it->second;
	unsigned long long shash = seed_hashes[seed_hashes.size() - 1];
	seed_hashes.pop_back();
	expand_paths(out_paths, shash, seed_hashes);
    }

    // Update selected.
    selected.clear();
    for (size_t i = 0; i < out_paths.size(); i++) {
	unsigned long long phash = dbis->path_hash(out_paths[i], 0);
	selected[phash] = out_paths[i];
    }

    sync_to_bsg();
    characterize();
}

void
SelectionSet::collapse()
{
    sync_from_bsg();
    std::vector<std::vector<unsigned long long>> collapsed;
    std::map<size_t, std::unordered_set<unsigned long long>> depth_groups;
    std::unordered_map<unsigned long long, std::vector<unsigned long long>>::iterator s_it;
    std::unordered_set<unsigned long long>::iterator u_it;

    // Group paths of the same depth.  Depth == 1 paths are already
    // top level objects and need no further processing.
    for (s_it = selected.begin(); s_it != selected.end(); s_it++) {
	if (s_it->second.size() == 1) {
	    collapsed.push_back(s_it->second);
	} else {
	    depth_groups[s_it->second.size()].insert(s_it->first);
	}
    }

    // Whittle down the mode depth groups until we find not-fully-drawn
    // parents - when we find that, the children constitute non-collapsible
    // paths based on what's drawn in this mode
    while (depth_groups.size()) {
	size_t plen = depth_groups.rbegin()->first;
	if (plen == 1)
	    break;
	std::unordered_set<unsigned long long> &pckeys = depth_groups.rbegin()->second;

	// For a given depth, group the paths by parent path.  This results
	// in path sub-groups which will define for us how "fully drawn"
	// that particular parent comb instance is.
	std::unordered_map<unsigned long long, std::unordered_set<unsigned long long>> grouped_pckeys;
	std::unordered_map<unsigned long long, unsigned long long> pcomb;
	for (u_it = pckeys.begin(); u_it != pckeys.end(); u_it++) {
	    std::vector<unsigned long long> &pc_path = selected[*u_it];
	    unsigned long long ppathhash = dbis->path_hash(pc_path, plen - 1);
	    grouped_pckeys[ppathhash].insert(*u_it);
	    pcomb[ppathhash] = pc_path[plen-2];
	}

	// For each parent/child grouping, compare it against the .g ground
	// truth set.  If they match, fully drawn and we promote the path to
	// the parent depth.  If not, the paths do not collapse further and are
	// added to drawn paths.
	std::unordered_map<unsigned long long, std::unordered_set<unsigned long long>>::iterator pg_it;
	for (pg_it = grouped_pckeys.begin(); pg_it != grouped_pckeys.end(); pg_it++) {

	    // As above, use the full path from selected, but this time
	    // we're collecting the children.  This is the set we need to compare
	    // against the .g ground truth to determine fully or partially drawn.
	    std::unordered_set<unsigned long long> g_children;
	    std::unordered_set<unsigned long long> &g_pckeys = pg_it->second;
	    for (u_it = g_pckeys.begin(); u_it != g_pckeys.end(); u_it++) {
		std::vector<unsigned long long> &pc_path = selected[*u_it];
		g_children.insert(pc_path[plen-1]);
	    }

	    // Do the check against the .g comb children info - the "ground truth"
	    // that defines what must be present for a fully drawn comb
	    bool is_fully_selected = true;
	    std::unordered_set<unsigned long long> &ground_truth = dbis->p_c[pcomb[pg_it->first]];
	    for (u_it = ground_truth.begin(); u_it != ground_truth.end(); u_it++) {
		if (g_children.find(*u_it) == g_children.end()) {
		    is_fully_selected = false;
		    break;
		}
	    }

	    if (is_fully_selected) {
		// If fully selected, depth_groups[plen-1] gets the first path in
		// g_pckeys.  The path is longer than that depth, but contains
		// all the necessary information and using that approach avoids
		// the need to duplicate paths.
		depth_groups[plen - 1].insert(*g_pckeys.begin());
	    } else {
		// No further collapsing - add to final.  We must make trimmed
		// versions of the paths in case this depth holds promoted
		// paths from deeper levels, since we are duplicating the full
		// path contents.
		for (u_it = g_pckeys.begin(); u_it != g_pckeys.end(); u_it++) {
		    std::vector<unsigned long long> trimmed = selected[*u_it];
		    trimmed.resize(plen);
		    collapsed.push_back(trimmed);
		}
	    }
	}

	// Done with this depth
	depth_groups.erase(plen);
    }

    // If we collapsed all the way to top level objects, make sure to add them
    // if they are still valid entries.  If a toplevel entry is invalid, there
    // is no parent comb to refer to it as an "invalid" object and it can no
    // longer be drawn.
    if (depth_groups.find(1) != depth_groups.end()) {
	std::unordered_set<unsigned long long> &pckeys = depth_groups.rbegin()->second;
	for (u_it = pckeys.begin(); u_it != pckeys.end(); u_it++) {
	    std::vector<unsigned long long> trimmed = selected[*u_it];
	    trimmed.resize(1);
	    collapsed.push_back(trimmed);
	}
    }

    selected.clear();
    for (size_t i = 0; i < collapsed.size(); i++) {
	unsigned long long phash = dbis->path_hash(collapsed[i], 0);
	selected[phash] = collapsed[i];
    }

    sync_to_bsg();
    characterize();
}

void
SelectionSet::characterize()
{
    //bu_log("SelectionSet::characterize\n");
    active_parents.clear();
    immediate_parents.clear();
    grand_parents.clear();

    std::unordered_map<unsigned long long, std::vector<unsigned long long>>::iterator s_it;
    for (s_it = selected.begin(); s_it != selected.end(); s_it++) {
	std::vector<unsigned long long> seed_hashes = s_it->second;
	unsigned long long shash = seed_hashes[seed_hashes.size() - 1];
	seed_hashes.pop_back();
	add_paths(shash, seed_hashes);

	// Stash the parent paths above this specific selection
	std::vector<unsigned long long> pitems = s_it->second;
	size_t c = s_it->second.size() - 1;
	while (c > 0) {
	    pitems.pop_back();
	    unsigned long long pphash = dbis->path_hash(s_it->second, c);
	    active_parents.insert(pphash);
	    c--;
	}
    }

    // Now, characterizing related objects.  This is not just the immediate
    // path parents - anything above the selected object is impacted.

    // Because we don't want to keep iterating over p_c, make a reverse map of children
    // to parents
    std::unordered_map<unsigned long long, std::unordered_set<unsigned long long>> reverse_map;
    std::unordered_map<unsigned long long, std::unordered_set<unsigned long long>>::iterator pc_it;
    for (pc_it = dbis->p_c.begin(); pc_it != dbis->p_c.end(); pc_it++) {
	std::unordered_set<unsigned long long>::iterator sc_it;
	for (sc_it = pc_it->second.begin(); sc_it != pc_it->second.end(); sc_it++) {
	    reverse_map[*sc_it].insert(pc_it->first);
	}
    }

    // Find the leaf children - they're the seeds
    std::unordered_set<unsigned long long> active_children;
    for (s_it = selected.begin(); s_it != selected.end(); s_it++) {
	active_children.insert(s_it->second[s_it->second.size()-1]);
    }

    // Find the immediate parents - they can be highlighted differently
    std::unordered_set<unsigned long long>::iterator c_it, p_it;
    std::unordered_map<unsigned long long, std::unordered_set<unsigned long long>>::iterator r_it;
    for (c_it = active_children.begin(); c_it != active_children.end(); c_it++) {
	r_it = reverse_map.find(*c_it);
	if (r_it == reverse_map.end())
	    continue;
	for (p_it = r_it->second.begin(); p_it != r_it->second.end(); p_it++)
	    immediate_parents.insert(*p_it);
    }

    // Work our way up from the immediate parents - we want the higher levels to
    // be known as active so they may indicate that active selections can be found
    // below
    std::queue<unsigned long long> gqueue;
    for (p_it = immediate_parents.begin(); p_it != immediate_parents.end(); p_it++) {
	gqueue.push(*p_it);
    }
    while (!gqueue.empty()) {
	unsigned long long obj = gqueue.front();
	gqueue.pop();
	r_it = reverse_map.find(obj);
	if (r_it == reverse_map.end())
	    continue;
	for (p_it = r_it->second.begin(); p_it != r_it->second.end(); p_it++) {
	    gqueue.push(*p_it);
	    grand_parents.insert(*p_it);
	}
    }
}

void
SelectionSet::refresh()
{
    sync_from_bsg();
    // If the database may have changed, we need to revalidate selected
    // paths are still current, and regenerate the active_paths set.
    active_paths.clear();

    // Unlike drawing, nothing fancy here - if a selected path is invalid,
    // it's gone.
    std::vector<unsigned long long> to_clear;
    std::unordered_map<unsigned long long, std::vector<unsigned long long>>::iterator s_it;
    for (s_it = selected.begin(); s_it != selected.end(); s_it++) {
	std::vector<unsigned long long> &cpath = s_it->second;
	for (size_t i = 1; i < cpath.size(); i++) {
	    unsigned long long phash = cpath[i-1];
	    unsigned long long chash = cpath[i];
	    if (dbis->p_c.find(phash) == dbis->p_c.end()) {
		to_clear.push_back(s_it->first);
		continue;
	    }
	    if (dbis->p_c[phash].find(chash) == dbis->p_c[phash].end()) {
		to_clear.push_back(s_it->first);
		continue;
	    }
	}
    }

    // Erase invalid paths
    for (size_t i = 0; i < to_clear.size(); i++) {
	selected.erase(to_clear[i]);
    }

    // For all surviving selections, generate paths
    for (s_it = selected.begin(); s_it != selected.end(); s_it++) {
	std::vector<unsigned long long> seed_hashes = s_it->second;
	unsigned long long shash = seed_hashes[seed_hashes.size() - 1];
	seed_hashes.pop_back();
	add_paths(shash, seed_hashes);
    }
    sync_to_bsg();
}

bool
SelectionSet::draw_sync()
{
    sync_from_bsg();
    bool changed = false;
    std::unordered_set<BViewState *> vstates;

    struct bu_ptbl *views = bv_set_views(&dbis->gedp->ged_views);
    for (size_t i = 0; i < BU_PTBL_LEN(views); i++) {
	struct bview *v = (struct bview *)BU_PTBL_GET(views, i);
	BViewState *vs = dbis->get_view_state(v);
	vstates.insert(vs);
    }

    std::unordered_map<unsigned long long, std::unordered_map<int, struct bv_scene_obj *>>::iterator so_it;
    std::unordered_map<int, struct bv_scene_obj *>::iterator m_it;
    std::unordered_set<BViewState *>::iterator vs_it;
    for (vs_it = vstates.begin(); vs_it != vstates.end(); vs_it++) {
	for (so_it = (*vs_it)->s_map.begin(); so_it != (*vs_it)->s_map.end(); so_it++) {
	    char ill_state = is_active(so_it->first) ? UP : DOWN;
	    //bu_log("select ill_state: %s\n", (ill_state == UP) ? "up" : "down");
	    for (m_it = so_it->second.begin(); m_it != so_it->second.end(); m_it++) {
		struct bv_scene_obj *so = m_it->second;
		int ill_changed = bv_illum_obj(so, ill_state);
		if (ill_changed)
		    changed = true;
	    }
	}
    }

    return changed;
}

unsigned long long
SelectionSet::state_hash()
{
    sync_from_bsg();
    std::unordered_map<unsigned long long, std::vector<unsigned long long>>::iterator s_it;
    struct bu_data_hash_state *s = bu_data_hash_create();
    if (!s)
	return 0;
    for (s_it = selected.begin(); s_it != selected.end(); s_it++) {
	bu_data_hash_update(s, &s_it->first, sizeof(unsigned long long));
    }
    unsigned long long hval = bu_data_hash_val(s);
    bu_data_hash_destroy(s);
    return hval;
}



/* ---- Phase 1-F: new SelectionSet methods ----------------------------- */

bool
SelectionSet::select(unsigned long long path_hash,
                     const std::vector<unsigned long long> &path_vec,
                     bool update_hierarchy)
{
    sync_from_bsg();
    if (!path_hash) return false;
    if (selected.find(path_hash) != selected.end()) return false;
    selected[path_hash] = path_vec;
    sync_to_bsg();
    if (update_hierarchy)
	characterize();
    return true;
}

bool
SelectionSet::deselect(unsigned long long path_hash, bool update_hierarchy)
{
    sync_from_bsg();
    if (selected.erase(path_hash) == 0) return false;
    sync_to_bsg();
    if (update_hierarchy)
	characterize();
    return true;
}

bool
SelectionSet::select(const DbiPath &path, bool update_hierarchy)
{
    if (path.empty()) return false;
    unsigned long long ph = dbis->path_hash(
	const_cast<std::vector<unsigned long long>&>(path.hashes), 0);
    return select(ph, path.hashes, update_hierarchy);
}

bool
SelectionSet::deselect(const DbiPath &path, bool update_hierarchy)
{
    if (path.empty()) return false;
    unsigned long long ph = dbis->path_hash(
	const_cast<std::vector<unsigned long long>&>(path.hashes), 0);
    return deselect(ph, update_hierarchy);
}

bool
SelectionSet::select(const char *path_str, bool update_hierarchy)
{
    return select_path(path_str, update_hierarchy);
}

bool
SelectionSet::deselect(const char *path_str, bool update_hierarchy)
{
    return deselect_path(path_str, update_hierarchy);
}

bool
SelectionSet::is_parent(unsigned long long path_hash) const
{
    return active_parents.find(path_hash) != active_parents.end();
}

bool
SelectionSet::is_ancestor(unsigned long long path_hash) const
{
    return (active_parents.find(path_hash) != active_parents.end()) ||
           (grand_parents.find(path_hash) != grand_parents.end());
}

bool
SelectionSet::is_obj_immediate_parent(unsigned long long obj_hash) const
{
    return immediate_parents.find(obj_hash) != immediate_parents.end();
}

bool
SelectionSet::is_obj_ancestor(unsigned long long obj_hash) const
{
    return grand_parents.find(obj_hash) != grand_parents.end();
}

void
SelectionSet::recompute_hierarchy()
{
    characterize();
}

std::vector<std::string>
SelectionSet::selected_paths() const
{
    return const_cast<SelectionSet *>(this)->list_selected_paths();
}

std::unordered_set<unsigned long long>
SelectionSet::selected_hashes() const
{
    std::unordered_set<unsigned long long> result;
    for (auto &kv : selected)
        result.insert(kv.first);
    return result;
}

unsigned long long
SelectionSet::state_hash_val() const
{
    return const_cast<SelectionSet *>(this)->state_hash();
}

/* ---- Phase 1-E: DrawList implementation ------------------------------ */

void
DrawList::add(const std::vector<unsigned long long> &path_hashes, int mode,
              const DrawSettings *overrides)
{
    if (path_hashes.empty()) return;
    Entry e;
    e.path = path_hashes;
    e.full_hash = bu_data_hash(path_hashes.data(),
                               path_hashes.size() * sizeof(unsigned long long));
    e.mode = mode;
    if (overrides) {
        e.has_settings = true;
        e.settings = *overrides;
    }
    entries_.push_back(std::move(e));
    dirty_ = true;
}

void
DrawList::add(const DbiPath &path, int mode, const DrawSettings *overrides)
{
    add(path.hashes, mode, overrides);
}

void
DrawList::drop(unsigned long long path_hash, int mode)
{
    auto it = entries_.begin();
    while (it != entries_.end()) {
        if (it->full_hash == path_hash && (mode < 0 || it->mode == mode))
            it = entries_.erase(it);
        else
            ++it;
    }
    dirty_ = true;
}

void
DrawList::clear()
{
    entries_.clear();
    drawn_hash_modes_.clear();
    dirty_ = false;
}

void
DrawList::clear(int mode)
{
    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(),
                       [mode](const Entry &e) { return e.mode == mode; }),
        entries_.end());
    dirty_ = true;
}

void
DrawList::rebuild_index() const
{
    drawn_hash_modes_.clear();
    for (const auto &e : entries_) {
        for (const auto &h : e.path)
            drawn_hash_modes_[h].insert(e.mode);
    }
    dirty_ = false;
}

DrawState
DrawList::query(unsigned long long path_hash, int mode) const
{
    if (dirty_) rebuild_index();
    auto it = drawn_hash_modes_.find(path_hash);
    if (it == drawn_hash_modes_.end()) return DrawState::NOT_DRAWN;
    if (mode < 0) return DrawState::FULLY_DRAWN;
    if (it->second.find(mode) != it->second.end()) return DrawState::FULLY_DRAWN;
    return DrawState::NOT_DRAWN;
}

std::vector<std::vector<unsigned long long>>
DrawList::drawn_path_hashes(int mode) const
{
    std::vector<std::vector<unsigned long long>> result;
    for (const auto &e : entries_) {
        if (mode < 0 || e.mode == mode)
            result.push_back(e.path);
    }
    return result;
}

size_t
DrawList::count(int mode) const
{
    if (mode < 0) return entries_.size();
    size_t n = 0;
    for (const auto &e : entries_)
        if (e.mode == mode) ++n;
    return n;
}

bool DrawList::empty() const { return entries_.empty(); }

/* ---- Phase 1-D: GObj and CombInst implementation -------------------- */

struct gobj_walk_data {
    GObj *gobj = NULL;
    std::unordered_map<unsigned long long, unsigned long long> i_count;
};

static void
populate_gobj_leaf(void *cd, const char *name, matp_t c_m, int op)
{
    struct gobj_walk_data *d = (struct gobj_walk_data *)cd;
    unsigned long long chash = bu_data_hash(name, strlen(name) * sizeof(char));
    d->i_count[chash] += 1;
    CombInst *ci = new CombInst(d->gobj->d, d->gobj->dp->d_namep, name,
				d->i_count[chash], op, c_m);
    d->gobj->cv.push_back(ci);
}

CombInst::CombInst(DbiState *dbis, const char *p_name, const char *o_name,
                   unsigned long long icnt, int i_op, matp_t i_mat)
{
    d = dbis;
    cname = std::string(p_name);
    oname = std::string(o_name);
    iname = std::string("");
    id = icnt;
    boolean_op = i_op;

    if (i_mat) {
	MAT_COPY(m, i_mat);
	non_default_matrix = true;
    } else {
	MAT_IDN(m);
    }

    /* Build iname for duplicate instances (same algorithm as populate_leaf) */
    if (icnt > 1) {
	struct bu_vls iname_c = BU_VLS_INIT_ZERO;
	bu_vls_sprintf(&iname_c, "%s@%llu", o_name, icnt - 1);
	iname = std::string(bu_vls_cstr(&iname_c));
	bu_vls_free(&iname_c);
    }

    /* ohash = hash(oname), matching the key space used by d_map/gobjs */
    ohash = bu_data_hash(oname.c_str(), oname.size() * sizeof(char));

    /* chash = hash(parent comb name) */
    chash = bu_data_hash(cname.c_str(), cname.size() * sizeof(char));

    /* ihash: if duplicated use hash(iname), else use ohash */
    if (!iname.empty())
	ihash = bu_data_hash(iname.c_str(), iname.size() * sizeof(char));
    else
	ihash = ohash;
}

CombInst::~CombInst()
{
    /* CombInst is owned by GObj::cv; no global registry to deregister from. */
}

db_op_t
CombInst::bool_op()
{
    if (boolean_op == OP_SUBTRACT)
	return DB_OP_SUBTRACT;
    if (boolean_op == OP_INTERSECT)
	return DB_OP_INTERSECT;
    return DB_OP_UNION;
}

void
CombInst::bbox(point_t *min, point_t *max)
{
    if (!min || !max || !d)
	return;

    auto g_it = d->gobjs.find(ohash);
    if (g_it == d->gobjs.end())
	return;

    point_t lbmin, lbmax;
    VSETALL(lbmin,  INFINITY);
    VSETALL(lbmax, -INFINITY);
    g_it->second->bbox(&lbmin, &lbmax);

    if (non_default_matrix) {
	point_t tbmin, tbmax;
	MAT4X3PNT(tbmin, m, lbmin);
	VMOVE(lbmin, tbmin);
	MAT4X3PNT(tbmax, m, lbmax);
	VMOVE(lbmax, tbmax);
    }

    VMINMAX(*min, *max, lbmin);
    VMINMAX(*min, *max, lbmax);
}

/* GObj constructor: reads attribute caches from the already-populated flat
 * maps (avoids a second disk read since update_dp() loaded them first). */
GObj::GObj(DbiState *dbis, struct directory *dp_i)
{
    if (!dbis || !dp_i)
	return;

    d  = dbis;
    dp = dp_i;
    name = std::string(dp->d_namep);
    hash = bu_data_hash(dp->d_namep, strlen(dp->d_namep) * sizeof(char));

    VSETALL(bb_min,  INFINITY);
    VSETALL(bb_max, -INFINITY);
    bb_valid = false;

    {
	auto it = dbis->c_inherit.find(hash);
	if (it != dbis->c_inherit.end())
	    c_inherit = it->second;
    }
    {
	auto it = dbis->region_id.find(hash);
	if (it != dbis->region_id.end()) {
	    region_id   = it->second;
	    region_flag = 1;
	}
    }
    {
	auto it = dbis->rgb.find(hash);
	if (it != dbis->rgb.end()) {
	    unsigned int cval = it->second;
	    unsigned char lrgb[3];
	    lrgb[0] = static_cast<unsigned char>( cval        & 0xFF);
	    lrgb[1] = static_cast<unsigned char>((cval >>  8) & 0xFF);
	    lrgb[2] = static_cast<unsigned char>((cval >> 16) & 0xFF);
	    bu_color_from_rgb_chars(&color, lrgb);
	    color_set = true;
	}
    }

    if (dp->d_flags & RT_DIR_COMB)
	GenCombInstances();

    dbis->gobjs[hash] = this;
}

GObj::~GObj()
{
    for (CombInst *ci : cv)
	delete ci;
    cv.clear();

    if (d)
	d->gobjs.erase(hash);
}

void
GObj::GenCombInstances()
{
    if (!dp || !(dp->d_flags & RT_DIR_COMB) || !d)
	return;

    struct rt_db_internal in;
    if (rt_db_get_internal(&in, dp, d->gedp->dbip, NULL) < 0)
	return;
    struct rt_comb_internal *comb = (struct rt_comb_internal *)in.idb_ptr;
    if (!comb->tree) {
	rt_db_free_internal(&in);
	return;
    }

    struct gobj_walk_data dw;
    dw.gobj = this;
    populate_walk_tree(comb->tree, (void *)&dw, 0, OP_UNION, populate_gobj_leaf);

    rt_db_free_internal(&in);
}

void
GObj::bbox(point_t *min, point_t *max)
{
    if (!min || !max || !d)
	return;

    if (!cv.empty()) {
	for (CombInst *ci : cv) {
	    point_t lbmin, lbmax;
	    VSETALL(lbmin,  INFINITY);
	    VSETALL(lbmax, -INFINITY);
	    ci->bbox(&lbmin, &lbmax);
	    VMINMAX(*min, *max, lbmin);
	    VMINMAX(*min, *max, lbmax);
	}
	return;
    }

    if (bb_valid) {
	VMINMAX(*min, *max, bb_min);
	VMINMAX(*min, *max, bb_max);
	return;
    }

    point_t bmin, bmax;
    VSETALL(bmin,  INFINITY);
    VSETALL(bmax, -INFINITY);
    if (d->get_bbox(&bmin, &bmax, NULL, hash)) {
	VMOVE(bb_min, bmin);
	VMOVE(bb_max, bmax);
	bb_valid = true;
	VMINMAX(*min, *max, bb_min);
	VMINMAX(*min, *max, bb_max);
    }
}

/* ---- Phase 3.5: DbiState pipeline management ----------------------- */

void
DbiState::start_geom_load(const std::vector<DrawPipeline::WorkItem> &items)
{
    if (items.empty() || !draw_pipeline_)
	return;
    if (gedp && gedp->ged_lod)
	draw_pipeline_->set_lod_ctx(gedp->ged_lod);
    draw_pipeline_->push(items);
}

size_t
DbiState::drain_geom_results()
{
    if (!draw_pipeline_)
	return 0;

    std::vector<DrawPipeline::Result> results;
    size_t n = draw_pipeline_->drain(results);
    if (n == 0)
	return 0;

    for (const auto &r : results) {
	if (r.type == DrawPipeline::Result::AABB) {
	    /* Only update if we don't already have a synchronous bbox.
	     * The sync path in get_bbox may have beaten us to it; if so,
	     * the sync version wins. */
	    if (bboxes.find(r.hash) == bboxes.end()) {
		bboxes[r.hash].clear();
		bboxes[r.hash].reserve(6);
		for (int i = 0; i < 3; i++) bboxes[r.hash].push_back(r.bmin[i]);
		for (int i = 0; i < 3; i++) bboxes[r.hash].push_back(r.bmax[i]);
	    }
	} else if (r.type == DrawPipeline::Result::OBB && r.obb_valid) {
	    /* Store 8 OBB corner points */
	    std::array<fastf_t, 24> obb_data;
	    for (int k = 0; k < 8; k++) {
		obb_data[k*3+0] = r.obb_pts[k][X];
		obb_data[k*3+1] = r.obb_pts[k][Y];
		obb_data[k*3+2] = r.obb_pts[k][Z];
	    }
	    obbs[r.hash] = obb_data;
	}
	/* LOD results: LoD data is now in the bv_mesh_lod_context cache;
	 * bv_mesh_lod_view() will use it on the next redraw.  No extra work
	 * needed here for Phase 3.5; future phases can stale scene objects. */
    }

    /* Fire a batched scene-change notification so that observers
     * (e.g. QgViewport) know to request a repaint. */
    {
	std::vector<SceneChangeEvent> events;
	events.reserve(n);
	for (const auto &r : results) {
	    SceneChangeEvent ev;
	    ev.path  = PathHash{r.hash};
	    ev.batch = false;
	    events.push_back(ev);
	}
	notify_scene_observers(events);
    }

    return n;
}

size_t
DbiState::wait_for_pipeline(int max_ms)
{
    if (!draw_pipeline_)
	return 0;

    size_t total = 0;
    auto t0 = std::chrono::steady_clock::now();

    while (true) {
	total += drain_geom_results();

	if (draw_pipeline_->settled())
	    break;

	if (max_ms > 0) {
	    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - t0).count();
	    if (elapsed >= max_ms)
		break;
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    /* One final drain to capture any results posted just before settled(). */
    total += drain_geom_results();
    return total;
}

/** @} */
/* ============================================================
 * Phase D: thin C interface to DbiState / BViewState
 * (drawing_stack_modernization.txt "Phase D" section).
 *
 * These wrappers let C callers query drawn-set state without
 * depending on the C++ dbi.h private header.  They are safe
 * to call with any ged instance: when dbi_state is NULL (e.g.
 * MGED) they return 0 / empty immediately.
 * ============================================================ */

extern "C" {

int
ged_dbi_is_drawn(struct ged *gedp, struct bview *v, const char *path)
{
    if (!gedp || !gedp->dbi_state || !path)
	return 0;

    DbiState *dbis = (DbiState *)gedp->dbi_state;

    /* Resolve the view: fall back to the shared view state when v is NULL. */
    BViewState *bvs = dbis->get_view_state(v);
    if (!bvs)
	return 0;

    /* Digest the path string to the per-object hash sequence. */
    std::vector<unsigned long long> hashes = dbis->digest_path(path);
    if (hashes.empty())
	return 0;

    /* The drawn check is keyed on the full-path hash (last element of the
     * digest, which encodes the complete path). */
    unsigned long long phash = dbis->path_hash(hashes, 0);
    return bvs->is_hdrawn(-1, phash);
}

size_t
ged_dbi_list_drawn(struct ged *gedp, struct bview *v, int mode, struct bu_vls *result)
{
    if (!gedp || !gedp->dbi_state || !result)
	return 0;

    DbiState *dbis = (DbiState *)gedp->dbi_state;

    BViewState *bvs = dbis->get_view_state(v);
    if (!bvs)
	return 0;

    std::vector<std::string> paths = bvs->list_drawn_paths(mode, /*list_collapsed=*/false);
    for (const std::string &p : paths) {
	bu_vls_printf(result, "%s\n", p.c_str());
    }
    return paths.size();
}

} /* extern "C" */

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
