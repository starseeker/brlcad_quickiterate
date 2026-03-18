/*                        P I X 2 F B . C
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
/** @file libged/pix2fb.c
 *
 * Program to take bottom-up pixel files and send them to a framebuffer.
 *
 */

#include "common.h"

#include <stdlib.h>
#ifdef HAVE_SYS_TYPES_H
#  include <sys/types.h>
#endif
#ifdef HAVE_SYS_STAT_H
#  include <sys/stat.h>
#endif
#ifdef HAVE_WINSOCK_H
#  include <winsock.h>
#endif

#include "bio.h"

#include "bu/getopt.h"
#include "bu/snooze.h"
#ifndef BRLCAD_ENABLE_OBOL
#  include "dm.h"
#  include "pkg.h"
#else
#  include <string.h>
#endif

#include "ged.h"


static int multiple_lines = 0;	/* Streamlined operation */

static char *file_name;
static int infd;

static int fileinput = 0;	/* file or pipe on input? */
static int autosize = 0;	/* !0 to autosize input */

static size_t file_width = 512;	/* default input width */
static size_t file_height = 512;/* default input height */
static int scr_width = 0;	/* screen tracks file if not given */
static int scr_height = 0;
static int file_xoff, file_yoff;
static int scr_xoff, scr_yoff;
static int clear = 0;
static int zoom = 0;
static int inverse = 0;		/* Draw upside-down */
static int one_line_only = 0;	/* insist on 1-line writes */
static int pause_sec = 0; 	/* Pause that many seconds before
				   closing the FB and exiting */

static char usage[] = "\
Usage: pix-fb [-a -h -i -c -z -1] [-m #lines] [-F framebuffer]\n\
	[-s squarefilesize] [-w file_width] [-n file_height]\n\
	[-x file_xoff] [-y file_yoff] [-X scr_xoff] [-Y scr_yoff]\n\
	[-S squarescrsize] [-W scr_width] [-N scr_height] [-p seconds]\n\
	[file.pix]\n";

static int
pix2fb_get_args(int argc, char **argv)
{
    int c;

    bu_optind = 1;
    while ((c = bu_getopt(argc, argv, "1m:aiczF:p:s:w:n:x:y:X:Y:S:W:N:h?")) != -1) {
	switch (c) {
	    case '1':
		one_line_only = 1;
		break;
	    case 'm':
		multiple_lines = atoi(bu_optarg);
		break;
	    case 'a':
		autosize = 1;
		break;
	    case 'i':
		inverse = 1;
		break;
	    case 'c':
		clear = 1;
		break;
	    case 'z':
		zoom = 1;
		break;
	    case 's':
		/* square file size */
		file_height = file_width = atoi(bu_optarg);
		autosize = 0;
		break;
	    case 'w':
		file_width = atoi(bu_optarg);
		autosize = 0;
		break;
	    case 'n':
		file_height = atoi(bu_optarg);
		autosize = 0;
		break;
	    case 'x':
		file_xoff = atoi(bu_optarg);
		break;
	    case 'y':
		file_yoff = atoi(bu_optarg);
		break;
	    case 'X':
		scr_xoff = atoi(bu_optarg);
		break;
	    case 'Y':
		scr_yoff = atoi(bu_optarg);
		break;
	    case 'S':
		scr_height = scr_width = atoi(bu_optarg);
		break;
	    case 'W':
		scr_width = atoi(bu_optarg);
		break;
	    case 'N':
		scr_height = atoi(bu_optarg);
		break;
	    case 'p':
		pause_sec = atoi(bu_optarg);
		break;

	    default:		/* 'h' '?' */
		return 0;
	}
    }

    if (bu_optind >= argc) {
	if (isatty(fileno(stdin)))
	    return 0;
	file_name = "-";
	infd = 0;
	setmode(infd, O_BINARY);
    } else {
	file_name = argv[bu_optind];
	infd = open(file_name, 0);
	if (infd < 0) {
	    perror(file_name);
	    fprintf(stderr,
			  "pix-fb: cannot open \"%s\" for reading\n",
			  file_name);
	    bu_exit(1, NULL);
	}
	setmode(infd, O_BINARY);
	fileinput++;
    }

    if (argc > ++bu_optind)
	fprintf(stderr, "pix-fb: excess argument(s) ignored\n");

    return 1;		/* OK */
}


