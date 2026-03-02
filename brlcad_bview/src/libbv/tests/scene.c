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
 * Phase 1 – bview_settings_apply tests
 * ================================================================ */

static int
test_bview_settings_apply(void)
{
    struct bview_new *v;
    const struct bview_camera  *cam;
    const struct bview_viewport *vp;
    const struct bview_appearance *app;
    const struct bview_overlay *ov;

    v = bview_create("settings_apply_test");
    CHECK(v != NULL, "bview_create non-NULL");
    if (!v) return 0;

    bview_settings_apply(v);

    /* Camera defaults */
    cam = bview_camera_get(v);
    CHECK(cam != NULL, "camera non-NULL after settings_apply");
    if (cam) {
	vect_t eye_default = {0.0, 0.0, 1.0};
	vect_t tgt_default = {0.0, 0.0, 0.0};
	CHECK(VNEAR_EQUAL(cam->position, eye_default, 1e-10),
	      "settings_apply: camera.position == (0,0,1)");
	CHECK(VNEAR_EQUAL(cam->target, tgt_default, 1e-10),
	      "settings_apply: camera.target == (0,0,0)");
	CHECK(cam->perspective == 0, "settings_apply: orthographic by default");
	CHECK(fabs(cam->fov) < 1e-10, "settings_apply: fov == 0");
    }

    /* Viewport defaults */
    vp = bview_viewport_get(v);
    CHECK(vp != NULL, "viewport non-NULL after settings_apply");
    if (vp) {
	CHECK(vp->width  == 0, "settings_apply: viewport width == 0");
	CHECK(vp->height == 0, "settings_apply: viewport height == 0");
	CHECK(fabs(vp->dpi - 96.0) < 1e-5, "settings_apply: dpi == 96.0");
    }

    /* Appearance defaults */
    app = bview_appearance_get(v);
    CHECK(app != NULL, "appearance non-NULL after settings_apply");
    if (app) {
	CHECK(app->show_grid   == 0, "settings_apply: show_grid == 0");
	CHECK(app->show_axes   == 0, "settings_apply: show_axes == 0");
	CHECK(app->show_origin == 0, "settings_apply: show_origin == 0");
	CHECK(fabs(app->line_width) < 1e-5, "settings_apply: line_width == 0");
    }

    /* Overlay defaults */
    ov = bview_overlay_get(v);
    CHECK(ov != NULL, "overlay non-NULL after settings_apply");
    if (ov) {
	CHECK(ov->show_fps        == 0, "settings_apply: show_fps == 0");
	CHECK(ov->show_gizmos     == 0, "settings_apply: show_gizmos == 0");
	CHECK(ov->show_annotation == 0, "settings_apply: show_annotation == 0");
    }

    bview_destroy(v);
    return 1;
}

static int
test_bview_settings_apply_null(void)
{
    /* Must not crash */
    bview_settings_apply(NULL);
    CHECK(1, "settings_apply(NULL) does not crash");
    return 1;
}

static int
test_bview_settings_apply_idempotent(void)
{
    struct bview_new *v = bview_create("idempotent_test");
    const struct bview_camera *cam1, *cam2;
    struct bview_camera snap1, snap2;
    CHECK(v != NULL, "bview_create non-NULL");
    if (!v) return 0;

    /* Calling settings_apply twice should yield the same result */
    bview_settings_apply(v);
    cam1 = bview_camera_get(v);
    if (cam1) snap1 = *cam1;

    bview_settings_apply(v);
    cam2 = bview_camera_get(v);
    if (cam2) snap2 = *cam2;

    if (cam1 && cam2) {
	CHECK(VNEAR_EQUAL(snap1.position, snap2.position, 1e-10),
	      "settings_apply idempotent: position unchanged");
	CHECK(snap1.perspective == snap2.perspective,
	      "settings_apply idempotent: perspective unchanged");
    }

    bview_destroy(v);
    return 1;
}


/* ================================================================
 * Phase 2 – bv_scene_obj_to_node tests
 *
 * We cannot easily create a fully-populated bv_scene_obj from C without
 * calling into higher-level libged infrastructure, so these tests work
 * with a bview that has been initialized via bv_init / bv_obj_get and
 * verify the structural properties of the resulting bv_node.
 * ================================================================ */

