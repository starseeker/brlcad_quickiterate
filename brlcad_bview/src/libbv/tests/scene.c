/*                       S C E N E . C
 * BRL-CAD
 *
 * Copyright (c) 2025 United States Government as represented by
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
/**
 * Unit tests for the new bv_node / bv_scene / bview_new API.
 *
 * These tests provide coverage for the EXPERIMENTAL scene graph API so that
 * future migration work can verify no regressions are introduced.
 *
 * Rules for this file:
 *  - Use ONLY the public API (no internal struct field access).
 *  - Store function results in local variables before passing to macros
 *    that cast them, to avoid -Wbad-function-cast.
 *  - Each test_*() returns 1 on success, 0 on failure.
 *  - scene_main() dispatches to the appropriate test via argv[1].
 */

#include "common.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "bu.h"
#include "bn.h"
#include "bv.h"


/* ---- helpers ---- */

static int pass_count = 0;
static int fail_count = 0;

#define CHECK(cond, msg) \
    do { \
	if (cond) { \
	    pass_count++; \
	} else { \
	    fail_count++; \
	    printf("FAIL: %s\n", msg); \
	} \
    } while (0)

/* Returns 1 if two mat_t are approximately equal element-wise */
static int
mats_equal(const mat_t a, const mat_t b)
{
    int i;
    for (i = 0; i < 16; i++) {
	if (fabs(a[i] - b[i]) > 1e-10)
	    return 0;
    }
    return 1;
}

/* Safe BU_PTBL_LEN on a stored pointer (avoids -Wbad-function-cast) */
#define PTBL_LEN_OF(ptbl_ptr) \
    (((ptbl_ptr) != NULL) ? (ptbl_ptr)->end : (size_t)0)


/* ================================================================
 * bv_node lifecycle tests
 * ================================================================ */

static int
test_bv_node_create_destroy(void)
{
    struct bv_node *n;
    const struct bu_ptbl *ch;

    /* NULL name allowed - must not crash */
    n = bv_node_create(NULL, BV_NODE_GEOMETRY);
    CHECK(n != NULL, "bv_node_create(NULL name) returns non-NULL");
    if (n) {
	CHECK(bv_node_type_get(n) == BV_NODE_GEOMETRY,
	      "type correct with NULL name");
	bv_node_destroy(n);
    }

    /* Normal create */
    n = bv_node_create("mynode", BV_NODE_GROUP);
    CHECK(n != NULL, "bv_node_create returns non-NULL");
    if (!n) return 0;

    CHECK(bv_node_type_get(n) == BV_NODE_GROUP,
	  "bv_node_type_get returns correct type");
    CHECK(bu_strcmp(bv_node_name_get(n), "mynode") == 0,
	  "bv_node_name_get returns correct name");
    CHECK(bv_node_visible_get(n) == 1,
	  "new node is visible by default");
    CHECK(bv_node_user_data_get(n) == NULL,
	  "new node has NULL user_data");
    CHECK(bv_node_parent_get(n) == NULL,
	  "new node has NULL parent");

    ch = bv_node_children(n);
    CHECK(ch != NULL, "bv_node_children returns non-NULL");
    if (ch)
	CHECK(PTBL_LEN_OF(ch) == 0, "new node has no children");

    bv_node_destroy(n);
    return 1;
}

static int
test_bv_node_types(void)
{
    struct bv_node *n;
    size_t i;
    enum bv_node_type types[] = {
	BV_NODE_GROUP, BV_NODE_SEPARATOR, BV_NODE_GEOMETRY,
	BV_NODE_TRANSFORM, BV_NODE_CAMERA, BV_NODE_LIGHT,
	BV_NODE_MATERIAL, BV_NODE_OTHER
    };
    const char *names[] = {
	"group", "separator", "geometry",
	"transform", "camera", "light",
	"material", "other"
    };

    for (i = 0; i < sizeof(types)/sizeof(types[0]); i++) {
	n = bv_node_create(names[i], types[i]);
	CHECK(n != NULL, "bv_node_create returns non-NULL");
	if (n) {
	    CHECK(bv_node_type_get(n) == types[i], "type matches");
	    bv_node_destroy(n);
	}
    }
    return 1;
}

