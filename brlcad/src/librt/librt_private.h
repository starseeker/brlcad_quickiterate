/*                 L I B R T _ P R I V A T E . H
 * BRL-CAD
 *
 * Copyright (c) 2011-2025 United States Government as represented by
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
/** @file librt_private.h
 *
 * These are declarations for functions that are for internal use by
 * LIBRT but are not public API.  NO CODE outside of LIBRT should use
 * these functions.
 *
 * Non-public functions should NOT have an rt_*() or db_*() prefix.
 * Consider using an _rt_, _db_, or other file-identifying prefix
 * accordingly (e.g., ell_*() for functions defined in primitives/ell)
 *
 */
#ifndef LIBRT_LIBRT_PRIVATE_H
#define LIBRT_LIBRT_PRIVATE_H

#include "common.h"

#include "vmath.h"
#include "bsg.h"
#include "rt/db4.h"
#include "raytrace.h"

/* approximation formula for the circumference of an ellipse */
#define ELL_CIRCUMFERENCE(a, b) M_PI * ((a) + (b)) * \
    (1.0 + (3.0 * ((((a) - b))/((a) + (b))) * ((((a) - b))/((a) + (b))))) \
    / (10.0 + sqrt(4.0 - 3.0 * ((((a) - b))/((a) + (b))) * ((((a) - b))/((a) + (b)))))

/* logic to ensure bboxes are not degenerate in any dimension - zero thickness
 * bounding boxes will get missed by the raytracer */
#define BBOX_NONDEGEN(min, max, dist) do {\
    if (NEAR_EQUAL((min)[X], (max)[X], (dist))) { \
	(min)[X] -= dist; \
	(max)[X] += dist; \
    } \
    if (NEAR_EQUAL((min)[Y], (max)[Y], (dist))) { \
	(min)[Y] -= dist; \
	(max)[Y] += dist; \
    } \
    if (NEAR_EQUAL((min)[Z], (max)[Z], (dist))) { \
	(min)[Z] -= dist; \
	(max)[Z] += dist; \
    } \
} while (0)

/* -----------------------------------------------------------------------
 * Drawing cache infrastructure
 * -----------------------------------------------------------------------
 * cache_drawing.cpp implements a 5-stage concurrent pipeline that
 * computes drawing data (attrs → AABB → OBB → LoD → write) in background
 * threads, storing results in an LMDB-backed bu_cache and posting
 * DrawResult notifications to a lock-free queue readable by the main thread.
 *
 * The concurrentqueue.h header (moodycamel, MIT license) provides the
 * lock-free multi-producer/multi-consumer queue used between stages.
 * ----------------------------------------------------------------------- */

#ifdef __cplusplus
#  include "concurrentqueue.h"

#  include <atomic>
#  include <string>
#  include <thread>
#  include <vector>

/* Single item queued for writing to the LMDB drawing cache. */
class CacheWriteItem {
public:
    CacheWriteItem();
    CacheWriteItem(const char *key, const void *data, size_t len);
    CacheWriteItem(const CacheWriteItem &o);
    CacheWriteItem& operator=(const CacheWriteItem &o);
    ~CacheWriteItem();

    char   key[BU_CACHE_KEY_MAXLEN] = {0};
    bool   erase_op  = false;
    size_t data_len  = 0;
    void  *data      = nullptr;
};

/* Result notification posted to the main-thread result queue after each
 * pipeline stage finishes for one object. */
struct DrawResult {
    enum Type { AABB, OBB, LOD } type;
    unsigned long long hash = 0;
    char dp_name[512] = {0};
    /* AABB */
    point_t bmin = VINIT_ZERO;
    point_t bmax = VINIT_ZERO;
    /* OBB: center + 3 half-extent vectors (as produced by bg_3d_obb) */
    point_t obb_pts[8];    /* 8 corner points of the oriented bounding box */
    bool    obb_valid = false;
    /* LOD */
    unsigned long long lod_key = 0;
};

/* The per-database 5-stage pipeline state.  Lives in db_i_internal::draw_pipeline
 * as an opaque void* so that C translation units (db_open.c) don't need to
 * include C++ headers. */
class DrawPipelineState {
public:
    std::atomic<bool> shutdown{false};
    std::atomic<int>  thread_cnt{0};

    /* Inter-stage queues (lock-free, moodycamel). */
    moodycamel::ConcurrentQueue<std::string>             q_init;   /* object names → attr stage */
    moodycamel::ConcurrentQueue<struct rt_db_internal *> q_aabb;   /* cracked internal → AABB */
    moodycamel::ConcurrentQueue<struct rt_db_internal *> q_obb;    /* post-AABB → OBB */
    moodycamel::ConcurrentQueue<struct rt_db_internal *> q_lod;    /* post-OBB  → LoD */
    moodycamel::ConcurrentQueue<CacheWriteItem>          q_write;  /* pending LMDB writes */

