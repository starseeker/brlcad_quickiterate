/*                    B N _ T E S T . C . I N
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

/* test sources */

#include "libbn/tests/complex.c"
#include "libbn/tests/mat.c"
#include "libbn/tests/noise.c"
#include "libbn/tests/poly_add.c"
#include "libbn/tests/poly_multiply.c"
#include "libbn/tests/poly_scale.c"
#include "libbn/tests/poly_sub.c"
#include "libbn/tests/poly_synthetic_div.c"
#include "libbn/tests/qmath.c"
#include "libbn/tests/sobolseq.c"
#include "libbn/tests/tabdata.c"

int
main(int ac, char *av[])
{
    bu_setprogname(av[0]);

    if (ac < 2)	bu_exit(1, "Usage: %s [test] options\n\nAvailable tests:\n\n  complex\n  mat\n  noise\n  poly_add\n  poly_multiply\n  poly_scale\n  poly_sub\n  poly_synthetic_div\n  qmath\n  sobolseq\n  tabdata", av[0]);

    ac--; av++;

    
   if (BU_STR_EQUAL(av[0], "complex")) return complex_main(ac,av);
   if (BU_STR_EQUAL(av[0], "mat")) return mat_main(ac,av);
   if (BU_STR_EQUAL(av[0], "noise")) return noise_main(ac,av);
   if (BU_STR_EQUAL(av[0], "poly_add")) return poly_add_main(ac,av);
   if (BU_STR_EQUAL(av[0], "poly_multiply")) return poly_multiply_main(ac,av);
   if (BU_STR_EQUAL(av[0], "poly_scale")) return poly_scale_main(ac,av);
   if (BU_STR_EQUAL(av[0], "poly_sub")) return poly_sub_main(ac,av);
   if (BU_STR_EQUAL(av[0], "poly_synthetic_div")) return poly_synthetic_div_main(ac,av);
   if (BU_STR_EQUAL(av[0], "qmath")) return qmath_main(ac,av);
   if (BU_STR_EQUAL(av[0], "sobolseq")) return sobolseq_main(ac,av);
   if (BU_STR_EQUAL(av[0], "tabdata")) return tabdata_main(ac,av);

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
