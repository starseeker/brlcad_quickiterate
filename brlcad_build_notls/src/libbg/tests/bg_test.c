/*                    B G _ T E S T . C . I N
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

#include "common.h"

#include <stdio.h>
#include <string.h>

#include "bu.h"
#include "bn.h"
#include "bg.h"

/* test sources */

#include "libbg/tests/plane_dist.c"
#include "libbg/tests/plane_isect.c"
#include "libbg/tests/plane_pt.c"

int
main(int ac, char *av[])
{
    bu_setprogname(av[0]);

    if (ac < 2)	bu_exit(1, "Usage: %s [test] options\n\nAvailable tests:\n\n  plane_dist\n  plane_isect\n  plane_pt", av[0]);

    ac--; av++;

    
   if (BU_STR_EQUAL(av[0], "plane_dist")) return plane_dist_main(ac,av);
   if (BU_STR_EQUAL(av[0], "plane_isect")) return plane_isect_main(ac,av);
   if (BU_STR_EQUAL(av[0], "plane_pt")) return plane_pt_main(ac,av);

    bu_log("\nError: Unknown test type: %s\n\n", av[0]);

    return 1;
}


/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
