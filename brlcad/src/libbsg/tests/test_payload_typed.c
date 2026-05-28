/*            T E S T _ P A Y L O A D _ T Y P E D . C
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
/** @file libbsg/tests/test_payload_typed.c
 *
 * Phase D1 typed payload regression tests.
 */

#include "common.h"

#include <stdio.h>
#include <string.h>

#include "bu/app.h"
#include "bu/malloc.h"
#include "bsg/faceplate.h"
#include "bsg/node_shape.h"
#include "bsg/payload_typed.h"
#include "bsg/polygon.h"
#include "bsg/util.h"
#include "bsg/vlist.h"

#define PASS(msg) do { printf("  PASS: %s\n", (msg)); } while (0)
#define FAIL(msg) do { printf("  FAIL: %s\n", (msg)); return 1; } while (0)

static struct bsg_view *
make_view(void)
{
    struct bsg_view *v;
    BU_ALLOC(v, struct bsg_view);
    bsg_view_init(v, NULL);
    bu_vls_sprintf(&v->gv_name, "payload_view");
    return v;
}

static void
free_view(struct bsg_view *v)
{
    if (!v)
	return;
    bsg_view_free(v);
    bu_free(v, "payload_view");
}

struct sketch_live_stub {
    uint64_t revision;
    int update_calls;
    int bounds_calls;
    int pick_calls;
    int snap_calls;
    point_t bmin;
    point_t bmax;
    point_t snap_out;
    int pick_id;
};

static int g_sketch_live_free_calls = 0;

static uint64_t
sketch_stub_revision(void *live_ctx)
{
    struct sketch_live_stub *stub = (struct sketch_live_stub *)live_ctx;
    return stub->revision;
}

static int
sketch_stub_update(void *live_ctx, struct bsg_view *UNUSED(v))
{
    struct sketch_live_stub *stub = (struct sketch_live_stub *)live_ctx;
    stub->update_calls++;
    stub->revision++;
    return 1;
}

static int
sketch_stub_bounds(void *live_ctx, point_t *bmin, point_t *bmax)
{
    struct sketch_live_stub *stub = (struct sketch_live_stub *)live_ctx;
    stub->bounds_calls++;
    VMOVE((*bmin), stub->bmin);
    VMOVE((*bmax), stub->bmax);
    return 1;
}

static int
sketch_stub_pick(void *live_ctx, struct bsg_view *UNUSED(v), int UNUSED(x), int UNUSED(y), void *pick_out)
{
    struct sketch_live_stub *stub = (struct sketch_live_stub *)live_ctx;
    stub->pick_calls++;
    if (pick_out)
	*((int *)pick_out) = stub->pick_id;
    return 1;
}

static int
sketch_stub_snap(void *live_ctx, struct bsg_view *UNUSED(v), const point_t UNUSED(sample_pt), point_t out_pt)
{
    struct sketch_live_stub *stub = (struct sketch_live_stub *)live_ctx;
    stub->snap_calls++;
    VMOVE(out_pt, stub->snap_out);
    return 1;
}

static void
sketch_stub_free(void *live_ctx)
{
    struct sketch_live_stub *stub = (struct sketch_live_stub *)live_ctx;
    g_sketch_live_free_calls++;
    bu_free(stub, "sketch live stub");
}

static int
test_vlist_node_helpers(void)
{
    printf("=== Test 1: vlist node helpers ===\n");

    struct bsg_view *v = make_view();
    bsg_node *shape = bsg_shape_create(v);
    if (!shape) FAIL("bsg_shape_create returned NULL");

    point_t p1 = VINIT_ZERO;
    point_t p2 = VINIT_ZERO;
    point_t p3 = VINIT_ZERO;
    VSET(p1, 0.0, 0.0, 0.0);
    VSET(p2, 1.0, 0.0, 0.0);
    VSET(p3, 1.0, 1.0, 0.0);

    if (!bsg_node_clear_vlist_payload(shape)) FAIL("clear vlist payload");
    if (!bsg_node_append_vlist_payload(shape, p1, BSG_VLIST_LINE_MOVE)) FAIL("append move");
    if (!bsg_node_append_vlist_payload(shape, p2, BSG_VLIST_LINE_DRAW)) FAIL("append draw 1");
    if (!bsg_node_append_vlist_payload(shape, p3, BSG_VLIST_LINE_DRAW)) FAIL("append draw 2");

    struct bsg_payload *pl = bsg_node_get_payload(shape);
    if (!pl || pl->pl_type != BSG_PL_VLIST) FAIL("shape missing vlist payload");
    if (shape->s_vlen != 3) FAIL("shape vlen not updated");
    if (pl->pl_revision != 4) FAIL("payload revision not updated");

    if (!bsg_payload_vlist_get(pl) || !bsg_payload_vlist_get(pl)->vlist)
	FAIL("vlist payload data missing");

    bsg_shape_destroy(shape);
    free_view(v);

    PASS("vlist node helpers");
    return 0;
}

