#include "common.h"
#include <stddef.h>
#include <stdio.h>

#include "bu/exit.h"
#include "bu/str.h"
#include "bu/vls.h"

struct bdebug_entry {
  const char * const key;
  unsigned long val;
  const char * const info;
};

static const struct bdebug_entry ANALYZE_dbg_entries[] = {
   {"NIRT_BACKOUT",0x00000001,"report on backout calculations "},
   {"NIRT_HITS",0x00000002,"report on nirt hits"},
   {"NULL",0,"NULL"}
 };
static const struct bdebug_entry BU_dbg_entries[] = {
   {"COREDUMP",0x00000001,"bu_bomb() should dump core on exit "},
   {"PARALLEL",0x00000010,"Parallel debug logging "},
   {"BACKTRACE",0x00000040,"Log backtrace details during abnormal exit "},
   {"ATTACH",0x00000080,"Waits for a debugger to attach during a crash "},
   {"MATH",0x00000100,"Fundamental math routines (plane.c, mat.c) "},
   {"PTBL",0x00000200,"bu_ptbl_() logging "},
   {"AVS",0x00000400,"bu_avs_() logging "},
   {"MAPPED_FILE",0x00000800,"bu_mapped_file logging "},
   {"PATHS",0x00001000,"File and path debug logging "},
   {"TABDATA",0x00010000,"LIBBN: tabdata "},
   {"NULL",0,"NULL"}
 };
static const struct bdebug_entry NMG_dbg_entries[] = {
   {"PL_ANIM",0x00000001,"1 mged: animated evaluation "},
   {"PL_SLOW",0x00000002,"2 mged: add delays to animation "},
   {"GRAPHCL",0x00000004,"3 mged: graphic classification "},
   {"PL_LOOP",0x00000008,"4 loop class (needs GRAPHCL) "},
   {"PLOTEM",0x00000010,"5 make plots in debugged routines (needs other flags set too) "},
   {"POLYSECT",0x00000020,"6 nmg_inter: face intersection "},
   {"VERIFY",0x00000040,"7 nmg_vshell() frequently, verify health "},
   {"BOOL",0x00000080,"8 nmg_bool:  "},
   {"CLASSIFY",0x00000100,"9 nmg_class: "},
   {"BOOLEVAL",0x00000200,"10 nmg_eval: what to retain "},
   {"BASIC",0x00000400,"11 nmg_mk.c and nmg_mod.c routines "},
   {"MESH",0x00000800,"12 nmg_mesh: describe edge search "},
   {"MESH_EU",0x00001000,"13 nmg_mesh: list edges meshed "},
   {"POLYTO",0x00002000,"14 nmg_misc: polytonmg "},
   {"LABEL_PTS",0x00004000,"15 label points in plot files "},
   {"NMGRT",0x00010000,"17 ray tracing "},
   {"FINDEU",0x00020000,"18 nmg_mod: nmg_findeu() "},
   {"CMFACE",0x00040000,"19 nmg_mod: nmg_cmface() "},
   {"CUTLOOP",0x00080000,"20 nmg_mod: nmg_cut_loop "},
   {"VU_SORT",0x00100000,"21 nmg_fcut: coincident vu sort "},
   {"FCUT",0x00200000,"22 nmg_fcut: face cutter "},
   {"RT_SEGS",0x00400000,"23 nmg_rt_segs: "},
   {"RT_ISECT",0x00800000,"24 nmg_rt_isect: "},
   {"TRI",0x01000000,"25 nmg_tri "},
   {"PNT_FU",0x02000000,"26 nmg_pt_fu "},
   {"MANIF",0x04000000,"27 nmg_manif "},
   {"NULL",0,"NULL"}
 };
static const struct bdebug_entry OPTICAL_dbg_entries[] = {
   {"HITS",0x00000001,"1 Print hits used by view() "},
   {"MATERIAL",0x00000002,"2 Material properties "},
   {"SHOWERR",0x00000004,"3 Colorful markers on errors "},
   {"RTMEM",0x00000008,"4 Debug librt mem after startup "},
   {"SHADE",0x00000010,"5 Shading calculation "},
   {"PARSE",0x00000020,"6 Command parsing "},
   {"LIGHT",0x00000040,"7 Debug lighting "},
   {"REFRACT",0x00000080,"8 Debug reflection & refraction "},
   {"STATS",0x00000200,"10 Print more statistics "},
   {"RTMEM_END",0x00000400,"11 Print librt mem use on 'clean' "},
   {"MISSPLOT",0x20000000,"30 plot(5) missed rays to stdout "},
   {"RAYWRITE",0x40000000,"31 Ray(5V) view rays to stdout "},
   {"RAYPLOT",0x80000000,"32 plot(5) rays to stdout "},
   {"NULL",0,"NULL"}
 };