static int
test_bv_node_null_safety(void)
{
    const struct bu_ptbl *ch;

    /* All getters/setters must tolerate NULL without crashing */
    bv_node_transform_set(NULL, bn_mat_identity);
    CHECK(bv_node_transform_get(NULL) == NULL,
	  "transform_get(NULL) returns NULL");

    bv_node_geometry_set(NULL, NULL);
    bv_node_material_set(NULL, NULL);
    CHECK(bv_node_material_get(NULL) == NULL,
	  "material_get(NULL) returns NULL");

    bv_node_add_child(NULL, NULL);
    bv_node_remove_child(NULL, NULL);

    ch = bv_node_children(NULL);
    CHECK(ch == NULL, "children(NULL) returns NULL");

    bv_node_visible_set(NULL, 1);
    CHECK(bv_node_visible_get(NULL) == 0, "visible_get(NULL) returns 0");
    CHECK(bv_node_type_get(NULL) == BV_NODE_OTHER,
	  "type_get(NULL) returns BV_NODE_OTHER");
    CHECK(bv_node_name_get(NULL) == NULL,
	  "name_get(NULL) returns NULL");

    bv_node_user_data_set(NULL, NULL);
    CHECK(bv_node_user_data_get(NULL) == NULL,
	  "user_data_get(NULL) returns NULL");
    CHECK(bv_node_world_transform_get(NULL) == NULL,
	  "world_transform_get(NULL) returns NULL");
    CHECK(bv_node_parent_get(NULL) == NULL,
	  "parent_get(NULL) returns NULL");

    bv_node_traverse(NULL, NULL, NULL);
    bv_node_destroy(NULL); /* must not crash */
    return 1;
}


/* ================================================================
 * bv_node transform tests
 * ================================================================ */

static int
test_bv_node_transform(void)
{
    struct bv_node *n = bv_node_create("xform_test", BV_NODE_TRANSFORM);
    const mat_t *t;
    mat_t xlate;
    if (!n) return 0;

    /* Default transform is identity */
    t = bv_node_transform_get(n);
    CHECK(t != NULL, "transform_get returns non-NULL");
    if (t)
	CHECK(mats_equal(*t, bn_mat_identity), "default transform is identity");

    /* Set a translation matrix */
    MAT_IDN(xlate);
    xlate[3]  = 10.0; /* tx */
    xlate[7]  = 20.0; /* ty */
    xlate[11] = 30.0; /* tz */

    bv_node_transform_set(n, xlate);
    t = bv_node_transform_get(n);
    CHECK(t != NULL, "transform_get after set non-NULL");
    if (t)
	CHECK(mats_equal(*t, xlate), "transform round-trips correctly");

    bv_node_destroy(n);
    return 1;
}


/* ================================================================
 * bv_node visibility tests
 * ================================================================ */

static int
test_bv_node_visibility(void)
{
    struct bv_node *n = bv_node_create("vis_test", BV_NODE_GEOMETRY);
    if (!n) return 0;

    CHECK(bv_node_visible_get(n) == 1, "new node visible by default");

    bv_node_visible_set(n, 0);
    CHECK(bv_node_visible_get(n) == 0, "node hidden after visible_set(0)");

    bv_node_visible_set(n, 1);
    CHECK(bv_node_visible_get(n) == 1, "node visible after visible_set(1)");

    bv_node_destroy(n);
    return 1;
}


/* ================================================================
 * bv_node user data tests
 * ================================================================ */

static int
test_bv_node_user_data(void)
{
    int sentinel = 42;
    struct bv_node *n = bv_node_create("udata_test", BV_NODE_OTHER);
    if (!n) return 0;

    CHECK(bv_node_user_data_get(n) == NULL, "user_data NULL initially");

    bv_node_user_data_set(n, &sentinel);
    CHECK(bv_node_user_data_get(n) == &sentinel, "user_data round-trips");

    bv_node_user_data_set(n, NULL);
    CHECK(bv_node_user_data_get(n) == NULL, "user_data can be cleared");

    bv_node_destroy(n);
    return 1;
}


/* ================================================================
 * bv_node hierarchy tests
 * ================================================================ */

static int
test_bv_node_hierarchy(void)
{
    struct bv_node *parent = bv_node_create("parent", BV_NODE_GROUP);
    struct bv_node *child1 = bv_node_create("child1", BV_NODE_GEOMETRY);
    struct bv_node *child2 = bv_node_create("child2", BV_NODE_GEOMETRY);
    const struct bu_ptbl *ch;

    if (!parent || !child1 || !child2) {
	bv_node_destroy(parent);
	bv_node_destroy(child1);
	bv_node_destroy(child2);
	return 0;
    }

    /* Add first child */
    bv_node_add_child(parent, child1);
    ch = bv_node_children(parent);
    CHECK(ch && PTBL_LEN_OF(ch) == 1, "parent has 1 child after first add");
    CHECK(bv_node_parent_get(child1) == parent,
	  "child1 parent is parent after add");

    /* Add second child */
    bv_node_add_child(parent, child2);
    ch = bv_node_children(parent);
    CHECK(ch && PTBL_LEN_OF(ch) == 2, "parent has 2 children after second add");

    /* Duplicate add is a no-op */
    bv_node_add_child(parent, child1);
    ch = bv_node_children(parent);
    CHECK(ch && PTBL_LEN_OF(ch) == 2, "duplicate add is a no-op");

    /* Remove one child */
    bv_node_remove_child(parent, child1);
    ch = bv_node_children(parent);
    CHECK(ch && PTBL_LEN_OF(ch) == 1, "parent has 1 child after remove");
    CHECK(bv_node_parent_get(child1) == NULL,
	  "child1 parent is NULL after remove");

    bv_node_destroy(child1); /* detached - destroy manually */
    bv_node_destroy(parent); /* recursively destroys child2 */
    return 1;
}