static int
test_polygon_payload(void)
{
    printf("=== Test 2: polygon payload ===\n");

    struct bsg_view *v = make_view();
    bsg_node *root = bsg_scene_root_create(v);
    if (!root) FAIL("bsg_scene_root_create returned NULL");
    point_t origin = VINIT_ZERO;
    bsg_node *poly = bsg_create_polygon(v, BSG_OBJ_VIEW, BSG_POLYGON_RECTANGLE, &origin);
    if (!poly) FAIL("bsg_create_polygon returned NULL");
    if (!bsg_node_polygon(poly)) FAIL("node polygon accessor returned NULL");
    if (!bsg_node_get_payload(poly) || bsg_node_get_payload(poly)->pl_type != BSG_PL_POLYGON)
	FAIL("polygon node missing typed payload");

    bsg_obj_put(poly);
    bsg_scene_root_destroy(root);
    free_view(v);

    PASS("polygon payload");
    return 0;
}

static int
test_remaining_payload_builders(void)
{
    printf("=== Test 3: remaining payload builders ===\n");

    struct bsg_label *label;
    BU_GET(label, struct bsg_label);
    memset(label, 0, sizeof(*label));
    BU_VLS_INIT(&label->label);
    bu_vls_sprintf(&label->label, "hud");

    struct bsg_payload *hud = bsg_payload_hud_text_create(label);
    if (!hud || !bsg_payload_hud_text_get(hud)) FAIL("hud text payload");
    bsg_payload_free(hud);

    point_t pts[2] = {VINIT_ZERO, VINIT_ZERO};
    int cmds[2] = {BSG_VLIST_LINE_MOVE, BSG_VLIST_LINE_DRAW};
    struct bsg_payload *line_set = bsg_payload_line_set_create(pts, cmds, 2);
    if (!line_set || !bsg_payload_line_set_get(line_set)) FAIL("line set payload");
    bsg_payload_free(line_set);

    unsigned char px[4] = {255, 0, 0, 255};
    struct bsg_payload *image = bsg_payload_image_create(1, 1, 4, px);
    if (!image || !bsg_payload_image_get(image)) FAIL("image payload");
    bsg_payload_free(image);

    struct bsg_payload *fb = bsg_payload_framebuffer_create(NULL, 7);
    if (!fb || !bsg_payload_framebuffer_get(fb) || bsg_payload_framebuffer_get(fb)->mode != 7)
	FAIL("framebuffer payload");
    bsg_payload_free(fb);

    struct bsg_grid_state grid;
    memset(&grid, 0, sizeof(grid));
    grid.draw = 1;
    struct bsg_payload *gpl = bsg_payload_grid_create(&grid);
    if (!gpl || !bsg_payload_grid_get(gpl) || !bsg_payload_grid_get(gpl)->draw)
	FAIL("grid payload");
    bsg_payload_free(gpl);

    struct bsg_payload *ann = bsg_payload_annotation_create("measure", pts, 2);
    if (!ann || !bsg_payload_annotation_get(ann)) FAIL("annotation payload");
    bsg_payload_free(ann);

    struct bsg_payload *mesh = bsg_payload_mesh_create(NULL);
    struct bsg_payload *csg = bsg_payload_csg_create(NULL);
    struct bsg_payload *brep = bsg_payload_brep_create(NULL);
    if (!mesh || mesh->pl_type != BSG_PL_MESH) FAIL("mesh payload");
    if (!csg || csg->pl_type != BSG_PL_CSG) FAIL("csg payload");
    if (!brep || brep->pl_type != BSG_PL_BREP) FAIL("brep payload");
    bsg_payload_free(mesh);
    bsg_payload_free(csg);
    bsg_payload_free(brep);

    PASS("remaining payload builders");
    return 0;
}