static int
test_bv_scene_obj_to_node_null(void)
{
    struct bv_node *n = bv_scene_obj_to_node(NULL);
    CHECK(n == NULL, "bv_scene_obj_to_node(NULL) returns NULL");
    return 1;
}

static int
test_bv_scene_obj_to_node_basic(void)
{
    struct bview_set  vset;
    struct bview      v;
    struct bv_scene_obj *s;
    struct bv_node      *n;
    const struct bu_ptbl *ch;

    /* Set up a minimal legacy view with one leaf scene object */
    bv_set_init(&vset);
    memset(&v, 0, sizeof(v));
    bv_init(&v, &vset);

    /* Create a view-only object (leaf, no children) */
    s = bv_obj_get(&v, BV_VIEWONLY);
    CHECK(s != NULL, "bv_obj_get returns non-NULL");
    if (!s) {
	bv_free(&v);
	bv_set_free(&vset);
	return 0;
    }

    /* Set a distinctive name and color */
    bu_vls_trunc(&s->s_name, 0);
    bu_vls_strcpy(&s->s_name, "test_solid");
    s->s_color[0] = 200;
    s->s_color[1] = 100;
    s->s_color[2] = 50;

    /* Convert to node */
    n = bv_scene_obj_to_node(s);
    CHECK(n != NULL, "bv_scene_obj_to_node returns non-NULL");
    if (!n) {
	bv_obj_put(s);
	bv_free(&v);
	bv_set_free(&vset);
	return 0;
    }

    /* Node type: leaf → BV_NODE_GEOMETRY */
    CHECK(bv_node_type_get(n) == BV_NODE_GEOMETRY,
	  "leaf obj → BV_NODE_GEOMETRY");

    /* Node name matches s_name */
    CHECK(bu_strcmp(bv_node_name_get(n), "test_solid") == 0,
	  "node name matches s_name");

    /* user_data == original bv_scene_obj */
    CHECK(bv_node_user_data_get(n) == s,
	  "user_data points to original bv_scene_obj");

    /* No children */
    ch = bv_node_children(n);
    CHECK(ch && PTBL_LEN_OF(ch) == 0, "leaf node has no children");

    bv_node_destroy(n);
    bv_obj_put(s);
    bv_free(&v);
    bv_set_free(&vset);
    return 1;
}

static int
test_bv_scene_obj_to_node_with_children(void)
{
    struct bview_set   vset;
    struct bview       v;
    struct bv_scene_obj *parent_s;
    struct bv_scene_obj *child_s;
    struct bv_node      *parent_n;
    const struct bu_ptbl *ch;

    /* Set up a minimal legacy view */
    bv_set_init(&vset);
    memset(&v, 0, sizeof(v));
    bv_init(&v, &vset);

    /* Create parent scene object */
    parent_s = bv_obj_get(&v, BV_VIEWONLY);
    CHECK(parent_s != NULL, "parent bv_obj_get non-NULL");
    if (!parent_s) {
	bv_free(&v);
	bv_set_free(&vset);
	return 0;
    }

    bu_vls_trunc(&parent_s->s_name, 0);
    bu_vls_strcpy(&parent_s->s_name, "group_node");

    /* Create a child scene object under parent */
    child_s = bv_obj_get_child(parent_s);
    CHECK(child_s != NULL, "child bv_obj_get_child non-NULL");
    if (!child_s) {
	bv_obj_put(parent_s);
	bv_free(&v);
	bv_set_free(&vset);
	return 0;
    }

    bu_vls_trunc(&child_s->s_name, 0);
    bu_vls_strcpy(&child_s->s_name, "child_solid");

    /* Now wrap the parent - should recursively wrap the child */
    parent_n = bv_scene_obj_to_node(parent_s);
    CHECK(parent_n != NULL, "parent bv_scene_obj_to_node non-NULL");
    if (!parent_n) {
	bv_obj_put(parent_s);
	bv_free(&v);
	bv_set_free(&vset);
	return 0;
    }

    /* Parent has children → BV_NODE_GROUP */
    CHECK(bv_node_type_get(parent_n) == BV_NODE_GROUP,
	  "parent with children → BV_NODE_GROUP");

    /* Should have exactly 1 child node */
    ch = bv_node_children(parent_n);
    CHECK(ch && PTBL_LEN_OF(ch) == 1,
	  "parent node has exactly 1 child");

    bv_node_destroy(parent_n); /* recursively destroys child node */
    bv_obj_put(parent_s);
    bv_free(&v);
    bv_set_free(&vset);
    return 1;
}


