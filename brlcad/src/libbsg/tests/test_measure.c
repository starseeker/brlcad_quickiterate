/*                 T E S T _ M E A S U R E . C
 * BRL-CAD
 */

#include "common.h"

#include <stdio.h>

#include "bu/app.h"
#include "bsg/measure.h"

int
main(int argc, char **argv)
{
    bu_setprogname(argv[0]);
    (void)argc;

    struct bsg_measure_result out;
    point_t a = {0.0, 0.0, 0.0};
    point_t b = {3.0, 4.0, 0.0};
    if (!bsg_measure_candidates(NULL, a, b, &out)) {
	printf("FAIL: expected valid measure result\n");
	return 1;
    }
    if (!out.mr_valid) {
	printf("FAIL: result marked invalid\n");
	return 1;
    }
    if (out.mr_distance < 4.999 || out.mr_distance > 5.001) {
	printf("FAIL: expected distance 5, got %g\n", out.mr_distance);
	return 1;
    }
    printf("PASS test_measure\n");
    return 0;
}