    /* Result queue read by the main thread (drain → scene change). */
    moodycamel::ConcurrentQueue<DrawResult> results_q;

    struct db_i           *dbip    = nullptr;
    bsg_mesh_lod_context  *lod_ctx = nullptr; /* set from gedp->ged_lod when available */
    struct bu_cache       *dcache  = nullptr; /* same pointer as dbip->i->dcache */

    /* Worker thread handles. */
    std::vector<std::thread> threads;
};

#endif  /* __cplusplus */

__BEGIN_DECLS

/* C-visible struct – C++ members are hidden behind the opaque void* fields. */
struct db_i_internal {
    uint32_t dbi_magic;

    /* BoT level of detail cached data for drawing (legacy – kept for bsg API) */
    bsg_mesh_lod_context *mesh_c;
    int mesh_c_completed;
    int mesh_c_target;

    /* General drawing data cache (AABB, OBB, attrs).  Opened/closed by
     * db_cache_start/stop in cache_drawing.cpp. */
    struct bu_cache *dcache;

    /* Opaque pointer to DrawPipelineState (C++ type).  Managed entirely by
     * db_cache_start / db_cache_stop. */
    void *draw_pipeline;

    // TODO - really need to get the rt prep cache container
    // in here and add a pointer slot to it for rt_db_internal
    // so the librt point generation routines can take advantage
    // of cached prep even if they're using an inmem db...
};

/* Drawing-cache lifecycle – implemented in cache_drawing.cpp, called from
 * db_open.c as extern "C". */
extern int  db_cache_start(struct db_i *dbip);
extern void db_cache_stop(struct db_i *dbip);
extern void db_cache_queue_obj(struct db_i *dbip, const char *name);
extern int  db_cache_settled(struct db_i *dbip);
extern void db_cache_set_lod_ctx(struct db_i *dbip, bsg_mesh_lod_context *lod_ctx);

struct db_i_internal * db_i_internal_create(void);
void db_i_internal_destroy(struct db_i_internal *i);


/* Used by sketch extrude revolve */
extern int curve_to_vlist(struct bu_list              *vlfree,
                         struct bu_list              *vhead,
                         const struct bg_tess_tol    *ttol,
                         point_t                     V,
                         vect_t                      u_vec,
                         vect_t                      v_vec,
                         struct rt_sketch_internal *sketch_ip,
                         struct rt_curve             *crv);

/* db_flip.c */

/**
 * function similar to ntohs() but always flips the bytes.
 * used for v4 compatibility.
 */
extern short flip_short(short s);

/**
 * function similar to ntohf() but always flips the types.
 * used for v4 compatibility.
 */
extern fastf_t flip_dbfloat(dbfloat_t d);

/**
 * function that flips a dbfloat_t[3] vector into fastf_t[3]
 */
extern void flip_fastf_float(fastf_t *ff, const dbfloat_t *fp, int n, int flip);

/**
 * function that flips a dbfloat_t[16] matrix into fastf_t[16]
 */
extern void flip_mat_dbmat(fastf_t *ff, const dbfloat_t *dbp, int flip);

/**
 * function that flips a fastf_t[16] matrix into dbfloat_t[16]
 */
extern void flip_dbmat_mat(dbfloat_t *dbp, const fastf_t *ff);


/**
 * return angle required for smallest side to fall within tolerances
 * for ellipse.  Smallest side is a side with an endpoint at (a, 0, 0)
 * where a is the semi-major axis.
 */
extern fastf_t ell_angle(fastf_t *p1, fastf_t a, fastf_t b, fastf_t dtol, fastf_t ntol);

/**
 * used by rt_shootray_bundle()
 * FIXME: non-public API shouldn't be using rt_ prefix
 */
extern const union cutter *rt_advance_to_next_cell(struct rt_shootray_status *ssp);

/**
 * used by rt_shootray_bundle()
 * FIXME: non-public API shouldn't be using rt_ prefix
 */
extern void rt_plot_cell(const union cutter *cutp, struct rt_shootray_status *ssp, struct bu_list *waiting_segs_hd, struct rt_i *rtip);

/* db_fullpath.c */

/**
 * Function to test whether a path has a cyclic entry in it.
 *
 * @param fp [i] Full path to test
 * @param test_name [i] String to use when checking path.
 * @param depth [i] Starting depth for path node name comparisons.
 * @return 1 if the path is cyclic, 0 if it is not.
 */
extern int cyclic_path(const struct db_full_path *fp, const char *test_name, long int depth);


/* db_diff.c */

/**
 * Function to convert an ft_get list of parameters into an avs.
 * @return 0 if the conversion succeeds, -1 if it does not.
 */
extern int tcl_list_to_avs(const char *tcl_list, struct bu_attribute_value_set *avs, int offset);

/* db_io.c */

struct dbi_changed_clbk {
    dbi_changed_t f;
    void *u_data;
};