/* ================================================================
 * Phase 2 – bv_scene_from_view tests
 * ================================================================ */

static int
test_bv_scene_from_view_null(void)
{
    struct bv_scene *scene = bv_scene_from_view(NULL);
    CHECK(scene == NULL, "bv_scene_from_view(NULL) returns NULL");
    return 1;
}

static int
test_bv_scene_from_view_empty(void)
{
    struct bview_set  vset;
    struct bview      v;
    struct bv_scene  *scene;
    const struct bu_ptbl *nodes;

    bv_set_init(&vset);
    memset(&v, 0, sizeof(v));
    bv_init(&v, &vset);

    /* Empty view → scene with only root node */
    scene = bv_scene_from_view(&v);
    CHECK(scene != NULL, "bv_scene_from_view(empty view) non-NULL");
    if (scene) {
	nodes = bv_scene_nodes(scene);
	/* Empty view: no scene objects → flat table is empty */
	CHECK(nodes != NULL, "scene nodes table non-NULL");
	if (nodes)
	    CHECK(PTBL_LEN_OF(nodes) == 0,
		  "empty view → no nodes in flat table");
	bv_scene_destroy(scene);
    }

    bv_free(&v);
    bv_set_free(&vset);
    return 1;
}

static int
test_bv_scene_from_view_with_objects(void)
{
    struct bview_set  vset;
    struct bview      v;
    struct bv_scene_obj *s1, *s2;
    struct bv_scene  *scene;
    const struct bu_ptbl *nodes;
    size_t flat_count;

    bv_set_init(&vset);
    memset(&v, 0, sizeof(v));
    bv_init(&v, &vset);

    /* Add two view objects */
    s1 = bv_obj_get(&v, BV_VIEWONLY);
    s2 = bv_obj_get(&v, BV_VIEWONLY);
    CHECK(s1 != NULL && s2 != NULL, "both bv_obj_get calls succeed");
    if (!s1 || !s2) {
	bv_free(&v);
	bv_set_free(&vset);
	return 0;
    }

    bu_vls_trunc(&s1->s_name, 0);
    bu_vls_strcpy(&s1->s_name, "obj1");
    bu_vls_trunc(&s2->s_name, 0);
    bu_vls_strcpy(&s2->s_name, "obj2");

    scene = bv_scene_from_view(&v);
    CHECK(scene != NULL, "bv_scene_from_view non-NULL with objects");
    if (!scene) {
	bv_obj_put(s1);
	bv_obj_put(s2);
	bv_free(&v);
	bv_set_free(&vset);
	return 0;
    }

    /* Should have 2 nodes in the flat table */
    nodes = bv_scene_nodes(scene);
    flat_count = nodes ? PTBL_LEN_OF(nodes) : 0;
    CHECK(flat_count == 2, "scene from 2-obj view has 2 nodes");

    /* Can find by name */
    CHECK(bv_scene_find_node(scene, "obj1") != NULL,
	  "can find 'obj1' in converted scene");
    CHECK(bv_scene_find_node(scene, "obj2") != NULL,
	  "can find 'obj2' in converted scene");

    /* user_data points back to original bv_scene_obj */
    {
	struct bv_node *n1 = bv_scene_find_node(scene, "obj1");
	if (n1)
	    CHECK(bv_node_user_data_get(n1) == s1,
		  "obj1 node user_data == original s1");
    }

    bv_scene_destroy(scene); /* destroys wrapper nodes; s1/s2 still owned by view */
    bv_obj_put(s1);
    bv_obj_put(s2);
    bv_free(&v);
    bv_set_free(&vset);
    return 1;
}


/* ================================================================
 * Phase 3 – bv_scene_from_view_set tests
 * ================================================================ */

static int
test_bv_scene_from_view_set_null(void)
{
    struct bv_scene *scene = bv_scene_from_view_set(NULL);
    CHECK(scene == NULL, "bv_scene_from_view_set(NULL) returns NULL");
    return 1;
}

