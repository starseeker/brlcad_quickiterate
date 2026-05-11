/*            T E S T _ R E N D E R . C
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
/** @file libbsg/tests/test_render.c
 *
 * Phase 8 unit tests: BSG render action and renderer contract.
 *
 * Test groups:
 *   Test 1: null-safety / init
 *   Test 2: no-op renderer apply completes without crash
 *   Test 3: counting renderer — expected draw_payload count
 *   Test 4: view-scope filtering
 *   Test 5: transform push/pop callbacks
 *   Test 6: two-pass transparency (query_capability path)
 *   Test 7: BSG_NODE_SENSOR nodes are skipped
 *   Test 8: bsg_node_drawn_rev get/set accessors
 *   Test 9: overlay/image-layer hooks
 */

#include "common.h"

#include <stdio.h>
#include <string.h>

#include "bu/app.h"
#include "bu/malloc.h"
#include "bv/defines.h"
#include "bv/util.h"
#include "bsg/node.h"
#include "bsg/node_shape.h"
#include "bsg/node_transform.h"
#include "bsg/payload.h"
#include "bsg/render.h"
#include "bsg/util.h"
#include "bsg/view_scope.h"

#define PASS(msg) do { printf("  PASS: %s\n", (msg)); } while (0)
#define FAIL(msg) do { printf("  FAIL: %s\n", (msg)); return 1; } while (0)


/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static struct bview *
make_view(void)
{
    struct bview *v;
    BU_ALLOC(v, struct bview);
    bv_init(v, NULL);
    bu_vls_sprintf(&v->gv_name, "test_view_render");
    v->gv_width  = 800;
    v->gv_height = 600;
    return v;
}

static void
free_view(struct bview *v)
{
    if (!v)
	return;
    bv_free(v);
    bu_free(v, "test_view_render");
}

static bsg_node *
make_shape_node(struct bview *v, const char *name)
{
    bsg_node *n = bsg_shape_create(v);
    if (n)
	bsg_node_set_name(n, name);
    return n;
}


/* ------------------------------------------------------------------ */
/* Counting renderer — records callback invocations                    */
/* ------------------------------------------------------------------ */

struct count_ctx {
    int begin_frame_count;
    int end_frame_count;
    int set_material_count;
    int set_appearance_count;
    int draw_payload_count;
    int draw_overlay_count;
    int draw_image_layer_count;
    int draw_image_continue;
    int push_transform_count;
    int pop_transform_count;
    int set_depth_mask_count;
    int depth_mask_last;
    int query_cap_return; /* value returned from query_capability */
};

static void cnt_begin_frame(void *d, struct bview *v)
{
    (void)v;
    ((struct count_ctx *)d)->begin_frame_count++;
}

static void cnt_end_frame(void *d, struct bview *v)
{
    (void)v;
    ((struct count_ctx *)d)->end_frame_count++;
}

static void cnt_set_material(void *d, bsg_node *n,
			     const struct bsg_material *m, int hm,
			     int hl, fastf_t t)
{
    (void)n; (void)m; (void)hm; (void)hl; (void)t;
    ((struct count_ctx *)d)->set_material_count++;
}

static void cnt_set_appearance(void *d, bsg_node *n,
			       const struct bsg_appearance *a, int ha)
{
    (void)n; (void)a; (void)ha;
    ((struct count_ctx *)d)->set_appearance_count++;
}

static void cnt_draw_payload(void *d, bsg_node *n, struct bview *v,
			     const mat_t w, int pass)
{
    (void)n; (void)v; (void)w; (void)pass;
    ((struct count_ctx *)d)->draw_payload_count++;
}

static void cnt_draw_overlay(void *d, bsg_node *n, struct bview *v)
{
    (void)n; (void)v;
    ((struct count_ctx *)d)->draw_overlay_count++;
}

static int cnt_draw_image_layer(void *d, bsg_node *root, struct bview *v)
{
    struct count_ctx *c = (struct count_ctx *)d;
    (void)root; (void)v;
    c->draw_image_layer_count++;
    return c->draw_image_continue;
}

static void cnt_push_transform(void *d, const mat_t nw, const mat_t ow)
{
    (void)nw; (void)ow;
    ((struct count_ctx *)d)->push_transform_count++;
}

static void cnt_pop_transform(void *d, const mat_t rw)
{
    (void)rw;
    ((struct count_ctx *)d)->pop_transform_count++;
}