static int
test_lifecycle_hooks(void)
{
    printf("=== Test 5: lifecycle hook dispatch ===\n");

    /* ---- VLIST payload: real bounds and export hooks ---- */
    struct bsg_view *v = make_view();
    bsg_node *shape = bsg_shape_create(v);
    if (!shape) FAIL("bsg_shape_create");

    /* Add a unit triangle so the bounds are well-defined. */
    point_t pa = VINIT_ZERO;
    point_t pb = VINIT_ZERO;
    point_t pc = VINIT_ZERO;
    VSET(pa, 0.0, 0.0, 0.0);
    VSET(pb, 1.0, 0.0, 0.0);
    VSET(pc, 0.5, 1.0, 0.0);
    bsg_node_append_vlist_payload(shape, pa, BSG_VLIST_LINE_MOVE);
    bsg_node_append_vlist_payload(shape, pb, BSG_VLIST_LINE_DRAW);
    bsg_node_append_vlist_payload(shape, pc, BSG_VLIST_LINE_DRAW);

    struct bsg_payload *pl = bsg_node_get_payload(shape);
    if (!pl || pl->pl_type != BSG_PL_VLIST) FAIL("vlist payload missing");

    /* bounds hook should return 1 and give sensible extents */
    if (!pl->pl_bounds) FAIL("vlist pl_bounds is NULL");
    point_t bmin = VINIT_ZERO;
    point_t bmax = VINIT_ZERO;
    int bounds_ok = pl->pl_bounds(pl, &bmin, &bmax);
    if (!bounds_ok) FAIL("vlist pl_bounds returned 0 (expected 1)");
    if (bmin[0] > 0.0 || bmax[0] < 1.0) FAIL("vlist bounds X range wrong");
    if (bmin[1] > 0.0 || bmax[1] < 1.0) FAIL("vlist bounds Y range wrong");

    /* export hook should return 0 (success) */
    if (!pl->pl_export) FAIL("vlist pl_export is NULL");
    struct bu_vls export_out = BU_VLS_INIT_ZERO;
    int export_rc = pl->pl_export(pl, &export_out);
    if (export_rc != 0) FAIL("vlist pl_export returned non-zero (expected 0)");
    if (bu_vls_strlen(&export_out) == 0) FAIL("vlist pl_export produced empty output");
    bu_vls_free(&export_out);

    /* backend_prepare sentinel should return 0 (no-op) */
    if (!pl->pl_backend_prepare) FAIL("vlist pl_backend_prepare is NULL");
    if (pl->pl_backend_prepare(pl, NULL) != 0) FAIL("vlist backend_prepare sentinel returned non-zero");

    bsg_shape_destroy(shape);
    free_view(v);

    /* ---- Non-VLIST types: sentinel hooks return 0 ---- */

    /* TEXT sentinel */
    struct bsg_label *label;
    BU_GET(label, struct bsg_label);
    memset(label, 0, sizeof(*label));
    BU_VLS_INIT(&label->label);
    bu_vls_sprintf(&label->label, "sentinel test");
    struct bsg_payload *text_pl = bsg_payload_hud_text_create(label);
    if (!text_pl) FAIL("hud_text payload create");
    if (!text_pl->pl_bounds) FAIL("text pl_bounds is NULL");
    point_t tbmin = VINIT_ZERO, tbmax = VINIT_ZERO;
    if (text_pl->pl_bounds(text_pl, &tbmin, &tbmax) != 0)
	FAIL("text pl_bounds sentinel returned non-zero");
    if (!text_pl->pl_export) FAIL("text pl_export is NULL");
    struct bu_vls text_export = BU_VLS_INIT_ZERO;
    if (text_pl->pl_export(text_pl, &text_export) != 0)
	FAIL("text pl_export sentinel returned non-zero");
    bu_vls_free(&text_export);
    if (!text_pl->pl_backend_prepare) FAIL("text pl_backend_prepare is NULL");
    if (text_pl->pl_backend_prepare(text_pl, NULL) != 0)
	FAIL("text pl_backend_prepare sentinel returned non-zero");
    bsg_payload_free(text_pl);

    /* IMAGE sentinel */
    unsigned char px[4] = {128, 64, 32, 255};
    struct bsg_payload *img_pl = bsg_payload_image_create(1, 1, 4, px);
    if (!img_pl) FAIL("image payload create");
    if (!img_pl->pl_bounds) FAIL("image pl_bounds is NULL");
    point_t ibmin = VINIT_ZERO, ibmax = VINIT_ZERO;
    if (img_pl->pl_bounds(img_pl, &ibmin, &ibmax) != 0)
	FAIL("image pl_bounds sentinel returned non-zero");
    bsg_payload_free(img_pl);

    PASS("lifecycle hook dispatch");
    return 0;
}


