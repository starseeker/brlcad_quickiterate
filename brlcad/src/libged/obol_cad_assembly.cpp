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
    return SbMatrix(
	(float)m[0],  (float)m[1],  (float)m[2],  (float)m[3],
	(float)m[4],  (float)m[5],  (float)m[6],  (float)m[7],
	(float)m[8],  (float)m[9],  (float)m[10], (float)m[11],
	(float)m[12], (float)m[13], (float)m[14], (float)m[15]
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

    /* Need the full path to build the InstanceId */
    struct db_full_path *fp = (struct db_full_path *)s->s_path;
    if (!fp || fp->fp_len == 0)
	return false;

    struct directory *dp = DB_FULL_PATH_CUR_DIR(fp);
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
    }

    /* ------------------------------------------------------------------ *
     * Instance: register / update transform + style                       *
     * ------------------------------------------------------------------ */
    obol::InstanceId iid = fp_to_instance_id(fp);

    obol::InstanceRecord rec;
    rec.part        = pid;
    rec.localToRoot = mat_to_sbmatrix(s->s_mat);
    rec.parent      = fp_to_parent_id(fp);
    rec.childName   = dp->d_namep;
    rec.occurrenceIndex = fp->fp_cinst ? (uint32_t)fp->fp_cinst[fp->fp_len-1] : 0;
    rec.boolOp = 0;
    if (fp->fp_bool) {
	switch (fp->fp_bool[fp->fp_len-1]) {
	    case 4: rec.boolOp = 1; break;   /* subtract */
	    case 3: rec.boolOp = 2; break;   /* intersect */
	    default: rec.boolOp = 0; break;
	}
    }

    /* Per-instance colour from s_os */
    if (s->s_os) {
	rec.style.hasColorOverride = true;
	rec.style.color = SbColor4f(
	    s->s_os->color[0] / 255.0f,
	    s->s_os->color[1] / 255.0f,
	    s->s_os->color[2] / 255.0f,
	    1.0f);
    }

    cad_asm->upsertInstance(iid, rec);

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