struct dbi_update_nref_clbk {
    dbi_update_nref_t f;
    void *u_data;
};

extern int db_read(const struct db_i *dbip, void *addr, size_t count, b_off_t offset);

/* db5_io.c */
#define DB_SIZE_OBJ 0x1
#define DB_SIZE_TREE_INSTANCED 0x2
#define DB_SIZE_TREE_DEINSTANCED 0x4
#define DB_SIZE_ATTR 0x8
#define DB_SIZE_FORCE_RECALC 0x10
/**
 * Flag behavior: The OBJ, KEEP, and XPUSH flags tell db5_size to add the total
 * from the object(s) calculations for that size value to the summed total.
 * Specifying multiple flags from the OBJ/KEEP/XPUSH set will result in the
 * smallest specified size being reported. If none of these flags are specified
 * the default is DB_SIZE_OBJ
 *
 * The ATTR flag is optional and tells db5_size whether to report totals with
 * or without the sizes of the attributes stored on the objects added in.  By
 * default, attributes are not included in size calculations.
 *
 * The FORCE_RECALC flag is also optional and will result in a re-evaluation of
 * the stored size information in the directory structures instead of using any
 * stored information from previous db5_size evaluations. This flag should be
 * supplied if the geometry information in the database has changed since the
 * last db5_size call.
 */
extern RT_EXPORT long db5_size(struct db_i *dbip, struct directory *dp, int flags);

/* FIXME: should have gone away with v6.  needed now to pass the minor_type down during read */
extern int rt_binunif_import5_minor_type(struct rt_db_internal *, const struct bu_external *, const mat_t, const struct db_i *, struct resource *, int);

extern const char *rt_binunif_type_to_string(int type);

/* primitive_util.c */

extern void primitive_hitsort(struct hit h[], int nh);

extern fastf_t primitive_get_absolute_tolerance(
	const struct bg_tess_tol *ttol,
	fastf_t rel_to_abs);

extern fastf_t primitive_diagonal_samples(
	struct rt_db_internal *ip,
	const bsg_view *v,
	const struct bn_tol *tol,
	fastf_t s_size);

extern int approximate_parabolic_curve(
	struct rt_pnt_node *pts,
	fastf_t p,
	int num_new_points);

extern fastf_t primitive_curve_count(
	struct rt_db_internal *ip,
	const struct bn_tol *tol,
	fastf_t curve_scale,
	fastf_t s_size);

extern int approximate_hyperbolic_curve(
	struct rt_pnt_node *pts,
	fastf_t a,
	fastf_t b,
	int num_new_points);

extern void
ellipse_point_at_radian(
	point_t result,
	const vect_t center,
	const vect_t axis_a,
	const vect_t axis_b,
	fastf_t radian);

extern void plot_ellipse(
	struct bu_list *vlfree,
	struct bu_list *vhead,
	const vect_t t,
	const vect_t a,
	const vect_t b,
	int num_points);

extern int _rt_tcl_list_to_int_array(const char *list, int **array, int *array_len);
extern int _rt_tcl_list_to_fastf_array(const char *list, fastf_t **array, int *array_len);

/* view.c */
extern fastf_t solid_point_spacing(const bsg_view *gvp, fastf_t solid_width);
extern fastf_t view_avg_sample_spacing(const bsg_view *gvp);


#ifdef USE_OPENCL
extern cl_device_id clt_get_cl_device(void);
extern cl_program clt_get_program(cl_context context, cl_device_id device, cl_uint count, const char *filename[], const char *options);


#define CLT_DECLARE_INTERFACE(name) \
    extern size_t clt_##name##_pack(struct bu_pool *pool, struct soltab *stp)

CLT_DECLARE_INTERFACE(tor);
CLT_DECLARE_INTERFACE(tgc);
CLT_DECLARE_INTERFACE(ell);
CLT_DECLARE_INTERFACE(arb);
CLT_DECLARE_INTERFACE(ars);
CLT_DECLARE_INTERFACE(rec);
CLT_DECLARE_INTERFACE(sph);
CLT_DECLARE_INTERFACE(ebm);
CLT_DECLARE_INTERFACE(arbn);
CLT_DECLARE_INTERFACE(part);
CLT_DECLARE_INTERFACE(epa);
CLT_DECLARE_INTERFACE(ehy);
CLT_DECLARE_INTERFACE(bot);
CLT_DECLARE_INTERFACE(eto);
CLT_DECLARE_INTERFACE(rhc);
CLT_DECLARE_INTERFACE(rpc);
CLT_DECLARE_INTERFACE(hrt);
CLT_DECLARE_INTERFACE(superell);
CLT_DECLARE_INTERFACE(hyp);

extern size_t clt_bot_pack(struct bu_pool *pool, struct soltab *stp);
#endif

__END_DECLS

#endif /* LIBRT_LIBRT_PRIVATE_H */

/*
 * Local Variables:
 * tab-width: 8
 * mode: C
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
