#include "common.h"
#include <stdlib.h>
#include <string.h>
#include "bu/env.h"
#include "bu/opt.h"
#include "bu/path.h"
#include "bu/str.h"
#include "bu/units.h"
#include "bu/vls.h"

static const char * const env_vars[] = {
  "BRLCAD_EDITOR_CONSOLE",
  "BRLCAD_EDITOR_GUI",
  "BRLCAD_FILE_PATH",
  "BRLCAD_ROOT",
  "BRLEDIT_NO_STTY",
  "BRLEDIT_TERMIO_DEBUG",
  "BU_DIR_CACHE",
  "BU_HEAP_PRINT",
  "BU_MEM_NOCHECK",
  "BV_LOG",
  "ConEmuPID",
  "DISABLE_VT_CONSOLE",
  "DISPLAY",
  "DM_SWRAST",
  "EDITOR",
  "FBSERV_TLS",
  "FBSERV_TOKEN",
  "FB_FILE",
  "GED_EXEC_TIME",
  "GED_MANIFOLD_DEBUG",
  "GED_MAN_CMDLINE_MODE",
  "GED_MAN_LANG_MODE",
  "GED_NO_PLUGIN_SCAN",
  "HOME",
  "ITCL_LIBRARY",
  "ITK_LIBRARY",
  "LIBBU_AFFINITY",
  "LIBGED_MAKE_SKETCH",
  "LIBRT_BOT_MINTIE",
  "LIBRT_BREP_DEBUG",
  "LIBRT_CACHE",
  "LIBRT_DEBUG",
  "LIBRT_USE_COMB_INSTANCE_SPECIFIERS",
  "LIBRT_V4FLIP",
  "LIB_PKG_DEBUG",
  "MSYSTEM",
  "PATH",
  "PROJ_LIB",
  "PWD",
  "QTCAD_REPORT_EVENTS",
  "QTCAD_REPORT_SLOTS",
  "RT_SETUP_DEBUG",
  "TCLCAD_LIBRARY_PATH",
  "TCL_LIBRARY",
  "TERM",
  "VISUAL",
  "XDG_CACHE_HOME",
"NULL"};

static const char * const cad_env_vars[] = {
  "BRLCAD_EDITOR_CONSOLE",
  "BRLCAD_EDITOR_GUI",
  "BRLCAD_FILE_PATH",
  "BRLCAD_ROOT",
  "BRLEDIT_NO_STTY",
  "BRLEDIT_TERMIO_DEBUG",
  "BU_DIR_CACHE",
  "BU_HEAP_PRINT",
  "BU_MEM_NOCHECK",
  "BV_LOG",
  "CLEAN",
  "CLOBBER",
  "CMP",
  "ConEmuPID",
  "DB",
  "DISABLE_VT_CONSOLE",
  "DISPLAY",
  "DM_SWRAST",
  "EDITOR",
  "ELP",
  "FBSERV_TLS",
  "FBSERV_TLS_CERT",
  "FBSERV_TLS_KEY",
  "FBSERV_TOKEN",
  "FB_FILE",
  "GED_EXEC_TIME",
  "GED_MANIFOLD_DEBUG",
  "GED_MAN_CMDLINE_MODE",
  "GED_MAN_LANG_MODE",
  "GED_NO_PLUGIN_SCAN",
  "HELP",
  "HOME",
  "INSTRUCTIONS",
  "ITCL_LIBRARY",
  "ITK_LIBRARY",
  "LANG",
  "LC_CTYPE",
  "LIBBU_AFFINITY",
  "LIBGED_MAKE_SKETCH",
  "LIBRT_BOT_MINTIE",
  "LIBRT_BREP_DEBUG",
  "LIBRT_CACHE",
  "LIBRT_DEBUG",
  "LIBRT_EXP_MODE",
  "LIBRT_RAND_MODE",
  "LIBRT_USE_COMB_INSTANCE_SPECIFIERS",
  "LIBRT_V4FLIP",
  "LIB_PKG_DEBUG",
  "LOG",
  "LOGFILE",
  "MAXTIME",
  "MSYSTEM",
  "PATH",
  "PIX",
  "PROJ_LIB",
  "PWD",
  "QTCAD_REPORT_EVENTS",
  "QTCAD_REPORT_SLOTS",
  "QUIET",
  "REMRT_TLS_CERT",
  "REMRT_TLS_KEY",
  "RT",
  "RTWIZARD_RCFILE",
  "RT_SETUP_DEBUG",
  "RUN",
  "TCLCAD_LIBRARY_PATH",
  "TCL_LIBRARY",
  "TERM",
  "TIMEFRAME",
  "VERBOSE",
  "VISUAL",
  "XDG_CACHE_HOME",
"NULL"};