/* ================================================================
 * bv_node world transform tests
 * ================================================================ */

static int
test_bv_node_world_transform(void)
{
    struct bv_node *root  = bv_node_create("root",  BV_NODE_SEPARATOR);
    struct bv_node *child = bv_node_create("child", BV_NODE_TRANSFORM);
    mat_t t, expected;
    const mat_t *wt;

    if (!root || !child) {
	bv_node_destroy(root);
	bv_node_destroy(child);
	return 0;
    }

    /* Translate child by (5,0,0) */
    MAT_IDN(t);
    t[3] = 5.0;
    bv_node_transform_set(child, t);
    bv_node_add_child(root, child);

    /* Root world transform == identity */
    wt = bv_node_world_transform_get(root);
    CHECK(wt != NULL, "root world_transform non-NULL");
    if (wt)
	CHECK(mats_equal(*wt, bn_mat_identity), "root world transform is identity");

    /* Child world = parent_world * child_local = I * T(5,0,0) */
    MAT_IDN(expected);
    expected[3] = 5.0;
    wt = bv_node_world_transform_get(child);
    CHECK(wt != NULL, "child world_transform non-NULL");
    if (wt)
	CHECK(mats_equal(*wt, expected), "child world transform is T(5,0,0)");

    bv_node_destroy(root); /* destroys child recursively */
    return 1;
}


/* ================================================================
 * bv_node traversal tests
 * ================================================================ */

struct _traverse_state {
    int count;
    char names[16][64];
};

static void
_traverse_cb(struct bv_node *node, void *data)
{
    struct _traverse_state *s = (struct _traverse_state *)data;
    if (s->count < 16)
	bu_strlcpy(s->names[s->count], bv_node_name_get(node), 64);
    s->count++;
}

static int
test_bv_node_traverse(void)
{
    struct bv_node *root = bv_node_create("root", BV_NODE_SEPARATOR);
    struct bv_node *a    = bv_node_create("a",    BV_NODE_GROUP);
    struct bv_node *b    = bv_node_create("b",    BV_NODE_GEOMETRY);
    struct bv_node *a1   = bv_node_create("a1",   BV_NODE_GEOMETRY);
    struct _traverse_state s;
    s.count = 0;

    if (!root || !a || !b || !a1) {
	bv_node_destroy(root);
	bv_node_destroy(a);
	bv_node_destroy(b);
	bv_node_destroy(a1);
	return 0;
    }

    bv_node_add_child(root, a);
    bv_node_add_child(root, b);
    bv_node_add_child(a, a1);

    /* pre-order depth-first: root, a, a1, b */
    bv_node_traverse(root, _traverse_cb, &s);

    CHECK(s.count == 4, "traverse visits exactly 4 nodes");
    if (s.count >= 4) {
	CHECK(bu_strcmp(s.names[0], "root") == 0, "traverse[0] == root");
	CHECK(bu_strcmp(s.names[1], "a")    == 0, "traverse[1] == a");
	CHECK(bu_strcmp(s.names[2], "a1")   == 0, "traverse[2] == a1");
	CHECK(bu_strcmp(s.names[3], "b")    == 0, "traverse[3] == b");
    }

    bv_node_destroy(root);
    return 1;
}


/* ================================================================
 * bv_scene lifecycle tests
 * ================================================================ */

static int
test_bv_scene_create_destroy(void)
{
    struct bv_scene *scene = bv_scene_create();
    const struct bu_ptbl *nodes;
    struct bv_node *root;

    CHECK(scene != NULL, "bv_scene_create returns non-NULL");
    if (!scene) return 0;

    root = bv_scene_root(scene);
    CHECK(root != NULL, "bv_scene_root non-NULL");
    if (root) {
	CHECK(bv_node_type_get(root) == BV_NODE_SEPARATOR,
	      "scene root is BV_NODE_SEPARATOR");
	CHECK(bu_strcmp(bv_node_name_get(root), "root") == 0,
	      "scene root name is 'root'");
    }

    nodes = bv_scene_nodes(scene);
    CHECK(nodes != NULL, "bv_scene_nodes non-NULL");
    CHECK(bv_scene_default_camera(scene) == NULL,
	  "new scene has no default camera");

    bv_scene_destroy(scene);
    return 1;
}

