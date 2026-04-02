/*               O B O L _ C A D _ A S S E M B L Y . C P P
 * BRL-CAD
 *
 * Copyright (c) 2025 United States Government as represented by
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
/** @file libged/obol_cad_assembly.cpp
 *
 * SoCADAssembly-based scene manager for large CAD hierarchies.
 *
 * @see obol_cad_assembly.h for API documentation
 * @see RADICAL_MIGRATION.md Stage 3
 */

#include "common.h"

#ifdef BRLCAD_ENABLE_OBOL

#include "obol_cad_assembly.h"

#include "bsg.h"
#include "bu/str.h"
#include "raytrace.h"
#include "rt/db_fullpath.h"
#include "ged/view.h"

/* Suppress -Wfloat-equal from third-party Obol/Inventor headers */
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wfloat-equal"
#endif
#include <Inventor/SoDB.h>
#include <Inventor/SbMatrix.h>
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif

#include <unordered_map>
#include <unordered_set>
#include <string>


/* --------------------------------------------------------------------------
 * Module-level state
 * -------------------------------------------------------------------------- */

/* InstanceId → bsg_shape* reverse lookup (populated by upsert, cleared by
 * remove/clear).  Allows SoCADDetail picks to resolve back to a bsg_shape. */
static std::unordered_map<obol::InstanceId, bsg_shape *> &
instance_shape_map()
{
    static std::unordered_map<obol::InstanceId, bsg_shape *> m;
    return m;
}

/* bsg_shape* → InstanceId forward map (populated alongside instance_shape_map).
 * Used for targeted removal and selection updates. */
static std::unordered_map<bsg_shape *, obol::InstanceId> &
shape_instance_map()
{
    static std::unordered_map<bsg_shape *, obol::InstanceId> m;
    return m;
}

/* Set of selected shapes — kept in sync with the assembly's selection buffer. */
static std::unordered_set<bsg_shape *> &
selected_asm_shapes()
{
    static std::unordered_set<bsg_shape *> s;
    return s;
}

/* Set of PartIds that have already been uploaded to the most recently used
 * SoCADAssembly.  Reset on obol_cad_assembly_clear(). */
static std::unordered_set<obol::PartId> &
uploaded_parts()
{
    static std::unordered_set<obol::PartId> s;
    return s;
}


/* --------------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------------- */

/**
 * Convert a BRL-CAD 8-bit RGB triplet (e.g. bsg_obj_settings::color) to a
 * normalized SbColor4f with alpha = 1.
 */
static inline SbColor4f
rgb_to_sbcolor4f(const unsigned char rgb[3])
{
    return SbColor4f(rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f, 1.0f);
}

/**
 * Build an obol::InstanceId from a path string of the form
 * "comp1/comp2/solid" (as produced by DbiState::print_path).
 * occ=0 and boolOp=0 are used for all components because the string form
 * does not carry occurrence or boolean information.
 */
static obol::InstanceId
pathstr_to_instance_id(const char *path_str)
{
    obol::InstanceId id = obol::CadIdBuilder::Root();
    if (!path_str || !path_str[0])
	return id;

    /* Split by '/' — skip any leading '/' */
    const char *p = path_str;
    while (*p == '/') p++;

    while (*p) {
	const char *start = p;
	while (*p && *p != '/') p++;
	if (p > start) {
	    char comp[512];
	    size_t len = (size_t)(p - start);
	    if (len >= sizeof(comp))
		len = sizeof(comp) - 1;
	    bu_strlcpy(comp, start, len + 1);
	    id = obol::CadIdBuilder::extendNameOccBool(id, comp, 0, 0);
	}
	if (*p == '/') p++;
    }
    return id;
}

/**
 * Build the InstanceId for the *parent* of the leaf in a path string.
 * Strips the last '/'-separated component and calls pathstr_to_instance_id.
 */
static obol::InstanceId
pathstr_to_parent_id(const char *path_str)
{
    if (!path_str || !path_str[0])
	return obol::CadIdBuilder::Root();

    /* Find the last '/' */
    const char *last_sep = nullptr;
    for (const char *p = path_str; *p; p++)
	if (*p == '/')
	    last_sep = p;

    if (!last_sep)
	return obol::CadIdBuilder::Root();   /* single component — parent is root */

    /* Build a NUL-terminated string of the parent portion */
    char parent[4096];
    size_t len = (size_t)(last_sep - path_str);
    if (len >= sizeof(parent))
	len = sizeof(parent) - 1;
    bu_strlcpy(parent, path_str, len + 1);
    return pathstr_to_instance_id(parent);
}

