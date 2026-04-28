/*
 * Minimal BRL-CAD consumer example.
 *
 * Demonstrates using the modern find_package(BRLCAD CONFIG ...) interface.
 * Calls a few BRL-CAD API functions to verify that headers and libraries
 * are found correctly.
 */

#include "brlcad/bu/log.h"
#include "brlcad/bu/str.h"
#include "brlcad/raytrace.h"

int main(int argc, char *argv[])
{
    struct rt_i *rtip = NULL;

    bu_log("BRL-CAD consumer example\n");

    if (argc < 2) {
	bu_log("Usage: %s model.g [objects...]\n", argv[0]);
	bu_log("(No model supplied - demonstrating API linkage only)\n");
	return 0;
    }

    /* Open a geometry database file to exercise the rt API */
    rtip = rt_dirbuild(argv[1], NULL, 0);
    if (rtip == RTI_NULL) {
	bu_log("rt_dirbuild(%s) FAILED\n", argv[1]);
	return 1;
    }

    bu_log("Opened %s successfully.\n", argv[1]);
    rt_free_rti(rtip);
    return 0;
}
