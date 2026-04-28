/*                      P N G . C
 * BRL-CAD
 *
 * Copyright (c) 2007-2026 United States Government as represented by
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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "png.h"

#include "bio.h"

#include "bu/str.h"
#include "bu/file.h"
#include "bu/log.h"
#include "bu/malloc.h"
#include "bu/vls.h"
#include "vmath.h"
#include "icv_private.h"

/* PNG tEXt chunk keys used for BRL-CAD render metadata */
#define ICV_PNG_KEY_DB      "BRL-CAD-db"
#define ICV_PNG_KEY_OBJECTS "BRL-CAD-objects"
#define ICV_PNG_KEY_CAMERA  "BRL-CAD-camera"

/*
 * Number of significant decimal digits needed to round-trip a double
 * (equivalent to std::numeric_limits<double>::max_digits10 = 17).
 */
#define ICV_DBL_DIGITS 17


/**
 * Serialise an icv_render_info into the camera text-chunk value.
 * Format:
 *   viewrotscale <16 values>
 *   eye_model <3 values>
 *   viewsize <value>
 *   aspect <value>
 *   perspective <value>
 *
 * All doubles are written with ICV_DBL_DIGITS significant digits.
 */
static void
render_info_to_camera_str(const struct icv_render_info *info, struct bu_vls *out)
{
    int i;
    char numbuf[64];

    /* Use snprintf to ensure correct %g formatting with full precision */
    bu_vls_strcat(out, "viewrotscale");
    for (i = 0; i < 16; i++) {
	snprintf(numbuf, sizeof(numbuf), " %.17g", info->viewrotscale[i]);
	bu_vls_strcat(out, numbuf);
    }
    bu_vls_strcat(out, "\neye_model");
    for (i = 0; i < 3; i++) {
	snprintf(numbuf, sizeof(numbuf), " %.17g", info->eye_model[i]);
	bu_vls_strcat(out, numbuf);
    }
    snprintf(numbuf, sizeof(numbuf), "\nviewsize %.17g\n", info->viewsize);
    bu_vls_strcat(out, numbuf);
    snprintf(numbuf, sizeof(numbuf), "aspect %.17g\n", info->aspect);
    bu_vls_strcat(out, numbuf);
    snprintf(numbuf, sizeof(numbuf), "perspective %.17g\n", info->perspective);
    bu_vls_strcat(out, numbuf);
}


/**
 * Parse a camera text-chunk string back into an icv_render_info.
 * Returns 1 on success, 0 on parse failure.
 */
static int
camera_str_to_render_info(const char *text, struct icv_render_info *info)
{
    const char *p = text;
    int i;

    /* viewrotscale */
    if (bu_strncmp(p, "viewrotscale", 12) != 0)
	return 0;
    p += 12;
    for (i = 0; i < 16; i++) {
	char *end;
	while (*p == ' ' || *p == '\t') p++;
	info->viewrotscale[i] = strtod(p, &end);
	if (end == p) return 0;
	p = end;
    }
    while (*p == '\n' || *p == '\r') p++;

    /* eye_model */
    if (bu_strncmp(p, "eye_model", 9) != 0)
	return 0;
    p += 9;
    {
	char *end;
	for (i = 0; i < 3; i++) {
	    while (*p == ' ' || *p == '\t') p++;
	    info->eye_model[i] = strtod(p, &end);
	    if (end == p) return 0;
	    p = end;
	}
    }
    while (*p == '\n' || *p == '\r') p++;

    /* viewsize */
    if (bu_strncmp(p, "viewsize", 8) != 0)
	return 0;
    p += 8;
    {
	char *end;
	while (*p == ' ' || *p == '\t') p++;
	info->viewsize = strtod(p, &end);
	if (end == p) return 0;
	p = end;
    }
    while (*p == '\n' || *p == '\r') p++;

    /* aspect */
    if (bu_strncmp(p, "aspect", 6) != 0)
	return 0;
    p += 6;
    {
	char *end;
	while (*p == ' ' || *p == '\t') p++;
	info->aspect = strtod(p, &end);
	if (end == p) return 0;
	p = end;
    }
    while (*p == '\n' || *p == '\r') p++;

    /* perspective */
    if (bu_strncmp(p, "perspective", 11) != 0)
	return 0;
    p += 11;
    {
	char *end;
	while (*p == ' ' || *p == '\t') p++;
	info->perspective = strtod(p, &end);
	if (end == p) return 0;
    }

    return 1;
}


