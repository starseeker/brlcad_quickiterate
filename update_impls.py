#!/usr/bin/env python3
"""
Comprehensive refactoring script for BRL-CAD:
Remove struct resource * from import/export APIs.
"""

import re
import os

BRLCAD = "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad"

def read_file(path):
    with open(path, 'r', encoding='utf-8', errors='replace') as f:
        return f.read()

def write_file(path, content):
    with open(path, 'w', encoding='utf-8') as f:
        f.write(content)

def subst(content, old, new):
    if old in content:
        return content.replace(old, new)
    return content

# ================================================================
# 1. Update HEADERS
# ================================================================

print("=== Updating Headers ===")

# --- functab.h ---
path = f"{BRLCAD}/include/rt/functab.h"
c = read_file(path)
c = subst(c,
    "    int (*ft_import5)(struct rt_db_internal * /*ip*/,\n"
    "\t\t      const struct bu_external * /*ep*/,\n"
    "\t\t      const mat_t /*mat*/,\n"
    "\t\t      const struct db_i * /*dbip*/,\n"
    "\t\t      struct resource * /*resp*/);\n"
    "#define RTFUNCTAB_FUNC_IMPORT5_CAST(_func) ((int (*)(struct rt_db_internal *, const struct bu_external *, const mat_t, const struct db_i *, struct resource *))((void (*)(void))_func))",
    "    int (*ft_import5)(struct rt_db_internal * /*ip*/,\n"
    "\t\t      const struct bu_external * /*ep*/,\n"
    "\t\t      const mat_t /*mat*/,\n"
    "\t\t      const struct db_i * /*dbip*/);\n"
    "#define RTFUNCTAB_FUNC_IMPORT5_CAST(_func) ((int (*)(struct rt_db_internal *, const struct bu_external *, const mat_t, const struct db_i *))((void (*)(void))_func))"
)
c = subst(c,
    "    int (*ft_export5)(struct bu_external * /*ep*/,\n"
    "\t\t      const struct rt_db_internal * /*ip*/,\n"
    "\t\t      double /*local2mm*/,\n"
    "\t\t      const struct db_i * /*dbip*/,\n"
    "\t\t      struct resource * /*resp*/);\n"
    "#define RTFUNCTAB_FUNC_EXPORT5_CAST(_func) ((int (*)(struct bu_external *, const struct rt_db_internal *, double, const struct db_i *, struct resource *))((void (*)(void))_func))",
    "    int (*ft_export5)(struct bu_external * /*ep*/,\n"
    "\t\t      const struct rt_db_internal * /*ip*/,\n"
    "\t\t      double /*local2mm*/,\n"
    "\t\t      const struct db_i * /*dbip*/);\n"
    "#define RTFUNCTAB_FUNC_EXPORT5_CAST(_func) ((int (*)(struct bu_external *, const struct rt_db_internal *, double, const struct db_i *))((void (*)(void))_func))"
)
c = subst(c,
    "    int (*ft_import4)(struct rt_db_internal * /*ip*/,\n"
    "\t\t      const struct bu_external * /*ep*/,\n"
    "\t\t      const mat_t /*mat*/,\n"
    "\t\t      const struct db_i * /*dbip*/,\n"
    "\t\t      struct resource * /*resp*/);\n"
    "#define RTFUNCTAB_FUNC_IMPORT4_CAST(_func) ((int (*)(struct rt_db_internal *, const struct bu_external *, const mat_t, const struct db_i *, struct resource *))((void (*)(void))_func))",
    "    int (*ft_import4)(struct rt_db_internal * /*ip*/,\n"
    "\t\t      const struct bu_external * /*ep*/,\n"
    "\t\t      const mat_t /*mat*/,\n"
    "\t\t      const struct db_i * /*dbip*/);\n"
    "#define RTFUNCTAB_FUNC_IMPORT4_CAST(_func) ((int (*)(struct rt_db_internal *, const struct bu_external *, const mat_t, const struct db_i *))((void (*)(void))_func))"
)
c = subst(c,
    "    int (*ft_export4)(struct bu_external * /*ep*/,\n"
    "\t\t      const struct rt_db_internal * /*ip*/,\n"
    "\t\t      double /*local2mm*/,\n"
    "\t\t      const struct db_i * /*dbip*/,\n"
    "\t\t      struct resource * /*resp*/);\n"
    "#define RTFUNCTAB_FUNC_EXPORT4_CAST(_func) ((int (*)(struct bu_external *, const struct rt_db_internal *, double, const struct db_i *, struct resource *))((void (*)(void))_func))",
    "    int (*ft_export4)(struct bu_external * /*ep*/,\n"
    "\t\t      const struct rt_db_internal * /*ip*/,\n"
    "\t\t      double /*local2mm*/,\n"
    "\t\t      const struct db_i * /*dbip*/);\n"
    "#define RTFUNCTAB_FUNC_EXPORT4_CAST(_func) ((int (*)(struct bu_external *, const struct rt_db_internal *, double, const struct db_i *))((void (*)(void))_func))"
)
write_file(path, c)
print(f"  functab.h")