static void cnt_set_depth_mask(void *d, int on)
{
    struct count_ctx *c = (struct count_ctx *)d;
    c->set_depth_mask_count++;
    c->depth_mask_last = on;
}

static int cnt_query_capability(void *d, int cap)
{
    (void)cap;
    return ((struct count_ctx *)d)->query_cap_return;
}

static const struct bsg_renderer_ops counting_ops = {
    cnt_begin_frame,
    cnt_end_frame,
    NULL,             /* set_camera */
    cnt_push_transform,
    cnt_pop_transform,
    cnt_set_material,
    cnt_set_appearance,
    cnt_draw_payload,
    cnt_draw_overlay,
    cnt_draw_image_layer,
    cnt_set_depth_mask,
    cnt_query_capability
};

static void
count_ctx_init(struct count_ctx *c, int cap_return)
{
    memset(c, 0, sizeof(*c));
    c->query_cap_return = cap_return;
    c->draw_image_continue = 1;
}


/* ------------------------------------------------------------------ */
/* Tests                                                                */
/* ------------------------------------------------------------------ */

/* Test 1: null-safety and init */
static int
test_null_safety(void)
{
    printf("Test 1: null-safety / init\n");

    /* NULL render action must not crash */
    bsg_render_action_init(NULL, &bsg_renderer_noop, NULL);
    bsg_render_action_set_view(NULL, NULL);
    int r = bsg_render_action_apply(NULL, NULL);
    if (r != 0)
	FAIL("apply(NULL, NULL) should return 0");

    /* Valid action with NULL root */
    struct bsg_render_action ra;
    bsg_render_action_init(&ra, &bsg_renderer_noop, NULL);
    r = bsg_render_action_apply(&ra, NULL);
    if (r != 0)
	FAIL("apply(ra, NULL) should return 0");

    /* NULL ops must not install */
    bsg_render_action_init(&ra, NULL, NULL);
    if (ra.ops != NULL)
	FAIL("init with NULL ops should leave ra.ops NULL");

    PASS("null-safety");
    return 0;
}


/* Test 2: no-op renderer on a real scene root */
static int
test_noop_renderer(void)
{
    printf("Test 2: no-op renderer\n");

    struct bview *v = make_view();
    if (!v)
	FAIL("make_view returned NULL");

    bsg_node *root = bsg_scene_root_create(v);
    if (!root) {
	free_view(v);
	FAIL("bsg_scene_root_create returned NULL");
    }

    bsg_node *sh = make_shape_node(v, "s1");
    if (!sh) {
	bsg_scene_root_destroy(root); free_view(v);
	FAIL("make_shape_node returned NULL");
    }
    bsg_node_add_child(root, sh);

    struct bsg_render_action ra;
    bsg_render_action_init(&ra, &bsg_renderer_noop, NULL);
    int r = bsg_render_action_apply(&ra, root);
    if (r != 1) {
	bsg_scene_root_destroy(root); free_view(v);
	FAIL("apply with no-op renderer should return 1");
    }

    bsg_scene_root_destroy(root);
    free_view(v);
    PASS("no-op renderer");
    return 0;
}


/* Test 3: counting renderer — expected draw_payload calls */
static int
test_counting_renderer(void)
{
    printf("Test 3: counting renderer\n");

    struct bview *v = make_view();
    if (!v)
	FAIL("make_view");

    bsg_node *root = bsg_scene_root_create(v);
    if (!root) {
	free_view(v);
	FAIL("scene root");
    }

    bsg_node *s1 = make_shape_node(v, "s1");
    bsg_node *s2 = make_shape_node(v, "s2");
    bsg_node *s3 = make_shape_node(v, "s3");
    if (!s1 || !s2 || !s3) {
	bsg_scene_root_destroy(root); free_view(v);
	FAIL("shape nodes");
    }

    bsg_node_add_child(root, s1);
    bsg_node_add_child(root, s2);
    bsg_node_add_child(root, s3);

    struct count_ctx ctx;
    count_ctx_init(&ctx, 0 /* no two-pass */);

    struct bsg_render_action ra;
    bsg_render_action_init(&ra, &counting_ops, &ctx);
    bsg_render_action_apply(&ra, root);

    if (ctx.begin_frame_count != 1)
	{ bsg_scene_root_destroy(root); free_view(v); FAIL("begin_frame not called once"); }
    if (ctx.end_frame_count != 1)
	{ bsg_scene_root_destroy(root); free_view(v); FAIL("end_frame not called once"); }
    if (ctx.draw_payload_count != 3)
	{ bsg_scene_root_destroy(root); free_view(v); FAIL("expected 3 draw_payload calls"); }
    if (ctx.set_material_count != 3)
	{ bsg_scene_root_destroy(root); free_view(v); FAIL("expected 3 set_material calls"); }
    if (ctx.set_appearance_count != 3)
	{ bsg_scene_root_destroy(root); free_view(v); FAIL("expected 3 set_appearance calls"); }

    bsg_scene_root_destroy(root);
    free_view(v);
    PASS("counting renderer");
    return 0;
}