struct envcmd_entry {
	const char * const key;
	const char * const var;
};

static const struct envcmd_entry other_vars[] = {
	{"NULL", "NULL"}
};

static const struct envcmd_entry lib_vars[] = {
	{"","QTCAD_REPORT_EVENTS"},
	{"","QTCAD_REPORT_SLOTS"},
	{"bu","BRLCAD_EDITOR_CONSOLE"},
	{"bu","BRLCAD_EDITOR_GUI"},
	{"bu","BRLCAD_ROOT"},
	{"bu","BU_DIR_CACHE"},
	{"bu","BU_HEAP_PRINT"},
	{"bu","BU_MEM_NOCHECK"},
	{"bu","EDITOR"},
	{"bu","HOME"},
	{"bu","LIBBU_AFFINITY"},
	{"bu","PATH"},
	{"bu","PWD"},
	{"bu","VISUAL"},
	{"bu","XDG_CACHE_HOME"},
	{"bv","BV_LOG"},
	{"dm","DISPLAY"},
	{"dm","DM_SWRAST"},
	{"dm","FBSERV_TLS"},
	{"dm","FBSERV_TOKEN"},
	{"dm","FB_FILE"},
	{"gcv","PROJ_LIB"},
	{"ged","GED_EXEC_TIME"},
	{"ged","GED_MANIFOLD_DEBUG"},
	{"ged","GED_MAN_CMDLINE_MODE"},
	{"ged","GED_MAN_LANG_MODE"},
	{"ged","GED_NO_PLUGIN_SCAN"},
	{"ged","LIBGED_MAKE_SKETCH"},
	{"optical","RT_SETUP_DEBUG"},
	{"pkg","LIB_PKG_DEBUG"},
	{"rt","BRLCAD_FILE_PATH"},
	{"rt","LIBRT_BOT_MINTIE"},
	{"rt","LIBRT_BREP_DEBUG"},
	{"rt","LIBRT_CACHE"},
	{"rt","LIBRT_DEBUG"},
	{"rt","LIBRT_USE_COMB_INSTANCE_SPECIFIERS"},
	{"rt","LIBRT_V4FLIP"},
	{"tclcad","ITCL_LIBRARY"},
	{"tclcad","ITK_LIBRARY"},
	{"tclcad","TCLCAD_LIBRARY_PATH"},
	{"tclcad","TCL_LIBRARY"},
	{"termio","BRLEDIT_NO_STTY"},
	{"termio","BRLEDIT_TERMIO_DEBUG"},
	{"termio","ConEmuPID"},
	{"termio","DISABLE_VT_CONSOLE"},
	{"termio","MSYSTEM"},
	{"termio","TERM"},
	{"NULL", "NULL"}
};

static const struct envcmd_entry exe_vars[] = {
	{"bench","CLEAN"},
	{"bench","CLOBBER"},
	{"bench","CMP"},
	{"bench","DB"},
	{"bench","ELP"},
	{"bench","HELP"},
	{"bench","INSTRUCTIONS"},
	{"bench","LOG"},
	{"bench","LOGFILE"},
	{"bench","MAXTIME"},
	{"bench","PIX"},
	{"bench","QUIET"},
	{"bench","RT"},
	{"bench","RUN"},
	{"bench","TIMEFRAME"},
	{"bench","VERBOSE"},
	{"brlman","LANG"},
	{"fb","FB_FILE"},
	{"fbserv","FBSERV_TLS"},
	{"fbserv","FBSERV_TLS_CERT"},
	{"fbserv","FBSERV_TLS_KEY"},
	{"fbserv","FBSERV_TOKEN"},
	{"gtools","LANG"},
	{"gtools","LC_CTYPE"},
	{"mged","DISPLAY"},
	{"mged","HOME"},
	{"remrt","REMRT_TLS_CERT"},
	{"remrt","REMRT_TLS_KEY"},
	{"rt","LIBRT_EXP_MODE"},
	{"rt","LIBRT_RAND_MODE"},
	{"rtwizard","HOME"},
	{"rtwizard","RTWIZARD_RCFILE"},
	{"NULL", "NULL"}
};