static int
test_bv_scene_from_view_set_empty(void)
{
    struct bview_set  vset;
    struct bv_scene  *scene;
    const struct bu_ptbl *nodes;

    bv_set_init(&vset);

    /* Empty set: no shared objects */
    scene = bv_scene_from_view_set(&vset);
    CHECK(scene != NULL, "bv_scene_from_view_set(empty) non-NULL");
    if (scene) {
	nodes = bv_scene_nodes(scene);
	CHECK(nodes != NULL, "nodes table non-NULL");
	if (nodes)
	    CHECK(PTBL_LEN_OF(nodes) == 0,
		  "empty view set → 0 nodes in flat table");
	bv_scene_destroy(scene);
    }

    bv_set_free(&vset);
    return 1;
}


/* ================================================================
 * Phase 1 (continued) – bview_to_old round-trip tests
 * ================================================================ */

static int
test_bview_to_old_appearance(void)
{
    struct bview_set  vset;
    struct bview      old;
    struct bview_new *nv;
    struct bview_appearance app;
    unsigned char expected_color[3] = {10, 20, 30};

    bv_set_init(&vset);
    memset(&old, 0, sizeof(old));
    bv_init(&old, &vset);

    nv = bview_create("to_old_appearance_test");
    CHECK(nv != NULL, "bview_create non-NULL");
    if (!nv) {
	bv_free(&old);
	bv_set_free(&vset);
	return 0;
    }

    /* Set up appearance in the new view */
    bview_from_old(nv, &old);
    {
	const struct bview_appearance *a = bview_appearance_get(nv);
	if (a) app = *a;
    }

    /* Modify: enable grid with a specific color */
    app.show_grid = 1;
    bu_color_from_rgb_chars(&app.grid_color, expected_color);
    app.show_axes   = 1;
    app.show_origin = 1;
    app.line_width  = 3.0f;
    bview_appearance_set(nv, &app);

    /* Push back */
    bview_to_old(nv, &old);

    {
	struct bview_settings *s = old.gv_s ? old.gv_s : &old.gv_ls;
	unsigned char got[3];
	CHECK(s->gv_grid.draw == 1,    "to_old: gv_grid.draw == 1");
	CHECK(s->gv_view_axes.draw == 1, "to_old: gv_view_axes.draw == 1");
	CHECK(s->gv_model_axes.draw == 1, "to_old: gv_model_axes.draw == 1");
	CHECK(s->gv_view_axes.line_width == 3, "to_old: line_width == 3");
	got[0] = (unsigned char)s->gv_grid.color[0];
	got[1] = (unsigned char)s->gv_grid.color[1];
	got[2] = (unsigned char)s->gv_grid.color[2];
	CHECK(got[0] == expected_color[0] &&
	      got[1] == expected_color[1] &&
	      got[2] == expected_color[2],
	      "to_old: grid color round-trip");
    }

    bview_destroy(nv);
    bv_free(&old);
    bv_set_free(&vset);
    return 1;
}

static int
test_bview_to_old_overlay(void)
{
    struct bview_set  vset;
    struct bview      old;
    struct bview_new *nv;
    struct bview_overlay ov;

    bv_set_init(&vset);
    memset(&old, 0, sizeof(old));
    bv_init(&old, &vset);

    nv = bview_create("to_old_overlay_test");
    CHECK(nv != NULL, "bview_create non-NULL");
    if (!nv) {
	bv_free(&old);
	bv_set_free(&vset);
	return 0;
    }

    bview_from_old(nv, &old);

    {
	const struct bview_overlay *o = bview_overlay_get(nv);
	if (o) ov = *o;
    }
    ov.show_fps = 1;
    bview_overlay_set(nv, &ov);

    /* Push back and verify */
    bview_to_old(nv, &old);

    {
	struct bview_settings *s = old.gv_s ? old.gv_s : &old.gv_ls;
	CHECK(s->gv_view_params.draw_fps == 1, "to_old: draw_fps == 1");
    }

    bview_destroy(nv);
    bv_free(&old);
    bv_set_free(&vset);
    return 1;
}

static int
test_bview_to_old_null(void)
{
    struct bview_set vset;
    struct bview     old;
    struct bview_new *nv;

    bv_set_init(&vset);
    memset(&old, 0, sizeof(old));
    bv_init(&old, &vset);

    nv = bview_create("to_old_null_test");

    /* NULL safety */
    bview_to_old(NULL, &old);
    bview_to_old(nv, NULL);
    bview_to_old(NULL, NULL);
    CHECK(1, "bview_to_old null safety OK");

    bview_destroy(nv);
    bv_free(&old);
    bv_set_free(&vset);
    return 1;
}