/**
 * Build an obol::InstanceId from a db_full_path by chaining
 * CadIdBuilder::extendNameOccBool() from the root to the leaf.
 */
static obol::InstanceId
fp_to_instance_id(const struct db_full_path *fp)
{
    obol::InstanceId id = obol::CadIdBuilder::Root();
    if (!fp)
	return id;

    for (size_t i = 0; i < fp->fp_len; i++) {
	const char *name = fp->fp_names[i]->d_namep;
	uint32_t occ = (fp->fp_cinst) ? (uint32_t)fp->fp_cinst[i] : 0;

	/* Map BRL-CAD boolean op codes to SoCADAssembly boolOp:
	 *   OP_UNION (2)     → 0
	 *   OP_INTERSECT (3) → 2
	 *   OP_SUBTRACT (4)  → 1
	 *   default          → 0 (union)
	 */
	uint8_t bool_op = 0;
	if (fp->fp_bool) {
	    switch (fp->fp_bool[i]) {
		case 4:  bool_op = 1; break;   /* OP_SUBTRACT */
		case 3:  bool_op = 2; break;   /* OP_INTERSECT */
		default: bool_op = 0; break;   /* OP_UNION or unset */
	    }
	}

	id = obol::CadIdBuilder::extendNameOccBool(id, name, occ, bool_op);
    }

    return id;
}

/**
 * Build the InstanceId for the *parent* of the leaf in @p fp (i.e. all path
 * steps up to but not including the last element).
 */
static obol::InstanceId
fp_to_parent_id(const struct db_full_path *fp)
{
    if (!fp || fp->fp_len == 0)
	return obol::CadIdBuilder::Root();

    /* Create a temporary path one element shorter */
    struct db_full_path parent;
    db_full_path_init(&parent);
    db_dup_full_path(&parent, fp);
    if (parent.fp_len > 0)
	parent.fp_len--;

    obol::InstanceId id = fp_to_instance_id(&parent);
    db_free_full_path(&parent);
    return id;
}

/**
 * Convert a BRL-CAD mat_t (row-major) to an SbMatrix.
 */
static SbMatrix
mat_to_sbmatrix(const mat_t m)
{
    /* BRL-CAD mat_t uses a COLUMN-VECTOR convention (dst = M * src) stored
     * in row-major order: m[0..3]=row0, m[4..7]=row1, m[8..11]=row2,
     * m[12..15]=row3.  Translation is at m[3], m[7], m[11].
     *
     * Coin3D SbMatrix uses a ROW-VECTOR convention (dst = src * M) and its
     * multVecMatrix reads: dst[j] = sum_i( src[i] * M[i][j] ) + M[3][j].
     * Translation must be at M[3][0..2], which corresponds to the 4th row.
     *
     * The two conventions are related by a matrix transpose, so we swap
     * rows and columns when converting. */
    return SbMatrix(
	(float)m[0],  (float)m[4],  (float)m[8],  (float)m[12],
	(float)m[1],  (float)m[5],  (float)m[9],  (float)m[13],
	(float)m[2],  (float)m[6],  (float)m[10], (float)m[14],
	(float)m[3],  (float)m[7],  (float)m[11], (float)m[15]
    );
}


/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

void
obol_cad_assembly_init_classes(void)
{
    /* These calls are idempotent and thread-safe in Coin3D. */
    SoCADAssembly::initClass();
    SoCADDetail::initClass();
}

/* --------------------------------------------------------------------------
 * GL2 compatibility flag
 *
 * When the active GL context only supports OpenGL 2.x (e.g. the bundled
 * Mesa 6.5 OSMesa), SoCADAssembly's shader pipeline cannot be used.
 * Setting this flag makes obol_cad_assembly_upsert_shape() always return
 * false so every shape falls back to the traditional per-shape SoSeparator
 * path (using ft_scene_obj / s_obol_node), which works with GL 1.x/2.x.
 * -------------------------------------------------------------------------- */

static bool s_gl2_compat_mode = false;

void
obol_cad_assembly_set_gl2_compat(bool enabled)
{
    s_gl2_compat_mode = enabled;
    if (enabled)
	bu_log("obol_cad_assembly: GL 2.x context detected — "
	       "SoCADAssembly disabled, using per-shape SoSeparator fallback\n");
}

bool
obol_cad_assembly_is_gl2_compat(void)
{
    return s_gl2_compat_mode;
}