# --- db_internal.h ---
path = f"{BRLCAD}/include/rt/db_internal.h"
c = read_file(path)
c = subst(c,
    "RT_EXPORT extern int rt_db_get_internal(struct rt_db_internal   *ip,\n"
    "\t\t\t\tconst struct directory  *dp,\n"
    "\t\t\t\tconst struct db_i       *dbip,\n"
    "\t\t\t\tconst mat_t             mat,\n"
    "\t\t\t\tstruct resource         *resp);",
    "RT_EXPORT extern int rt_db_get_internal(struct rt_db_internal   *ip,\n"
    "\t\t\t\tconst struct directory  *dp,\n"
    "\t\t\t\tconst struct db_i       *dbip,\n"
    "\t\t\t\tconst mat_t             mat);"
)
c = subst(c,
    "RT_EXPORT extern int rt_db_put_internal(struct directory        *dp,\n"
    "\t\t\t\tstruct db_i             *dbip,\n"
    "\t\t\t\tstruct rt_db_internal   *ip,\n"
    "\t\t\t\tstruct resource         *resp);",
    "RT_EXPORT extern int rt_db_put_internal(struct directory        *dp,\n"
    "\t\t\t\tstruct db_i             *dbip,\n"
    "\t\t\t\tstruct rt_db_internal   *ip);"
)
c = subst(c,
    "RT_EXPORT extern int rt_db_lookup_internal(struct db_i *dbip,\n"
    "\t\t\t\t   const char *obj_name,\n"
    "\t\t\t\t   struct directory **dpp,\n"
    "\t\t\t\t   struct rt_db_internal *ip,\n"
    "\t\t\t\t   int noisy,\n"
    "\t\t\t\t   struct resource *resp);",
    "RT_EXPORT extern int rt_db_lookup_internal(struct db_i *dbip,\n"
    "\t\t\t\t   const char *obj_name,\n"
    "\t\t\t\t   struct directory **dpp,\n"
    "\t\t\t\t   struct rt_db_internal *ip,\n"
    "\t\t\t\t   int noisy);"
)
c = subst(c,
    "\n__END_DECLS\n\n#endif /* RT_DB_INTERNAL_H */",
    "\nDEPRECATED static inline int rt_db_get_internal_old(struct rt_db_internal *ip, const struct directory *dp, const struct db_i *dbip, const mat_t mat, struct resource *resp) { (void)resp; return rt_db_get_internal(ip, dp, dbip, mat); }\n"
    "DEPRECATED static inline int rt_db_put_internal_old(struct directory *dp, struct db_i *dbip, struct rt_db_internal *ip, struct resource *resp) { (void)resp; return rt_db_put_internal(dp, dbip, ip); }\n"
    "DEPRECATED static inline int rt_db_lookup_internal_old(struct db_i *dbip, const char *obj_name, struct directory **dpp, struct rt_db_internal *ip, int noisy, struct resource *resp) { (void)resp; return rt_db_lookup_internal(dbip, obj_name, dpp, ip, noisy); }\n"
    "\n__END_DECLS\n\n#endif /* RT_DB_INTERNAL_H */"
)
write_file(path, c)
print(f"  db_internal.h")

# --- calc.h ---
path = f"{BRLCAD}/include/rt/calc.h"
c = read_file(path)
c = subst(c,
    "RT_EXPORT extern int rt_matrix_transform(struct rt_db_internal *output, const mat_t matrix, struct rt_db_internal *input, int free_input, struct db_i *dbip, struct resource *resource);",
    "RT_EXPORT extern int rt_matrix_transform(struct rt_db_internal *output, const mat_t matrix, struct rt_db_internal *input, int free_input, struct db_i *dbip);\n"
    "DEPRECATED static inline int rt_matrix_transform_old(struct rt_db_internal *output, const mat_t matrix, struct rt_db_internal *input, int free_input, struct db_i *dbip, struct resource *resource) { (void)resource; return rt_matrix_transform(output, matrix, input, free_input, dbip); }"
)
write_file(path, c)
print(f"  calc.h")

# --- func.h ---
path = f"{BRLCAD}/include/rt/func.h"
c = read_file(path)
c = subst(c,
    "RT_EXPORT extern int rt_obj_import(struct rt_db_internal *ip, const struct bu_external *ep, const mat_t mat, const struct db_i *dbip, struct resource *resp);",
    "RT_EXPORT extern int rt_obj_import(struct rt_db_internal *ip, const struct bu_external *ep, const mat_t mat, const struct db_i *dbip);"
)
c = subst(c,
    "RT_EXPORT extern int rt_obj_export(struct bu_external *ep, const struct rt_db_internal *ip, double local2mm, const struct db_i *dbip, struct resource *resp);",
    "RT_EXPORT extern int rt_obj_export(struct bu_external *ep, const struct rt_db_internal *ip, double local2mm, const struct db_i *dbip);\n"
    "DEPRECATED static inline int rt_obj_import_old(struct rt_db_internal *ip, const struct bu_external *ep, const mat_t mat, const struct db_i *dbip, struct resource *resp) { (void)resp; return rt_obj_import(ip, ep, mat, dbip); }\n"
    "DEPRECATED static inline int rt_obj_export_old(struct bu_external *ep, const struct rt_db_internal *ip, double local2mm, const struct db_i *dbip, struct resource *resp) { (void)resp; return rt_obj_export(ep, ip, local2mm, dbip); }"
)
write_file(path, c)
print(f"  func.h")

# --- rt_instance.h ---
path = f"{BRLCAD}/include/rt/rt_instance.h"
c = read_file(path)
c = subst(c,
    "/* Plot a solid */\nint rt_plot_solid(\n    FILE                *fp,\n    struct rt_i         *rtip,\n    const struct soltab *stp,\n    struct resource     *resp);",
    "/* Plot a solid */\nint rt_plot_solid(\n    FILE                *fp,\n    struct rt_i         *rtip,\n    const struct soltab *stp);"
)
c = subst(c,
    "RT_EXPORT extern int rt_del_regtree(struct rt_i *rtip,\n\t\t\t\t    struct region *delregp,\n\t\t\t\t    struct resource *resp);",
    "RT_EXPORT extern int rt_del_regtree(struct rt_i *rtip,\n\t\t\t\t    struct region *delregp);"
)
write_file(path, c)
print(f"  rt_instance.h")