/* Test 4: view-scope filtering */
static int
test_view_scope_filter(void)
{
    printf("Test 4: view-scope filtering\n");

    struct bview *va = make_view();
    struct bview *vb = make_view();
    if (!va || !vb) {
	free_view(va); free_view(vb);
	FAIL("make_view");
    }
    bu_vls_sprintf(&va->gv_name, "va");
    bu_vls_sprintf(&vb->gv_name, "vb");

    bsg_node *root = bsg_scene_root_create(va);
    if (!root) {
	free_view(va); free_view(vb);
	FAIL("root");
    }

    /* scope_a: view-private to va, contains shape_a */
    bsg_node *scope_a = bsg_view_scope_create(va);
    bsg_node *shape_a = make_shape_node(va, "shape_a");

    /* scope_shared: create with va, then clear s_v to make it shared. */
    bsg_node *scope_shared = bsg_view_scope_create(va);
    bsg_node *shape_b = make_shape_node(va, "shape_b");

    if (!scope_a || !shape_a || !scope_shared || !shape_b) {
	bsg_scene_root_destroy(root);
	free_view(va); free_view(vb);
	FAIL("node allocation");
    }

    /* scope_shared has s_v cleared so it is visible to all views. */
    ((struct bv_scene_obj *)scope_shared)->s_v = NULL;

    bsg_node_add_child(root, scope_a);
    bsg_node_add_child(scope_a, shape_a);

    bsg_node_add_child(root, scope_shared);
    bsg_node_add_child(scope_shared, shape_b);

    /* Render with va: should see shape_a (via scope_a) + shape_b (shared) = 2 */
    {
	struct count_ctx ctx;
	count_ctx_init(&ctx, 0);
	struct bsg_render_action ra;
	bsg_render_action_init(&ra, &counting_ops, &ctx);
	bsg_render_action_set_view(&ra, va);
	bsg_render_action_apply(&ra, root);

	if (ctx.draw_payload_count != 2) {
	    bsg_scene_root_destroy(root);
	    free_view(va); free_view(vb);
	    FAIL("va: expected 2 draw_payload calls");
	}
    }

    /* Render with vb: scope_a is skipped (wrong view), only shape_b (shared) = 1 */
    {
	struct count_ctx ctx;
	count_ctx_init(&ctx, 0);
	struct bsg_render_action ra;
	bsg_render_action_init(&ra, &counting_ops, &ctx);
	bsg_render_action_set_view(&ra, vb);
	bsg_render_action_apply(&ra, root);

	if (ctx.draw_payload_count != 1) {
	    bsg_scene_root_destroy(root);
	    free_view(va); free_view(vb);
	    FAIL("vb: expected 1 draw_payload call");
	}
    }

    bsg_scene_root_destroy(root);
    free_view(va);
    free_view(vb);
    PASS("view-scope filtering");
    return 0;
}