static const char * const _vars[] = {
	"QTCAD_REPORT_EVENTS",
	"QTCAD_REPORT_SLOTS",
	NULL};
static const char * const bench_vars[] = {
	"CLEAN",
	"CLOBBER",
	"CMP",
	"DB",
	"ELP",
	"HELP",
	"INSTRUCTIONS",
	"LOG",
	"LOGFILE",
	"MAXTIME",
	"PIX",
	"QUIET",
	"RT",
	"RUN",
	"TIMEFRAME",
	"VERBOSE",
	NULL};
static const char * const brlman_vars[] = {
	"LANG",
	NULL};
static const char * const bu_vars[] = {
	"BRLCAD_EDITOR_CONSOLE",
	"BRLCAD_EDITOR_GUI",
	"BRLCAD_ROOT",
	"BU_DIR_CACHE",
	"BU_HEAP_PRINT",
	"BU_MEM_NOCHECK",
	"EDITOR",
	"HOME",
	"LIBBU_AFFINITY",
	"PATH",
	"PWD",
	"VISUAL",
	"XDG_CACHE_HOME",
	NULL};
static const char * const bv_vars[] = {
	"BV_LOG",
	NULL};
static const char * const dm_vars[] = {
	"DISPLAY",
	"DM_SWRAST",
	"FBSERV_TLS",
	"FBSERV_TOKEN",
	"FB_FILE",
	NULL};
static const char * const fb_vars[] = {
	"FB_FILE",
	NULL};
static const char * const fbserv_vars[] = {
	"FBSERV_TLS",
	"FBSERV_TLS_CERT",
	"FBSERV_TLS_KEY",
	"FBSERV_TOKEN",
	NULL};
static const char * const gcv_vars[] = {
	"PROJ_LIB",
	NULL};
static const char * const ged_vars[] = {
	"GED_EXEC_TIME",
	"GED_MANIFOLD_DEBUG",
	"GED_MAN_CMDLINE_MODE",
	"GED_MAN_LANG_MODE",
	"GED_NO_PLUGIN_SCAN",
	"LIBGED_MAKE_SKETCH",
	NULL};
static const char * const gtools_vars[] = {
	"LANG",
	"LC_CTYPE",
	NULL};
static const char * const mged_vars[] = {
	"DISPLAY",
	"HOME",
	NULL};
static const char * const optical_vars[] = {
	"RT_SETUP_DEBUG",
	NULL};
static const char * const pkg_vars[] = {
	"LIB_PKG_DEBUG",
	NULL};
static const char * const remrt_vars[] = {
	"REMRT_TLS_CERT",
	"REMRT_TLS_KEY",
	NULL};
static const char * const rt_vars[] = {
	"BRLCAD_FILE_PATH",
	"LIBRT_BOT_MINTIE",
	"LIBRT_BREP_DEBUG",
	"LIBRT_CACHE",
	"LIBRT_DEBUG",
	"LIBRT_EXP_MODE",
	"LIBRT_RAND_MODE",
	"LIBRT_USE_COMB_INSTANCE_SPECIFIERS",
	"LIBRT_V4FLIP",
	NULL};
static const char * const rtwizard_vars[] = {
	"HOME",
	"RTWIZARD_RCFILE",
	NULL};
static const char * const tclcad_vars[] = {
	"ITCL_LIBRARY",
	"ITK_LIBRARY",
	"TCLCAD_LIBRARY_PATH",
	"TCL_LIBRARY",
	NULL};
static const char * const termio_vars[] = {
	"BRLEDIT_NO_STTY",
	"BRLEDIT_TERMIO_DEBUG",
	"ConEmuPID",
	"DISABLE_VT_CONSOLE",
	"MSYSTEM",
	"TERM",
	NULL};