# --- tree.h ---
path = f"{BRLCAD}/include/rt/tree.h"
c = read_file(path)
c = subst(c,
    "RT_EXPORT extern void db_init_db_tree_state(struct db_tree_state *tsp,\n"
    "\t\t\t\t    struct db_i *dbip,\n"
    "\t\t\t\t    struct resource *resp);",
    "RT_EXPORT extern void db_init_db_tree_state(struct db_tree_state *tsp,\n"
    "\t\t\t\t    struct db_i *dbip);"
)
c = subst(c,
    "RT_EXPORT extern void db_tree_del_lhs(union tree *tp,\n"
    "\t\t\t      struct resource *resp);\n"
    "RT_EXPORT extern void db_tree_del_rhs(union tree *tp,\n"
    "\t\t\t      struct resource *resp);",
    "RT_EXPORT extern void db_tree_del_lhs(union tree *tp);\n"
    "RT_EXPORT extern void db_tree_del_rhs(union tree *tp);"
)
c = subst(c,
    "RT_EXPORT extern int db_tree_del_dbleaf(union tree **tp,\n"
    "\t\t\t\tconst char *cp,\n"
    "\t\t\t\tstruct resource *resp,\n"
    "\t\t\t\tint nflag);",
    "RT_EXPORT extern int db_tree_del_dbleaf(union tree **tp,\n"
    "\t\t\t\tconst char *cp,\n"
    "\t\t\t\tint nflag);"
)
c = subst(c,
    "RT_EXPORT extern union tree *db_dup_subtree(const union tree *tp,\n"
    "\t\t\t\t    struct resource *resp);",
    "RT_EXPORT extern union tree *db_dup_subtree(const union tree *tp);"
)
c = subst(c,
    "RT_EXPORT extern void db_free_tree(union tree *tp,\n"
    "\t\t\t   struct resource *resp);",
    "RT_EXPORT extern void db_free_tree(union tree *tp);"
)
c = subst(c,
    "RT_EXPORT extern void db_non_union_push(union tree *tp,\n"
    "\t\t\t\tstruct resource *resp);",
    "RT_EXPORT extern void db_non_union_push(union tree *tp);"
)
c = subst(c,
    "RT_EXPORT extern int db_tally_subtree_regions(union tree        *tp,\n"
    "\t\t\t\t      union tree        **reg_trees,\n"
    "\t\t\t\t      int               cur,\n"
    "\t\t\t\t      int               lim,\n"
    "\t\t\t\t      struct resource *resp);",
    "RT_EXPORT extern int db_tally_subtree_regions(union tree        *tp,\n"
    "\t\t\t\t      union tree        **reg_trees,\n"
    "\t\t\t\t      int               cur,\n"
    "\t\t\t\t      int               lim);"
)
c = subst(c,
    "RT_EXPORT extern union tree *db_tree_parse(struct bu_vls *vls, const char *str, struct resource *resp);",
    "RT_EXPORT extern union tree *db_tree_parse(struct bu_vls *vls, const char *str);"
)
c = subst(c,
    "RT_EXPORT extern void db_functree(struct db_i *dbip,\n"
    "\t\t\t  struct directory *dp,\n"
    "\t\t\t  void (*comb_func)(struct db_i *,\n"
    "\t\t\t\t\t    struct directory *,\n"
    "\t\t\t\t\t    void *),\n"
    "\t\t\t  void (*leaf_func)(struct db_i *,\n"
    "\t\t\t\t\t    struct directory *,\n"
    "\t\t\t\t\t    void *),\n"
    "\t\t\t  struct resource *resp,\n"
    "\t\t\t  void *client_data);",
    "RT_EXPORT extern void db_functree(struct db_i *dbip,\n"
    "\t\t\t  struct directory *dp,\n"
    "\t\t\t  void (*comb_func)(struct db_i *,\n"
    "\t\t\t\t\t    struct directory *,\n"
    "\t\t\t\t\t    void *),\n"
    "\t\t\t  void (*leaf_func)(struct db_i *,\n"
    "\t\t\t\t\t    struct directory *,\n"
    "\t\t\t\t\t    void *),\n"
    "\t\t\t  void *client_data);"
)
c = subst(c,
    "RT_EXPORT extern int rt_tree_elim_nops(union tree *,\n"
    "\t\t\t       struct resource *resp);",
    "RT_EXPORT extern int rt_tree_elim_nops(union tree *);"
)
c = subst(c,
    "RT_EXPORT extern struct rt_tree_array *db_flatten_tree(struct rt_tree_array *rt_tree_array, union tree *tp, int op, int avail, struct resource *resp);",
    "RT_EXPORT extern struct rt_tree_array *db_flatten_tree(struct rt_tree_array *rt_tree_array, union tree *tp, int op, int avail);"
)
c = subst(c,
    "RT_EXPORT extern void db_tree_flatten_describe(struct bu_vls    *vls,\n"
    "\t\t\t\t       const union tree *tp,\n"
    "\t\t\t\t       int              indented,\n"
    "\t\t\t\t       int              lvl,\n"
    "\t\t\t\t       double           mm2local,\n"
    "\t\t\t\t       struct resource  *resp);",
    "RT_EXPORT extern void db_tree_flatten_describe(struct bu_vls    *vls,\n"
    "\t\t\t\t       const union tree *tp,\n"
    "\t\t\t\t       int              indented,\n"
    "\t\t\t\t       int              lvl,\n"
    "\t\t\t\t       double           mm2local);"
)
write_file(path, c)
print(f"  tree.h")

# --- db_io.h ---
path = f"{BRLCAD}/include/rt/db_io.h"
c = read_file(path)
c = subst(c,
    "RT_EXPORT extern int rt_db_get_internal5(struct rt_db_internal *ip,\n"
    "\t\t\t\t const struct directory *dp,\n"
    "\t\t\t\t const struct db_i *dbip,\n"
    "\t\t\t\t const mat_t mat,\n"
    "\t\t\t\t struct resource *resp);",
    "RT_EXPORT extern int rt_db_get_internal5(struct rt_db_internal *ip,\n"
    "\t\t\t\t const struct directory *dp,\n"
    "\t\t\t\t const struct db_i *dbip,\n"
    "\t\t\t\t const mat_t mat);"
)
c = subst(c,
    "RT_EXPORT extern void db_alloc_directory_block(struct resource *resp);",
    "RT_EXPORT extern void db_alloc_directory_block(struct db_i *dbip);"
)
c = subst(c,
    "RT_EXPORT extern int\nrt_db_external5_to_internal5(\n    struct rt_db_internal *ip,\n    const struct bu_external *ep,\n    const char *name,\n    const struct db_i *dbip,\n    const mat_t mat,\n    struct resource *resp);",
    "RT_EXPORT extern int\nrt_db_external5_to_internal5(\n    struct rt_db_internal *ip,\n    const struct bu_external *ep,\n    const char *name,\n    const struct db_i *dbip,\n    const mat_t mat);"
)
write_file(path, c)
print(f"  db_io.h")