/* ================================================================
 * Phase 2 (continued) – bv_node_bbox / bv_scene_bbox tests
 * ================================================================ */

static int
test_bv_node_bbox_null(void)
{
    point_t bmin = VINIT_ZERO, bmax = VINIT_ZERO;
    int r;

    r = bv_node_bbox(NULL, &bmin, &bmax);
    CHECK(r == 0, "bv_node_bbox(NULL, ...) returns 0");

    {
	struct bv_node *n = bv_node_create("bbox_null_test", BV_NODE_GEOMETRY);
	r = bv_node_bbox(n, NULL, &bmax);
	CHECK(r == 0, "bv_node_bbox(..., NULL, ...) returns 0");
	r = bv_node_bbox(n, &bmin, NULL);
	CHECK(r == 0, "bv_node_bbox(..., ..., NULL) returns 0");
	bv_node_destroy(n);
    }
    return 1;
}

static int
test_bv_node_bbox_no_geom(void)
{
    struct bv_node *n;
    point_t bmin = VINIT_ZERO, bmax = VINIT_ZERO;
    int r;

    /* A geometry node with no user_data (no legacy bv_scene_obj) */
    n = bv_node_create("bbox_no_geom", BV_NODE_GEOMETRY);
    CHECK(n != NULL, "bv_node_create non-NULL");
    if (!n) return 0;

    r = bv_node_bbox(n, &bmin, &bmax);
    CHECK(r == 0, "node without bv_scene_obj user_data → returns 0");

    bv_node_destroy(n);
    return 1;
}

static int
test_bv_scene_bbox_null(void)
{
    point_t bmin = VINIT_ZERO, bmax = VINIT_ZERO;
    int r;

    r = bv_scene_bbox(NULL, &bmin, &bmax);
    CHECK(r == 0, "bv_scene_bbox(NULL, ...) returns 0");

    {
	struct bv_scene *scene = bv_scene_create();
	r = bv_scene_bbox(scene, NULL, &bmax);
	CHECK(r == 0, "bv_scene_bbox(..., NULL, ...) returns 0");
	r = bv_scene_bbox(scene, &bmin, NULL);
	CHECK(r == 0, "bv_scene_bbox(..., ..., NULL) returns 0");
	bv_scene_destroy(scene);
    }
    return 1;
}

static int
test_bv_scene_bbox_empty(void)
{
    struct bv_scene *scene;
    point_t bmin = VINIT_ZERO, bmax = VINIT_ZERO;
    int r;

    scene = bv_scene_create();
    CHECK(scene != NULL, "scene non-NULL");
    if (!scene) return 0;

    /* No geometry nodes → empty bbox */
    r = bv_scene_bbox(scene, &bmin, &bmax);
    CHECK(r == 0, "empty scene → bv_scene_bbox returns 0");

    bv_scene_destroy(scene);
    return 1;
}

static int
test_bv_scene_bbox_invisible(void)
{
    struct bview_set  vset;
    struct bview      v;
    struct bv_scene_obj *s;
    struct bv_scene   *scene;
    struct bv_node    *n;
    point_t bmin = VINIT_ZERO, bmax = VINIT_ZERO;
    int r;

    /* Create a scene object with non-zero bounds, wrap it, hide it */
    bv_set_init(&vset);
    memset(&v, 0, sizeof(v));
    bv_init(&v, &vset);

    s = bv_obj_get(&v, BV_VIEWONLY);
    CHECK(s != NULL, "bv_obj_get non-NULL");
    if (!s) {
	bv_free(&v);
	bv_set_free(&vset);
	return 0;
    }

    /* Manually set bounding sphere data (normally set by bv_scene_obj_bound) */
    VSET(s->s_center, 10.0, 0.0, 0.0);
    s->s_size = 5.0;
    s->s_flag = DOWN;  /* invisible */

    n = bv_scene_obj_to_node(s);
    CHECK(n != NULL, "bv_scene_obj_to_node non-NULL");
    if (!n) {
	bv_obj_put(s);
	bv_free(&v);
	bv_set_free(&vset);
	return 0;
    }

    scene = bv_scene_create();
    bv_scene_add_node(scene, n);

    r = bv_scene_bbox(scene, &bmin, &bmax);
    CHECK(r == 0, "hidden node → bv_scene_bbox returns 0");

    bv_scene_destroy(scene);
    bv_obj_put(s);
    bv_free(&v);
    bv_set_free(&vset);
    return 1;
}

