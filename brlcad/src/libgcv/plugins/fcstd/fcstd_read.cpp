/*                  F C S T D _ R E A D . C P P
 * BRL-CAD
 *
 * Copyright (c) 2026 United States Government as represented by
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
/** @file fcstd_read.cpp
 *
 * Import FreeCAD .FCStd files into BRL-CAD.
 *
 * An .FCStd file is a ZIP archive containing OCCT BRep shape data and XML
 * metadata.  This plugin uses the open2open library to convert those shapes
 * through openNURBS ON_Brep objects, which are then written into the .g
 * database via mk_brep().
 *
 * For each shape object in the FCStd file, a brep solid is created in the .g
 * file using the object label as the solid name.  All solids are collected
 * under a top-level combination named after the input file.
 *
 * Requirements:
 *   open2open library (built with OpenCASCADE and libzip support)
 */

#include "common.h"

#include "bu/log.h"
#include "bu/path.h"
#include "gcv/api.h"
#include "gcv/util.h"
#include "wdb.h"

#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

/* openNURBS is pulled in transitively through open2open */
#include "opennurbs.h"

/* open2open FreeCAD reader + brep converter */
#include "open2open/fcstd_convert.h"


namespace fcstd_read
{


/* sanitise an object name for use as a BRL-CAD primitive name */
static std::string
sanitize_name(const std::string &raw, int idx)
{
    if (raw.empty())
	return std::string("shape_") + std::to_string(idx);

    std::string out;
    out.reserve(raw.size());
    for (char c : raw) {
	if (c == '/' || c == ' ')
	    out += '_';
	else
	    out += c;
    }
    return out;
}


/* ensure name is unique in the names set, appending a numeric suffix as needed */
static std::string
unique_name(const std::string &base, std::map<std::string, int> &used)
{
    if (used.find(base) == used.end()) {
	used[base] = 1;
	return base;
    }
    int n = used[base]++;
    return base + "_" + std::to_string(n);
}


int
fcstd_read(gcv_context *context, const gcv_opts *gcv_options,
	   const void *UNUSED(options_data), const char *source_path)
{
    /* derive top-level combination name from the filename */
    std::string root_name;
    {
	bu_vls temp = BU_VLS_INIT_ZERO;
	if (!bu_path_component(&temp, source_path, BU_PATH_BASENAME_EXTLESS))
	    root_name = gcv_options->default_name
		? gcv_options->default_name : "fcstd_model";
	else
	    root_name = bu_vls_addr(&temp);
	bu_vls_free(&temp);
    }

    struct rt_wdb *wdbp = wdb_dbopen(context->dbip, RT_WDB_TYPE_DB_INMEM);

    /* Read FcstdDoc for CSG structure (type, base/tool/shapes links). */
    open2open::FcstdDoc doc;
    bool has_csg_info = open2open::ReadFcstdDoc(source_path, doc);

    /* Convert FCStd BRep geometry → ONX_Model via open2open */
    ONX_Model model;
    int n_converted = open2open::FCStdFileToONX_Model(source_path, model);
    if (n_converted <= 0) {
	bu_log("fcstd_read: no shapes could be converted from '%s'\n",
	       source_path);
	return 0;
    }

    /* ------------------------------------------------------------------ *
     * Pass 1: write every converted BRep solid into the .g database and  *
     * build a label → database-name map for later CSG tree construction. *
     * ------------------------------------------------------------------ */
    std::map<std::string, int> used_names;
    /* label_to_db: ON_3dmObjectAttributes name → solid db name */
    std::map<std::string, std::string> label_to_db;
    /* ordered list of every solid written */
    std::vector<std::string> all_solid_names;

    ONX_ModelComponentIterator it(model,
				  ON_ModelComponent::Type::ModelGeometry);
    int idx = 0;
    for (const ON_ModelComponent *mc = it.FirstComponent();
	 mc != nullptr; mc = it.NextComponent(), ++idx) {

	const ON_ModelGeometryComponent *mgc =
	    ON_ModelGeometryComponent::Cast(mc);
	if (!mgc)
	    continue;

	const ON_Geometry *geom = mgc->Geometry(nullptr);
	if (!geom)
	    continue;

	std::unique_ptr<ON_Brep> temp_brep;
	const ON_Brep *brep = nullptr;

	if (geom->ObjectType() == ON::brep_object) {
	    brep = ON_Brep::Cast(geom);
	} else if (geom->HasBrepForm()) {
	    temp_brep.reset(geom->BrepForm());
	    brep = temp_brep.get();
	}

	if (!brep)
	    continue;

	/* derive a candidate name from the model attributes */
	std::string label;
	const ON_3dmObjectAttributes *attrs = mgc->Attributes(nullptr);
	if (attrs && attrs->m_name.IsNotEmpty()) {
	    ON_String utf8;
	    utf8 = attrs->m_name;
	    label = std::string(utf8.Array());
	}
	std::string candidate = sanitize_name(label, idx);
	std::string solid_name = unique_name(candidate, used_names);

	ON_Brep *writable = const_cast<ON_Brep *>(brep);
	if (mk_brep(wdbp, solid_name.c_str(), writable)) {
	    bu_log("fcstd_read: mk_brep() failed for object '%s'\n",
		   solid_name.c_str());
	    continue;
	}

	all_solid_names.push_back(solid_name);
	if (!label.empty())
	    label_to_db[label] = solid_name;
    }

    if (all_solid_names.empty()) {
	bu_log("fcstd_read: no BRep solids written for '%s'\n", source_path);
	return 0;
    }

    /* ------------------------------------------------------------------ *
     * Pass 2 (CSG tree): if FcstdDoc was read successfully, create       *
     * combination objects for boolean operation types (Part::Cut,        *
     * Part::Fuse, Part::MultiFuse, Part::Common, Part::MultiCommon).     *
     *                                                                    *
     * Strategy:                                                          *
     *  - Build fcstd_internal_name → solid_db_name from label_to_db     *
     *  - Process doc.objects in order (FreeCAD writes topological order) *
     *  - For each boolean op, look up inputs in csg_name map first,     *
     *    falling back to solid name; create a "<label>_csg" combination  *
     *  - Track which FCStd names are consumed as inputs                  *
     *  - Top-level comb includes only non-consumed objects               *
     * ------------------------------------------------------------------ */

    /* fcstd_internal_name → solid db name */
    std::map<std::string, std::string> name_to_db;
    /* fcstd_internal_name → CSG combination db name (filled below) */
    std::map<std::string, std::string> name_to_csg;
    /* fcstd_internal_names consumed as boolean-op inputs */
    std::set<std::string> consumed;

    if (has_csg_info) {
	/* Build name_to_db: map from FCStd internal name to solid db name */
	for (const auto &obj : doc.objects) {
	    const std::string &key =
		obj.label.empty() ? obj.name : obj.label;
	    auto it2 = label_to_db.find(key);
	    if (it2 != label_to_db.end())
		name_to_db[obj.name] = it2->second;
	}

	/* Collect the set of FCStd internal names that are boolean-op types.
	 * These inputs must resolve to a CSG combo (not a brep solid) so that
	 * the BRL-CAD tree references the parametric representation.  If the
	 * combo isn't ready yet, lookup_input returns "" to defer processing. */
	std::set<std::string> bool_op_names;
	for (const auto &obj : doc.objects) {
	    const std::string &t = obj.type;
	    if (t == "Part::Cut"   || t == "Part::Fuse" ||
		t == "Part::MultiFuse" || t == "Part::Common" ||
		t == "Part::MultiCommon")
		bool_op_names.insert(obj.name);
	}

	/* Helper: given an FCStd internal name, return the db name to use as
	 * an input reference.
	 * - Boolean-op inputs MUST resolve to their CSG combo; return "" to
	 *   defer if the combo is not yet created (multi-pass will retry).
	 * - Non-boolean-op inputs use the brep solid directly. */
	auto lookup_input = [&](const std::string &fcstd_name) -> std::string {
	    if (bool_op_names.count(fcstd_name)) {
		auto ci = name_to_csg.find(fcstd_name);
		return (ci != name_to_csg.end()) ? ci->second : std::string();
	    }
	    auto di = name_to_db.find(fcstd_name);
	    return (di != name_to_db.end()) ? di->second : std::string();
	};

	/* Helper: attempt to create the CSG combo for one boolean-op object.
	 * Returns true if a combo was successfully created, false if inputs
	 * are not yet available (caller should retry later). */
	auto try_create_csg = [&](const open2open::FcstdObject &obj) -> bool {
	    if (name_to_csg.count(obj.name))
		return true; /* already created */

	    const std::string &type = obj.type;
	    bool is_cut       = (type == "Part::Cut");
	    bool is_fuse      = (type == "Part::Fuse");
	    bool is_multifuse = (type == "Part::MultiFuse");
	    bool is_common    = (type == "Part::Common");
	    bool is_multicommon = (type == "Part::MultiCommon");

	    if (!is_cut && !is_fuse && !is_multifuse &&
		!is_common && !is_multicommon)
		return true; /* not a boolean op; nothing to create */

	    struct wmember wm;
	    BU_LIST_INIT(&wm.l);
	    bool ok = false;

	    if (is_cut) {
		std::string base = lookup_input(obj.base_name);
		std::string tool = lookup_input(obj.tool_name);
		if (!base.empty() && !tool.empty()) {
		    mk_addmember(base.c_str(), &wm.l, NULL, WMOP_UNION);
		    mk_addmember(tool.c_str(), &wm.l, NULL, WMOP_SUBTRACT);
		    consumed.insert(obj.base_name);
		    consumed.insert(obj.tool_name);
		    ok = true;
		}
	    } else if (is_fuse) {
		std::string base = lookup_input(obj.base_name);
		std::string tool = lookup_input(obj.tool_name);
		if (!base.empty() && !tool.empty()) {
		    mk_addmember(base.c_str(), &wm.l, NULL, WMOP_UNION);
		    mk_addmember(tool.c_str(), &wm.l, NULL, WMOP_UNION);
		    consumed.insert(obj.base_name);
		    consumed.insert(obj.tool_name);
		    ok = true;
		}
	    } else if (is_multifuse) {
		bool all_found = !obj.shapes.empty();
		for (const auto &sn : obj.shapes) {
		    std::string inp = lookup_input(sn);
		    if (inp.empty()) { all_found = false; break; }
		    mk_addmember(inp.c_str(), &wm.l, NULL, WMOP_UNION);
		    consumed.insert(sn);
		}
		if (all_found && !BU_LIST_IS_EMPTY(&wm.l))
		    ok = true;
	    } else if (is_common) {
		std::string base = lookup_input(obj.base_name);
		std::string tool = lookup_input(obj.tool_name);
		if (!base.empty() && !tool.empty()) {
		    mk_addmember(base.c_str(), &wm.l, NULL, WMOP_UNION);
		    mk_addmember(tool.c_str(), &wm.l, NULL, WMOP_INTERSECT);
		    consumed.insert(obj.base_name);
		    consumed.insert(obj.tool_name);
		    ok = true;
		}
	    } else if (is_multicommon) {
		bool all_found = !obj.shapes.empty();
		bool first = true;
		for (const auto &sn : obj.shapes) {
		    std::string inp = lookup_input(sn);
		    if (inp.empty()) { all_found = false; break; }
		    mk_addmember(inp.c_str(), &wm.l, NULL,
				 first ? WMOP_UNION : WMOP_INTERSECT);
		    first = false;
		    consumed.insert(sn);
		}
		if (all_found && !BU_LIST_IS_EMPTY(&wm.l))
		    ok = true;
	    }

	    if (!ok) {
		if (!BU_LIST_IS_EMPTY(&wm.l))
		    mk_freemembers(&wm.l);
		return false; /* inputs not yet available */
	    }

	    std::string csg_label = obj.label.empty() ? obj.name : obj.label;
	    std::string csg_name = unique_name(
		sanitize_name(csg_label, 0) + "_csg", used_names);

	    if (!mk_comb(wdbp, csg_name.c_str(), &wm.l,
			 0, NULL, NULL, NULL,
			 0, 0, 0, 0, 0, 0, 0)) {
		name_to_csg[obj.name] = csg_name;
		/* NOTE: do NOT insert obj.name into consumed here.
		 * Consumed tracks which objects are used as INPUTS to
		 * other ops.  Whether this op is itself consumed is
		 * determined when a higher-level op references it. */
	    } else {
		bu_log("fcstd_read: mk_comb() failed for '%s'\n",
		       csg_name.c_str());
		mk_freemembers(&wm.l);
	    }
	    return true; /* attempted (even if mk_comb failed) */
	};

	/* Multi-pass loop: doc.objects may come from a std::map and therefore
	 * arrive in alphabetical order rather than topological order.  Keep
	 * iterating until no new CSG combos were created in a full pass. */
	bool any_progress = true;
	while (any_progress) {
	    any_progress = false;
	    for (const auto &obj : doc.objects) {
		if (name_to_csg.count(obj.name))
		    continue; /* already done */
		/* skip non-boolean-op objects */
		const std::string &t = obj.type;
		if (t != "Part::Cut" && t != "Part::Fuse" &&
		    t != "Part::MultiFuse" && t != "Part::Common" &&
		    t != "Part::MultiCommon")
		    continue;
		size_t before = name_to_csg.size();
		try_create_csg(obj);
		if (name_to_csg.size() > before)
		    any_progress = true;
	    }
	}
    }

    /* ------------------------------------------------------------------ *
     * Pass 3: build the top-level combination.                           *
     *                                                                    *
     * If CSG info is available, include:                                 *
     *   - CSG combos for non-consumed boolean ops (the "roots")          *
     *   - Brep solids for non-consumed non-boolean objects               *
     * Otherwise, include all brep solids.                                *
     * ------------------------------------------------------------------ */
    struct wmember root_wm;
    BU_LIST_INIT(&root_wm.l);

    if (has_csg_info && !name_to_csg.empty()) {
	/* Add CSG combos for root (non-consumed) boolean ops */
	for (const auto &obj : doc.objects) {
	    auto csg_it = name_to_csg.find(obj.name);
	    if (csg_it == name_to_csg.end())
		continue;	/* no CSG combo for this object */
	    if (consumed.count(obj.name))
		continue;	/* this op is an input to another op */
	    mk_addmember(csg_it->second.c_str(), &root_wm.l, NULL, WMOP_UNION);
	}

	/* Add brep solids for non-consumed non-boolean objects.
	 * We need a reverse map: db_name → fcstd_name to check consumed set. */
	std::map<std::string, std::string> db_to_fcstd;
	for (const auto &kv : name_to_db)
	    db_to_fcstd[kv.second] = kv.first;

	for (const auto &solid : all_solid_names) {
	    auto ri = db_to_fcstd.find(solid);
	    if (ri == db_to_fcstd.end()) {
		/* solid not found in FCStd map — include it */
		mk_addmember(solid.c_str(), &root_wm.l, NULL, WMOP_UNION);
	    } else if (!consumed.count(ri->second)) {
		/* FCStd object not consumed AND no CSG combo → include brep */
		if (!name_to_csg.count(ri->second))
		    mk_addmember(solid.c_str(), &root_wm.l, NULL, WMOP_UNION);
	    }
	}
    } else {
	/* Flat fallback: include all brep solids */
	for (const auto &sn : all_solid_names)
	    mk_addmember(sn.c_str(), &root_wm.l, NULL, WMOP_UNION);
    }

    if (BU_LIST_IS_EMPTY(&root_wm.l)) {
	/* All objects were consumed — add them all as fallback */
	for (const auto &sn : all_solid_names)
	    mk_addmember(sn.c_str(), &root_wm.l, NULL, WMOP_UNION);
    }

    if (mk_comb(wdbp, root_name.c_str(), &root_wm.l,
		0 /*region*/, NULL, NULL, NULL,
		0, 0, 0, 0,
		0 /*inherit*/, 0 /*append*/, 0 /*gift_semantics*/)) {
	bu_log("fcstd_read: mk_comb() failed for '%s'\n", root_name.c_str());
	/* non-fatal — the individual solids and CSG combos were written */
    }

    return 1;
}


int
fcstd_can_read(const char *source_path)
{
    if (!source_path)
	return 0;

    /* check the file extension first (fast path) */
    const char *ext = strrchr(source_path, '.');
    if (ext) {
	if (BU_STR_EQUIV(ext + 1, "fcstd"))
	    return 1;
	if (BU_STR_EQUIV(ext + 1, "FCStd"))
	    return 1;
    }

    /* open2open will return 0 shapes on a non-FCStd file, but a cheap
     * confirming test is to check whether ReadFcstdDoc succeeds on the
     * archive (it checks for a valid ZIP with Document.xml inside). */
    open2open::FcstdDoc doc;
    return open2open::ReadFcstdDoc(source_path, doc) ? 1 : 0;
}


struct gcv_filter gcv_conv_fcstd_read = {
    "FreeCAD FCStd Reader", GCV_FILTER_READ, BU_MIME_MODEL_VND_FREECAD,
    fcstd_can_read, NULL, NULL, fcstd_read
};

static const gcv_filter * const filters[] = {&gcv_conv_fcstd_read, NULL};


} /* namespace fcstd_read */


extern "C"
{
    extern const gcv_plugin gcv_plugin_info_s = {fcstd_read::filters};
    COMPILER_DLLEXPORT const struct gcv_plugin *
    gcv_plugin_info()
    {
	return &gcv_plugin_info_s;
    }
}


// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