# --- nmg_conv.h ---
path = f"{BRLCAD}/include/rt/nmg_conv.h"
c = read_file(path)
c = subst(c,
    "RT_EXPORT extern union tree *nmg_booltree_evaluate(union tree *tp,\n"
    "\t\t\t\t\t   struct bu_list *vlfree,\n"
    "\t\t\t\t\t   const struct bn_tol *tol,\n"
    "\t\t\t\t\t   struct resource *resp);",
    "RT_EXPORT extern union tree *nmg_booltree_evaluate(union tree *tp,\n"
    "\t\t\t\t\t   struct bu_list *vlfree,\n"
    "\t\t\t\t\t   const struct bn_tol *tol);"
)
write_file(path, c)
print(f"  nmg_conv.h")

# --- directory.h - RT_GET_DIRECTORY macro ---
path = f"{BRLCAD}/include/rt/directory.h"
c = read_file(path)
c = subst(c,
    "#define RT_GET_DIRECTORY(_p, _res) { \\\n"
    "\twhile (((_p) = (_res)->re_directory_hd) == NULL) \\\n"
    "\t    db_alloc_directory_block(_res); \\\n"
    "\t(_res)->re_directory_hd = (_p)->d_forw; \\\n"
    "\t(_p)->d_forw = NULL; }",
    "#define RT_GET_DIRECTORY(_p, _dbip) { \\\n"
    "\twhile (((_p) = (_dbip)->i->dbi_directory_hd) == NULL) \\\n"
    "\t    db_alloc_directory_block(_dbip); \\\n"
    "\t(_dbip)->i->dbi_directory_hd = (_p)->d_forw; \\\n"
    "\t(_p)->d_forw = NULL; }"
)
write_file(path, c)
print(f"  directory.h")

# ================================================================
# 2. Update librt_private.h (Phase 2 - add fields to db_i_internal)
# ================================================================
print("\n=== Updating librt_private.h ===")
path = f"{BRLCAD}/src/librt/librt_private.h"
c = read_file(path)
c = subst(c,
    "    int dbi_use_comb_instance_ids;        /**< @brief flag for comb instance tracking */\n};",
    "    int dbi_use_comb_instance_ids;        /**< @brief flag for comb instance tracking */\n"
    "\n"
    "    struct directory *dbi_directory_hd;         /**< @brief directory entry freelist */\n"
    "    struct bu_ptbl   dbi_directory_blocks;      /**< @brief Table of malloc'ed blocks */\n"
    "};"
)
write_file(path, c)
print(f"  librt_private.h")

# ================================================================
# 3. Update db_open.c (Phase 2 - init/destroy)
# ================================================================
print("Updating db_open.c...")
path = f"{BRLCAD}/src/librt/db_open.c"
c = read_file(path)
c = subst(c,
    "    BU_GET(i, struct db_i_internal);\n    i->dbi_magic = DBI_MAGIC;\n    i->material_head = MATER_NULL;\n\n    return i;",
    "    BU_GET(i, struct db_i_internal);\n    i->dbi_magic = DBI_MAGIC;\n    i->material_head = MATER_NULL;\n    i->dbi_directory_hd = NULL;\n    bu_ptbl_init(&i->dbi_directory_blocks, 8, \"dbi_directory_blocks\");\n\n    return i;"
)
c = subst(c,
    "    if (i->mesh_c)\n\tbv_mesh_lod_context_destroy(i->mesh_c);\n\n    /* Free any remaining material entries (normally freed by db_mater_free) */",
    "    if (i->mesh_c)\n\tbv_mesh_lod_context_destroy(i->mesh_c);\n\n    /* Free any directory blocks */\n    for (size_t ii = 0; ii < BU_PTBL_LEN(&i->dbi_directory_blocks); ii++)\n\tbu_free(BU_PTBL_GET(&i->dbi_directory_blocks, ii), \"directory block\");\n    bu_ptbl_free(&i->dbi_directory_blocks);\n\n    /* Free any remaining material entries (normally freed by db_mater_free) */"
)
write_file(path, c)
print(f"  db_open.c")

# ================================================================
# 4. Update db_alloc.c (Phase 2 - change db_alloc_directory_block)
# ================================================================
print("Updating db_alloc.c...")
path = f"{BRLCAD}/src/librt/db_alloc.c"
c = read_file(path)
# Change the function signature and body
c = subst(c,
    "db_alloc_directory_block(struct resource *resp)",
    "db_alloc_directory_block(struct db_i *dbip)"
)
c = subst(c,
    "    RT_CK_RESOURCE(resp);\n    BU_CK_PTBL(&resp->re_directory_blocks);\n\n    BU_ASSERT(resp->re_directory_hd == NULL);",
    "    RT_CK_DBI(dbip);\n    BU_CK_PTBL(&dbip->i->dbi_directory_blocks);\n\n    BU_ASSERT(dbip->i->dbi_directory_hd == NULL);"
)
c = subst(c,
    'dp = (struct directory *)bu_calloc(1, bytes, "re_directory_blocks from db_alloc_directory_block() " CPP_FILELINE);',
    'dp = (struct directory *)bu_calloc(1, bytes, "dbi_directory_blocks from db_alloc_directory_block() " CPP_FILELINE);'
)
c = subst(c,
    "    bu_ptbl_ins(&resp->re_directory_blocks, (long *)dp);",
    "    bu_ptbl_ins(&dbip->i->dbi_directory_blocks, (long *)dp);"
)
c = subst(c,
    "\tdp->d_forw = resp->re_directory_hd;\n\tresp->re_directory_hd = dp;",
    "\tdp->d_forw = dbip->i->dbi_directory_hd;\n\tdbip->i->dbi_directory_hd = dp;"
)
write_file(path, c)
print(f"  db_alloc.c")