static int
test_bv_scene_null_safety(void)
{
    CHECK(bv_scene_root(NULL)                == NULL, "root(NULL)");
    CHECK(bv_scene_nodes(NULL)               == NULL, "nodes(NULL)");
    CHECK(bv_scene_default_camera(NULL)      == NULL, "default_camera(NULL)");
    CHECK(bv_scene_find_node(NULL, NULL)     == NULL, "find_node(NULL,NULL)");
    bv_scene_add_node(NULL, NULL);
    bv_scene_remove_node(NULL, NULL);
    bv_scene_add_child(NULL, NULL, NULL);
    bv_scene_remove_child(NULL, NULL, NULL);
    bv_scene_traverse(NULL, NULL, NULL);
    bv_scene_default_camera_set(NULL, NULL); /* must not crash */
    bv_scene_destroy(NULL);                  /* must not crash */
    return 1;
}

static int
test_bv_scene_add_remove(void)
{
    struct bv_scene *scene = bv_scene_create();
    struct bv_node *n1 = bv_node_create("geom1", BV_NODE_GEOMETRY);
    struct bv_node *n2 = bv_node_create("geom2", BV_NODE_GEOMETRY);
    const struct bu_ptbl *nodes;
    size_t initial;

    if (!scene || !n1 || !n2) {
	bv_scene_destroy(scene);
	bv_node_destroy(n1);
	bv_node_destroy(n2);
	return 0;
    }

    nodes   = bv_scene_nodes(scene);
    initial = PTBL_LEN_OF(nodes);

    bv_scene_add_node(scene, n1);
    nodes = bv_scene_nodes(scene);
    CHECK(PTBL_LEN_OF(nodes) == initial + 1, "count +1 after add_node");
    CHECK(bv_node_parent_get(n1) == bv_scene_root(scene),
	  "added node is child of root");

    bv_scene_add_node(scene, n2);
    nodes = bv_scene_nodes(scene);
    CHECK(PTBL_LEN_OF(nodes) == initial + 2, "count +2 after second add");

    bv_scene_remove_node(scene, n1);
    nodes = bv_scene_nodes(scene);
    CHECK(PTBL_LEN_OF(nodes) == initial + 1, "count back to initial+1 after remove");
    CHECK(bv_node_parent_get(n1) == NULL, "removed node parent is NULL");

    bv_node_destroy(n1); /* manually destroyed after removal */
    bv_scene_destroy(scene); /* destroys root (and n2 as child) */
    return 1;
}

static int
test_bv_scene_add_child(void)
{
    struct bv_scene *scene = bv_scene_create();
    struct bv_node *group  = bv_node_create("group",  BV_NODE_GROUP);
    struct bv_node *child  = bv_node_create("gchild", BV_NODE_GEOMETRY);
    const struct bu_ptbl *nodes, *gchildren;
    size_t flat_before;

    if (!scene || !group || !child) {
	bv_scene_destroy(scene);
	bv_node_destroy(group);
	bv_node_destroy(child);
	return 0;
    }

    bv_scene_add_node(scene, group);
    nodes = bv_scene_nodes(scene);
    flat_before = PTBL_LEN_OF(nodes);

    bv_scene_add_child(scene, group, child);
    nodes = bv_scene_nodes(scene);
    CHECK(PTBL_LEN_OF(nodes) == flat_before + 1,
	  "flat list grows by 1 after scene_add_child");
    CHECK(bv_node_parent_get(child) == group,
	  "child parent is group");

    gchildren = bv_node_children(group);
    CHECK(gchildren && PTBL_LEN_OF(gchildren) == 1,
	  "group has 1 child");

    bv_scene_remove_child(scene, group, child);
    nodes = bv_scene_nodes(scene);
    CHECK(PTBL_LEN_OF(nodes) == flat_before,
	  "flat list shrinks by 1 after scene_remove_child");
    CHECK(bv_node_parent_get(child) == NULL, "child parent NULL after remove");

    bv_node_destroy(child);
    bv_scene_destroy(scene); /* destroys group via root */
    return 1;
}

