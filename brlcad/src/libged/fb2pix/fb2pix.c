/*                        F B 2 P I X . C
 * BRL-CAD
 *
 * Copyright (c) 1986-2025 United States Government as represented by
 * the U.S. Army Research Laboratory.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this file; see the file named COPYING for more
 * information.
 *
 */
/** @file libged/fb2pix.c
 *
 * fb2pix writes a framebuffer image to a .pix file.
 *
 */

#include "common.h"

#include <stdlib.h>
#include <sys/stat.h>

#include "bio.h"

#include "bu/app.h"
#include "bu/getopt.h"
#ifndef BRLCAD_ENABLE_OBOL
#  include "dm.h"
#  include "pkg.h"
#endif
#include "ged.h"


struct fb2pix_state {
    char *file_name;
    FILE *outfp;
    int crunch;		/* Color map crunch? */
    int inverse;	/* Draw upside-down */
    int screen_height;	/* input height */
    int screen_width;	/* input width */
};

#define FB2PIX_INIT {NULL, NULL, 0, 0, 512, 512}

static int
fb2pix_get_args(struct fb2pix_state *s, int argc, char **argv)
{
    int c;

    bu_optind = 1;
    while ((c = bu_getopt(argc, argv, "ciF:s:w:n:h?")) != -1) {
	switch (c) {
	    case 'c':
		s->crunch = 1;
		break;
	    case 'i':
		s->inverse = 1;
		break;
	    case 's':
		/* square size */
		s->screen_height = s->screen_width = atoi(bu_optarg);
		break;
	    case 'w':
		s->screen_width = atoi(bu_optarg);
		break;
	    case 'n':
		s->screen_height = atoi(bu_optarg);
		break;

	    default:		/* 'h' '?' */
		return 0;
	}
    }

    if (bu_optind >= argc) {
	if (isatty(fileno(stdout)))
	    return 0;
	s->file_name = "-";
	s->outfp = stdout;
    } else {
	s->file_name = argv[bu_optind];
	s->outfp = fopen(s->file_name, "wb");
	if (s->outfp == NULL) {
	    fprintf(stderr,
			  "fb-pix: cannot open \"%s\" for writing\n",
			  s->file_name);
	    return 0;
	}
	(void)bu_fchmod(fileno(s->outfp), 0444);
    }

    if (argc > ++bu_optind)
	fprintf(stderr, "fb-pix: excess argument(s) ignored\n");

    return 1;		/* OK */
}


int
ged_fb2pix_core(struct ged *gedp, int argc, const char *argv[])
{
    char usage[] = "Usage: fb-pix [-h -i -c] \n\
	[-s squaresize] [-w width] [-n height] [file.pix]\n";

    GED_CHECK_ARGC_GT_0(gedp, argc, BRLCAD_ERROR);

    if (!gedp->ged_gvp) {
	bu_vls_printf(gedp->ged_result_str, ": no current view set\n");
	return BRLCAD_ERROR;
    }

    /* initialize result */
    bu_vls_trunc(gedp->ged_result_str, 0);

    /* must be wanting help */
    if (argc == 1) {
	bu_vls_printf(gedp->ged_result_str, "Usage: %s %s", argv[0], usage);
	return GED_HELP;
    }

    struct fb2pix_state f2ps = FB2PIX_INIT;

    if (!fb2pix_get_args(&f2ps, argc, (char **)argv)) {
	bu_vls_printf(gedp->ged_result_str, "Usage: %s %s", argv[0], usage);
	return GED_HELP;
    }

#ifdef BRLCAD_ENABLE_OBOL
    /* Obol path: read from fbs_pixbuf (packed RGB888, bottom-left origin)
     * and write it as a raw .pix file. */
    {
	struct fbserv_obj *fbs = gedp->ged_fbs;
	if (!fbs || !fbs->fbs_pixbuf) {
	    bu_vls_printf(gedp->ged_result_str,
			  "fb2pix: no framebuffer data (run ert first)\n");
	    if (outfp && outfp != stdout) fclose(outfp);
	    return BRLCAD_ERROR;
	}
	int w = (screen_width  > 0) ? screen_width  : fbs->fbs_pixbuf_w;
	int h = (screen_height > 0) ? screen_height : fbs->fbs_pixbuf_h;
	w = (w > fbs->fbs_pixbuf_w) ? fbs->fbs_pixbuf_w : w;
	h = (h > fbs->fbs_pixbuf_h) ? fbs->fbs_pixbuf_h : h;
	setmode(fileno(outfp), O_BINARY);
	for (int row = 0; row < h; row++) {
	    int src_row = inverse ? row : (fbs->fbs_pixbuf_h - 1 - row);
	    if (src_row < 0 || src_row >= fbs->fbs_pixbuf_h)
		src_row = 0;
	    unsigned char *rowp = fbs->fbs_pixbuf + (size_t)src_row * fbs->fbs_pixbuf_w * 3;
	    fwrite(rowp, 3, (size_t)w, outfp);
	}
	if (outfp != stdout) fclose(outfp);
	return BRLCAD_OK;
    }
#else
    struct dm *dmp = (struct dm *)gedp->ged_gvp->dmp;
    if (!dmp) {
	bu_vls_printf(gedp->ged_result_str, "framebuffer operations require a display manager backend");
	return BRLCAD_ERROR;
    }

    struct fb *fbp = dm_get_fb(dmp);
    if (!fbp) {
	bu_vls_printf(gedp->ged_result_str, "display manager does not have a framebuffer");
	return BRLCAD_ERROR;
    }

    int ret;
    setmode(fileno(stdout), O_BINARY);

    ret = fb_write_fp(fbp, f2ps.outfp,
		      f2ps.screen_width, f2ps.screen_height,
		      f2ps.crunch, f2ps.inverse, gedp->ged_result_str);

    if (f2ps.outfp != stdout)
	fclose(f2ps.outfp);

    if (ret == BRLCAD_OK) {
	(void)dm_draw_begin(dmp);
	fb_refresh(fbp, 0, 0, fb_getwidth(fbp), fb_getheight(fbp));
	(void)dm_draw_end(dmp);
	return BRLCAD_OK;
    }

    return BRLCAD_ERROR;
#endif /* BRLCAD_ENABLE_OBOL */
}

#include "../include/plugin.h"

#define GED_FB2PIX_COMMANDS(X, XID) \
    X(fb2pix, ged_fb2pix_core, GED_CMD_DEFAULT) \

GED_DECLARE_COMMAND_SET(GED_FB2PIX_COMMANDS)
GED_DECLARE_PLUGIN_MANIFEST("libged_fb2pix", 1, GED_FB2PIX_COMMANDS)

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