# ================================================================
# 5. Update RT_GET_DIRECTORY callers
# ================================================================
print("Updating RT_GET_DIRECTORY callers...")
# db5_scan.c - using &rt_uniresource, change to dbip
path = f"{BRLCAD}/src/librt/db5_scan.c"
c = read_file(path)
c = subst(c, "RT_GET_DIRECTORY(dp, &rt_uniresource);", "RT_GET_DIRECTORY(dp, dbip);")
write_file(path, c)
print(f"  db5_scan.c")

# db_lookup.c - using &rt_uniresource
path = f"{BRLCAD}/src/librt/db_lookup.c"
c = read_file(path)
c = subst(c, "RT_GET_DIRECTORY(dp, &rt_uniresource);", "RT_GET_DIRECTORY(dp, dbip);")
write_file(path, c)
print(f"  db_lookup.c")

# ================================================================
# 6. Update IMPLEMENTATIONS
# ================================================================
print("\n=== Updating Implementations ===")

# --- dir.c ---
path = f"{BRLCAD}/src/librt/dir.c"
c = read_file(path)
c = subst(c,
    "int\nrt_db_get_internal(\n    struct rt_db_internal *ip,\n    const struct directory *dp,\n    const struct db_i *dbip,\n    const mat_t mat,\n    struct resource *resp)\n{",
    "int\nrt_db_get_internal(\n    struct rt_db_internal *ip,\n    const struct directory *dp,\n    const struct db_i *dbip,\n    const mat_t mat)\n{"
)
c = subst(c, "return rt_db_get_internal5(ip, dp, dbip, mat, resp);",
    "return rt_db_get_internal5(ip, dp, dbip, mat);")
c = subst(c, "ret = OBJ[id].ft_import4(ip, &ext, mat, dbip, resp);",
    "ret = OBJ[id].ft_import4(ip, &ext, mat, dbip);")
c = subst(c,
    "int\nrt_db_put_internal(\n    struct directory *dp,\n    struct db_i *dbip,\n    struct rt_db_internal *ip,\n    struct resource *resp)\n{",
    "int\nrt_db_put_internal(\n    struct directory *dp,\n    struct db_i *dbip,\n    struct rt_db_internal *ip)\n{"
)
c = subst(c,
    "return rt_db_put_internal5(dp, dbip, ip, resp,\n\t\t\t\t   ip->idb_major_type);",
    "return rt_db_put_internal5(dp, dbip, ip,\n\t\t\t\t   ip->idb_major_type);"
)
c = subst(c, "ret = ip->idb_meth->ft_export4(&ext, ip, 1.0, dbip, resp);",
    "ret = ip->idb_meth->ft_export4(&ext, ip, 1.0, dbip);")
c = subst(c,
    "int\nrt_db_lookup_internal (\n    struct db_i *dbip,\n    const char *obj_name,\n    struct directory **dpp,\n    struct rt_db_internal *ip,\n    int noisy,\n    struct resource *resp)\n{",
    "int\nrt_db_lookup_internal (\n    struct db_i *dbip,\n    const char *obj_name,\n    struct directory **dpp,\n    struct rt_db_internal *ip,\n    int noisy)\n{"
)
c = subst(c, "if (rt_db_get_internal(ip, dp, dbip, (matp_t) NULL, resp) < 0) {",
    "if (rt_db_get_internal(ip, dp, dbip, (matp_t) NULL) < 0) {")
# Also fix rt_fwrite_internal which calls ft_export4 with rt_uniresource
c = subst(c, "ret = ip->idb_meth->ft_export4(&ext, ip, conv2mm, NULL /*dbip*/, &rt_uniresource);",
    "ret = ip->idb_meth->ft_export4(&ext, ip, conv2mm, NULL /*dbip*/);")
write_file(path, c)
print(f"  dir.c")

# --- db5_io.c ---
path = f"{BRLCAD}/src/librt/db5_io.c"
c = read_file(path)
# rt_db_get_internal5
c = subst(c,
    "int\nrt_db_get_internal5(\n    struct rt_db_internal *ip,\n    const struct directory *dp,\n    const struct db_i *dbip,\n    const mat_t mat,\n    struct resource *resp)\n{",
    "int\nrt_db_get_internal5(\n    struct rt_db_internal *ip,\n    const struct directory *dp,\n    const struct db_i *dbip,\n    const mat_t mat)\n{"
)
c = subst(c,
    "    RT_DB_INTERNAL_INIT(ip);\n    if (resp) {\n\tRT_CK_RESOURCE(resp);\n    }\n\n    BU_ASSERT(dbip->i->dbi_version == 5);\n\n    if (db_get_external(&ext, dp, dbip) < 0)\n\treturn -2;\t\t/* FAIL */\n\n    ret = rt_db_external5_to_internal5(ip, &ext, dp->d_namep, dbip, mat, resp);",
    "    RT_DB_INTERNAL_INIT(ip);\n\n    BU_ASSERT(dbip->i->dbi_version == 5);\n\n    if (db_get_external(&ext, dp, dbip) < 0)\n\treturn -2;\t\t/* FAIL */\n\n    ret = rt_db_external5_to_internal5(ip, &ext, dp->d_namep, dbip, mat);"
)
# rt_db_external5_to_internal5
c = subst(c,
    "rt_db_external5_to_internal5(\n    struct rt_db_internal *ip,\n    const struct bu_external *ep,\n    const char *name,\n    const struct db_i *dbip,\n    const mat_t mat,\n    struct resource *resp)\n{",
    "rt_db_external5_to_internal5(\n    struct rt_db_internal *ip,\n    const struct bu_external *ep,\n    const char *name,\n    const struct db_i *dbip,\n    const mat_t mat)\n{"
)
c = subst(c,
    "    if (resp) {\n\tRT_CK_RESOURCE(resp);\n    } else {\n\t/* needed for call into functab */\n\tresp = &rt_uniresource;\n    }\n\n    BU_ASSERT(dbip->i->dbi_version == 5);",
    "    BU_ASSERT(dbip->i->dbi_version == 5);"
)
# Fix any remaining ft_import5 calls with resp  
c = re.sub(r'(\.ft_import5\([^,]+,\s*[^,]+,\s*[^,]+,\s*[^,]+),\s*resp\)', r'\1)', c)
c = re.sub(r'(ft_import5\([^,]+,\s*[^,]+,\s*[^,]+,\s*[^,]+),\s*resp\)', r'\1)', c)
c = re.sub(r'(ft_import5\([^,]+,\s*[^,]+,\s*[^,]+,\s*[^,]+),\s*&rt_uniresource\)', r'\1)', c)
# Also rt_db_put_internal5
c = re.sub(r'rt_db_put_internal5\(([^,]+),\s*([^,]+),\s*([^,]+),\s*(?:resp|&rt_uniresource),\s*([^)]+)\)',
           r'rt_db_put_internal5(\1, \2, \3, \4)', c)