static int
test_bv_scene_find_node(void)
{
    struct bv_scene *scene = bv_scene_create();
    struct bv_node *a = bv_node_create("alpha", BV_NODE_GROUP);
    struct bv_node *b = bv_node_create("beta",  BV_NODE_GEOMETRY);

    if (!scene || !a || !b) {
	bv_scene_destroy(scene);
	bv_node_destroy(a);
	bv_node_destroy(b);
	return 0;
    }

    bv_scene_add_node(scene, a);
    bv_scene_add_child(scene, a, b);

    CHECK(bv_scene_find_node(scene, "root")  != NULL, "can find root");
    CHECK(bv_scene_find_node(scene, "alpha") == a,    "can find 'alpha'");
    CHECK(bv_scene_find_node(scene, "beta")  == b,    "can find nested 'beta'");
    CHECK(bv_scene_find_node(scene, "nope")  == NULL, "NULL for missing name");

    bv_scene_destroy(scene); /* destroys a and b recursively */
    return 1;
}

static int
test_bv_scene_traverse(void)
{
    struct bv_scene *scene = bv_scene_create();
    struct bv_node *n1 = bv_node_create("n1", BV_NODE_GEOMETRY);
    struct bv_node *n2 = bv_node_create("n2", BV_NODE_GEOMETRY);
    struct _traverse_state s;
    s.count = 0;

    if (!scene || !n1 || !n2) {
	bv_scene_destroy(scene);
	bv_node_destroy(n1);
	bv_node_destroy(n2);
	return 0;
    }

    bv_scene_add_node(scene, n1);
    bv_scene_add_node(scene, n2);

    /* root + 2 children = 3 total */
    bv_scene_traverse(scene, _traverse_cb, &s);
    CHECK(s.count == 3, "scene traverse visits 3 nodes (root + 2)");

    bv_scene_destroy(scene);
    return 1;
}

static int
test_bv_scene_default_camera(void)
{
    struct bv_scene *scene = bv_scene_create();
    struct bv_node *cam = bv_node_create("default_cam", BV_NODE_CAMERA);

    if (!scene || !cam) {
	bv_scene_destroy(scene);
	bv_node_destroy(cam);
	return 0;
    }

    CHECK(bv_scene_default_camera(scene) == NULL,
	  "new scene has no default camera");

    bv_scene_default_camera_set(scene, cam);
    CHECK(bv_scene_default_camera(scene) == cam,
	  "default_camera returns set node");

    bv_scene_default_camera_set(scene, NULL);
    CHECK(bv_scene_default_camera(scene) == NULL,
	  "default_camera cleared after set(NULL)");

    bv_node_destroy(cam);
    bv_scene_destroy(scene);
    return 1;
}


/* ================================================================
 * bview_new lifecycle tests
 * ================================================================ */

static int
test_bview_create_destroy(void)
{
    struct bview_new *v;
    const struct bview_camera  *cam;
    const struct bview_viewport *vp;

    v = bview_create("testview");
    CHECK(v != NULL, "bview_create returns non-NULL");
    if (!v) return 0;

    cam = bview_camera_get(v);
    vp  = bview_viewport_get(v);
    CHECK(cam != NULL, "camera_get non-NULL on new view");
    CHECK(vp  != NULL, "viewport_get non-NULL on new view");
    CHECK(bview_scene_get(v)       == NULL, "new view has no scene");
    CHECK(bview_camera_node_get(v) == NULL, "new view has no camera node");
    CHECK(bview_old_get(v)         == NULL, "new view has no legacy bview");

    bview_destroy(v);

    /* NULL name is safe */
    v = bview_create(NULL);
    CHECK(v != NULL, "bview_create(NULL name) non-NULL");
    if (v) bview_destroy(v);

    return 1;
}

static int
test_bview_null_safety(void)
{
    bview_scene_set(NULL, NULL);
    CHECK(bview_scene_get(NULL)        == NULL, "scene_get(NULL)");
    bview_camera_set(NULL, NULL);
    CHECK(bview_camera_get(NULL)       == NULL, "camera_get(NULL)");
    bview_camera_node_set(NULL, NULL);
    CHECK(bview_camera_node_get(NULL)  == NULL, "camera_node_get(NULL)");
    bview_viewport_set(NULL, NULL);
    CHECK(bview_viewport_get(NULL)     == NULL, "viewport_get(NULL)");
    bview_material_set(NULL, NULL);
    CHECK(bview_material_get(NULL)     == NULL, "material_get(NULL)");
    bview_appearance_set(NULL, NULL);
    CHECK(bview_appearance_get(NULL)   == NULL, "appearance_get(NULL)");
    bview_overlay_set(NULL, NULL);
    CHECK(bview_overlay_get(NULL)      == NULL, "overlay_get(NULL)");
    bview_pick_set_set(NULL, NULL);
    CHECK(bview_pick_set_get(NULL)     == NULL, "pick_set_get(NULL)");
    bview_redraw_callback_set(NULL, NULL, NULL);
    bview_lod_update(NULL);
    bview_redraw(NULL);
    bview_from_old(NULL, NULL);
    bview_to_old(NULL, NULL);
    CHECK(bview_old_get(NULL) == NULL, "old_get(NULL)");
    bview_destroy(NULL); /* must not crash */
    return 1;
}