struct env_context_entry {
	const char * const key;
	const char * const * vars;
};

static const struct env_context_entry context_vars[] = {
	{"", (const char * const *)&_vars},
	{"bench", (const char * const *)&bench_vars},
	{"brlman", (const char * const *)&brlman_vars},
	{"bu", (const char * const *)&bu_vars},
	{"bv", (const char * const *)&bv_vars},
	{"dm", (const char * const *)&dm_vars},
	{"fb", (const char * const *)&fb_vars},
	{"fbserv", (const char * const *)&fbserv_vars},
	{"gcv", (const char * const *)&gcv_vars},
	{"ged", (const char * const *)&ged_vars},
	{"gtools", (const char * const *)&gtools_vars},
	{"mged", (const char * const *)&mged_vars},
	{"optical", (const char * const *)&optical_vars},
	{"pkg", (const char * const *)&pkg_vars},
	{"remrt", (const char * const *)&remrt_vars},
	{"rt", (const char * const *)&rt_vars},
	{"rtwizard", (const char * const *)&rtwizard_vars},
	{"tclcad", (const char * const *)&tclcad_vars},
	{"termio", (const char * const *)&termio_vars},
{"NULL", NULL}
};
static const char *
validate_env(const char *var)
{
	int i = 0;
	while (!BU_STR_EQUAL(env_vars[i], "NULL")) {
		if (BU_STR_EQUAL(env_vars[i], var)) return var;
		i++;
	}
	return NULL;
}

static void
list_env(struct bu_vls *vl, const char *pattern, int list_all, int include_system, int include_context)
{
	int i = 0;
	if (!include_context) {
		if (include_system) {
			while (!BU_STR_EQUAL(env_vars[i], "NULL")) {
				if (!bu_path_match(pattern, env_vars[i], 0)) {
					char *evval = getenv(env_vars[i]);
					if (!list_all && !evval) {
						i++;
						continue;
					}
					bu_vls_printf(vl, "%s=%s\n", env_vars[i], evval);
				}
				i++;
			}
			return;
		}
		while (!BU_STR_EQUAL(cad_env_vars[i], "NULL")) {
			if (!bu_path_match(pattern, cad_env_vars[i], 0)) {
				char *evval = getenv(cad_env_vars[i]);
				if (!list_all && !evval) {
					i++;
					continue;
				}
				bu_vls_printf(vl, "%s=%s\n", cad_env_vars[i], evval);
			}
			i++;
		}
		return;
	}

	if (include_system) {
		while (!BU_STR_EQUAL(other_vars[i].key, "NULL")) {
			if (!bu_path_match(pattern, other_vars[i].var, 0)) {
				char *evval = getenv(other_vars[i].var);
				if (!list_all && !evval) {
					i++;
					continue;
				}
				bu_vls_printf(vl, "[%s] %s=%s\n", other_vars[i].key, other_vars[i].var, evval);
			}
			i++;
		}
	}
	i = 0;
	while (!BU_STR_EQUAL(lib_vars[i].key, "NULL")) {
		if (!bu_path_match(pattern, lib_vars[i].var, 0)) {
			char *evval = getenv(lib_vars[i].var);
			if (!list_all && !evval) {
				i++;
				continue;
			}
			bu_vls_printf(vl, "[lib%s] %s=%s\n", lib_vars[i].key, lib_vars[i].var, evval);
		}
		i++;
	}

	i = 0;
	while (!BU_STR_EQUAL(exe_vars[i].key, "NULL")) {
		if (!bu_path_match(pattern, exe_vars[i].var, 0)) {
			char *evval = getenv(exe_vars[i].var);
			if (!list_all && !evval) {
				i++;
				continue;
			}
			bu_vls_printf(vl, "[%s] %s=%s\n", exe_vars[i].key, exe_vars[i].var, evval);
		}
		i++;
	}
}

