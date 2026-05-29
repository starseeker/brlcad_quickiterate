/*                 T E S T _ F A C E T I Z E _ A P I . C
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

#include "common.h"

#include "bu/app.h"
#include "bu/avs.h"
#include "bu/vls.h"
#include "gcv.h"


static int
test_defaults(void)
{
    struct gcv_facetize_opts opts;

    gcv_facetize_opts_default(&opts);

    if (opts.output_mode != GCV_FACETIZE_OUTPUT_SINGLE_BOT)
	return 1;
    if (opts.boolean_engine != GCV_FACETIZE_BOOL_MANIFOLD)
	return 1;
    if (!opts.subprocess)
	return 1;
    if (opts.max_sampled_points != 200000)
	return 1;
    if (!NEAR_EQUAL(opts.perturb_volume_threshold, 10.0, VUNITIZE_TOL))
	return 1;
    if (!NEAR_EQUAL(opts.perturb_surface_area_threshold, 10.0, VUNITIZE_TOL))
	return 1;

    bu_avs_free(&opts.method_options);
    return 0;
}


static int
test_methods(void)
{
    const struct gcv_facetize_method_info *methods = NULL;
    const struct gcv_facetize_method_info *resolved[3] = {NULL, NULL, NULL};
    size_t method_cnt = gcv_facetize_methods(&methods);

    if (method_cnt != 3)
	return 1;
    if (!methods)
	return 1;
    if (!BU_STR_EQUAL(methods[0].name, "NMG"))
	return 1;
    if (!BU_STR_EQUAL(methods[1].name, "CM"))
	return 1;
    if (!BU_STR_EQUAL(methods[2].name, "SPSR"))
	return 1;
    if (!gcv_facetize_method("NMG"))
	return 1;
    if (gcv_facetize_method("missing"))
	return 1;
    if (gcv_facetize_resolve_methods(NULL, resolved, 3) != 3)
	return 1;
    if (!resolved[0] || !BU_STR_EQUAL(resolved[0]->name, "NMG"))
	return 1;

    return 0;
}


static int
test_method_options(void)
{
    const struct gcv_facetize_option_desc *options = NULL;
    size_t option_cnt = gcv_facetize_method_options("SPSR", &options);

    if (!options)
	return 1;
    if (option_cnt == 0)
	return 1;
    if (!gcv_facetize_method_options("NMG", NULL))
	return 1;
    if (gcv_facetize_method_options("missing", &options) != 0)
	return 1;
    if (options)
	return 1;

    return 0;
}


static int
test_describe(void)
{
    struct bu_vls desc = BU_VLS_INIT_ZERO;

    gcv_facetize_describe_options(&desc);
    if (!bu_vls_strlen(&desc)) {
	bu_vls_free(&desc);
	return 1;
    }
    if (!strstr(bu_vls_cstr(&desc), "NMG")) {
	bu_vls_free(&desc);
	return 1;
    }

    bu_vls_free(&desc);
    return 0;
}


int
main(int argc, char **argv)
{
    int failures = 0;

    bu_setprogname(argv[0]);
    if (argc != 1)
	return 1;

    failures += test_defaults();
    failures += test_methods();
    failures += test_method_options();
    failures += test_describe();

    return failures;
}
