/*                   G E D _ E D I T 2 . H
 * BRL-CAD
 *
 * Copyright (c) 2008-2026 United States Government as represented by
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
/** @file ged_edit2.h
 *
 * Private header for libged edit2 cmd.
 *
 */

#ifndef LIBGED_EDIT2_GED_PRIVATE_H
#define LIBGED_EDIT2_GED_PRIVATE_H

#include "common.h"

#include <set>
#include <string>
#include <vector>
#include <time.h>

#include "ged.h"

__BEGIN_DECLS

#define HELPFLAG "--print-help"
#define PURPOSEFLAG "--print-purpose"


/**
 * One resolved geometry specifier from the edit command line.
 *
 * Each token that resolves to a geometry object (or the batch marker ".") is
 * parsed into this structure during Pass 2 of the three-pass parser.
 */
struct ged_edit_geom_spec {
    std::string raw;       /**< @brief original argv token */
    std::string path;      /**< @brief resolved path string (no URI components) */
    std::string fragment;  /**< @brief URI fragment, e.g. "V1" from "obj.s#V1" */
    std::string query;     /**< @brief URI query, e.g. "V*" from "obj.s?V*"   */
    bool is_batch;         /**< @brief true when token was "." (each-object)   */
    std::vector<unsigned long long> hashes; /**< @brief from DbiState::digest_path */
    struct directory *dp;  /**< @brief head dp; RT_DIR_NULL for comb instances */
};


/**
 * Top-level parsing context for the edit command.
 *
 * Created once per ged_edit2_core() invocation and passed to subcommands.
 * Replaces the legacy struct edit_info.
 *
 * Pass 1 (global opts) fills the flag_* fields.
 * Pass 2 (geometry specifiers) fills geom_specs and from_selection.
 * Pass 3 (subcommand dispatch) uses geom_specs and the subcommand args.
 */
struct ged_edit_ctx {
    struct ged *gedp;
    int verbosity;
    float *prand;

    /* Conflict arbiter flags (set by Pass 1 global options) */
    int flag_S;  /**< @brief -S: operate on selection, ignoring cmd-line specifier */
    int flag_f;  /**< @brief -f: force: apply op, write to disk, clear conflict     */
    int flag_F;  /**< @brief -F: abandon: discard intermediate state, use on-disk   */
    int flag_i;  /**< @brief -i: intermediate: apply to temp buf only (no disk write)*/

    /* Resolved geometry specifiers (populated by Pass 2) */
    std::vector<ged_edit_geom_spec> geom_specs;
    bool from_selection; /**< @brief true if geom_specs came from selection fallback */

    /* Conflict state: set when an explicit specifier conflicts with the
     * active selection and no arbiter flag was given. */
    bool has_conflict;

    /* Convenience: dp for the primary (first) object.
     * RT_DIR_NULL when the primary spec is a multi-segment comb path. */
    struct directory *dp;
};

__END_DECLS

#endif /* LIBGED_EDIT2_GED_PRIVATE_H */

/** @} */
/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