static int
test_sketch_live_contract(void)
{
    printf("=== Test 4: sketch live-source contract ===\n");

    int rt_edit_placeholder = 0;
    int grid_placeholder = 0;
    struct bsg_payload *sketch =
	bsg_payload_sketch_create((void *)&rt_edit_placeholder, (void *)&grid_placeholder);
    if (!sketch || sketch->pl_type != BSG_PL_SKETCH) FAIL("sketch payload create");

    struct sketch_live_stub *stub =
	(struct sketch_live_stub *)bu_calloc(1, sizeof(struct sketch_live_stub), "sketch live stub");
    stub->revision = 7;
    stub->pick_id = 42;
    VSET(stub->bmin, -1.0, -2.0, -3.0);
    VSET(stub->bmax, 4.0, 5.0, 6.0);
    VSET(stub->snap_out, 0.25, 0.5, 0.75);

    g_sketch_live_free_calls = 0;
    if (bsg_payload_sketch_set_live_ops(
	    sketch,
	    stub,
	    1,
	    sketch_stub_revision,
	    sketch_stub_update,
	    sketch_stub_bounds,
	    sketch_stub_pick,
	    sketch_stub_snap,
	    sketch_stub_free))
	FAIL("set sketch live ops");

    if (bsg_payload_sketch_revision(sketch) != 7)
	FAIL("initial sketch revision");

    point_t bmin = VINIT_ZERO;
    point_t bmax = VINIT_ZERO;
    if (!bsg_payload_sketch_bounds(sketch, &bmin, &bmax))
	FAIL("sketch bounds callback");
    if (!NEAR_EQUAL(bmin[0], -1.0, SMALL_FASTF) || !NEAR_EQUAL(bmax[2], 6.0, SMALL_FASTF))
	FAIL("sketch bounds values");

    int pick_id = -1;
    if (!bsg_payload_sketch_pick(sketch, NULL, 10, 20, &pick_id))
	FAIL("sketch pick callback");
    if (pick_id != 42)
	FAIL("sketch pick value");

    point_t sample_pt = VINIT_ZERO;
    point_t snap_pt = VINIT_ZERO;
    if (!bsg_payload_sketch_snap(sketch, NULL, sample_pt, snap_pt))
	FAIL("sketch snap callback");
    if (!NEAR_EQUAL(snap_pt[1], 0.5, SMALL_FASTF))
	FAIL("sketch snap value");

    if (bsg_payload_sketch_realize(sketch, NULL) != 1)
	FAIL("sketch realize should report revision change");
    if (stub->update_calls != 1)
	FAIL("sketch update callback count");
    if (bsg_payload_sketch_revision(sketch) != 8)
	FAIL("sketch revision after realize");
    if (stub->bounds_calls != 1 || stub->pick_calls != 1 || stub->snap_calls != 1)
	FAIL("sketch callback counts");

    bsg_payload_free(sketch);
    if (g_sketch_live_free_calls != 1)
	FAIL("sketch live free callback");

    PASS("sketch live-source contract");
    return 0;
}