write_file(path, c)
print(f"  db5_io.c")

# Check rt_db_put_internal5 definition
path = f"{BRLCAD}/src/librt/db5_io.c"
c = read_file(path)
if "rt_db_put_internal5(\n    struct directory *dp,\n    struct db_i *dbip,\n    struct rt_db_internal *ip,\n    struct resource *resp," in c:
    c = subst(c,
        "rt_db_put_internal5(\n    struct directory *dp,\n    struct db_i *dbip,\n    struct rt_db_internal *ip,\n    struct resource *resp,",
        "rt_db_put_internal5(\n    struct directory *dp,\n    struct db_i *dbip,\n    struct rt_db_internal *ip,"
    )
    # Remove resp usage in body
    c = re.sub(r',\s*resp\b', '', c)
    c = subst(c, "    RT_CK_RESOURCE(resp);\n", "")
    write_file(path, c)

# Also update rt_db_put_internal5 in db_io.h
path = f"{BRLCAD}/include/rt/db_io.h"
c = read_file(path)
c = subst(c,
    "RT_EXPORT extern int rt_db_put_internal5(struct directory *dp,\n\t\t\t\t struct db_i *dbip,\n\t\t\t\t struct rt_db_internal *ip,\n\t\t\t\t struct resource *resp,",
    "RT_EXPORT extern int rt_db_put_internal5(struct directory *dp,\n\t\t\t\t struct db_i *dbip,\n\t\t\t\t struct rt_db_internal *ip,"
)
write_file(path, c)
print(f"  db_io.h (rt_db_put_internal5)")

# --- transform.c ---
path = f"{BRLCAD}/src/librt/transform.c"
c = read_file(path)
c = re.sub(
    r'rt_matrix_transform\(struct rt_db_internal \*output, const mat_t matrix, struct rt_db_internal \*input, int (?:freeflag|free_input), struct db_i \*dbip, struct resource \*\w+\)',
    'rt_matrix_transform(struct rt_db_internal *output, const mat_t matrix, struct rt_db_internal *input, int freeflag, struct db_i *dbip)',
    c
)
# Fix rt_db_get/put calls inside
c = re.sub(r'rt_db_get_internal\(([^,]+),\s*([^,]+),\s*([^,]+),\s*([^,]+),\s*[^)]+\)',
           r'rt_db_get_internal(\1, \2, \3, \4)', c)
c = re.sub(r'rt_db_put_internal\(([^,]+),\s*([^,]+),\s*([^,]+),\s*[^)]+\)',
           r'rt_db_put_internal(\1, \2, \3)', c)
write_file(path, c)
print(f"  transform.c")

# --- obj_import.c ---
path = f"{BRLCAD}/src/librt/primitives/obj_import.c"
c = read_file(path)
c = subst(c,
    "rt_obj_import(struct rt_db_internal *ip, const struct bu_external *ep, const mat_t mat, const struct db_i *dbip, struct resource *resp)",
    "rt_obj_import(struct rt_db_internal *ip, const struct bu_external *ep, const mat_t mat, const struct db_i *dbip)"
)
# Fix any ft_import calls inside
c = re.sub(r'(->ft_import[45]\([^,]+,\s*[^,]+,\s*[^,]+,\s*[^,]+),\s*(?:resp|&rt_uniresource)\)',
           r'\1)', c)
c = re.sub(r'(\bft_import[45]\([^,]+,\s*[^,]+,\s*[^,]+,\s*[^,]+),\s*(?:resp|&rt_uniresource)\)',
           r'\1)', c)
write_file(path, c)
print(f"  obj_import.c")

# --- obj_export.c ---
path = f"{BRLCAD}/src/librt/primitives/obj_export.c"
c = read_file(path)
c = subst(c,
    "rt_obj_export(struct bu_external *ep, const struct rt_db_internal *ip, double local2mm, const struct db_i *dbip, struct resource *resp)",
    "rt_obj_export(struct bu_external *ep, const struct rt_db_internal *ip, double local2mm, const struct db_i *dbip)"
)
c = re.sub(r'(->ft_export[45]\([^,]+,\s*[^,]+,\s*[^,]+,\s*[^,]+),\s*(?:resp|&rt_uniresource)\)',
           r'\1)', c)
c = re.sub(r'(\bft_export[45]\([^,]+,\s*[^,]+,\s*[^,]+,\s*[^,]+),\s*(?:resp|&rt_uniresource)\)',
           r'\1)', c)
write_file(path, c)
print(f"  obj_export.c")

