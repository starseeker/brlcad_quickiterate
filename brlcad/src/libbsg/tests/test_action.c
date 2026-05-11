/*                   T E S T _ A C T I O N . C
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
/** @file libbsg/tests/test_action.c */

#include "common.h"

#include <stdio.h>

#include "bu/app.h"
#include "bu/log.h"
#include "bu/malloc.h"
#include "bv/defines.h"
#include "bv/util.h"
#include "bv/vlist.h"

#include "bsg/action.h"
#include "bsg/identity.h"
#include "bsg/lod_ops.h"
#include "bsg/node.h"
#include "bsg/node_group.h"
#include "bsg/node_shape.h"
#include "bsg/node_transform.h"
#include "bsg/view_scope.h"
#include "bsg/util.h"

#define CHECK(cond, msg) do { if (!(cond)) { bu_log("FAIL [%s:%d] %s\n", __FILE__, __LINE__, (msg)); return 1; } } while (0)

static struct bview *
make_view(const char *name)
{
    struct bview *v;
    BU_ALLOC(v, struct bview);
    bv_init(v, NULL);
    bu_vls_sprintf(&v->gv_name, "%s", name);
    return v;
}

static void
free_view(struct bview *v)
{
    if (!v)
return;
    bv_free(v);
    bu_free(v, "test view");
}

static int
test_transformed_bbox_payload(void)
{
    struct bview *v = make_view("bbox_payload");
    bsg_node *root = bsg_scene_root_create(v);
    bsg_node *xform = bsg_transform_create(v);
    bsg_node *shape = bsg_shape_create(v);
    struct bu_list vhead = BU_LIST_INIT_ZERO;
    struct bu_list vlfree = BU_LIST_INIT_ZERO;
    point_t p0 = VINIT_ZERO;
    point_t p1 = {1.0, 2.0, 3.0};
    vect_t bmin, bmax;
    mat_t tmat;
    struct bsg_bbox_action bbox;

    CHECK(root != NULL, "scene root");
    CHECK(xform != NULL && shape != NULL, "transform and shape");

    BU_LIST_INIT(&vhead);
    BU_LIST_INIT(&vlfree);
    BV_ADD_VLIST(&vlfree, &vhead, p0, BV_VLIST_LINE_MOVE);
    BV_ADD_VLIST(&vlfree, &vhead, p1, BV_VLIST_LINE_DRAW);
    bsg_shape_set_vlist(shape, &vhead);
    BV_FREE_VLIST(&vlfree, &vhead);

    MAT_IDN(tmat);
    MAT_DELTAS(tmat, 10.0, 0.0, 0.0);
    bsg_transform_set_matrix(xform, tmat);

    bsg_group_add_child(root, xform);
    bsg_group_add_child(xform, shape);

    bsg_bbox_action_init(&bbox);
    CHECK(bsg_action_apply(&bbox.base, root) == 1, "bbox action apply");
    CHECK(bsg_bbox_action_result(&bbox, &bmin, &bmax) == 1, "bbox action has result");

    CHECK(NEAR_EQUAL(bmin[X], 10.0, SMALL_FASTF), "transformed payload bmin.x");
    CHECK(NEAR_EQUAL(bmax[X], 11.0, SMALL_FASTF), "transformed payload bmax.x");
    CHECK(NEAR_EQUAL(bmax[Z], 3.0, SMALL_FASTF), "transformed payload bmax.z");

    bsg_scene_root_destroy(root);
    v->gv_draw_root = NULL;
    bsg_node_identity_clear(root);
    free_view(v);
    return 0;
}

static int
test_view_scope_filtering(void)
{
    struct bview *v1 = make_view("scope_owner");
    struct bview *v2 = make_view("scope_other");
    bsg_node *root = bsg_scene_root_create(v1);
    bsg_node *scope = bsg_view_scope_create(v2);
    bsg_node *shape = bsg_shape_create(v1);
    point_t lmin = {2.0, 2.0, 2.0};
    point_t lmax = {3.0, 3.0, 3.0};
    vect_t bmin, bmax;
    struct bsg_bbox_action bbox;

    CHECK(root != NULL, "scene root");
    CHECK(scope != NULL && shape != NULL, "scope and shape");

    bsg_node_bounds_set(shape, lmin, lmax);
    bsg_group_add_child(root, scope);
    bsg_group_add_child(scope, shape);

    bsg_bbox_action_init(&bbox);
    bsg_action_set_view(&bbox.base, v1);
    CHECK(bsg_action_apply(&bbox.base, root) == 1, "bbox apply for mismatched view");
    CHECK(bsg_bbox_action_result(&bbox, &bmin, &bmax) == 0, "view-scope mismatch skips subtree");

    bsg_bbox_action_init(&bbox);
    bsg_action_set_view(&bbox.base, v2);
    CHECK(bsg_action_apply(&bbox.base, root) == 1, "bbox apply for owner view");
    CHECK(bsg_bbox_action_result(&bbox, &bmin, &bmax) == 1, "view-scope owner includes subtree");
    CHECK(NEAR_EQUAL(bmin[X], 2.0, SMALL_FASTF), "view-scope included min x");

    bsg_scene_root_destroy(root);
    v1->gv_draw_root = NULL;
    bsg_node_identity_clear(root);
    free_view(v1);
    free_view(v2);
    return 0;
}