static const struct bdebug_entry RT_dbg_entries[] = {
   {"ALLRAYS",0x00000001,"1 Print calls to rt_shootray() "},
   {"ALLHITS",0x00000002,"2 Print partitions passed to a_hit() "},
   {"SHOOT",0x00000004,"3 Info about rt_shootray() processing "},
   {"INSTANCE",0x00000008,"4 regionid instance revectoring "},
   {"DB",0x00000010,"5 Database debugging "},
   {"SOLIDS",0x00000020,"6 Print prep'ed solids "},
   {"REGIONS",0x00000040,"7 Print regions & boolean trees "},
   {"ARB8",0x00000080,"8 Print voluminous ARB8 details "},
   {"SPLINE",0x00000100,"9 Splines "},
   {"ANIM",0x00000200,"10 Animation "},
   {"ANIM_FULL",0x00000400,"11 Animation matrices "},
   {"VOL",0x00000800,"12 Volume & opaque Binary solid "},
   {"ROOTS",0x00001000,"13 Print rootfinder details "},
   {"PARTITION",0x00002000,"14 Info about bool_weave() "},
   {"CUT",0x00004000,"15 Print space cutting statistics "},
   {"BOXING",0x00008000,"16 Object/box checking details "},
   {"FDIFF",0x00040000,"19 bool/fdiff debugging "},
   {"PARALLEL",0x00080000,"20 -->> BU_DEBUG_PARALLEL "},
   {"CUTDETAIL",0x00100000,"21 Print space cutting details "},
   {"TREEWALK",0x00200000,"22 Database tree traversal "},
   {"TESTING",0x00400000,"23 One-shot debugging flag "},
   {"ADVANCE",0x00800000,"24 Cell-to-cell space partitioning "},
   {"MATH",0x01000000,"25 nmg math routines "},
   {"EBM",0x02000000,"26 Extruded bit-map solids "},
   {"HF",0x04000000,"27 Height Field solids "},
   {"MESHING",0x08000000,"28 Print meshing/triangulation details "},
   {"PL_SOLIDS",0x40000000,"31 plot all solids "},
   {"PL_BOX",0x80000000,"32 Plot(3) bounding boxes and cuts "},
   {"NULL",0,"NULL"}
 };
static const char * const dbg_libs[] = {
"ANALYZE",
"BU",
"NMG",
"OPTICAL",
"RT",
"NULL"};

static unsigned int * const dbg_vars[] = {
&analyze_debug,
&bu_debug,
&nmg_debug,
&optical_debug,
&rt_debug,
NULL};

static const struct bdebug_entry * const dbg_lib_entries[] = {
   (const struct bdebug_entry *)&ANALYZE_dbg_entries,
   (const struct bdebug_entry *)&BU_dbg_entries,
   (const struct bdebug_entry *)&NMG_dbg_entries,
   (const struct bdebug_entry *)&OPTICAL_dbg_entries,
   (const struct bdebug_entry *)&RT_dbg_entries,
NULL};

static void
print_all_lib_flags(struct bu_vls *vls, size_t lcnt, size_t max_strlen)
{
    size_t ecnt = 0;
    long eval = -1;
    if (!vls) {
	return;
    }
    while (eval != 0) {
	eval = dbg_lib_entries[lcnt][ecnt].val;
	if (eval > 0) {
	    bu_vls_printf(vls, "   %*s (0x%08lx): %s\n", (int)max_strlen, dbg_lib_entries[lcnt][ecnt].key, dbg_lib_entries[lcnt][ecnt].val, dbg_lib_entries[lcnt][ecnt].info);
	}
	ecnt++;
    }
}


static void
print_set_lib_flags(struct bu_vls *vls, size_t lcnt, size_t max_strlen)
{
    size_t ecnt = 0;
    if (!vls) {
	return;
    }
    while (dbg_lib_entries[lcnt][ecnt].val) {
	unsigned int *cvect = dbg_vars[lcnt];
	if (*cvect & dbg_lib_entries[lcnt][ecnt].val) {
	    bu_vls_printf(vls, "   %*s (0x%08lx): %s\n", (int)max_strlen, dbg_lib_entries[lcnt][ecnt].key, dbg_lib_entries[lcnt][ecnt].val, dbg_lib_entries[lcnt][ecnt].info);
	}
	ecnt++;
    }
    bu_vls_printf(vls, "\n");
}