SoCADAssembly *
obol_cad_assembly_create(void)
{
    SoCADAssembly *asm_node = new SoCADAssembly;
    asm_node->ref();
    asm_node->drawMode   = SoCADAssembly::WIREFRAME;
    asm_node->pickMode   = SoCADAssembly::PICK_HYBRID;
    asm_node->lodEnabled = FALSE;
    return asm_node;
}

bool
obol_cad_assembly_upsert_shape(SoCADAssembly *cad_asm, bsg_shape *s)
{
    if (!cad_asm || !s)
	return false;

    /* Debugging escape hatch: BRLCAD_NO_CAD_ASM=1 forces all shapes to the
     * traditional Obol per-shape path so the CAD assembly can be bypassed. */
    if (getenv("BRLCAD_NO_CAD_ASM"))
	return false;

    /* GL2 compat: SoCADAssembly requires GL 3.3+.  When running with a GL 2.x
     * context (e.g. the bundled OSMesa), route all shapes to the traditional
     * per-shape SoSeparator path instead. */
    if (s_gl2_compat_mode)
	return false;

    /* Get the leaf directory pointer.
     *
     * Two code paths create bsg_shape leaf objects:
     *   (a) draw_gather_paths (draw.cpp): sets s_path (db_full_path) and dp.
     *   (b) BViewState::scene_obj (dbi_state.cpp): sets dp only, s_path=NULL.
     *
     * Prefer s_path when available (carries occ/bool metadata); fall back to
     * sp->dp for the new async-first pipeline. */
    struct db_full_path *fp = (struct db_full_path *)s->s_path;
    struct directory *dp = nullptr;

    if (fp && fp->fp_len > 0) {
	dp = DB_FULL_PATH_CUR_DIR(fp);
    } else if (s->dp) {
	dp = (struct directory *)s->dp;
	fp = nullptr;   /* explicitly clear so callers below use string-based path */
    }

    if (!dp)
	return false;

    /* Only proceed if this primitive type has a native ft_scene_obj_part */
    if (!OBJ[dp->d_minor_type].ft_scene_obj_part)
	return false;

    /* Retrieve draw context from s_i_data */
    struct draw_update_data_t *d = (struct draw_update_data_t *)s->s_i_data;
    if (!d || !d->dbip)
	return false;

    const struct bg_tess_tol *ttol = d->ttol;
    const struct bn_tol     *tol   = d->tol;
    int dmode = s->s_os ? s->s_os->s_dmode : 0;

    /* ------------------------------------------------------------------ *
     * Part: upload geometry once per unique primitive name                *
     * ------------------------------------------------------------------ */
    obol::PartId pid = obol::CadIdBuilder::hash128(dp->d_namep);

    if (!uploaded_parts().count(pid)) {
	obol::PartGeometry geom;
	int ret = OBJ[dp->d_minor_type].ft_scene_obj_part(
	    &geom, dp, d->dbip, ttol, tol, dmode);

	if (ret != BRLCAD_OK)
	    return false;

	/* If the geometry plugin produced nothing (empty WireRep and no shaded
	 * mesh), fall back to the traditional per-shape Obol path which can
	 * render the shape via ft_scene_obj / s_obol_node. */
	bool has_geom = (geom.wire.has_value() && !geom.wire->polylines.empty())
		     || (geom.shaded.has_value() && !geom.shaded->positions.empty());
	if (!has_geom)
	    return false;

	/* Propagate the drawing mode to the assembly node */
	switch (dmode) {
	    case 2:
	    case 4:
		cad_asm->drawMode = SoCADAssembly::SHADED_WITH_EDGES;
		break;
	    default:
		cad_asm->drawMode = SoCADAssembly::WIREFRAME;
		break;
	}

	cad_asm->upsertPart(pid, geom);
	uploaded_parts().insert(pid);
	/* Diagnostic: report WireRep content and bounds */
	if (geom.wire.has_value()) {
	    const obol::WireRep &wr = *geom.wire;
	    SbVec3f bmin, bmax;
	    wr.bounds.getBounds(bmin, bmax);
	    bu_log("obol_asm upsertPart '%s': %zu polylines bounds(%.1f,%.1f,%.1f)-(%.1f,%.1f,%.1f)\n",
		dp->d_namep, wr.polylines.size(),
		bmin[0], bmin[1], bmin[2], bmax[0], bmax[1], bmax[2]);
	} else {
	    bu_log("obol_asm upsertPart '%s': no wire (shaded only)\n", dp->d_namep);
	}
    }

    /* ------------------------------------------------------------------ *
     * Instance: register / update transform + style                       *
     * ------------------------------------------------------------------ */
    obol::InstanceId iid;
    obol::InstanceRecord rec;
    rec.part        = pid;
    rec.localToRoot = mat_to_sbmatrix(s->s_mat);
    rec.childName   = dp->d_namep;

    if (fp) {
	/* Full path available — use high-fidelity occ/bool metadata */
	iid             = fp_to_instance_id(fp);
	rec.parent      = fp_to_parent_id(fp);
	rec.occurrenceIndex = fp->fp_cinst ? (uint32_t)fp->fp_cinst[fp->fp_len-1] : 0;
	rec.boolOp = 0;
	if (fp->fp_bool) {
	    switch (fp->fp_bool[fp->fp_len-1]) {
		case 4: rec.boolOp = 1; break;   /* subtract */
		case 3: rec.boolOp = 2; break;   /* intersect */
		default: rec.boolOp = 0; break;
	    }
	}
    } else {
	/* No db_full_path — use path string from s_name (new async pipeline).
	 * occ=0, boolOp=0 since the string representation doesn't carry them. */
	const char *path_str = bu_vls_cstr(&s->s_name);
	iid             = pathstr_to_instance_id(path_str);
	rec.parent      = pathstr_to_parent_id(path_str);
	rec.occurrenceIndex = 0;
	rec.boolOp      = 0;
    }

    /* Per-instance colour: prefer the database-derived s_color (always set),
     * but override with s_os->color when an explicit color override is active. */
    if (s->s_os && s->s_os->color_override) {
	rec.style.hasColorOverride = true;
	rec.style.color = rgb_to_sbcolor4f(s->s_os->color);
    } else {
	/* Use the database path colour stored in s_color */
	rec.style.hasColorOverride = true;
	rec.style.color = rgb_to_sbcolor4f(s->s_color);
    }

    cad_asm->upsertInstance(iid, rec);
    /* Diagnostic: report transform translation (direct matrix read, no SbRotation needed) */
    {
	/* SbMatrix row-vector: translation is last row [3][0..2] */
	float tx = rec.localToRoot[3][0];
	float ty = rec.localToRoot[3][1];
	float tz = rec.localToRoot[3][2];
	bu_log("obol_asm upsertInstance '%s': localToRoot tx=(%.1f,%.1f,%.1f)\n",
	    dp->d_namep, tx, ty, tz);
    }

    /* Update reverse-lookup tables */
    instance_shape_map()[iid] = s;
    shape_instance_map()[s]   = iid;

    return true;
}