# --- prep.cpp ---
path = f"{BRLCAD}/src/librt/prep.cpp"
c = read_file(path)
c = subst(c,
    "rt_plot_solid(\n    FILE *fp,\n    struct rt_i *rtip,\n    const struct soltab *stp,\n    struct resource *resp)\n{",
    "rt_plot_solid(\n    FILE *fp,\n    struct rt_i *rtip,\n    const struct soltab *stp)\n{"
)
c = subst(c, "if (rt_vlist_solid(&vhead, rtip, stp, resp) < 0) {",
    "if (rt_vlist_solid(&vhead, rtip, stp, &rt_uniresource) < 0) {")
c = subst(c,
    "rt_del_regtree(struct rt_i *rtip, struct region *delregp, struct resource *resp)\n{",
    "rt_del_regtree(struct rt_i *rtip, struct region *delregp)\n{"
)
c = subst(c,
    "    RT_CK_RESOURCE(resp);\n    RT_CK_REGION(delregp);",
    "    RT_CK_REGION(delregp);"
)
c = subst(c, "    db_free_tree(delregp->reg_treetop, resp);",
    "    db_free_tree(delregp->reg_treetop);")
c = subst(c, "(void)rt_plot_solid(fp, rtip, stp, resp);",
    "(void)rt_plot_solid(fp, rtip, stp);")
c = subst(c, "(void)rt_plot_solid(fp, rtip, stp, ap->a_resource);",
    "(void)rt_plot_solid(fp, rtip, stp);")
write_file(path, c)
print(f"  prep.cpp")

# --- db_tree.cpp ---
path = f"{BRLCAD}/src/librt/db_tree.cpp"
c = read_file(path)
# db_init_db_tree_state
c = subst(c,
    "db_init_db_tree_state(struct db_tree_state *tsp, struct db_i *dbip, struct resource *resp)",
    "db_init_db_tree_state(struct db_tree_state *tsp, struct db_i *dbip)"
)
# db_tree_del_lhs
c = subst(c,
    "db_tree_del_lhs(union tree *tp, struct resource *resp)",
    "db_tree_del_lhs(union tree *tp)"
)
# db_tree_del_rhs
c = subst(c,
    "db_tree_del_rhs(union tree *tp, struct resource *resp)",
    "db_tree_del_rhs(union tree *tp)"
)
# db_tree_del_dbleaf
c = subst(c,
    "db_tree_del_dbleaf(union tree **tp, const char *cp, struct resource *resp, int nflag)",
    "db_tree_del_dbleaf(union tree **tp, const char *cp, int nflag)"
)
# db_dup_subtree
c = subst(c,
    "db_dup_subtree(const union tree *tp, struct resource *resp)",
    "db_dup_subtree(const union tree *tp)"
)
# db_free_tree
c = subst(c,
    "db_free_tree(union tree *tp, struct resource *resp)",
    "db_free_tree(union tree *tp)"
)
# db_non_union_push  
c = subst(c,
    "db_non_union_push(union tree *tp, struct resource *resp)",
    "db_non_union_push(union tree *tp)"
)
# db_tally_subtree_regions - look for multiline
c = re.sub(
    r'db_tally_subtree_regions\(\s*\n\s*union tree\s*\*tp,\s*\n\s*union tree\s*\*\*reg_trees,\s*\n\s*int\s*cur,\s*\n\s*int\s*lim,\s*\n\s*struct resource \*resp\)',
    'db_tally_subtree_regions(\n    union tree *tp,\n    union tree **reg_trees,\n    int cur,\n    int lim)',
    c
)
# db_tree_parse
c = subst(c,
    "db_tree_parse(struct bu_vls *vls, const char *str, struct resource *resp)",
    "db_tree_parse(struct bu_vls *vls, const char *str)"
)
# Remove RT_CK_RESOURCE(resp) calls in these functions (where resp is no longer a param)
# Do this carefully - only where resp was just a parameter being removed
# In db_init_db_tree_state, db_non_union_push etc.
# Replace patterns like: just "    RT_CK_RESOURCE(resp);\n"  
# But we need to be careful not to remove them from functions that still have resp
# Let's do targeted substitutions based on context

# db_init_db_tree_state uses resp for ts_resp - need to check
# For now, remove standalone RT_CK_RESOURCE(resp) patterns
# that appear right after the parameter removal
c = re.sub(r'\n    RT_CK_RESOURCE\(resp\);\n', '\n', c)

# Fix internal calls to db_free_tree
c = re.sub(r'db_free_tree\(([^,)]+),\s*(?:resp|&rt_uniresource)\)', r'db_free_tree(\1)', c)
c = re.sub(r'db_dup_subtree\(([^,)]+),\s*(?:resp|&rt_uniresource)\)', r'db_dup_subtree(\1)', c)
c = re.sub(r'db_non_union_push\(([^,)]+),\s*(?:resp|&rt_uniresource)\)', r'db_non_union_push(\1)', c)
c = re.sub(r'db_tree_del_lhs\(([^,)]+),\s*(?:resp|&rt_uniresource)\)', r'db_tree_del_lhs(\1)', c)
c = re.sub(r'db_tree_del_rhs\(([^,)]+),\s*(?:resp|&rt_uniresource)\)', r'db_tree_del_rhs(\1)', c)
c = re.sub(r'db_tally_subtree_regions\(([^,]+),\s*([^,]+),\s*([^,]+),\s*([^,]+),\s*(?:resp|&rt_uniresource)\)',
           r'db_tally_subtree_regions(\1, \2, \3, \4)', c)

write_file(path, c)
print(f"  db_tree.cpp")

# --- db_comb.c ---
path = f"{BRLCAD}/src/librt/comb/db_comb.c"
c = read_file(path)
# db_flatten_tree
c = subst(c,
    "db_flatten_tree(\n    struct rt_tree_array *rt_tree_array,\n    union tree *tp,\n    int op,\n    int freeflag,\n    struct resource *resp)\n{",
    "db_flatten_tree(\n    struct rt_tree_array *rt_tree_array,\n    union tree *tp,\n    int op,\n    int freeflag)\n{"
)
c = subst(c, "    RT_CK_RESOURCE(resp);\n", "")
# Fix recursive calls
c = re.sub(r'db_flatten_tree\(([^,]+),\s*([^,]+),\s*([^,]+),\s*([^,]+),\s*(?:resp|&rt_uniresource)\)',
           r'db_flatten_tree(\1, \2, \3, \4)', c)
