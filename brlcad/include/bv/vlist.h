/*                        V L I S T . H
 * BRL-CAD
 *
 * Copyright (c) 2004-2026 United States Government as represented by
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
/** @addtogroup bv_vlist
 *
 * @brief
 * Backward-compatibility bridge.
 *
 * All vlist type definitions, constants, macros and the BSG vlist API
 * now live in bsg/vlist.h.  This header provides backward-compatibility
 * aliases so that code not yet migrated continues to compile.
 *
 * TODO: migrate all callers to bsg/vlist.h then delete this file.
 */
/** @{ */
/** @file bv/vlist.h */

#ifndef BV_VLIST_H
#define BV_VLIST_H

#include "bsg/vlist.h"

/* Compat aliases for callers not yet migrated to bsg/vlist.h */
/* #define covers both struct-tag (BU_LIST_FOR etc.) and plain-type contexts */
#define bv_vlist bsg_vlist

#define BV_VLIST_CHUNK              BSG_VLIST_CHUNK
#define BV_VLIST_NULL               BSG_VLIST_NULL
#define BV_CK_VLIST(_p)             BSG_CK_VLIST(_p)

#define BV_VLIST_LINE_MOVE          BSG_VLIST_LINE_MOVE
#define BV_VLIST_LINE_DRAW          BSG_VLIST_LINE_DRAW
#define BV_VLIST_POLY_START         BSG_VLIST_POLY_START
#define BV_VLIST_POLY_MOVE          BSG_VLIST_POLY_MOVE
#define BV_VLIST_POLY_DRAW          BSG_VLIST_POLY_DRAW
#define BV_VLIST_POLY_END           BSG_VLIST_POLY_END
#define BV_VLIST_POLY_VERTNORM      BSG_VLIST_POLY_VERTNORM
#define BV_VLIST_TRI_START          BSG_VLIST_TRI_START
#define BV_VLIST_TRI_MOVE           BSG_VLIST_TRI_MOVE
#define BV_VLIST_TRI_DRAW           BSG_VLIST_TRI_DRAW
#define BV_VLIST_TRI_END            BSG_VLIST_TRI_END
#define BV_VLIST_TRI_VERTNORM       BSG_VLIST_TRI_VERTNORM
#define BV_VLIST_POINT_DRAW         BSG_VLIST_POINT_DRAW
#define BV_VLIST_POINT_SIZE         BSG_VLIST_POINT_SIZE
#define BV_VLIST_LINE_WIDTH         BSG_VLIST_LINE_WIDTH
#define BV_VLIST_DISPLAY_MAT        BSG_VLIST_DISPLAY_MAT
#define BV_VLIST_MODEL_MAT          BSG_VLIST_MODEL_MAT
#define BV_VLIST_CMD_MAX            BSG_VLIST_CMD_MAX

#define BV_GET_VLIST(_fh, p)                    BSG_GET_VLIST(_fh, p)
#define BV_FREE_VLIST(_fh, hd)                  BSG_FREE_VLIST(_fh, hd)
#define BV_ADD_VLIST(_fh, _dh, pnt, draw)       BSG_ADD_VLIST(_fh, _dh, pnt, draw)
#define BV_VLIST_SET_DISP_MAT(_fh, _dh, _rp)   BSG_VLIST_SET_DISP_MAT(_fh, _dh, _rp)
#define BV_VLIST_SET_MODEL_MAT(_fh, _dh)        BSG_VLIST_SET_MODEL_MAT(_fh, _dh)
#define BV_VLIST_SET_POINT_SIZE(_fh, _dh, _sz)  BSG_VLIST_SET_POINT_SIZE(_fh, _dh, _sz)
#define BV_VLIST_SET_LINE_WIDTH(_fh, _dh, _w)   BSG_VLIST_SET_LINE_WIDTH(_fh, _dh, _w)

/* vlblock — BV-namespaced colored vlist set */
__BEGIN_DECLS

struct bv_vlblock {
    uint32_t magic;
    size_t nused;
    size_t max;
    long *rgb;                      /**< @brief rgb[max] variable size array */
    struct bu_list *head;           /**< @brief head[max] variable size array */
    struct bu_list *free_vlist_hd;  /**< @brief where to get/put free vlists */
};
#define BV_CK_VLBLOCK(_p) BU_CKMAG((_p), BV_VLBLOCK_MAGIC, "bv_vlblock")

/* bv_vlist_* and bv_vlblock_* function declarations */
BV_EXPORT extern size_t bv_vlist_cmd_cnt(bsg_vlist *vlist);
BV_EXPORT extern int bv_vlist_bbox(struct bu_list *vlistp, point_t *bmin, point_t *bmax, size_t *length, int *dispmode);
BV_EXPORT extern void bv_vlist_3string(struct bu_list *vhead, struct bu_list *free_hd, const char *string, const point_t origin, const mat_t rot, double scale);
BV_EXPORT extern void bv_vlist_2string(struct bu_list *vhead, struct bu_list *free_hd, const char *string, double x, double y, double scale, double theta);
BV_EXPORT extern const char *bv_vlist_get_cmd_description(int cmd);
BV_EXPORT extern size_t bv_ck_vlist(const struct bu_list *vhead);
BV_EXPORT extern void bv_vlist_copy(struct bu_list *vlists, struct bu_list *dest, const struct bu_list *src);
BV_EXPORT extern void bv_vlist_export(struct bu_vls *vls, struct bu_list *hp, const char *name);
BV_EXPORT extern void bv_vlist_import(struct bu_list *vlists, struct bu_list *hp, struct bu_vls *namevls, const unsigned char *buf);
BV_EXPORT extern void bv_vlist_cleanup(struct bu_list *hd);
BV_EXPORT extern struct bv_vlblock *bv_vlblock_init(struct bu_list *free_vlist_hd, int max_ent);
BV_EXPORT extern void bv_vlblock_free(struct bv_vlblock *vbp);
BV_EXPORT extern struct bu_list *bv_vlblock_find(struct bv_vlblock *vbp, int r, int g, int b);
BV_EXPORT void bv_vlist_rpp(struct bu_list *vlists, struct bu_list *hd, const point_t minn, const point_t maxx);
BV_EXPORT extern void bv_plot_vlblock(FILE *fp, const struct bv_vlblock *vbp);
BV_EXPORT extern void bv_vlblock_to_objs(struct bu_ptbl *out, const char *name_root, struct bv_vlblock *vbp, struct bview *v, struct bv_scene_obj *f, struct bu_list *vlfree);
BV_EXPORT extern struct bv_scene_obj *bv_vlblock_obj(struct bv_vlblock *vbp, struct bview *v, const char *name);
BV_EXPORT extern void bv_vlist_to_uplot(FILE *fp, const struct bu_list *vhead);

__END_DECLS

#endif  /* BV_VLIST_H */

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