int
ged_pix2fb_core(struct ged *gedp, int argc, const char *argv[])
{
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

    if (!pix2fb_get_args(argc, (char **)argv)) {
	bu_vls_printf(gedp->ged_result_str, "Usage: %s %s", argv[0], usage);
	return GED_HELP;
    }

#ifdef BRLCAD_ENABLE_OBOL
    /* Obol path: load raw RGB888 .pix data into fbs_pixbuf and request a
     * repaint via the application refresh handler. */
    {
	struct fbserv_obj *fbs = gedp->ged_fbs;
	int w = (int)((scr_width  > 0) ? (size_t)scr_width  : file_width);
	int h = (int)((scr_height > 0) ? (size_t)scr_height : file_height);
	if (w <= 0) w = 512;
	if (h <= 0) h = 512;

	if (fbs) {
	    if (!fbs->fbs_pixbuf || fbs->fbs_pixbuf_w != w || fbs->fbs_pixbuf_h != h) {
		if (fbs->fbs_pixbuf)
		    bu_free(fbs->fbs_pixbuf, "fbs_pixbuf");
		fbs->fbs_pixbuf = (unsigned char *)bu_calloc((size_t)w * h * 3, 1, "fbs_pixbuf");
		fbs->fbs_pixbuf_w = w;
		fbs->fbs_pixbuf_h = h;
	    }
	    /* Read file_height rows of file_width RGB pixels.
	     * .pix files are bottom-left origin; fbs_pixbuf uses the same
	     * convention, so direct row reads are correct. */
	    size_t row_bytes = file_width * 3;
	    unsigned char *rowbuf = (unsigned char *)bu_malloc(row_bytes, "pix2fb row");
	    for (size_t row = 0; row < file_height && row < (size_t)h; row++) {
		ssize_t nr = read(infd, rowbuf, row_bytes);
		if (nr <= 0) break;
		size_t dest_row = inverse ? (size_t)h - 1 - row : row;
		size_t copy_cols = ((size_t)w < file_width) ? (size_t)w : file_width;
		memcpy(fbs->fbs_pixbuf + dest_row * (size_t)w * 3, rowbuf, copy_cols * 3);
	    }
	    bu_free(rowbuf, "pix2fb row");
	    if (gedp->ged_gvp && gedp->ged_gvp->gv_s)
		gedp->ged_gvp->gv_s->gv_fb_mode = 2;
	}
	if (infd != 0) close(infd);
	bu_snooze(BU_SEC2USEC(pause_sec));
	if (gedp->ged_refresh_handler)
	    (*gedp->ged_refresh_handler)(gedp->ged_refresh_clientdata);
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

    int ret = fb_read_fd(fbp, infd,
			 file_width, file_height,
			 file_xoff, file_yoff,
			 scr_width, scr_height,
			 scr_xoff, scr_yoff,
			 fileinput, file_name, one_line_only, multiple_lines,
			 autosize, inverse, clear, zoom,
			 gedp->ged_result_str);

    if (infd != 0)
	close(infd);

    bu_snooze(BU_SEC2USEC(pause_sec));

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

#define GED_PIX2FB_COMMANDS(X, XID) \
    X(pix2fb, ged_pix2fb_core, GED_CMD_DEFAULT) \

GED_DECLARE_COMMAND_SET(GED_PIX2FB_COMMANDS)
GED_DECLARE_PLUGIN_MANIFEST("libged_pix2fb", 1, GED_PIX2FB_COMMANDS)

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