static int
test_lod_selection(void)
{
    struct bview *v = make_view("lod_view");
    bsg_node *root = bsg_scene_root_create(v);
    bsg_node *lod = bsg_lod_node_create(v);
    bsg_node *level0 = bsg_shape_create(v);
    bsg_node *level1 = bsg_shape_create(v);
    struct bsg_lod_view_cursor *cursor = NULL;
    point_t l0min = {0.0, 0.0, 0.0}, l0max = {1.0, 1.0, 1.0};
    point_t l1min = {5.0, 0.0, 0.0}, l1max = {6.0, 1.0, 1.0};
    vect_t bmin, bmax;
    struct bsg_bbox_action bbox;

    CHECK(root && lod && level0 && level1, "lod setup");

    bsg_node_bounds_set(level0, l0min, l0max);
    bsg_node_bounds_set(level1, l1min, l1max);
    bsg_lod_node_attach_level(lod, level0);
    bsg_lod_node_attach_level(lod, level1);
    bsg_group_add_child(root, lod);

    cursor = bsg_lod_node_get_cursor(lod, v);
    CHECK(cursor != NULL, "lod cursor");
    cursor->level = 1;

    bsg_bbox_action_init(&bbox);
    bsg_action_set_view(&bbox.base, v);
    CHECK(bsg_action_apply(&bbox.base, root) == 1, "bbox apply with active lod");
    CHECK(bsg_bbox_action_result(&bbox, &bmin, &bmax) == 1, "lod bbox result");
    CHECK(NEAR_EQUAL(bmin[X], 5.0, SMALL_FASTF), "active lod level selected");

    bsg_bbox_action_init(&bbox);
    bsg_action_set_view(&bbox.base, v);
    bsg_action_set_lod_level(&bbox.base, 0);
    CHECK(bsg_action_apply(&bbox.base, root) == 1, "bbox apply with forced lod");
    CHECK(bsg_bbox_action_result(&bbox, &bmin, &bmax) == 1, "forced lod bbox result");
    CHECK(NEAR_EQUAL(bmin[X], 0.0, SMALL_FASTF), "forced lod level selected");

    bsg_scene_root_destroy(root);
    v->gv_draw_root = NULL;
    bsg_node_identity_clear(root);
    free_view(v);
    return 0;
}

static int
test_search_and_collect(void)
{
    struct bview *v = make_view("search_collect");
    bsg_node *root = bsg_scene_root_create(v);
    bsg_node *shape = bsg_shape_create(v);
    struct bsg_identity id;
    struct bsg_material m;
    struct bsg_search_action search;
    struct bsg_collect_action collect;
    const struct bsg_collect_shape *shapes = NULL;
    size_t shape_cnt = 0;
    point_t bmin = {1.0, 1.0, 1.0}, bmax = {2.0, 2.0, 2.0};

    CHECK(root && shape, "search setup");

    bsg_node_set_name(shape, "needle");
    bsg_node_set_payload_type(shape, BSG_PAYLOAD_VLIST);
    bsg_group_add_child(root, shape);
    bsg_node_bounds_set(shape, bmin, bmax);

    bsg_identity_from_path_str(&id, "/db/needle", BSG_SOURCE_DB_OBJECT);
    bsg_node_identity_set(shape, &id);

    bsg_material_init(&m);
    m.source_kind = BSG_MATERIAL_SOURCE_DB_TABLE;
    bsg_node_material_set(shape, &m);

    bsg_search_action_init(&search);
    bsg_search_action_add_name_criteria(&search, "needle");
    bsg_search_action_add_kind_criteria(&search, BSG_NODE_SHAPE);
    bsg_search_action_add_payload_criteria(&search, BSG_PAYLOAD_VLIST);
    bsg_search_action_add_source_path_criteria(&search, "/db/needle", BSG_SOURCE_DB_OBJECT);
    bsg_search_action_add_material_source_criteria(&search, BSG_MATERIAL_SOURCE_DB_TABLE);
    bsg_search_action_add_parent_criteria(&search, root);

    CHECK(bsg_action_apply(&search.base, root) == 1, "search apply");
    CHECK(bsg_search_action_result_count(&search) == 1, "search matched one node");
    CHECK(bsg_search_action_result_node(&search, 0) == shape, "search result pointer");

    bsg_collect_action_init(&collect);
    CHECK(bsg_action_apply(&collect.base, root) == 1, "collect apply");
    shapes = bsg_collect_action_shapes(&collect, &shape_cnt);
    CHECK(shapes != NULL && shape_cnt == 1, "collect found one renderable shape");
    CHECK(shapes[0].node == shape, "collect returned expected node");
    CHECK(NEAR_EQUAL(shapes[0].bmin[X], 1.0, SMALL_FASTF), "collect bounds copied");

    bsg_collect_action_reset(&collect);
    bsg_search_action_reset(&search);
    bsg_node_identity_clear(shape);
    bsg_scene_root_destroy(root);
    v->gv_draw_root = NULL;
    bsg_node_identity_clear(root);
    free_view(v);
    return 0;
}

int
main(int UNUSED(argc), const char **argv)
{
    int failures = 0;

    bu_setprogname(argv[0]);

    failures += test_transformed_bbox_payload();
    failures += test_view_scope_filtering();
    failures += test_lod_selection();
    failures += test_search_and_collect();

    if (!failures)
printf("RESULT: all action tests PASSED\n");
    else
printf("RESULT: %d action test(s) FAILED\n", failures);

    return failures;
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