/* ================================================================
 * bview_new camera tests
 * ================================================================ */

static int
test_bview_camera(void)
{
    struct bview_new *v = bview_create("cam_test");
    struct bview_camera cam;
    const struct bview_camera *got;
    if (!v) return 0;

    VSET(cam.position, 1.0, 2.0, 3.0);
    VSET(cam.target,   0.0, 0.0, 0.0);
    VSET(cam.up,       0.0, 0.0, 1.0);
    cam.fov         = 60.0;
    cam.perspective = 1;

    bview_camera_set(v, &cam);
    got = bview_camera_get(v);
    CHECK(got != NULL, "camera_get non-NULL after set");
    if (got) {
	CHECK(VNEAR_EQUAL(got->position, cam.position, 1e-10),
	      "camera position round-trips");
	CHECK(VNEAR_EQUAL(got->target, cam.target, 1e-10),
	      "camera target round-trips");
	CHECK(VNEAR_EQUAL(got->up, cam.up, 1e-10),
	      "camera up round-trips");
	CHECK(fabs(got->fov - cam.fov) < 1e-10, "camera fov round-trips");
	CHECK(got->perspective == 1, "camera perspective round-trips");
    }

    /* NULL camera pointer is safe - previous value unchanged */
    bview_camera_set(v, NULL);
    got = bview_camera_get(v);
    CHECK(got != NULL, "camera_get still non-NULL after set(NULL)");

    bview_destroy(v);
    return 1;
}


/* ================================================================
 * bview_new viewport tests
 * ================================================================ */

static int
test_bview_viewport(void)
{
    struct bview_new *v = bview_create("vp_test");
    struct bview_viewport vp;
    const struct bview_viewport *got;
    if (!v) return 0;

    vp.width  = 1920;
    vp.height = 1080;
    vp.dpi    = 96.0;

    bview_viewport_set(v, &vp);
    got = bview_viewport_get(v);
    CHECK(got != NULL, "viewport_get non-NULL after set");
    if (got) {
	CHECK(got->width  == 1920, "viewport width round-trips");
	CHECK(got->height == 1080, "viewport height round-trips");
	CHECK(fabs(got->dpi - 96.0) < 1e-10, "viewport dpi round-trips");
    }

    bview_destroy(v);
    return 1;
}


/* ================================================================
 * bview_new appearance tests
 * ================================================================ */

static int
test_bview_appearance(void)
{
    struct bview_new *v = bview_create("app_test");
    struct bview_appearance app;
    const struct bview_appearance *got;
    unsigned char rgb_black[3]  = {0, 0, 0};
    unsigned char rgb_grey[3]   = {128, 128, 128};
    unsigned char rgb_red[3]    = {255, 0, 0};
    if (!v) return 0;

    memset(&app, 0, sizeof(app));
    bu_color_from_rgb_chars(&app.bg_color,   rgb_black);
    bu_color_from_rgb_chars(&app.grid_color, rgb_grey);
    bu_color_from_rgb_chars(&app.axes_color, rgb_red);
    app.line_width  = 1.5f;
    app.show_axes   = 1;
    app.show_grid   = 0;
    app.show_origin = 1;

    bview_appearance_set(v, &app);
    got = bview_appearance_get(v);
    CHECK(got != NULL, "appearance_get non-NULL after set");
    if (got) {
	CHECK(fabs(got->line_width - 1.5f) < 1e-5f, "line_width round-trips");
	CHECK(got->show_axes   == 1, "show_axes round-trips");
	CHECK(got->show_grid   == 0, "show_grid round-trips");
	CHECK(got->show_origin == 1, "show_origin round-trips");
    }

    bview_destroy(v);
    return 1;
}


/* ================================================================
 * bview_new overlay tests
 * ================================================================ */