int
png_write(icv_image_t *bif, FILE *fp)
{
    if (UNLIKELY(!bif))
	return BRLCAD_ERROR;
    if (UNLIKELY(!fp))
	return BRLCAD_ERROR;

    static int png_color_type;
    struct bu_vls cam_str = BU_VLS_INIT_ZERO;

    switch (bif->color_space) {
	case ICV_COLOR_SPACE_GRAY:
	    png_color_type = PNG_COLOR_TYPE_GRAY;
	    break;
	default:
	    png_color_type = PNG_COLOR_TYPE_RGB;
    }

    unsigned char *data = icv_data2uchar(bif);

    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (UNLIKELY(png_ptr == NULL)) {
	bu_free(data, "png write uchar data");
	bu_vls_free(&cam_str);
	return BRLCAD_ERROR;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (info_ptr == NULL || setjmp(png_jmpbuf(png_ptr))) {
	png_destroy_read_struct(&png_ptr, info_ptr ? &info_ptr : NULL, NULL);
	bu_log("ERROR: Unable to create png header\n");
	bu_vls_free(&cam_str);
	bu_free(data, "png write uchar data");
	return BRLCAD_ERROR;
    }

    png_init_io(png_ptr, fp);
    png_set_IHDR(png_ptr, info_ptr, (unsigned)bif->width, (unsigned)bif->height, 8, png_color_type,
		 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
		 PNG_FILTER_TYPE_DEFAULT);

    /* Embed render metadata as tEXt chunks when available */
    if (bif->render_info) {
	const struct icv_render_info *ri = bif->render_info;
	png_text text_chunks[3];
	int nchunks = 0;

	memset(text_chunks, 0, sizeof(text_chunks));

	if (ri->db_filename) {
	    text_chunks[nchunks].compression = PNG_TEXT_COMPRESSION_NONE;
	    text_chunks[nchunks].key  = ICV_PNG_KEY_DB;
	    text_chunks[nchunks].text = ri->db_filename;
	    text_chunks[nchunks].text_length = strlen(ri->db_filename);
	    nchunks++;
	}
	if (ri->objects) {
	    text_chunks[nchunks].compression = PNG_TEXT_COMPRESSION_NONE;
	    text_chunks[nchunks].key  = ICV_PNG_KEY_OBJECTS;
	    text_chunks[nchunks].text = ri->objects;
	    text_chunks[nchunks].text_length = strlen(ri->objects);
	    nchunks++;
	}

	render_info_to_camera_str(ri, &cam_str);
	text_chunks[nchunks].compression = PNG_TEXT_COMPRESSION_NONE;
	text_chunks[nchunks].key  = ICV_PNG_KEY_CAMERA;
	text_chunks[nchunks].text = (char *)bu_vls_cstr(&cam_str);
	text_chunks[nchunks].text_length = bu_vls_strlen(&cam_str);
	nchunks++;

	/* cam_str must remain valid until after png_write_info() since some
	 * libpng builds may store a pointer rather than copying the text. */
	png_set_text(png_ptr, info_ptr, text_chunks, nchunks);
    }

    png_write_info(png_ptr, info_ptr);
    bu_vls_free(&cam_str);   /* safe to free after write_info; no-op if never filled */

    for (size_t i = bif->height-1; i > 0; --i) {
	png_write_row(png_ptr, (png_bytep) (data + bif->width*bif->channels*i));
    }
    png_write_row(png_ptr, (png_bytep) (data + 0));
    png_write_end(png_ptr, info_ptr);

    png_destroy_write_struct(&png_ptr, &info_ptr);
    bu_free(data, "png write uchar data");

    return BRLCAD_OK;
}

icv_image_t *
png_read(FILE *fp)
{
    if (UNLIKELY(!fp))
	return NULL;

    char header[8];
    if (fread(header, 8, 1, fp) != 1) {
	bu_log("png-pix: ERROR: Failed while reading file header!!!\n");
	return NULL;
    }

    if (png_sig_cmp((png_bytep)header, 0, 8)) {
	bu_log("png-pix: This is not a PNG file!!!\n");
	return NULL;
    }

    png_structp png_p = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_p) {
	bu_log("png-pix: png_create_read_struct() failed!!\n");
	return NULL;
    }

    png_infop info_p = png_create_info_struct(png_p);
    if (!info_p) {
	bu_log("png-pix: png_create_info_struct() failed!!\n");
	return NULL;
    }

    icv_image_t *bif;
    BU_ALLOC(bif, struct icv_image);
    ICV_IMAGE_INIT(bif);

    png_init_io(png_p, fp);
    png_set_sig_bytes(png_p, 8);
    png_read_info(png_p, info_p);
    int color_type = png_get_color_type(png_p, info_p);
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
	png_set_gray_to_rgb(png_p);
    }
    png_set_expand(png_p);
    int bit_depth = png_get_bit_depth(png_p, info_p);
    if (bit_depth == 16) png_set_strip_16(png_p);

    bif->width = png_get_image_width(png_p, info_p);
    bif->height = png_get_image_height(png_p, info_p);

    /* Read tEXt metadata chunks (must be done after png_read_info) */
    {
	png_textp text_ptr = NULL;
	int num_text = 0;
	if (png_get_text(png_p, info_p, &text_ptr, &num_text) > 0 && num_text > 0) {
	    int i;
	    struct icv_render_info *ri = NULL;

	    for (i = 0; i < num_text; i++) {
		if (!text_ptr[i].key || !text_ptr[i].text)
		    continue;

		if (BU_STR_EQUAL(text_ptr[i].key, ICV_PNG_KEY_DB) ||
		    BU_STR_EQUAL(text_ptr[i].key, ICV_PNG_KEY_OBJECTS) ||
		    BU_STR_EQUAL(text_ptr[i].key, ICV_PNG_KEY_CAMERA))
		{
		    if (!ri)
			ri = icv_render_info_create();
		}

		if (BU_STR_EQUAL(text_ptr[i].key, ICV_PNG_KEY_DB)) {
		    if (ri->db_filename) bu_free(ri->db_filename, "ri db_filename");
		    ri->db_filename = bu_strdup(text_ptr[i].text);
		} else if (BU_STR_EQUAL(text_ptr[i].key, ICV_PNG_KEY_OBJECTS)) {
		    if (ri->objects) bu_free(ri->objects, "ri objects");
		    ri->objects = bu_strdup(text_ptr[i].text);
		} else if (BU_STR_EQUAL(text_ptr[i].key, ICV_PNG_KEY_CAMERA)) {
		    if (!camera_str_to_render_info(text_ptr[i].text, ri)) {
			bu_log("png_read: WARNING: failed to parse BRL-CAD camera metadata\n");
		    }
		}
	    }

	    if (ri)
		bif->render_info = ri;
	}
    }

    png_color_16p input_backgrd;
    if (png_get_bKGD(png_p, info_p, &input_backgrd)) {
	png_set_background(png_p, input_backgrd, PNG_BACKGROUND_GAMMA_FILE, 1, 1.0);
    } else {
	png_color_16 def_backgrd={ 0, 0, 0, 0, 0 };
	png_set_background(png_p, &def_backgrd, PNG_BACKGROUND_GAMMA_FILE, 0, 1.0);
    }

    double gammaval;
    if (png_get_gAMA(png_p, info_p, &gammaval)) {
	png_set_gAMA(png_p, info_p, gammaval);
    }

    png_read_update_info(png_p, info_p);


    /* allocate memory for image */
    unsigned char *image = (unsigned char *)bu_calloc(1, bif->width*bif->height*3, "image");

    /* create rows array */
    unsigned char **rows = (unsigned char **)bu_calloc(bif->height, sizeof(unsigned char *), "rows");
    for (size_t i = 0; i < bif->height; i++)
	rows[bif->height - 1 - i] = image+(i * bif->width * 3);

    png_read_image(png_p, rows);

    bif->data = icv_uchar2double(image, 3 * bif->width * bif->height);
    bu_free(image, "png_read : unsigned char data");
    bif->magic = ICV_IMAGE_MAGIC;
    bif->channels = 3;
    bif->color_space = ICV_COLOR_SPACE_RGB;

    png_destroy_read_struct(&png_p, &info_p, NULL);
    bu_free(rows, "png rows");

    return bif;
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