void
obol_cad_assembly_remove_shape(SoCADAssembly *cad_asm, bsg_shape *s)
{
    if (!cad_asm || !s)
	return;

    auto it = shape_instance_map().find(s);
    if (it == shape_instance_map().end())
	return;

    obol::InstanceId iid = it->second;
    cad_asm->removeInstance(iid);
    instance_shape_map().erase(iid);
    shape_instance_map().erase(it);
    selected_asm_shapes().erase(s);
}

void
obol_cad_assembly_clear(SoCADAssembly *cad_asm)
{
    if (cad_asm) {
	/* Remove all instances and parts */
	for (auto &kv : shape_instance_map())
	    cad_asm->removeInstance(kv.second);
    }

    instance_shape_map().clear();
    shape_instance_map().clear();
    selected_asm_shapes().clear();
    uploaded_parts().clear();
}

bsg_shape *
obol_find_shape_for_instance_id(obol::InstanceId iid)
{
    auto it = instance_shape_map().find(iid);
    if (it == instance_shape_map().end())
	return nullptr;
    return it->second;
}

void
obol_cad_assembly_set_selected(SoCADAssembly *cad_asm, bsg_shape *s, bool selected)
{
    if (!cad_asm || !s)
	return;

    if (selected)
	selected_asm_shapes().insert(s);
    else
	selected_asm_shapes().erase(s);

    /* Rebuild the selection set for the assembly */
    std::vector<obol::InstanceId> sel_ids;
    sel_ids.reserve(selected_asm_shapes().size());
    for (bsg_shape *sel : selected_asm_shapes()) {
	auto it = shape_instance_map().find(sel);
	if (it != shape_instance_map().end())
	    sel_ids.push_back(it->second);
    }
    cad_asm->setSelectedInstances(sel_ids);
}

#endif /* BRLCAD_ENABLE_OBOL */

/*
 * Local Variables:
 * mode: C++
 * tab-width: 8
 * c-basic-offset: 4
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