static int
test_bview_overlay(void)
{
    struct bview_new *v = bview_create("overlay_test");
    struct bview_overlay ov;
    const struct bview_overlay *got;
    if (!v) return 0;

    memset(&ov, 0, sizeof(ov));
    ov.show_fps        = 1;
    ov.show_gizmos     = 0;
    ov.show_annotation = 1;
    bu_strlcpy(ov.annotation_text, "hello overlay",
	       sizeof(ov.annotation_text));

    bview_overlay_set(v, &ov);
    got = bview_overlay_get(v);
    CHECK(got != NULL, "overlay_get non-NULL after set");
    if (got) {
	CHECK(got->show_fps == 1,        "show_fps round-trips");
	CHECK(got->show_gizmos == 0,     "show_gizmos round-trips");
	CHECK(got->show_annotation == 1, "show_annotation round-trips");
	CHECK(bu_strcmp(got->annotation_text, "hello overlay") == 0,
	      "annotation_text round-trips");
    }

    bview_destroy(v);
    return 1;
}


/* ================================================================
 * bview_new redraw callback tests
 * ================================================================ */

static void
_test_redraw_cb(struct bview_new *UNUSED(v), void *data)
{
    int *flag = (int *)data;
    if (flag) (*flag)++;
}

static int
test_bview_redraw_callback(void)
{
    int counter = 0;
    struct bview_new *v = bview_create("redraw_test");
    if (!v) return 0;

    /* No callback - safe no-op */
    bview_redraw(v);
    CHECK(counter == 0, "redraw with no callback is safe");

    bview_redraw_callback_set(v, _test_redraw_cb, &counter);
    bview_redraw(v);
    CHECK(counter == 1, "callback called once after redraw");

    bview_redraw(v);
    CHECK(counter == 2, "callback called again on second redraw");

    /* Clear callback */
    bview_redraw_callback_set(v, NULL, NULL);
    bview_redraw(v);
    CHECK(counter == 2, "cleared callback not called on third redraw");

    bview_destroy(v);
    return 1;
}


/* ================================================================
 * bview_new scene association tests
 * ================================================================ */

static int
test_bview_scene_assoc(void)
{
    struct bview_new *v     = bview_create("scene_assoc");
    struct bv_scene  *scene = bv_scene_create();
    if (!v || !scene) {
	bview_destroy(v);
	bv_scene_destroy(scene);
	return 0;
    }

    CHECK(bview_scene_get(v) == NULL, "new view has no scene");

    bview_scene_set(v, scene);
    CHECK(bview_scene_get(v) == scene, "view returns scene after set");

    bview_scene_set(v, NULL);
    CHECK(bview_scene_get(v) == NULL, "scene cleared after set(NULL)");

    bv_scene_destroy(scene);
    bview_destroy(v);
    return 1;
}

static int
test_bview_camera_node(void)
{
    struct bview_new *v   = bview_create("cam_node_test");
    struct bv_node   *cam = bv_node_create("camera", BV_NODE_CAMERA);
    if (!v || !cam) {
	bview_destroy(v);
	bv_node_destroy(cam);
	return 0;
    }

    CHECK(bview_camera_node_get(v) == NULL, "no camera node initially");

    bview_camera_node_set(v, cam);
    CHECK(bview_camera_node_get(v) == cam, "camera node round-trips");

    bview_camera_node_set(v, NULL);
    CHECK(bview_camera_node_get(v) == NULL, "camera node cleared");

    bv_node_destroy(cam);
    bview_destroy(v);
    return 1;
}


/* ================================================================
 * Migration helper tests (bview_from_old / bview_to_old)
 * ================================================================ */