static int
test_line_set_builders(void)
{
    printf("=== Test 6: LINE_SET builder helpers ===\n");

    point_t pts2[2] = {{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}};
    int cmds2[2] = {BSG_VLIST_LINE_MOVE, BSG_VLIST_LINE_DRAW};
    struct bsg_payload *pl = bsg_payload_line_set_create(pts2, cmds2, 2);
    if (!pl) FAIL("line_set_create returned NULL");
    if (bsg_payload_line_set_point_count(pl) != 2) FAIL("initial point count");
    if (bsg_payload_line_set_cmd_at(pl, 0) != BSG_VLIST_LINE_MOVE) FAIL("cmd_at(0)");
    if (bsg_payload_line_set_cmd_at(pl, 1) != BSG_VLIST_LINE_DRAW) FAIL("cmd_at(1)");
    if (bsg_payload_line_set_cmd_at(pl, 99) != -1) FAIL("cmd_at out-of-range");

    /* append one more segment */
    point_t extra[1] = {{2.0, 0.0, 0.0}};
    int ecmd[1] = {BSG_VLIST_LINE_DRAW};
    uint64_t rev_before = pl->pl_revision;
    if (!bsg_payload_line_set_append_segments(pl, extra, ecmd, 1)) FAIL("append_segments");
    if (bsg_payload_line_set_point_count(pl) != 3) FAIL("point count after append");
    if (pl->pl_revision <= rev_before) FAIL("revision not bumped after append");

    /* replace with a single segment */
    point_t r_pts[2] = {{10.0, 0.0, 0.0}, {20.0, 0.0, 0.0}};
    int r_cmds[2] = {BSG_VLIST_LINE_MOVE, BSG_VLIST_LINE_DRAW};
    if (!bsg_payload_line_set_replace(pl, r_pts, r_cmds, 2)) FAIL("replace");
    if (bsg_payload_line_set_point_count(pl) != 2) FAIL("point count after replace");

    /* verify bounds are updated */
    point_t bmin = VINIT_ZERO, bmax = VINIT_ZERO;
    if (!pl->pl_bounds || pl->pl_bounds(pl, &bmin, &bmax) != 1) FAIL("line_set pl_bounds after replace");
    if (bmin[0] > 10.0 || bmax[0] < 20.0) FAIL("line_set bounds X after replace");

    /* clear */
    if (!bsg_payload_line_set_clear(pl)) FAIL("clear");
    if (bsg_payload_line_set_point_count(pl) != 0) FAIL("point count after clear");
    if (bsg_payload_line_set_cmd_at(pl, 0) != -1) FAIL("cmd_at on empty");
    /* bounds of empty line set should return 0 */
    if (pl->pl_bounds(pl, &bmin, &bmax) != 0) FAIL("bounds of empty line set");

    bsg_payload_free(pl);
    PASS("LINE_SET builder helpers");
    return 0;
}

static int
test_remaining_lifecycle_hooks(void)
{
    printf("=== Test 7: lifecycle hooks for remaining payload types ===\n");

    /* Verify every payload type has non-NULL pl_bounds/pl_export/pl_backend_prepare
     * (either a real implementation or the _no_* sentinel).  Also exercise the
     * return values so we confirm the sentinels actually run. */

#define CHECK_HOOKS(pl_, label_) do { \
    if (!(pl_)) FAIL(label_ " payload create"); \
    if (!(pl_)->pl_bounds) FAIL(label_ " pl_bounds NULL"); \
    if (!(pl_)->pl_export) FAIL(label_ " pl_export NULL"); \
    if (!(pl_)->pl_backend_prepare) FAIL(label_ " pl_backend_prepare NULL"); \
} while (0)