/* Test 5: transform push/pop */
static int
test_transform_pushpop(void)
{
    printf("Test 5: transform push/pop\n");

    struct bview *v = make_view();
    if (!v)
	FAIL("make_view");

    bsg_node *root = bsg_scene_root_create(v);
    if (!root) {
	free_view(v);
	FAIL("root");
    }

    /* Create a transform node with one shape child */
    bsg_node *xform = bsg_transform_create(v);
    bsg_node *shape = make_shape_node(v, "under_xform");
    if (!xform || !shape) {
	bsg_scene_root_destroy(root); free_view(v);
	FAIL("transform/shape nodes");
    }

    mat_t m;
    MAT_IDN(m);
    m[3] = 10.0; /* simple translation */
    bsg_node_transform_set(xform, m);

    bsg_node_add_child(root, xform);
    bsg_node_add_child(xform, shape);

    struct count_ctx ctx;
    count_ctx_init(&ctx, 0);
    struct bsg_render_action ra;
    bsg_render_action_init(&ra, &counting_ops, &ctx);
    bsg_render_action_apply(&ra, root);

    if (ctx.push_transform_count != 1)
	{ bsg_scene_root_destroy(root); free_view(v); FAIL("push_transform not called once"); }
    if (ctx.pop_transform_count != 1)
	{ bsg_scene_root_destroy(root); free_view(v); FAIL("pop_transform not called once"); }
    if (ctx.draw_payload_count != 1)
	{ bsg_scene_root_destroy(root); free_view(v); FAIL("expected 1 draw_payload"); }

    bsg_scene_root_destroy(root);
    free_view(v);
    PASS("transform push/pop");
    return 0;
}


/* Test 6: two-pass transparency via query_capability */
static int
test_two_pass_transparency(void)
{
    printf("Test 6: two-pass transparency\n");

    struct bview *v = make_view();
    if (!v)
	FAIL("make_view");

    bsg_node *root = bsg_scene_root_create(v);
    if (!root) {
	free_view(v);
	FAIL("root");
    }

    bsg_node *s1 = make_shape_node(v, "s1");
    bsg_node *s2 = make_shape_node(v, "s2");
    if (!s1 || !s2) {
	bsg_scene_root_destroy(root); free_view(v);
	FAIL("shape nodes");
    }
    bsg_node_add_child(root, s1);
    bsg_node_add_child(root, s2);

    /* Enable two-pass via query_capability returning non-zero */
    struct count_ctx ctx;
    count_ctx_init(&ctx, 1 /* cap_return: transparency supported */);
    struct bsg_render_action ra;
    bsg_render_action_init(&ra, &counting_ops, &ctx);
    bsg_render_action_apply(&ra, root);

    /* Each of the 2 shapes is visited twice (once per pass) = 4 */
    if (ctx.draw_payload_count != 4)
	{ bsg_scene_root_destroy(root); free_view(v); FAIL("expected 4 draw_payload calls (2 shapes * 2 passes)"); }

    /* set_depth_mask should have been called: 0 between passes, 1 after */
    if (ctx.set_depth_mask_count < 2)
	{ bsg_scene_root_destroy(root); free_view(v); FAIL("set_depth_mask called too few times"); }
    if (ctx.depth_mask_last != 1)
	{ bsg_scene_root_destroy(root); free_view(v); FAIL("depth mask not restored to 1 after transparent pass"); }

    bsg_scene_root_destroy(root);
    free_view(v);
    PASS("two-pass transparency");
    return 0;
}


/* Test 7: sensor nodes are skipped.
 * bsg_node_sensor_create requires a registered root, so we manually mark
 * a shape node as a sensor by setting BSG_NODE_SENSOR in s_type_flags.
 * This is the same bit-pattern the traversal checks. */
static int
test_sensor_skip(void)
{
    printf("Test 7: sensor nodes skipped\n");

    struct bview *v = make_view();
    if (!v)
	FAIL("make_view");

    bsg_node *root = bsg_scene_root_create(v);
    if (!root) {
	free_view(v);
	FAIL("root");
    }

    /* Use a plain shape and manually mark it as a sensor. */
    bsg_node *sensor = make_shape_node(v, "fake_sensor");
    bsg_node *shape  = make_shape_node(v, "real_shape");
    if (!sensor || !shape) {
	bsg_scene_root_destroy(root); free_view(v);
	FAIL("sensor/shape");
    }

    /* Mark as sensor so the traversal skips it. */
    bsg_node_set_kind(sensor,
		      bsg_node_kind(sensor) | (unsigned long long)BSG_NODE_SENSOR);

    bsg_node_add_child(root, sensor);
    bsg_node_add_child(root, shape);

    struct count_ctx ctx;
    count_ctx_init(&ctx, 0);
    struct bsg_render_action ra;
    bsg_render_action_init(&ra, &counting_ops, &ctx);
    bsg_render_action_apply(&ra, root);

    /* Sensor must not produce a draw_payload call */
    if (ctx.draw_payload_count != 1)
	{ bsg_scene_root_destroy(root); free_view(v); FAIL("sensor must be skipped; expected 1 draw_payload"); }

    bsg_scene_root_destroy(root);
    free_view(v);
    PASS("sensor skip");
    return 0;
}