static int
test_bv_scene_bbox_visible(void)
{
    struct bview_set   vset;
    struct bview       v;
    struct bv_scene_obj *s1, *s2;
    struct bv_node     *n1, *n2;
    struct bv_scene    *scene;
    point_t bmin, bmax;
    int r;

    bv_set_init(&vset);
    memset(&v, 0, sizeof(v));
    bv_init(&v, &vset);

    s1 = bv_obj_get(&v, BV_VIEWONLY);
    s2 = bv_obj_get(&v, BV_VIEWONLY);
    CHECK(s1 && s2, "both bv_obj_get calls succeed");
    if (!s1 || !s2) {
	bv_free(&v);
	bv_set_free(&vset);
	return 0;
    }

    /* s1: sphere centered at (10, 0, 0) with radius 5 */
    VSET(s1->s_center, 10.0, 0.0, 0.0);
    s1->s_size  = 5.0;
    s1->s_flag  = UP;  /* visible */

    /* s2: sphere centered at (-10, 0, 0) with radius 3 */
    VSET(s2->s_center, -10.0, 0.0, 0.0);
    s2->s_size  = 3.0;
    s2->s_flag  = UP;  /* visible */

    n1 = bv_scene_obj_to_node(s1);
    n2 = bv_scene_obj_to_node(s2);
    CHECK(n1 && n2, "both bv_scene_obj_to_node calls succeed");
    if (!n1 || !n2) {
	if (n1) bv_node_destroy(n1);
	if (n2) bv_node_destroy(n2);
	bv_obj_put(s1);
	bv_obj_put(s2);
	bv_free(&v);
	bv_set_free(&vset);
	return 0;
    }

    scene = bv_scene_create();
    bv_scene_add_node(scene, n1);
    bv_scene_add_node(scene, n2);

    r = bv_scene_bbox(scene, &bmin, &bmax);
    CHECK(r == 1, "visible nodes → bv_scene_bbox returns 1");

    if (r) {
	/* Expected AABB:
	 *   s1 center=(10,0,0) radius=5  → [5..15, -5..5, -5..5]
	 *   s2 center=(-10,0,0) radius=3 → [-13..-7, -3..3, -3..3]
	 *   union: x in [-13, 15], y in [-5, 5], z in [-5, 5]
	 */
	CHECK(fabs(bmin[X] - (-13.0)) < 1e-9,
	      "bbox: bmin.x == -13.0");
	CHECK(fabs(bmax[X] - 15.0) < 1e-9,
	      "bbox: bmax.x == 15.0");
	CHECK(fabs(bmin[Y] - (-5.0)) < 1e-9,
	      "bbox: bmin.y == -5.0");
	CHECK(fabs(bmax[Y] - 5.0) < 1e-9,
	      "bbox: bmax.y == 5.0");
    }

    bv_scene_destroy(scene);
    bv_obj_put(s1);
    bv_obj_put(s2);
    bv_free(&v);
    bv_set_free(&vset);
    return 1;
}


/* ================================================================
 * Phase 2 (continued) – bview_autoview_new tests
 * ================================================================ */

static int
test_bview_autoview_null(void)
{
    struct bview_new *view = bview_create("av_null_test");
    struct bv_scene  *scene = bv_scene_create();
    int r;

    r = bview_autoview_new(NULL, scene, -1);
    CHECK(r == 0, "bview_autoview_new(NULL, scene) returns 0");

    r = bview_autoview_new(view, NULL, -1);
    CHECK(r == 0, "bview_autoview_new(view, NULL) returns 0");

    bv_scene_destroy(scene);
    bview_destroy(view);
    return 1;
}

static int
test_bview_autoview_empty(void)
{
    struct bview_new *view = bview_create("av_empty_test");
    struct bv_scene  *scene = bv_scene_create();
    int r;

    bview_settings_apply(view);

    /* Empty scene: camera should be unchanged, return 0 */
    r = bview_autoview_new(view, scene, -1);
    CHECK(r == 0, "bview_autoview_new on empty scene returns 0");

    bv_scene_destroy(scene);
    bview_destroy(view);
    return 1;
}