static int
env_cmd(struct bu_vls *s_out, int argc, const char **argv)
{
	int print_help = 0;
	int list_all = 0;
	int include_system = 0;
	int include_context = 0;
	int report_mem = 0;

	static const char *usage1 = "Usage: env [-hA] [pattern]\n";
	static const char *usage2 = " env get var_name\n";
	static const char *usage3 = " env set var_name var_val\n";

	struct bu_opt_desc d[6];
	BU_OPT(d[0], "h", "help", "", NULL, &print_help, "Print help and exit");
	BU_OPT(d[1], "A", "all",  "", NULL, &list_all,   "List all relevant variables, not just those currently set");
	BU_OPT(d[2], "S", "system",  "", NULL, &include_system,   "Include known variables from external codes, not just BRL-CAD's own variables");
	BU_OPT(d[3], "C", "context",  "", NULL, &include_context,   "When listing variables, identify the library/executable in which they are used");
	BU_OPT(d[4], "M", "memory",  "", NULL, &report_mem,   "Report current memory status");
	BU_OPT_NULL(d[5]);

	/* skip command name argv[0] */
	argc-=(argc>0); argv+=(argc>0);

	/* parse standard options */
	argc = bu_opt_parse(NULL, argc, argv, d);

	if (print_help) {
		bu_vls_printf(s_out, "%s      %s      %s", usage1, usage2, usage3);
		return GED_HELP;
	}

	if (report_mem) {
		size_t all_mem, avail_mem, page_mem;
		int ret = 0;
		ret += bu_mem(BU_MEM_ALL, &all_mem);
		ret += bu_mem(BU_MEM_AVAIL, &avail_mem);
		ret += bu_mem(BU_MEM_PAGE_SIZE, &page_mem);
		if (ret)
			return BRLCAD_ERROR;

		char all_buf[6] = {'\0'};
		char avail_buf[6] = {'\0'};
		char p_buf[6] = {'\0'};
		bu_humanize_number(all_buf, 5, all_mem, "", BU_HN_AUTOSCALE, BU_HN_B | BU_HN_NOSPACE | BU_HN_DECIMAL);
		 bu_humanize_number(avail_buf, 5, avail_mem, "", BU_HN_AUTOSCALE, BU_HN_B | BU_HN_NOSPACE | BU_HN_DECIMAL);
		bu_humanize_number(p_buf, 5, page_mem, "", BU_HN_AUTOSCALE, BU_HN_B | BU_HN_NOSPACE | BU_HN_DECIMAL);

		bu_vls_printf(s_out, "MEM: all: %s(%zu) avail: %s(%zu) page_size: %s(%zu)\n",
		              all_buf, all_mem,
		              avail_buf, avail_mem,
		              p_buf, page_mem);

		 return BRLCAD_OK;
	}

	if (argc) {
		if (BU_STR_EQUAL(argv[0], "get")) {
			if (argc != 2) {
				bu_vls_printf(s_out, "Usage: %s", usage2);
				return BRLCAD_ERROR;
			}
			if (!validate_env(argv[1])) {
				bu_vls_printf(s_out, "unknown environment variable: %s", argv[1]);
				return BRLCAD_ERROR;
			}

			bu_vls_printf(s_out, "%s", getenv(argv[1]));
			return BRLCAD_OK;
		}

		if (BU_STR_EQUAL(argv[0], "set")) {
			if (argc != 3) {
				bu_vls_printf(s_out, "Usage: %s", usage3);
				return BRLCAD_ERROR;
			}
			if (!validate_env(argv[1])) {
				bu_vls_printf(s_out, "unknown environment variable: %s", argv[1]);
				return BRLCAD_ERROR;
			}

			if (bu_setenv(argv[1], argv[2], 1)) {
				bu_vls_printf(s_out, "error when setting variable %s to %s", argv[1], argv[2]);
				return BRLCAD_ERROR;
			} else {
				bu_vls_printf(s_out, "%s", argv[2]);
				return BRLCAD_OK;
			}
		}
	}

	/* Not getting or setting, so we must be listing. */
	if (!argc) {
		list_env(s_out, "*", list_all, include_system, include_context);
	} else {
		int i = 0;
		for (i = 0; i < argc; i++) {
		    list_env(s_out, argv[i], list_all, include_system, include_context);
		}
	}
	return BRLCAD_OK;
}