/* Test 8: bsg_node_drawn_rev accessors */
static int
test_drawn_rev_accessors(void)
{
    printf("Test 8: bsg_node_drawn_rev accessors\n");

    struct bview *v = make_view();
    if (!v)
	FAIL("make_view");

    bsg_node *n = make_shape_node(v, "rev_test");
    if (!n) {
	free_view(v);
	FAIL("shape_node");
    }

    /* Initial value should be 0 */
    if (bsg_node_drawn_rev(n) != 0)
	FAIL("initial drawn_rev should be 0");

    bsg_node_set_drawn_rev(n, 42);
    if (bsg_node_drawn_rev(n) != 42)
	FAIL("drawn_rev should be 42 after set");

    bsg_node_set_drawn_rev(n, 0xDEADBEEFULL);
    if (bsg_node_drawn_rev(n) != 0xDEADBEEFULL)
	FAIL("drawn_rev should hold large value");

    /* NULL safety */
    bsg_node_set_drawn_rev(NULL, 99);
    if (bsg_node_drawn_rev(NULL) != 0)
	FAIL("drawn_rev(NULL) should return 0");

    free_view(v);
    PASS("bsg_node_drawn_rev");
    return 0;
}

/* Test 9: overlay payload hook and image-layer traversal gate */
static int
test_overlay_image_layer_hooks(void)
{
    printf("Test 9: overlay/image-layer hooks\n");

    struct bview *v = make_view();
    if (!v)
	FAIL("make_view");

    bsg_node *root = bsg_scene_root_create(v);
    if (!root) {
	free_view(v);
	FAIL("root");
    }

    bsg_node *overlay = make_shape_node(v, "overlay_shape");
    bsg_node *shape = make_shape_node(v, "scene_shape");
    if (!overlay || !shape) {
	bsg_scene_root_destroy(root); free_view(v);
	FAIL("shape nodes");
    }
    bsg_node_set_payload_type(overlay, BSG_PAYLOAD_OVERLAY);
    bsg_node_add_child(root, overlay);
    bsg_node_add_child(root, shape);

    {
	struct count_ctx ctx;
	count_ctx_init(&ctx, 0);
	struct bsg_render_action ra;
	bsg_render_action_init(&ra, &counting_ops, &ctx);
	bsg_render_action_apply(&ra, root);

	if (ctx.draw_image_layer_count != 1) {
	    bsg_scene_root_destroy(root); free_view(v);
	    FAIL("expected draw_image_layer once");
	}
	if (ctx.draw_overlay_count != 1) {
	    bsg_scene_root_destroy(root); free_view(v);
	    FAIL("expected draw_overlay once");
	}
	if (ctx.draw_payload_count != 1) {
	    bsg_scene_root_destroy(root); free_view(v);
	    FAIL("expected only non-overlay shape via draw_payload");
	}
    }

    {
	struct count_ctx ctx;
	count_ctx_init(&ctx, 0);
	ctx.draw_image_continue = 0; /* image-layer callback skips scene */
	struct bsg_render_action ra;
	bsg_render_action_init(&ra, &counting_ops, &ctx);
	bsg_render_action_apply(&ra, root);

	if (ctx.draw_image_layer_count != 1) {
	    bsg_scene_root_destroy(root); free_view(v);
	    FAIL("expected draw_image_layer once when skipping scene");
	}
	if (ctx.draw_overlay_count != 0 || ctx.draw_payload_count != 0) {
	    bsg_scene_root_destroy(root); free_view(v);
	    FAIL("scene traversal should be skipped when draw_image_layer returns 0");
	}
    }

    bsg_scene_root_destroy(root);
    free_view(v);
    PASS("overlay/image-layer hooks");
    return 0;
}


/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */

int
main(int argc, char *argv[])
{
    bu_setprogname(argv[0]);
    (void)argc;

    int fail = 0;

    fail += test_null_safety();
    fail += test_noop_renderer();
    fail += test_counting_renderer();
    fail += test_view_scope_filter();
    fail += test_transform_pushpop();
    fail += test_two_pass_transparency();
    fail += test_sensor_skip();
    fail += test_drawn_rev_accessors();
    fail += test_overlay_image_layer_hooks();

    if (fail) {
	printf("FAILED: %d test(s)\n", fail);
	return 1;
    }
    printf("All Phase 8/9 render action tests passed.\n");
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