#define CHECK_SENTINEL_HOOKS(pl_, label_) do { \
    CHECK_HOOKS(pl_, label_); \
    point_t _bmin = VINIT_ZERO, _bmax = VINIT_ZERO; \
    if ((pl_)->pl_bounds((pl_), &_bmin, &_bmax) != 0) FAIL(label_ " pl_bounds sentinel"); \
    struct bu_vls _exp = BU_VLS_INIT_ZERO; \
    if ((pl_)->pl_export((pl_), &_exp) != 0) FAIL(label_ " pl_export sentinel"); \
    bu_vls_free(&_exp); \
    if ((pl_)->pl_backend_prepare((pl_), NULL) != 0) FAIL(label_ " pl_backend_prepare sentinel"); \
    bsg_payload_free(pl_); \
} while (0)

    /* TEXT */
    {
        struct bsg_label *lbl;
        BU_GET(lbl, struct bsg_label);
        memset(lbl, 0, sizeof(*lbl));
        BU_VLS_INIT(&lbl->label);
        bu_vls_sprintf(&lbl->label, "test");
        struct bsg_payload *pl = bsg_payload_text_create(lbl);
        CHECK_SENTINEL_HOOKS(pl, "TEXT");
    }

    /* LINE_SET (real bounds when non-empty, sentinel pl_export) */
    {
        point_t pts[2] = {{0,0,0}, {1,0,0}};
        int cmds[2] = {BSG_VLIST_LINE_MOVE, BSG_VLIST_LINE_DRAW};
        struct bsg_payload *pl = bsg_payload_line_set_create(pts, cmds, 2);
        CHECK_HOOKS(pl, "LINE_SET");
        point_t bmin = VINIT_ZERO, bmax = VINIT_ZERO;
        if (pl->pl_bounds(pl, &bmin, &bmax) != 1) FAIL("LINE_SET real pl_bounds");
        if (bmax[0] < 1.0) FAIL("LINE_SET bounds value");
        struct bu_vls exp = BU_VLS_INIT_ZERO;
        if (pl->pl_export(pl, &exp) != 0) FAIL("LINE_SET pl_export sentinel");
        bu_vls_free(&exp);
        if (pl->pl_backend_prepare(pl, NULL) != 0) FAIL("LINE_SET pl_backend_prepare sentinel");
        bsg_payload_free(pl);
    }

    /* POLYGON (real bounds when contours present, sentinel export) */
    {
        struct bsg_view *v = make_view();
        bsg_node *root = bsg_scene_root_create(v);
        point_t origin = VINIT_ZERO;
        bsg_node *poly_node = bsg_create_polygon(v, BSG_OBJ_VIEW, BSG_POLYGON_RECTANGLE, &origin);
        struct bsg_payload *pl = bsg_node_get_payload(poly_node);
        CHECK_HOOKS(pl, "POLYGON");
        /* empty polygon: bounds may return 0 */
        struct bu_vls exp = BU_VLS_INIT_ZERO;
        if (pl->pl_export(pl, &exp) != 0) FAIL("POLYGON pl_export sentinel");
        bu_vls_free(&exp);
        if (pl->pl_backend_prepare(pl, NULL) != 0) FAIL("POLYGON pl_backend_prepare sentinel");
        bsg_obj_put(poly_node);
        bsg_scene_root_destroy(root);
        free_view(v);
    }

    /* MESH */
    {
        struct bsg_payload *pl = bsg_payload_mesh_create(NULL);
        CHECK_SENTINEL_HOOKS(pl, "MESH");
    }

    /* CSG */
    {
        struct bsg_payload *pl = bsg_payload_csg_create(NULL);
        CHECK_SENTINEL_HOOKS(pl, "CSG");
    }

    /* BREP */
    {
        struct bsg_payload *pl = bsg_payload_brep_create(NULL);
        CHECK_SENTINEL_HOOKS(pl, "BREP");
    }

    /* FRAMEBUFFER */
    {
        struct bsg_payload *pl = bsg_payload_framebuffer_create(NULL, 0);
        CHECK_SENTINEL_HOOKS(pl, "FRAMEBUFFER");
    }

    /* AXES */
    {
        struct bsg_axes *axes;
        BU_GET(axes, struct bsg_axes);
        memset(axes, 0, sizeof(*axes));
        struct bsg_payload *pl = bsg_payload_axes_create(axes);
        CHECK_SENTINEL_HOOKS(pl, "AXES");
    }

    /* GRID */
    {
        struct bsg_grid_state gs;
        memset(&gs, 0, sizeof(gs));
        gs.draw = 1;
        struct bsg_payload *pl = bsg_payload_grid_create(&gs);
        CHECK_SENTINEL_HOOKS(pl, "GRID");
    }

    /* ANNOTATION */
    {
        point_t ann_pts[2] = {{0,0,0}, {1,1,0}};
        struct bsg_payload *pl = bsg_payload_annotation_create("check", ann_pts, 2);
        CHECK_SENTINEL_HOOKS(pl, "ANNOTATION");
    }

#undef CHECK_HOOKS
#undef CHECK_SENTINEL_HOOKS

    PASS("lifecycle hooks for remaining payload types");
    return 0;
}

int
main(int argc, char **argv)
{
    bu_setprogname(argv[0]);
    if (argc > 1)
	fprintf(stderr, "Unexpected arguments\n");

    int ret = 0;
    ret |= test_vlist_node_helpers();
    ret |= test_polygon_payload();
    ret |= test_remaining_payload_builders();
    ret |= test_lifecycle_hooks();
    ret |= test_sketch_live_contract();
    ret |= test_line_set_builders();
    ret |= test_remaining_lifecycle_hooks();

    return ret;
}