static int
test_bview_autoview_single_obj(void)
{
    struct bview_set   vset;
    struct bview       v;
    struct bv_scene_obj *s;
    struct bv_node     *n;
    struct bv_scene    *scene;
    struct bview_new   *view;
    const struct bview_camera *cam;
    vect_t to_center;
    int r;

    bv_set_init(&vset);
    memset(&v, 0, sizeof(v));
    bv_init(&v, &vset);

    s = bv_obj_get(&v, BV_VIEWONLY);
    CHECK(s != NULL, "bv_obj_get non-NULL");
    if (!s) {
	bv_free(&v);
	bv_set_free(&vset);
	return 0;
    }

    /* Sphere at origin, radius 100 */
    VSET(s->s_center, 0.0, 0.0, 0.0);
    s->s_size = 100.0;
    s->s_flag = UP;

    n = bv_scene_obj_to_node(s);
    scene = bv_scene_create();
    bv_scene_add_node(scene, n);

    /* Default camera: eye at (0,0,1), target at (0,0,0) */
    view = bview_create("av_single_test");
    bview_settings_apply(view);

    r = bview_autoview_new(view, scene, -1);
    CHECK(r == 1, "bview_autoview_new with geometry returns 1");

    if (r) {
	cam = bview_camera_get(view);
	if (cam) {
	    /* Target should now be at scene center (0,0,0) */
	    CHECK(VNEAR_ZERO(cam->target, 1e-9),
		  "autoview: camera target == scene center");

	    /* Eye should be along Z above the scene */
	    VSUB2(to_center, cam->position, cam->target);
	    CHECK(VDOT(to_center, to_center) > 0.0,
		  "autoview: camera not at target");

	    /* Distance should be roughly 2 * radius = 200 */
	    {
		double dist = MAGNITUDE(to_center);
		CHECK(fabs(dist - 200.0) < 1e-6,
		      "autoview: camera distance == scale_factor * radius");
	    }
	}
    }

    bv_scene_destroy(scene);
    bview_destroy(view);
    bv_obj_put(s);
    bv_free(&v);
    bv_set_free(&vset);
    return 1;
}

static int
test_bview_autoview_scale_factor(void)
{
    struct bview_set   vset;
    struct bview       v;
    struct bv_scene_obj *s;
    struct bv_node     *n;
    struct bv_scene    *scene;
    struct bview_new   *view;
    const struct bview_camera *cam;
    int r;

    bv_set_init(&vset);
    memset(&v, 0, sizeof(v));
    bv_init(&v, &vset);

    s = bv_obj_get(&v, BV_VIEWONLY);
    if (!s) { bv_free(&v); bv_set_free(&vset); return 0; }

    VSET(s->s_center, 0.0, 0.0, 0.0);
    s->s_size = 50.0;
    s->s_flag = UP;

    n     = bv_scene_obj_to_node(s);
    scene = bv_scene_create();
    bv_scene_add_node(scene, n);

    view  = bview_create("av_scale_test");
    bview_settings_apply(view);

    /* Use scale_factor = 4.0 */
    r = bview_autoview_new(view, scene, 4.0);
    CHECK(r == 1, "bview_autoview_new scale_factor=4 returns 1");

    if (r) {
	cam = bview_camera_get(view);
	if (cam) {
	    vect_t eye_to_tgt;
	    double dist;
	    VSUB2(eye_to_tgt, cam->position, cam->target);
	    dist = MAGNITUDE(eye_to_tgt);
	    /* Expect dist = 4.0 * 50.0 = 200.0 */
	    CHECK(fabs(dist - 200.0) < 1e-6,
		  "autoview scale_factor=4: distance == 200.0");
	}
    }

    bv_scene_destroy(scene);
    bview_destroy(view);
    bv_obj_put(s);
    bv_free(&v);
    bv_set_free(&vset);
    return 1;
}


/* ================================================================
 * Phase 4 – bview_lod_node_update stub tests
 * ================================================================ */

static int
test_bview_lod_node_update_null(void)
{
    int r = bview_lod_node_update(NULL, NULL);
    CHECK(r == 0, "bview_lod_node_update(NULL, NULL) returns 0");
    return 1;
}

static int
test_bview_lod_node_update_non_geom(void)
{
    struct bv_node *n = bv_node_create("lod_group", BV_NODE_GROUP);
    int r;
    CHECK(n != NULL, "bv_node_create non-NULL");
    if (!n) return 0;

    r = bview_lod_node_update(n, NULL);
    CHECK(r == 0, "lod_node_update on group node returns 0");

    bv_node_destroy(n);
    return 1;
}