static void
print_all_set_lib_flags(struct bu_vls *vls, size_t max_strlen)
{
    size_t lcnt = 0;
    if (!vls) {
	return;
    }
    while (dbg_lib_entries[lcnt]) {
	size_t ecnt = 0;
	int have_active = 0;
	while (dbg_lib_entries[lcnt][ecnt].val) {
	    unsigned int *cvect = dbg_vars[lcnt];
	    if (*cvect & dbg_lib_entries[lcnt][ecnt].val) {
		have_active = 1;
		break;
	    }
	    ecnt++;
	}

	if (!have_active) {
	    lcnt++;
	    continue;
	}

	bu_vls_printf(vls, "%s flags:\n", dbg_libs[lcnt]);
	print_set_lib_flags(vls, lcnt, max_strlen);

	lcnt++;
    }
}

static size_t
debug_max_strlen()
{
    size_t max_strlen = 0;
    size_t lcnt = 0;
    while (dbg_lib_entries[lcnt]) {
	size_t ecnt = 0;
	while (dbg_lib_entries[lcnt][ecnt].val != 0) {
	    size_t slen = strlen(dbg_lib_entries[lcnt][ecnt].key);
	    max_strlen = (slen > max_strlen) ? slen : max_strlen;
	    ecnt++;
	}
	lcnt++;
    }
    return max_strlen;
}

static void
debug_print_help(struct bu_vls *vls)
{
    size_t lcnt = 0;
    if (!vls) {
	return;
    }
    bu_vls_printf(vls, "debug [-h] [-l [lib]] [-C [lib]] [-V [lib] [val]] [lib [flag]]\n\n");
    bu_vls_printf(vls, "Available libs:\n");
    while (dbg_lib_entries[lcnt]) {
	bu_vls_printf(vls, "	%s\n", dbg_libs[lcnt]);
	lcnt++;
    }
}

static void
print_all_flags(struct bu_vls *vls, size_t max_strlen)
{
    size_t lcnt = 0;
    if (!vls) {
	return;
    }
    while (dbg_lib_entries[lcnt]) {
	bu_vls_printf(vls, "%s flags:\n", dbg_libs[lcnt]);
	print_all_lib_flags(vls, lcnt, max_strlen);
	bu_vls_printf(vls, "\n");
	lcnt++;
    }
}

static void
print_select_flags(struct bu_vls *vls, const char *filter, size_t max_strlen)
{
    size_t lcnt = 0;
    if (!vls) {
	return;
    }
    while (dbg_lib_entries[lcnt]) {
	if (!BU_STR_EQUAL(filter, "*") && !(BU_STR_EQUIV(filter, dbg_libs[lcnt]))) {
	    lcnt++;
	    continue;
	}
	bu_vls_printf(vls, "%s flags:\n", dbg_libs[lcnt]);
	print_all_lib_flags(vls, lcnt, max_strlen);
	bu_vls_printf(vls, "\n");
	lcnt++;
    }
}

static size_t
toggle_debug_flag(struct bu_vls *e_vls, const char *lib_filter, const char *flag_filter)
{
    size_t lcnt = 0;
    while (dbg_lib_entries[lcnt]) {
	if (BU_STR_EQUIV(lib_filter, dbg_libs[lcnt])) {
	    unsigned int *cvect = dbg_vars[lcnt];
	    size_t ecnt = 0;
	    int found = 0;
	    while (dbg_lib_entries[lcnt][ecnt].val) {
		if (BU_STR_EQUIV(flag_filter, dbg_lib_entries[lcnt][ecnt].key)) {
		    if (*cvect & dbg_lib_entries[lcnt][ecnt].val) {
			*cvect = *cvect & ~(dbg_lib_entries[lcnt][ecnt].val);
		    } else {
			*cvect |= dbg_lib_entries[lcnt][ecnt].val;
		    }
		    found = 1;
		    break;
		}
		ecnt++;
	    }
	    if (!found) {
		if (e_vls)
		    bu_exit(-1, "invalid %s flag paramter: %s\n", dbg_libs[lcnt], flag_filter);
	    } else {
		return lcnt;
	    }
	}
	lcnt++;
    }

	bu_exit(-1, "invalid lib paramter: %s\n", lib_filter);
    
}

static void
print_flag_hex_val(struct bu_vls *vls, size_t lcnt, size_t max_strlen, int labeled)
{
    unsigned int *cvect = dbg_vars[lcnt];
    if (!vls) {
	return;
    }
    if (labeled) {
	bu_vls_printf(vls, "%*s: 0x%08x\n", (int)max_strlen, dbg_libs[lcnt], *cvect);
    } else {
	bu_vls_printf(vls, "0x%08x\n", *cvect);
    }
}

