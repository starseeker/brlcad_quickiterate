/* Probe to calibrate expected values - not a test, not installed */
#include "common.h"
#include <stdio.h>
#include <math.h>
#include "vmath.h"
#include "bu/app.h"
#include "bu/log.h"
#include "bu/malloc.h"
#include "raytrace.h"
#include "rt/rt_ecmds.h"

int main(int argc, char *argv[]) {
    bu_setprogname(argv[0]);
    (void)argc;

    struct db_i *dbip = db_open_inmem();
    struct rt_wdb *wdbp = wdb_dbopen(dbip, RT_WDB_TYPE_DB_INMEM);

    /* Export TGC to database (wdb_export takes ownership of tgc struct) */
    {
        struct rt_tgc_internal *tgc;
        BU_ALLOC(tgc, struct rt_tgc_internal);
        tgc->magic = RT_TGC_INTERNAL_MAGIC;
        VSET(tgc->v, 5, 3, 10); VSET(tgc->h, 0, 0, 8);
        VSET(tgc->a, 3, 0, 0); VSET(tgc->b, 0, 2, 0);
        VSET(tgc->c, 2, 0, 0); VSET(tgc->d, 0, 1.5, 0);
        wdb_export(wdbp, "tgc", (void *)tgc, ID_TGC, 1.0);
    }
    struct directory *dp = db_lookup(dbip, "tgc", LOOKUP_QUIET);

    /* Get original values from the database */
    struct rt_db_internal orig_intern;
    rt_db_get_internal(&orig_intern, dp, dbip, NULL, &rt_uniresource);
    struct rt_tgc_internal *orig_tgc = (struct rt_tgc_internal *)orig_intern.idb_ptr;
    printf("orig_tgc from db: v=%g,%g,%g h=%g,%g,%g a=%g,%g,%g\n",
           V3ARGS(orig_tgc->v), V3ARGS(orig_tgc->h), V3ARGS(orig_tgc->a));

    struct bn_tol tol = BN_TOL_INIT_TOL;
    struct db_full_path fp;
    db_full_path_init(&fp);
    db_add_node_to_full_path(&fp, dp);

    struct bview *v;
    BU_GET(v, struct bview);
    bv_init(v, NULL);
    VSET(v->gv_aet, 45, 35, 0);
    bv_mat_aet(v);
    v->gv_size = 73.3197; v->gv_isize = 1.0/v->gv_size;
    v->gv_scale = 0.5*v->gv_size;
    bv_update(v);
    bu_vls_sprintf(&v->gv_name, "default");
    v->gv_width = 512; v->gv_height = 512;

    struct rt_edit *s = rt_edit_create(&fp, dbip, &tol, v);
    s->mv_context = 1;
    struct rt_tgc_internal *et = (struct rt_tgc_internal *)s->es_int.idb_ptr;

#define RESET() do { \
    VMOVE(et->v, orig_tgc->v); VMOVE(et->h, orig_tgc->h); \
    VMOVE(et->a, orig_tgc->a); VMOVE(et->b, orig_tgc->b); \
    VMOVE(et->c, orig_tgc->c); VMOVE(et->d, orig_tgc->d); \
    MAT_IDN(s->acc_rot_sol); MAT_IDN(s->incr_change); \
    s->e_inpara = 0; s->es_scale = 0.0; s->mv_context = 1; \
} while (0)

    /* Baseline: direct rotation */
    mat_t R;
    bn_mat_angles(R, 5, 5, 5);
    vect_t h0 = {0, 0, 8};
    vect_t h_rot;
    MAT4X3VEC(h_rot, R, h0);
    printf("Direct R*(0,0,8) = %.17f, %.17f, %.17f  |=%.6f\n",
           h_rot[0], h_rot[1], h_rot[2],
           sqrt(h_rot[0]*h_rot[0]+h_rot[1]*h_rot[1]+h_rot[2]*h_rot[2]));

    /* ECMD_TGC_ROT_H */
    RESET();
    EDOBJ[dp->d_minor_type].ft_set_edit_mode(s, ECMD_TGC_ROT_H);
    s->e_inpara = 3;
    VSET(s->e_para, 5, 5, 5);
    VMOVE(s->e_keypoint, orig_tgc->v);
    printf("  et->h before = %g,%g,%g\n", V3ARGS(et->h));
    rt_edit_process(s);
    printf("ECMD_TGC_ROT_H: h: %.17f, %.17f, %.17f  |h|=%.6f\n",
           V3ARGS(et->h), sqrt(et->h[0]*et->h[0]+et->h[1]*et->h[1]+et->h[2]*et->h[2]));

    /* ECMD_TGC_ROT_AB */
    RESET();
    EDOBJ[dp->d_minor_type].ft_set_edit_mode(s, ECMD_TGC_ROT_AB);
    s->e_inpara = 3;
    VSET(s->e_para, 5, 5, 5);
    VMOVE(s->e_keypoint, orig_tgc->v);
    rt_edit_process(s);
    printf("ECMD_TGC_ROT_AB:\n  a: %.17f, %.17f, %.17f  |a|=%.6f\n",
           V3ARGS(et->a), sqrt(et->a[0]*et->a[0]+et->a[1]*et->a[1]+et->a[2]*et->a[2]));
    printf("  b: %.17f, %.17f, %.17f\n", V3ARGS(et->b));
    printf("  c: %.17f, %.17f, %.17f\n", V3ARGS(et->c));
    printf("  d: %.17f, %.17f, %.17f\n", V3ARGS(et->d));

    /* RT_PARAMS_EDIT_ROT (kp) */
    RESET();
    EDOBJ[dp->d_minor_type].ft_set_edit_mode(s, RT_PARAMS_EDIT_ROT);
    s->e_inpara = 1; VSET(s->e_para, 5, 5, 5);
    s->vp->gv_rotate_about = 'k';
    VMOVE(s->e_keypoint, orig_tgc->v);
    rt_edit_process(s);
    printf("RT_PARAMS_EDIT_ROT (kp):\n  v: %.17f, %.17f, %.17f\n", V3ARGS(et->v));
    printf("  h: %.17f, %.17f, %.17f  |h|=%.6f\n",
           V3ARGS(et->h), sqrt(et->h[0]*et->h[0]+et->h[1]*et->h[1]+et->h[2]*et->h[2]));
    printf("  a: %.17f, %.17f, %.17f  |a|=%.6f\n",
           V3ARGS(et->a), sqrt(et->a[0]*et->a[0]+et->a[1]*et->a[1]+et->a[2]*et->a[2]));
    printf("  b: %.17f, %.17f, %.17f\n", V3ARGS(et->b));
    printf("  c: %.17f, %.17f, %.17f\n", V3ARGS(et->c));
    printf("  d: %.17f, %.17f, %.17f\n", V3ARGS(et->d));

    /* RT_PARAMS_EDIT_TRANS XY */
    RESET();
    VMOVE(s->curr_e_axes_pos, orig_tgc->v);
    EDOBJ[dp->d_minor_type].ft_set_edit_mode(s, RT_PARAMS_EDIT_TRANS);
    vect_t mousevec;
    int xpos = 1482, ypos = 762;
    mousevec[0] = xpos * INV_BV; mousevec[1] = ypos * INV_BV; mousevec[2] = 0;
    (*EDOBJ[dp->d_minor_type].ft_edit_xy)(s, mousevec);
    rt_edit_process(s);
    printf("RT_PARAMS_EDIT_TRANS (xy) v: %.17f, %.17f, %.17f\n", V3ARGS(et->v));

    /* ECMD_TGC_MV_H XY */
    RESET();
    VADD2(s->curr_e_axes_pos, orig_tgc->v, orig_tgc->h);   /* H endpoint */
    EDOBJ[dp->d_minor_type].ft_set_edit_mode(s, ECMD_TGC_MV_H);
    xpos = 500; ypos = -300;
    mousevec[0] = xpos * INV_BV; mousevec[1] = ypos * INV_BV; mousevec[2] = 0;
    (*EDOBJ[dp->d_minor_type].ft_edit_xy)(s, mousevec);
    printf("ECMD_TGC_MV_H (xy) before rt_edit_process: h=%g,%g,%g\n", V3ARGS(et->h));
    rt_edit_process(s);
    printf("ECMD_TGC_MV_H (xy) h: %.17f, %.17f, %.17f\n", V3ARGS(et->h));

    rt_edit_destroy(s);
    db_close(dbip);
    return 0;
}