static int
test_bview_migration(void)
{
    struct bview_set  vset;
    struct bview      old;
    struct bview_new *nv;
    const struct bview_camera  *cam;
    const struct bview_viewport *vp;

    /* Initialize legacy view */
    bv_set_init(&vset);
    memset(&old, 0, sizeof(old));
    bv_init(&old, &vset);

    /* Set distinctive values */
    VSET(old.gv_eye_pos, 10.0, 20.0, 30.0);
    old.gv_perspective = 45.0;
    old.gv_width       = 800;
    old.gv_height      = 600;

    /* Migrate to new view */
    nv = bview_create("migrated");
    CHECK(nv != NULL, "bview_create for migration non-NULL");
    if (!nv) {
	bv_free(&old);
	bv_set_free(&vset);
	return 0;
    }

    bview_from_old(nv, &old);

    cam = bview_camera_get(nv);
    vp  = bview_viewport_get(nv);
    CHECK(cam != NULL, "migrated camera non-NULL");
    CHECK(vp  != NULL, "migrated viewport non-NULL");

    if (cam) {
	CHECK(VNEAR_EQUAL(cam->position, old.gv_eye_pos, 1e-10),
	      "migrated camera position matches gv_eye_pos");
	CHECK(cam->perspective == 1, "migrated perspective flag set (fov>0)");
	CHECK(fabs(cam->fov - 45.0) < 1e-10, "migrated fov matches gv_perspective");
    }
    if (vp) {
	CHECK(vp->width  == 800, "migrated viewport width");
	CHECK(vp->height == 600, "migrated viewport height");
    }

    /* Verify appearance settings copied from bview_settings */
    {
	const struct bview_appearance *app = bview_appearance_get(nv);
	CHECK(app != NULL, "migrated appearance non-NULL");
	if (app) {
	    const struct bview_settings *s = old.gv_s ? old.gv_s : &old.gv_ls;
	    CHECK(app->show_grid == s->gv_grid.draw,
		  "migrated show_grid matches gv_grid.draw");
	    CHECK(app->show_axes == s->gv_view_axes.draw,
		  "migrated show_axes matches gv_view_axes.draw");
	}
    }

    CHECK(bview_old_get(nv) == &old, "bview_old_get returns original bview");

    /* bview_to_old: push ortho camera and new viewport back */
    {
	struct bview_camera cam2;
	struct bview_viewport vp2;

	VSET(cam2.position, 1.0, 2.0, 3.0);
	VSET(cam2.target,   0.0, 0.0, 0.0);
	VSET(cam2.up,       0.0, 0.0, 1.0);
	cam2.fov         = 0.0;
	cam2.perspective = 0;
	bview_camera_set(nv, &cam2);

	vp2.width  = 1920;
	vp2.height = 1080;
	vp2.dpi    = 96.0;
	bview_viewport_set(nv, &vp2);

	bview_to_old(nv, &old);
	CHECK(VNEAR_EQUAL(old.gv_eye_pos, cam2.position, 1e-10),
	      "bview_to_old updates gv_eye_pos");
	/* ortho: perspective should be 0.0 (test with tolerance) */
	CHECK(fabs(old.gv_perspective) < 1e-10,
	      "bview_to_old: ortho view gives perspective==0");
	CHECK(old.gv_width  == 1920, "bview_to_old updates gv_width");
	CHECK(old.gv_height == 1080, "bview_to_old updates gv_height");
    }

    bview_destroy(nv);
    bv_free(&old);
    bv_set_free(&vset);
    return 1;
}


/* ================================================================
 * scene_main dispatcher
 * ================================================================ */

struct test_entry {
    const char *name;
    int (*func)(void);
};

static struct test_entry scene_tests[] = {
    { "node_create_destroy",  test_bv_node_create_destroy  },
    { "node_types",           test_bv_node_types           },
    { "node_null_safety",     test_bv_node_null_safety     },
    { "node_transform",       test_bv_node_transform       },
    { "node_visibility",      test_bv_node_visibility      },
    { "node_user_data",       test_bv_node_user_data       },
    { "node_hierarchy",       test_bv_node_hierarchy       },
    { "node_world_transform", test_bv_node_world_transform },
    { "node_traverse",        test_bv_node_traverse        },
    { "scene_create_destroy", test_bv_scene_create_destroy },
    { "scene_null_safety",    test_bv_scene_null_safety    },
    { "scene_add_remove",     test_bv_scene_add_remove     },
    { "scene_add_child",      test_bv_scene_add_child      },
    { "scene_find_node",      test_bv_scene_find_node      },
    { "scene_traverse",       test_bv_scene_traverse       },
    { "scene_default_camera", test_bv_scene_default_camera },
    { "view_create_destroy",  test_bview_create_destroy    },
    { "view_null_safety",     test_bview_null_safety       },
    { "view_camera",          test_bview_camera            },
    { "view_viewport",        test_bview_viewport          },
    { "view_appearance",      test_bview_appearance        },
    { "view_overlay",         test_bview_overlay           },
    { "view_redraw_callback", test_bview_redraw_callback   },
    { "view_scene_assoc",     test_bview_scene_assoc       },
    { "view_camera_node",     test_bview_camera_node       },
    { "view_migration",       test_bview_migration         },
    { NULL, NULL }
};

int
scene_main(int argc, char *argv[])
{
    int i;
    const char *requested = NULL;

    if (argc >= 2)
	requested = argv[1];

    pass_count = 0;
    fail_count = 0;

    for (i = 0; scene_tests[i].name != NULL; i++) {
	if (requested && bu_strcmp(requested, scene_tests[i].name) != 0)
	    continue;

	if (!scene_tests[i].func())
	    fail_count++;
    }

    if (fail_count > 0) {
	printf("scene tests: %d passed, %d FAILED\n", pass_count, fail_count);
	return 1;
    }

    return 0;
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