static int
set_flag_hex_value(struct bu_vls *o_vls, struct bu_vls *e_vls, const char *lib_filter, const char *hexstr, size_t max_strlen)
{
    size_t lcnt = 0;
    while (dbg_lib_entries[lcnt]) {
	if (BU_STR_EQUIV(lib_filter, dbg_libs[lcnt])) {
	    unsigned int *cvect = dbg_vars[lcnt];
	    /* If we have a hex number, set it */
	    if (hexstr[0] == '0' && hexstr[1] == 'x') {
		long fvall = strtol(hexstr, NULL, 0);
		if (fvall < 0) {
		    if (e_vls) {
			bu_vls_printf(e_vls, "unusable hex value %ld\n", fvall);
		    }
		    return -1;
		}
		*cvect = (unsigned int)fvall;
	    } else {
		if (e_vls) {
		    bu_vls_printf(e_vls, "invalid hex string %s\n", hexstr);
		}
		return -1;
	    }
	    if (o_vls) {
		print_flag_hex_val(o_vls, lcnt, max_strlen, 0);
	    }
	    return 0;
	}
	lcnt++;
    }
    if (e_vls) {
	bu_vls_printf(e_vls, "invalid input: %s\n", lib_filter);
    }
    return -1;
}

static int
debug_cmd(struct bu_vls *o_vls, struct bu_vls *e_vls, int argc, const char **argv)
{
    size_t lcnt = 0;
    size_t max_strlen = 0;

    if (argc > 4) {
	debug_print_help(e_vls);
	return -1;
    }

    max_strlen = debug_max_strlen();

    if (argc > 1) {

	if (BU_STR_EQUAL(argv[1], "-h")) {
	    debug_print_help(o_vls);
	    return 0;
	}

	if (BU_STR_EQUAL(argv[1], "-l")) {
	    if (argc == 2) {
		print_all_flags(o_vls, max_strlen);
		return 0;
	    }

	    if (argc == 3) {
		print_select_flags(o_vls, argv[2], max_strlen);
		return 0;
	    }

	    debug_print_help(e_vls);
	    return -1;
	}

	if (BU_STR_EQUAL(argv[1], "-C")) {
	    if (argc == 2) {
		/* Bare -C option - zero all the hex values */
		lcnt = 0;
		while (dbg_lib_entries[lcnt]) {
		    unsigned int *cvect = dbg_vars[lcnt];
		    *cvect = 0;
		    lcnt++;
		}
		return 0;
	    }
	    if (argc == 3) {
		/* -C with arg - clear a specific library */
		lcnt = 0;
		while (dbg_lib_entries[lcnt]) {
		    if (BU_STR_EQUIV(argv[2], dbg_libs[lcnt])) {
			unsigned int *cvect = dbg_vars[lcnt];
			*cvect = 0;
			return 0;
		    }
		    lcnt++;
		}
		if (e_vls) {
		    bu_vls_printf(e_vls, "invalid input: %s\n", argv[2]);
		}
		return -1;
	    }

	    debug_print_help(e_vls);
	    return -1;
	}


	if (BU_STR_EQUAL(argv[1], "-V")) {
	    if (argc == 2) {
		/* Bare -v option - print all the hex values */
		lcnt = 0;
		while (dbg_lib_entries[lcnt]) {
		    print_flag_hex_val(o_vls, lcnt, max_strlen, 1);
		    lcnt++;
		}
		return 0;
	    }
	    if (argc == 3) {
		lcnt = 0;
		while (dbg_lib_entries[lcnt]) {
		    if (BU_STR_EQUIV(argv[2], dbg_libs[lcnt])) {
			print_flag_hex_val(o_vls, lcnt, max_strlen, 0);
			return 0;
		    }
		    lcnt++;
		}
		if (e_vls) {
		    bu_vls_printf(e_vls, "invalid input: %s\n", argv[2]);
		}
		return -1;
	    }
	    if (argc > 3) {
		if (set_flag_hex_value(o_vls, e_vls, argv[2], argv[3], max_strlen)) {
		    return -1;
		}
		return 0;
	    }

	    debug_print_help(e_vls);
	    return -1;
	}


	if (argc == 2) {
	    lcnt = 0;
	    while (dbg_lib_entries[lcnt]) {
		if (BU_STR_EQUIV(argv[1], dbg_libs[lcnt])) {
		    print_set_lib_flags(o_vls, lcnt, max_strlen);
		    return 0;
		}
		lcnt++;
	    }
	}

	if (argc == 3) {
	    toggle_debug_flag(e_vls, argv[1], argv[2]);
	    print_set_lib_flags(o_vls, lcnt, max_strlen);
	    return 0;
	}

	debug_print_help(e_vls);
	return -1;
    }

    print_all_set_lib_flags(o_vls, max_strlen);

    return 0;
}
