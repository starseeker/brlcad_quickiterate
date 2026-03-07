/*                    B S G / V L I S T . H
 * BRL-CAD
 *
 * Copyright (c) 2004-2025 United States Government as represented by
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
/** @file bsg/vlist.h
 *
 * @brief BSG vector-list (polyline segment) types and operations.
 *
 * The canonical BSG type name is @c bsg_vlist (typedef alias of
 * @c bv_vlist defined in @c bsg/defines.h).
 */

#ifndef BSG_VLIST_H
#define BSG_VLIST_H

#include "bsg/defines.h"

__BEGIN_DECLS

BSG_EXPORT extern size_t bsg_vlist_cmd_cnt(struct bv_vlist *vlist);
BSG_EXPORT extern int bsg_vlist_bbox(struct bu_list *vlistp, point_t *bmin, point_t *bmax, size_t *length, int *dispmode);
BSG_EXPORT extern const char *bsg_vlist_get_cmd_description(int cmd);
BSG_EXPORT extern size_t bsg_ck_vlist(const struct bu_list *vhead);
BSG_EXPORT extern void bsg_vlist_copy(struct bu_list *vlists, struct bu_list *dest, const struct bu_list *src);
BSG_EXPORT extern void bsg_vlist_export(struct bu_vls *vls, struct bu_list *hp, const char *name);
BSG_EXPORT extern void bsg_vlist_import(struct bu_list *vlists, struct bu_list *hp, struct bu_vls *namevls, const unsigned char *buf);
BSG_EXPORT extern void bsg_vlist_cleanup(struct bu_list *hd);
BSG_EXPORT extern struct bv_vlblock *bsg_vlblock_init(struct bu_list *free_vlist_hd, int max_ent);
BSG_EXPORT extern void bsg_vlblock_free(struct bv_vlblock *vbp);
BSG_EXPORT extern struct bu_list *bsg_vlblock_find(struct bv_vlblock *vbp, int r, int g, int b);
BSG_EXPORT extern void bsg_vlist_rpp(struct bu_list *vlists, struct bu_list *hd, const point_t minn, const point_t maxx);
BSG_EXPORT extern void bsg_plot_vlblock(FILE *fp, const struct bv_vlblock *vbp);
BSG_EXPORT extern void bsg_vlblock_to_objs(struct bu_ptbl *out, const char *name_root, struct bv_vlblock *vbp, struct bview *v, struct bv_scene_obj *f, struct bu_list *vlfree);
BSG_EXPORT extern struct bv_scene_obj *bsg_vlblock_obj(struct bv_vlblock *vbp, struct bview *v, const char *name);
BSG_EXPORT extern void bsg_vlist_to_uplot(FILE *fp, const struct bu_list *vhead);
BSG_EXPORT extern void bsg_vlist_3string(struct bu_list *vhead, struct bu_list *free_hd, const char *string, const point_t origin, const mat_t rot, double scale);
BSG_EXPORT extern void bsg_vlist_2string(struct bu_list *vhead, struct bu_list *free_hd, const char *string, double x, double y, double scale, double theta);

__END_DECLS

#endif /* BSG_VLIST_H */

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