static int
test_bview_lod_node_update_geom_no_obj(void)
{
    struct bv_node *n = bv_node_create("lod_geom", BV_NODE_GEOMETRY);
    int r;
    CHECK(n != NULL, "bv_node_create non-NULL");
    if (!n) return 0;

    /* No user_data set → returns 0 (no legacy obj to mark stale) */
    r = bview_lod_node_update(n, NULL);
    CHECK(r == 0, "lod_node_update on geom without user_data returns 0");

    bv_node_destroy(n);
    return 1;
}

static int
test_bview_lod_node_update_marks_stale(void)
{
    struct bview_set  vset;
    struct bview      v;
    struct bv_scene_obj *s;
    struct bv_node    *n;
    int r;

    bv_set_init(&vset);
    memset(&v, 0, sizeof(v));
    bv_init(&v, &vset);

    s = bv_obj_get(&v, BV_VIEWONLY);
    CHECK(s != NULL, "bv_obj_get non-NULL");
    if (!s) {
	bv_free(&v);
	bv_set_free(&vset);
	return 0;
    }

    s->s_dlist_stale = 0;
    n = bv_scene_obj_to_node(s);
    CHECK(n != NULL, "bv_scene_obj_to_node non-NULL");
    if (!n) {
	bv_obj_put(s);
	bv_free(&v);
	bv_set_free(&vset);
	return 0;
    }

    r = bview_lod_node_update(n, NULL);
    CHECK(r == 1, "lod_node_update on valid geom node returns 1");
    CHECK(s->s_dlist_stale == 1,
	  "lod_node_update sets s_dlist_stale on wrapped obj");

    bv_node_destroy(n);
    bv_obj_put(s);
    bv_free(&v);
    bv_set_free(&vset);
    return 1;
}


/* ================================================================
 * scene_main dispatcher  (appended entries)
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
    /* Phase 1 */
    { "settings_apply",           test_bview_settings_apply         },
    { "settings_apply_null",      test_bview_settings_apply_null    },
    { "settings_apply_idempotent",test_bview_settings_apply_idempotent },
    /* Phase 2 */
    { "obj_to_node_null",         test_bv_scene_obj_to_node_null    },
    { "obj_to_node_basic",        test_bv_scene_obj_to_node_basic   },
    { "obj_to_node_children",     test_bv_scene_obj_to_node_with_children },
    { "scene_from_view_null",     test_bv_scene_from_view_null      },
    { "scene_from_view_empty",    test_bv_scene_from_view_empty     },
    { "scene_from_view_objs",     test_bv_scene_from_view_with_objects },
    /* Phase 3 */
    { "scene_from_vset_null",         test_bv_scene_from_view_set_null        },
    { "scene_from_vset_empty",        test_bv_scene_from_view_set_empty       },
    /* Phase 1 continued: bview_to_old round-trip */
    { "to_old_appearance",            test_bview_to_old_appearance            },
    { "to_old_overlay",               test_bview_to_old_overlay               },
    { "to_old_null",                  test_bview_to_old_null                  },
    /* Phase 2 continued: bv_node_bbox / bv_scene_bbox */
    { "node_bbox_null",               test_bv_node_bbox_null                  },
    { "node_bbox_no_geom",            test_bv_node_bbox_no_geom               },
    { "scene_bbox_null",              test_bv_scene_bbox_null                 },
    { "scene_bbox_empty",             test_bv_scene_bbox_empty                },
    { "scene_bbox_invisible",         test_bv_scene_bbox_invisible            },
    { "scene_bbox_visible",           test_bv_scene_bbox_visible              },
    /* Phase 2 continued: bview_autoview_new */
    { "autoview_null",                test_bview_autoview_null                },
    { "autoview_empty",               test_bview_autoview_empty               },
    { "autoview_single_obj",          test_bview_autoview_single_obj          },
    { "autoview_scale_factor",        test_bview_autoview_scale_factor        },
    /* Phase 4 stub: bview_lod_node_update */
    { "lod_node_update_null",         test_bview_lod_node_update_null         },
    { "lod_node_update_non_geom",     test_bview_lod_node_update_non_geom     },
    { "lod_node_update_no_obj",       test_bview_lod_node_update_geom_no_obj  },
    { "lod_node_update_stale",        test_bview_lod_node_update_marks_stale  },
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
