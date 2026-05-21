#include "common.h"

#include "bu/opt.h"
#include "bu/str.h"
#include "ged.h"

#include "./ged_private.h"

namespace {

static int open_force_create = 0;
static int open_flip_endian = 0;
static int open_help = 0;
static int open_only = 0;

static int draw_help = 0;
static int draw_mode = 0;
static int draw_wireframe = 0;
static int draw_shaded = 0;
static int draw_shaded_all = 0;
static int draw_evaluate = 0;
static int draw_hidden_line = 0;
static int draw_add_mode = 0;
static fastf_t draw_transparency = 0.0;
static int draw_bot_threshold = 0;
static int draw_no_subtract = 0;
static int draw_no_dash = 0;
static struct bu_color draw_color = BU_COLOR_INIT_ZERO;
static int draw_line_width = 0;
static int draw_no_autoview = 0;
static struct bu_vls draw_view_name = BU_VLS_INIT_ZERO;

static int ls_help = 0;
static int ls_all = 0;
static int ls_combs = 0;
static int ls_regions = 0;
static int ls_primitives = 0;
static int ls_quiet = 0;
static int ls_long = 0;
static int ls_human = 0;
static int ls_sort = 0;
static int ls_attr = 0;
static int ls_or = 0;

static struct bu_vls erase_view_name = BU_VLS_INIT_ZERO;
static int erase_mode = 0;

static int who_help = 0;
static struct bu_vls who_view_name = BU_VLS_INIT_ZERO;
static int who_mode = 0;
static int who_expand = 0;
static int who_solids_help = 0;
static struct bu_vls who_solids_view_name = BU_VLS_INIT_ZERO;
static int who_solids_mode = 0;

static int select_help = 0;
static struct bu_vls select_set = BU_VLS_INIT_ZERO;

static int view_help = 0;
static int view_verbose = 0;
static struct bu_vls view_name = BU_VLS_INIT_ZERO;

static int dm_help = 0;
static int dm_verbose = 0;

static const struct bu_opt_operand_desc raw_args_operands[] = {
    {"args", BU_OPT_VAL_RAW, 0, BU_OPT_COUNT_UNLIMITED, "Additional subcommand arguments", NULL},
    BU_OPT_OPERAND_DESC_NULL
};

static const struct bu_opt_operand_desc open_operands[] = {
    {"filename", BU_OPT_VAL_FILE_PATH, 1, 1, "Database filename", NULL},
    BU_OPT_OPERAND_DESC_NULL
};

static const struct bu_opt_desc open_opts[] = {
    {"c", "create", "", NULL, (void *)&open_force_create, "Creates a new database if not already extant (default)."},
    {"f", "flip-endian", "", NULL, (void *)&open_flip_endian, "Opens file as a binary-incompatible v4 geometry database."},
    {"h", "help", "", NULL, (void *)&open_help, "Print help."},
    {"o", "open", "", NULL, (void *)&open_only, "Do not force creation of non-extant database.  Overridden by -c."},
    BU_OPT_DESC_NULL
};

static const struct bu_opt_cmd_desc open_cmd = {
    "open", "Open a geometry database", open_opts, NULL, open_operands, NULL, NULL
};

static const struct bu_opt_cmd_desc opendb_cmd = {
    "opendb", "Open a geometry database", open_opts, NULL, open_operands, NULL, NULL
};

static const struct bu_opt_cmd_desc reopen_cmd = {
    "reopen", "Reopen a geometry database", NULL, NULL, open_operands, NULL, NULL
};

static const struct bu_opt_operand_desc draw_operands[] = {
    {"path", BU_OPT_VAL_DB_PATH, 1, BU_OPT_COUNT_UNLIMITED, "Database object path to draw", NULL},
    BU_OPT_OPERAND_DESC_NULL
};

static const struct bu_opt_desc draw_opts[] = {
    {"V", "view", "name", &bu_opt_vls, (void *)&draw_view_name, "Specify view to draw on"},
    {"h", "help", "", NULL, (void *)&draw_help, "Print help and exit"},
    {"?", "", "", NULL, (void *)&draw_help, ""},
    {"m", "mode", "#", &bu_opt_int, (void *)&draw_mode, "0=wireframe;1=shaded bots;2=shaded;3=evaluated"},
    {"", "wireframe", "", NULL, (void *)&draw_wireframe, "Draw using only wireframes (mode = 0)"},
    {"", "shaded", "", NULL, (void *)&draw_shaded, "Shade bots, breps and polysolids (mode = 1)"},
    {"", "shaded-all", "", NULL, (void *)&draw_shaded_all, "Shade all solids, not evaluated (mode = 2)"},
    {"E", "evaluate", "", NULL, (void *)&draw_evaluate, "Wireframe with evaluate booleans (mode = 3)"},
    {"", "hidden-line", "", NULL, (void *)&draw_hidden_line, "Hidden line wireframes"},
    {"A", "add-mode", "", NULL, (void *)&draw_add_mode, "Don't erase other drawn modes for specified paths"},
    {"t", "transparency", "#", &bu_opt_fastf_t, (void *)&draw_transparency, "Set transparency level in drawing"},
    {"x", "", "#", &bu_opt_fastf_t, (void *)&draw_transparency, ""},
    {"L", "", "#", &bu_opt_int, (void *)&draw_bot_threshold, "Set face count level for drawing bounding boxes instead of BoT triangles"},
    {"S", "no-subtract", "", NULL, (void *)&draw_no_subtract, "Do not draw subtraction solids"},
    {"", "no-dash", "", NULL, (void *)&draw_no_dash, "Use solid lines rather than dashed for subtraction solids"},
    {"C", "color", "r/g/b", &bu_opt_color, (void *)&draw_color, "Override object colors"},
    {"", "line-width", "#", &bu_opt_int, (void *)&draw_line_width, "Override default line width"},
    {"R", "no-autoview", "", NULL, (void *)&draw_no_autoview, "Do not calculate automatic view"},
    BU_OPT_DESC_NULL
};

static const struct bu_opt_cmd_desc draw_cmd = {
    "draw", "Draw database objects", draw_opts, NULL, draw_operands, NULL, NULL
};

static const struct bu_opt_cmd_desc draw_alias_cmd = {
    "e", "Draw database objects", draw_opts, NULL, draw_operands, NULL, NULL
};

static const struct bu_opt_operand_desc ls_operands[] = {
    {"pattern", BU_OPT_VAL_DB_OBJECT, 0, BU_OPT_COUNT_UNLIMITED, "Object name or path pattern", NULL},
    BU_OPT_OPERAND_DESC_NULL
};

static const struct bu_opt_desc ls_opts[] = {
    {"h", "help", "", NULL, (void *)&ls_help, "Print help and exit"},
    {"a", "all", "", NULL, (void *)&ls_all, "Do not ignore static objects."},
    {"c", "combs", "", NULL, (void *)&ls_combs, "List combinations"},
    {"r", "regions", "", NULL, (void *)&ls_regions, "List regions"},
    {"p", "primitives", "", NULL, (void *)&ls_primitives, "List primitives"},
    {"s", "", "", NULL, (void *)&ls_primitives, ""},
    {"q", "quiet", "", NULL, (void *)&ls_quiet, "Suppress informational lookup messages"},
    {"l", "", "", NULL, (void *)&ls_long, "Use long reporting format"},
    {"H", "human-readable", "", NULL, (void *)&ls_human, "Use human readable sizes in long format"},
    {"S", "sort", "", NULL, (void *)&ls_sort, "Sort using object size"},
    {"A", "attributes", "", NULL, (void *)&ls_attr, "List objects matching attribute name/value pairs"},
    {"o", "or", "", NULL, (void *)&ls_or, "In attribute mode, match one or more patterns"},
    BU_OPT_DESC_NULL
};

static const struct bu_opt_cmd_desc ls_cmd = {
    "ls", "List database objects", ls_opts, NULL, ls_operands, NULL, NULL
};

static const struct bu_opt_cmd_desc ls_alias_cmd = {
    "t", "List database objects", ls_opts, NULL, ls_operands, NULL, NULL
};

static const struct bu_opt_operand_desc erase_operands[] = {
    {"path", BU_OPT_VAL_DB_PATH, 1, BU_OPT_COUNT_UNLIMITED, "Database object path to erase", NULL},
    BU_OPT_OPERAND_DESC_NULL
};

static const struct bu_opt_desc erase_opts[] = {
    {"V", "view", "name", &bu_opt_vls, (void *)&erase_view_name, "Specify view to erase from"},
    {"m", "mode", "#", &bu_opt_int, (void *)&erase_mode, "Erase objects drawn in the specified drawing mode"},
    BU_OPT_DESC_NULL
};

static const struct bu_opt_cmd_desc erase_cmd = {
    "erase", "Erase database objects from the scene", erase_opts, NULL, erase_operands, NULL, NULL
};

static const struct bu_opt_cmd_desc erase_alias_cmd = {
    "d", "Erase database objects from the scene", erase_opts, NULL, erase_operands, NULL, NULL
};

static const struct bu_opt_operand_desc who_level_operands[] = {
    {"level", BU_OPT_VAL_INTEGER, 0, 1, "Reporting detail level", NULL},
    BU_OPT_OPERAND_DESC_NULL
};

static const struct bu_opt_desc who_root_opts[] = {
    {"h", "help", "", NULL, (void *)&who_help, "Print help and exit"},
    {"?", "", "", NULL, (void *)&who_help, ""},
    {"V", "view", "name", &bu_opt_vls, (void *)&who_view_name, "Specify view to work with"},
    {"m", "mode", "#", &bu_opt_int, (void *)&who_mode, "Only report paths drawn in the specified drawing mode"},
    {"E", "expand", "", NULL, (void *)&who_expand, "Report individual drawn objects"},
    BU_OPT_DESC_NULL
};

static const struct bu_opt_desc who_solids_opts[] = {
    {"h", "help", "", NULL, (void *)&who_solids_help, "Print help and exit"},
    {"?", "", "", NULL, (void *)&who_solids_help, ""},
    {"V", "view", "name", &bu_opt_vls, (void *)&who_solids_view_name, "Specify view to report"},
    {"m", "mode", "#", &bu_opt_int, (void *)&who_solids_mode, "Only report objects drawn in the specified drawing mode"},
    BU_OPT_DESC_NULL
};

static const struct bu_opt_cmd_desc who_subcommands[] = {
    {"report", "Report drawn solids", who_solids_opts, NULL, who_level_operands, NULL, NULL},
    {"solids", "Report drawn solids", who_solids_opts, NULL, who_level_operands, NULL, NULL},
    BU_OPT_CMD_DESC_NULL
};

static const struct bu_opt_cmd_desc who_cmd = {
    "who", "List drawn paths", who_root_opts, NULL, NULL, who_subcommands, NULL
};

static const struct bu_opt_operand_desc select_set_operands[] = {
    {"set_name_pattern", BU_OPT_VAL_STRING, 0, 1, "Selection set name or pattern", NULL},
    BU_OPT_OPERAND_DESC_NULL
};

static const struct bu_opt_operand_desc select_path_operands[] = {
    {"path", BU_OPT_VAL_DB_PATH, 1, BU_OPT_COUNT_UNLIMITED, "Database path to add or remove", NULL},
    BU_OPT_OPERAND_DESC_NULL
};

static const struct bu_opt_desc select_opts[] = {
    {"h", "help", "", NULL, (void *)&select_help, "Print help"},
    {"S", "set", "name", &bu_opt_vls, (void *)&select_set, "Specify set to operate on"},
    BU_OPT_DESC_NULL
};

static const struct bu_opt_cmd_desc select_subcommands[] = {
    {"add", "Add paths to the active selection set", NULL, NULL, select_path_operands, NULL, NULL},
    {"clear", "Clear one or more selection sets", NULL, NULL, select_set_operands, NULL, NULL},
    {"collapse", "Collapse one or more selection sets", NULL, NULL, select_set_operands, NULL, NULL},
    {"expand", "Expand one or more selection sets", NULL, NULL, select_set_operands, NULL, NULL},
    {"list", "List selection sets or their contents", NULL, NULL, select_set_operands, NULL, NULL},
    {"rm", "Remove paths from the active selection set", NULL, NULL, select_path_operands, NULL, NULL},
    BU_OPT_CMD_DESC_NULL
};

static const struct bu_opt_cmd_desc select_cmd = {
    "select", "Manage selection sets", select_opts, NULL, NULL, select_subcommands, NULL
};

static const struct bu_opt_desc view_opts[] = {
    {"h", "help", "", NULL, (void *)&view_help, "Print help"},
    {"v", "verbose", "", &bu_opt_incr_long, (void *)&view_verbose, "Verbose output"},
    {"V", "view", "name", &bu_opt_vls, (void *)&view_name, "Specify view (default is GED current)"},
    BU_OPT_DESC_NULL
};

static const struct bu_opt_option_overrides view_meta[] = {
    {"v", "verbose",
	BU_OPT_ARG_FLAG, BU_OPT_VAL_BOOL, 1, NULL,
	BU_OPT_OVERRIDE_ARG_REQUIREMENT | BU_OPT_OVERRIDE_ARG_TYPE | BU_OPT_OVERRIDE_REPEAT,
	NULL, NULL, BU_OPT_VAL_UNKNOWN, BU_OPT_OPTION_FLAG_NONE},
    BU_OPT_OPTION_OVERRIDE_NULL
};

static const struct bu_opt_cmd_desc view_subcommands[] = {
    {"ae", "Get or set azimuth, elevation, and twist", NULL, NULL, raw_args_operands, NULL, NULL},
    {"aet", "Get or set azimuth, elevation, and twist", NULL, NULL, raw_args_operands, NULL, NULL},
    {"center", "Get or set the view center", NULL, NULL, raw_args_operands, NULL, NULL},
    {"eye", "Get or set the view eye point", NULL, NULL, raw_args_operands, NULL, NULL},
    {"faceplate", "Manage faceplate view elements", NULL, NULL, raw_args_operands, NULL, NULL},
    {"height", "Get or set the view height", NULL, NULL, raw_args_operands, NULL, NULL},
    {"independent", "Toggle view independence", NULL, NULL, raw_args_operands, NULL, NULL},
    {"knob", "Low level rotate, translate, and scale operations", NULL, NULL, raw_args_operands, NULL, NULL},
    {"list", "List available views", NULL, NULL, raw_args_operands, NULL, NULL},
    {"lod", "Manage level-of-detail settings", NULL, NULL, raw_args_operands, NULL, NULL},
    {"obj", "Manage view objects", NULL, NULL, raw_args_operands, NULL, NULL},
    {"objs", "Manage view objects", NULL, NULL, raw_args_operands, NULL, NULL},
    {"quat", "Get or set the view quaternion", NULL, NULL, raw_args_operands, NULL, NULL},
    {"selections", "Manage view selections", NULL, NULL, raw_args_operands, NULL, NULL},
    {"size", "Get or set the view size", NULL, NULL, raw_args_operands, NULL, NULL},
    {"snap", "Snap the view to a canonical orientation", NULL, NULL, raw_args_operands, NULL, NULL},
    {"vZ", "Report or set scene depth range state", NULL, NULL, raw_args_operands, NULL, NULL},
    {"width", "Get or set the view width", NULL, NULL, raw_args_operands, NULL, NULL},
    {"ypr", "Get or set yaw, pitch, and roll", NULL, NULL, raw_args_operands, NULL, NULL},
    BU_OPT_CMD_DESC_NULL
};

static const struct bu_opt_cmd_desc view_cmd = {
    "view", "Manage views", view_opts, view_meta, NULL, view_subcommands, NULL
};

static const struct bu_opt_cmd_desc view2_cmd = {
    "view2", "Manage views", view_opts, view_meta, NULL, view_subcommands, NULL
};

static const struct bu_opt_cmd_desc view_func_cmd = {
    "view_func", "Manage views", view_opts, view_meta, NULL, view_subcommands, NULL
};

static const struct bu_opt_desc dm_opts[] = {
    {"h", "help", "", NULL, (void *)&dm_help, "Print help"},
    {"v", "verbose", "", &bu_opt_incr_long, (void *)&dm_verbose, "Verbose output"},
    BU_OPT_DESC_NULL
};

static const struct bu_opt_option_overrides dm_meta[] = {
    {"v", "verbose",
	BU_OPT_ARG_FLAG, BU_OPT_VAL_BOOL, 1, NULL,
	BU_OPT_OVERRIDE_ARG_REQUIREMENT | BU_OPT_OVERRIDE_ARG_TYPE | BU_OPT_OVERRIDE_REPEAT,
	NULL, NULL, BU_OPT_VAL_UNKNOWN, BU_OPT_OPTION_FLAG_NONE},
    BU_OPT_OPTION_OVERRIDE_NULL
};

static const struct bu_opt_cmd_desc dm_subcommands[] = {
    {"attach", "Attach a display manager", NULL, NULL, raw_args_operands, NULL, NULL},
    {"bg", "Get or set the display manager background", NULL, NULL, raw_args_operands, NULL, NULL},
    {"debug", "Get or set display manager debugging", NULL, NULL, raw_args_operands, NULL, NULL},
    {"get", "Query display manager settings", NULL, NULL, raw_args_operands, NULL, NULL},
    {"height", "Get display manager height", NULL, NULL, raw_args_operands, NULL, NULL},
    {"initmsg", "Show display manager initialization messages", NULL, NULL, raw_args_operands, NULL, NULL},
    {"list", "List display managers", NULL, NULL, raw_args_operands, NULL, NULL},
    {"set", "Set display manager parameters", NULL, NULL, raw_args_operands, NULL, NULL},
    {"type", "Report display manager type", NULL, NULL, raw_args_operands, NULL, NULL},
    {"types", "List available display manager types", NULL, NULL, raw_args_operands, NULL, NULL},
    {"width", "Get display manager width", NULL, NULL, raw_args_operands, NULL, NULL},
    BU_OPT_CMD_DESC_NULL
};

static const struct bu_opt_cmd_desc dm_cmd = {
    "dm", "Manage display managers", dm_opts, dm_meta, NULL, dm_subcommands, NULL
};

static const struct bu_opt_cmd_desc *
open_schema(const char *cmd)
{
    if (!cmd)
        return NULL;
    if (BU_STR_EQUAL(cmd, "reopen"))
        return &reopen_cmd;
    if (BU_STR_EQUAL(cmd, "opendb"))
        return &opendb_cmd;
    return &open_cmd;
}

static const struct bu_opt_cmd_desc *
draw_schema(const char *cmd)
{
    if (cmd && BU_STR_EQUAL(cmd, "e"))
        return &draw_alias_cmd;
    return &draw_cmd;
}

static const struct bu_opt_cmd_desc *
ls_schema(const char *cmd)
{
    if (cmd && BU_STR_EQUAL(cmd, "t"))
        return &ls_alias_cmd;
    return &ls_cmd;
}

static const struct bu_opt_cmd_desc *
erase_schema(const char *cmd)
{
    if (cmd && BU_STR_EQUAL(cmd, "d"))
        return &erase_alias_cmd;
    return &erase_cmd;
}

static const struct bu_opt_cmd_desc *
view_schema(const char *cmd)
{
    if (cmd && BU_STR_EQUAL(cmd, "view2"))
        return &view2_cmd;
    if (cmd && BU_STR_EQUAL(cmd, "view_func"))
        return &view_func_cmd;
    return &view_cmd;
}

struct ged_schema_binding {
    const char *name;
    const struct bu_opt_cmd_desc *(*lookup)(const char *cmd);
};

static const struct ged_schema_binding ged_schema_bindings[] = {
    {"d", erase_schema},
    {"dm", [](const char *) -> const struct bu_opt_cmd_desc * { return &dm_cmd; }},
    {"draw", draw_schema},
    {"e", draw_schema},
    {"erase", erase_schema},
    {"ls", ls_schema},
    {"open", open_schema},
    {"opendb", open_schema},
    {"reopen", open_schema},
    {"select", [](const char *) -> const struct bu_opt_cmd_desc * { return &select_cmd; }},
    {"t", ls_schema},
    {"view", view_schema},
    {"view2", view_schema},
    {"view_func", view_schema},
    {"who", [](const char *) -> const struct bu_opt_cmd_desc * { return &who_cmd; }},
    {NULL, NULL}
};

}

extern "C" const struct bu_opt_cmd_desc *
_ged_cmd_schema(const char *cmd)
{
    if (!cmd)
        return NULL;

    const char *lookup = cmd;
    if (bu_strncmp(lookup, "_mged_", 6) == 0)
        lookup += 6;

    for (size_t i = 0; ged_schema_bindings[i].name; i++) {
        if (BU_STR_EQUAL(lookup, ged_schema_bindings[i].name))
            return ged_schema_bindings[i].lookup(lookup);
    }

    return NULL;
}