# rt_comb_export4 - resp was used for db_non_union_push and db_flatten_tree
c = subst(c,
    "rt_comb_export4(\n    struct bu_external *ep,\n    const struct rt_db_internal *ip,\n    double UNUSED(local2mm),\n    const struct db_i *dbip,\n    struct resource *resp)\n{",
    "rt_comb_export4(\n    struct bu_external *ep,\n    const struct rt_db_internal *ip,\n    double UNUSED(local2mm),\n    const struct db_i *dbip)\n{"
)
c = subst(c, "    RT_CK_RESOURCE(resp);\n    if (ip->idb_type != ID_COMBINATION)", 
    "    if (ip->idb_type != ID_COMBINATION)")
# rt_comb_import4 - resp was UNUSED
c = subst(c,
    "rt_comb_import4(\n    struct rt_db_internal *ip,\n    const struct bu_external *ep,\n    const mat_t matrix,\t\t/* NULL if identity */\n    const struct db_i *dbip,\n    struct resource *UNUSED(resp))\n{",
    "rt_comb_import4(\n    struct rt_db_internal *ip,\n    const struct bu_external *ep,\n    const mat_t matrix,\t\t/* NULL if identity */\n    const struct db_i *dbip)\n{"
)
# db_tree_flatten_describe 
c = subst(c,
    "db_tree_flatten_describe(\n    struct bu_vls *vls,\n    const union tree *tp,\n    int indented,\n    int lvl,\n    double mm2local,\n    struct resource *resp)\n{",
    "db_tree_flatten_describe(\n    struct bu_vls *vls,\n    const union tree *tp,\n    int indented,\n    int lvl,\n    double mm2local)\n{"
)
c = subst(c, "    RT_CK_RESOURCE(resp);\n\n    if (!tp) {", "    if (!tp) {")
# Fix internal calls
c = re.sub(r'db_dup_subtree\(([^,)]+),\s*(?:resp|&rt_uniresource)\)', r'db_dup_subtree(\1)', c)
c = re.sub(r'db_non_union_push\(([^,)]+),\s*(?:resp|&rt_uniresource)\)', r'db_non_union_push(\1)', c)
c = re.sub(r'db_flatten_tree\(([^,]+),\s*([^,]+),\s*([^,]+),\s*([^,]+),\s*(?:resp|&rt_uniresource)\)',
           r'db_flatten_tree(\1, \2, \3, \4)', c)
c = re.sub(r'db_free_tree\(([^,)]+),\s*(?:resp|&rt_uniresource)\)', r'db_free_tree(\1)', c)
c = re.sub(r'db_tree_flatten_describe\(([^,]+),\s*([^,]+),\s*([^,]+),\s*([^,]+),\s*([^,]+),\s*(?:resp|&rt_uniresource)\)',
           r'db_tree_flatten_describe(\1, \2, \3, \4, \5)', c)
write_file(path, c)
print(f"  db_comb.c")

# --- tree.c ---
path = f"{BRLCAD}/src/librt/tree.c"
c = read_file(path)
c = subst(c,
    "rt_tree_elim_nops(union tree *tp, struct resource *resp)",
    "rt_tree_elim_nops(union tree *tp)"
)
c = subst(c, "(void)rt_tree_elim_nops(regp->reg_treetop, &rt_uniresource);",
    "(void)rt_tree_elim_nops(regp->reg_treetop);")
c = re.sub(r'rt_tree_elim_nops\(([^,)]+),\s*(?:resp|&rt_uniresource)\)',
           r'rt_tree_elim_nops(\1)', c)
c = re.sub(r'db_free_tree\(([^,)]+),\s*(?:resp|&rt_uniresource)\)', r'db_free_tree(\1)', c)
write_file(path, c)
print(f"  tree.c")

# --- db_walk.c ---
path = f"{BRLCAD}/src/librt/db_walk.c"
c = read_file(path)
# db_functree definition
c = re.sub(
    r'db_functree\(struct db_i \*dbip,\s*\n\s*struct directory \*dp,\s*\n\s*void \(\*comb_func\).*\n.*\n.*\n\s*void \(\*leaf_func\).*\n.*\n.*\n\s*struct resource \*resp,\s*\n\s*void \*client_data\)',
    'db_functree(struct db_i *dbip,\n\t    struct directory *dp,\n\t    void (*comb_func)(struct db_i *, struct directory *, void *),\n\t    void (*leaf_func)(struct db_i *, struct directory *, void *),\n\t    void *client_data)',
    c
)
# Also fix recursive calls 
c = re.sub(r'db_functree\(([^,]+),\s*([^,]+),\s*([^,]+),\s*([^,]+),\s*(?:resp|&rt_uniresource),\s*([^)]+)\)',
           r'db_functree(\1, \2, \3, \4, \5)', c)
write_file(path, c)
print(f"  db_walk.c")

# --- nmg.c ---
path = f"{BRLCAD}/src/librt/primitives/nmg/nmg.c"
c = read_file(path)
c = subst(c,
    "nmg_booltree_evaluate(register union tree *tp, struct bu_list *vlfree, const struct bn_tol *tol, struct resource *resp)",
    "nmg_booltree_evaluate(register union tree *tp, struct bu_list *vlfree, const struct bn_tol *tol)"
)
c = subst(c, "    RT_CK_RESOURCE(resp);\n", "")
# Fix internal recursive calls
c = re.sub(r'nmg_booltree_evaluate\(([^,]+),\s*([^,]+),\s*([^,]+),\s*(?:resp|&rt_uniresource)\)',
           r'nmg_booltree_evaluate(\1, \2, \3)', c)
write_file(path, c)
print(f"  nmg.c")

print("\n=== Core implementation updates done ===")
