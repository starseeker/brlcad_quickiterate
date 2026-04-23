#!/usr/bin/env python3
"""Phase 1 & 2 refactoring: Remove struct resource * from import/export APIs."""

import re
import os
import glob

BRLCAD = "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad"

def read_file(path):
    with open(path, 'r', encoding='utf-8', errors='replace') as f:
        return f.read()

def write_file(path, content):
    with open(path, 'w', encoding='utf-8') as f:
        f.write(content)

def replace_in_file(path, old, new):
    content = read_file(path)
    if old in content:
        write_file(path, content.replace(old, new))
        print(f"  Updated: {path}")
        return True
    return False

# ============================================================
# PHASE 1 - Update Headers
# ============================================================

print("=== Phase 1: Updating Headers ===")

# 1. include/rt/functab.h
print("Updating functab.h...")
path = f"{BRLCAD}/include/rt/functab.h"
c = read_file(path)
c = c.replace(
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
c = c.replace(
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
c = c.replace(
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
c = c.replace(
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
print(f"  Updated: {path}")

# 2. include/rt/db_internal.h
print("Updating db_internal.h...")
path = f"{BRLCAD}/include/rt/db_internal.h"
c = read_file(path)
c = c.replace(
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
c = c.replace(
    "RT_EXPORT extern int rt_db_put_internal(struct directory        *dp,\n"
    "\t\t\t\tstruct db_i             *dbip,\n"
    "\t\t\t\tstruct rt_db_internal   *ip,\n"
    "\t\t\t\tstruct resource         *resp);",
    "RT_EXPORT extern int rt_db_put_internal(struct directory        *dp,\n"
    "\t\t\t\tstruct db_i             *dbip,\n"
    "\t\t\t\tstruct rt_db_internal   *ip);"
)
c = c.replace(
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
# Add DEPRECATED wrappers before __END_DECLS
c = c.replace(
    "\n__END_DECLS\n\n#endif /* RT_DB_INTERNAL_H */",
    "\nDEPRECATED static inline int rt_db_get_internal_old(struct rt_db_internal *ip, const struct directory *dp, const struct db_i *dbip, const mat_t mat, struct resource *resp) { (void)resp; return rt_db_get_internal(ip, dp, dbip, mat); }\n"
    "DEPRECATED static inline int rt_db_put_internal_old(struct directory *dp, struct db_i *dbip, struct rt_db_internal *ip, struct resource *resp) { (void)resp; return rt_db_put_internal(dp, dbip, ip); }\n"
    "DEPRECATED static inline int rt_db_lookup_internal_old(struct db_i *dbip, const char *obj_name, struct directory **dpp, struct rt_db_internal *ip, int noisy, struct resource *resp) { (void)resp; return rt_db_lookup_internal(dbip, obj_name, dpp, ip, noisy); }\n"
    "\n__END_DECLS\n\n#endif /* RT_DB_INTERNAL_H */"
)
# Ensure bu/defines.h is included (it comes via rt/defines.h typically)
if '#include "bu/defines.h"' not in c and '#include "bu/magic.h"' in c:
    c = c.replace('#include "bu/magic.h"', '#include "bu/magic.h"\n#include "bu/defines.h"')
write_file(path, c)
print(f"  Updated: {path}")

# 3. include/rt/calc.h
print("Updating calc.h...")
path = f"{BRLCAD}/include/rt/calc.h"
c = read_file(path)
c = c.replace(
    "RT_EXPORT extern int rt_matrix_transform(struct rt_db_internal *output, const mat_t matrix, struct rt_db_internal *input, int free_input, struct db_i *dbip, struct resource *resource);",
    "RT_EXPORT extern int rt_matrix_transform(struct rt_db_internal *output, const mat_t matrix, struct rt_db_internal *input, int free_input, struct db_i *dbip);\n"
    "DEPRECATED static inline int rt_matrix_transform_old(struct rt_db_internal *output, const mat_t matrix, struct rt_db_internal *input, int free_input, struct db_i *dbip, struct resource *resource) { (void)resource; return rt_matrix_transform(output, matrix, input, free_input, dbip); }"
)
if '#include "bu/defines.h"' not in c:
    # Insert after first #include or after __BEGIN_DECLS
    c = c.replace('__BEGIN_DECLS\n', '__BEGIN_DECLS\n\n#include "bu/defines.h"\n')
write_file(path, c)
print(f"  Updated: {path}")

# 4. include/rt/func.h
print("Updating func.h...")
path = f"{BRLCAD}/include/rt/func.h"
c = read_file(path)
c = c.replace(
    "RT_EXPORT extern int rt_obj_import(struct rt_db_internal *ip, const struct bu_external *ep, const mat_t mat, const struct db_i *dbip, struct resource *resp);",
    "RT_EXPORT extern int rt_obj_import(struct rt_db_internal *ip, const struct bu_external *ep, const mat_t mat, const struct db_i *dbip);"
)
c = c.replace(
    "RT_EXPORT extern int rt_obj_export(struct bu_external *ep, const struct rt_db_internal *ip, double local2mm, const struct db_i *dbip, struct resource *resp);",
    "RT_EXPORT extern int rt_obj_export(struct bu_external *ep, const struct rt_db_internal *ip, double local2mm, const struct db_i *dbip);\n"
    "DEPRECATED static inline int rt_obj_import_old(struct rt_db_internal *ip, const struct bu_external *ep, const mat_t mat, const struct db_i *dbip, struct resource *resp) { (void)resp; return rt_obj_import(ip, ep, mat, dbip); }\n"
    "DEPRECATED static inline int rt_obj_export_old(struct bu_external *ep, const struct rt_db_internal *ip, double local2mm, const struct db_i *dbip, struct resource *resp) { (void)resp; return rt_obj_export(ep, ip, local2mm, dbip); }"
)
write_file(path, c)
print(f"  Updated: {path}")

# 5. include/rt/rt_instance.h - rt_plot_solid and rt_del_regtree
print("Updating rt_instance.h...")
path = f"{BRLCAD}/include/rt/rt_instance.h"
c = read_file(path)
c = c.replace(
    "/* Plot a solid */\nint rt_plot_solid(\n    FILE                *fp,\n    struct rt_i         *rtip,\n    const struct soltab *stp,\n    struct resource     *resp);",
    "/* Plot a solid */\nint rt_plot_solid(\n    FILE                *fp,\n    struct rt_i         *rtip,\n    const struct soltab *stp);"
)
c = c.replace(
    "RT_EXPORT extern int rt_del_regtree(struct rt_i *rtip,\n\t\t\t\t    struct region *delregp,\n\t\t\t\t    struct resource *resp);",
    "RT_EXPORT extern int rt_del_regtree(struct rt_i *rtip,\n\t\t\t\t    struct region *delregp);"
)
write_file(path, c)
print(f"  Updated: {path}")

# 6. include/rt/tree.h - many functions
print("Updating tree.h...")
path = f"{BRLCAD}/include/rt/tree.h"
c = read_file(path)

c = c.replace(
    "RT_EXPORT extern void db_init_db_tree_state(struct db_tree_state *tsp,\n"
    "\t\t\t\t    struct db_i *dbip,\n"
    "\t\t\t\t    struct resource *resp);",
    "RT_EXPORT extern void db_init_db_tree_state(struct db_tree_state *tsp,\n"
    "\t\t\t\t    struct db_i *dbip);"
)
c = c.replace(
    "RT_EXPORT extern void db_tree_del_lhs(union tree *tp,\n"
    "\t\t\t      struct resource *resp);\n"
    "RT_EXPORT extern void db_tree_del_rhs(union tree *tp,\n"
    "\t\t\t      struct resource *resp);",
    "RT_EXPORT extern void db_tree_del_lhs(union tree *tp);\n"
    "RT_EXPORT extern void db_tree_del_rhs(union tree *tp);"
)
c = c.replace(
    "RT_EXPORT extern int db_tree_del_dbleaf(union tree **tp,\n"
    "\t\t\t\tconst char *cp,\n"
    "\t\t\t\tstruct resource *resp,\n"
    "\t\t\t\tint nflag);",
    "RT_EXPORT extern int db_tree_del_dbleaf(union tree **tp,\n"
    "\t\t\t\tconst char *cp,\n"
    "\t\t\t\tint nflag);"
)
c = c.replace(
    "RT_EXPORT extern union tree *db_dup_subtree(const union tree *tp,\n"
    "\t\t\t\t    struct resource *resp);",
    "RT_EXPORT extern union tree *db_dup_subtree(const union tree *tp);"
)
c = c.replace(
    "RT_EXPORT extern void db_free_tree(union tree *tp,\n"
    "\t\t\t   struct resource *resp);",
    "RT_EXPORT extern void db_free_tree(union tree *tp);"
)
c = c.replace(
    "RT_EXPORT extern void db_non_union_push(union tree *tp,\n"
    "\t\t\t\tstruct resource *resp);",
    "RT_EXPORT extern void db_non_union_push(union tree *tp);"
)
c = c.replace(
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
c = c.replace(
    "RT_EXPORT extern union tree *db_tree_parse(struct bu_vls *vls, const char *str, struct resource *resp);",
    "RT_EXPORT extern union tree *db_tree_parse(struct bu_vls *vls, const char *str);"
)
c = c.replace(
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
c = c.replace(
    "RT_EXPORT extern int rt_tree_elim_nops(union tree *,\n"
    "\t\t\t       struct resource *resp);",
    "RT_EXPORT extern int rt_tree_elim_nops(union tree *);"
)
c = c.replace(
    "RT_EXPORT extern struct rt_tree_array *db_flatten_tree(struct rt_tree_array *rt_tree_array, union tree *tp, int op, int avail, struct resource *resp);",
    "RT_EXPORT extern struct rt_tree_array *db_flatten_tree(struct rt_tree_array *rt_tree_array, union tree *tp, int op, int avail);"
)
c = c.replace(
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
print(f"  Updated: {path}")

# 7. include/rt/db_io.h - rt_db_get_internal5
print("Updating db_io.h...")
path = f"{BRLCAD}/include/rt/db_io.h"
c = read_file(path)
c = c.replace(
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
c = c.replace(
    "RT_EXPORT extern void db_alloc_directory_block(struct resource *resp);",
    "RT_EXPORT extern void db_alloc_directory_block(struct db_i *dbip);"
)
write_file(path, c)
print(f"  Updated: {path}")

# Also update rt_db_external5_to_internal5 declaration in db_io.h
path = f"{BRLCAD}/include/rt/db_io.h"
c = read_file(path)
c = c.replace(
    "RT_EXPORT extern int\nrt_db_external5_to_internal5(\n    struct rt_db_internal *ip,\n    const struct bu_external *ep,\n    const char *name,\n    const struct db_i *dbip,\n    const mat_t mat,\n    struct resource *resp);",
    "RT_EXPORT extern int\nrt_db_external5_to_internal5(\n    struct rt_db_internal *ip,\n    const struct bu_external *ep,\n    const char *name,\n    const struct db_i *dbip,\n    const mat_t mat);"
)
write_file(path, c)
print(f"  Updated rt_db_external5_to_internal5: {path}")

# 8. include/rt/nmg_conv.h - nmg_booltree_evaluate
print("Updating nmg_conv.h...")
path = f"{BRLCAD}/include/rt/nmg_conv.h"
c = read_file(path)
c = c.replace(
    "RT_EXPORT extern union tree *nmg_booltree_evaluate(union tree *tp,\n"
    "\t\t\t\t\t   struct bu_list *vlfree,\n"
    "\t\t\t\t\t   const struct bn_tol *tol,\n"
    "\t\t\t\t\t   struct resource *resp);",
    "RT_EXPORT extern union tree *nmg_booltree_evaluate(union tree *tp,\n"
    "\t\t\t\t\t   struct bu_list *vlfree,\n"
    "\t\t\t\t\t   const struct bn_tol *tol);"
)
write_file(path, c)
print(f"  Updated: {path}")

# ============================================================
# PHASE 1 - Update Implementations
# ============================================================
print("\n=== Phase 1: Updating Implementations ===")

# src/librt/dir.c - rt_db_get_internal, rt_db_put_internal, rt_db_lookup_internal
print("Updating dir.c...")
path = f"{BRLCAD}/src/librt/dir.c"
c = read_file(path)
c = c.replace(
    "int\nrt_db_get_internal(\n    struct rt_db_internal *ip,\n    const struct directory *dp,\n    const struct db_i *dbip,\n    const mat_t mat,\n    struct resource *resp)\n{",
    "int\nrt_db_get_internal(\n    struct rt_db_internal *ip,\n    const struct directory *dp,\n    const struct db_i *dbip,\n    const mat_t mat)\n{"
)
# Update internal call to rt_db_get_internal5 - remove resp arg
c = c.replace(
    "return rt_db_get_internal5(ip, dp, dbip, mat, resp);",
    "return rt_db_get_internal5(ip, dp, dbip, mat);"
)
# Update ft_import4 call - remove resp
c = c.replace(
    "ret = OBJ[id].ft_import4(ip, &ext, mat, dbip, resp);",
    "ret = OBJ[id].ft_import4(ip, &ext, mat, dbip);"
)
c = c.replace(
    "int\nrt_db_put_internal(\n    struct directory *dp,\n    struct db_i *dbip,\n    struct rt_db_internal *ip,\n    struct resource *resp)\n{",
    "int\nrt_db_put_internal(\n    struct directory *dp,\n    struct db_i *dbip,\n    struct rt_db_internal *ip)\n{"
)
# Update internal call to rt_db_put_internal5 - remove resp
c = c.replace(
    "return rt_db_put_internal5(dp, dbip, ip, resp,\n\t\t\t\t   ip->idb_major_type);",
    "return rt_db_put_internal5(dp, dbip, ip,\n\t\t\t\t   ip->idb_major_type);"
)
# Update ft_export4 call
c = c.replace(
    "ret = ip->idb_meth->ft_export4(&ext, ip, 1.0, dbip, resp);",
    "ret = ip->idb_meth->ft_export4(&ext, ip, 1.0, dbip);"
)
c = c.replace(
    "int\nrt_db_lookup_internal (\n    struct db_i *dbip,\n    const char *obj_name,\n    struct directory **dpp,\n    struct rt_db_internal *ip,\n    int noisy,\n    struct resource *resp)\n{",
    "int\nrt_db_lookup_internal (\n    struct db_i *dbip,\n    const char *obj_name,\n    struct directory **dpp,\n    struct rt_db_internal *ip,\n    int noisy)\n{"
)
c = c.replace(
    "if (rt_db_get_internal(ip, dp, dbip, (matp_t) NULL, resp) < 0) {",
    "if (rt_db_get_internal(ip, dp, dbip, (matp_t) NULL) < 0) {"
)
write_file(path, c)
print(f"  Updated: {path}")

# src/librt/db5_io.c - rt_db_get_internal5 and rt_db_external5_to_internal5
print("Updating db5_io.c...")
path = f"{BRLCAD}/src/librt/db5_io.c"
c = read_file(path)
# rt_db_get_internal5
c = c.replace(
    "int\nrt_db_get_internal5(\n    struct rt_db_internal *ip,\n    const struct directory *dp,\n    const struct db_i *dbip,\n    const mat_t mat,\n    struct resource *resp)\n{",
    "int\nrt_db_get_internal5(\n    struct rt_db_internal *ip,\n    const struct directory *dp,\n    const struct db_i *dbip,\n    const mat_t mat)\n{"
)
c = c.replace(
    "    RT_DB_INTERNAL_INIT(ip);\n    if (resp) {\n\tRT_CK_RESOURCE(resp);\n    }\n\n    BU_ASSERT(dbip->i->dbi_version == 5);\n\n    if (db_get_external(&ext, dp, dbip) < 0)\n\treturn -2;\t\t/* FAIL */\n\n    ret = rt_db_external5_to_internal5(ip, &ext, dp->d_namep, dbip, mat, resp);",
    "    RT_DB_INTERNAL_INIT(ip);\n\n    BU_ASSERT(dbip->i->dbi_version == 5);\n\n    if (db_get_external(&ext, dp, dbip) < 0)\n\treturn -2;\t\t/* FAIL */\n\n    ret = rt_db_external5_to_internal5(ip, &ext, dp->d_namep, dbip, mat);"
)
# rt_db_external5_to_internal5
c = c.replace(
    "rt_db_external5_to_internal5(\n    struct rt_db_internal *ip,\n    const struct bu_external *ep,\n    const char *name,\n    const struct db_i *dbip,\n    const mat_t mat,\n    struct resource *resp)\n{",
    "rt_db_external5_to_internal5(\n    struct rt_db_internal *ip,\n    const struct bu_external *ep,\n    const char *name,\n    const struct db_i *dbip,\n    const mat_t mat)\n{"
)
# Remove resp fallback code
c = c.replace(
    "    if (resp) {\n\tRT_CK_RESOURCE(resp);\n    } else {\n\t/* needed for call into functab */\n\tresp = &rt_uniresource;\n    }\n\n    BU_ASSERT(dbip->i->dbi_version == 5);",
    "    BU_ASSERT(dbip->i->dbi_version == 5);"
)
write_file(path, c)
print(f"  Updated: {path}")

# Now we need to update the ft_import5 call inside rt_db_external5_to_internal5
# Let's look at what remains and fix the ft_import5 call
c = read_file(path)
# Find and fix ft_import5 call
c = re.sub(r'(OBJ\[\w+\]\.ft_import5\([^,]+,[^,]+,[^,]+,[^,]+),\s*resp\)', r'\1)', c)
c = re.sub(r'(ip->idb_meth->ft_import5\([^,]+,[^,]+,[^,]+,[^,]+),\s*resp\)', r'\1)', c)
write_file(path, c)
print(f"  Fixed ft_import5 call in: {path}")

# Also update rt_db_put_internal5 signature check
path2 = f"{BRLCAD}/src/librt/db5_io.c"
c = read_file(path2)
# rt_db_put_internal5 - check if it has resp param  
if "rt_db_put_internal5(\n    struct directory *dp,\n    struct db_i *dbip,\n    struct rt_db_internal *ip,\n    struct resource *resp," in c:
    c = c.replace(
        "rt_db_put_internal5(\n    struct directory *dp,\n    struct db_i *dbip,\n    struct rt_db_internal *ip,\n    struct resource *resp,",
        "rt_db_put_internal5(\n    struct directory *dp,\n    struct db_i *dbip,\n    struct rt_db_internal *ip,"
    )
    # Remove resp usage inside
    c = re.sub(r',\s*resp\)', ')', c)
write_file(path2, c)

# src/librt/transform.c - rt_matrix_transform
print("Updating transform.c...")
path = f"{BRLCAD}/src/librt/transform.c"
c = read_file(path)
c = c.replace(
    "rt_matrix_transform(struct rt_db_internal *output, const mat_t matrix, struct rt_db_internal *input, int freeflag, struct db_i *dbip, struct resource *UNUSED(resource))",
    "rt_matrix_transform(struct rt_db_internal *output, const mat_t matrix, struct rt_db_internal *input, int freeflag, struct db_i *dbip)"
)
# Also handle the common form
c = c.replace(
    "rt_matrix_transform(struct rt_db_internal *output, const mat_t matrix, struct rt_db_internal *input, int free_input, struct db_i *dbip, struct resource *UNUSED(resource))",
    "rt_matrix_transform(struct rt_db_internal *output, const mat_t matrix, struct rt_db_internal *input, int free_input, struct db_i *dbip)"
)
c = c.replace(
    "rt_matrix_transform(struct rt_db_internal *output, const mat_t matrix, struct rt_db_internal *input, int free_input, struct db_i *dbip, struct resource *resource)",
    "rt_matrix_transform(struct rt_db_internal *output, const mat_t matrix, struct rt_db_internal *input, int free_input, struct db_i *dbip)"
)
# Fix calls within transform.c
c = re.sub(r'rt_db_get_internal\(([^,]+),\s*([^,]+),\s*([^,]+),\s*([^,]+),\s*[^)]+\)', 
           r'rt_db_get_internal(\1, \2, \3, \4)', c)
c = re.sub(r'rt_db_put_internal\(([^,]+),\s*([^,]+),\s*([^,]+),\s*[^)]+\)',
           r'rt_db_put_internal(\1, \2, \3)', c)
write_file(path, c)
print(f"  Updated: {path}")

# src/librt/primitives/obj_import.c - rt_obj_import
print("Updating obj_import.c...")
path = f"{BRLCAD}/src/librt/primitives/obj_import.c"
c = read_file(path)
c = c.replace(
    "rt_obj_import(struct rt_db_internal *ip, const struct bu_external *ep, const mat_t mat, const struct db_i *dbip, struct resource *resp)",
    "rt_obj_import(struct rt_db_internal *ip, const struct bu_external *ep, const mat_t mat, const struct db_i *dbip)"
)
# Remove resp usage
c = re.sub(r'(ft_import[45]\([^,]+,[^,]+,[^,]+,[^,]+),\s*resp\)', r'\1)', c)
c = re.sub(r'(->ft_import[45]\([^,]+,[^,]+,[^,]+,[^,]+),\s*resp\)', r'\1)', c)
write_file(path, c)
print(f"  Updated: {path}")

# src/librt/primitives/obj_export.c - rt_obj_export
print("Updating obj_export.c...")
path = f"{BRLCAD}/src/librt/primitives/obj_export.c"
c = read_file(path)
c = c.replace(
    "rt_obj_export(struct bu_external *ep, const struct rt_db_internal *ip, double local2mm, const struct db_i *dbip, struct resource *resp)",
    "rt_obj_export(struct bu_external *ep, const struct rt_db_internal *ip, double local2mm, const struct db_i *dbip)"
)
# Remove resp usage
c = re.sub(r'(ft_export[45]\([^,]+,[^,]+,[^,]+,[^,]+),\s*resp\)', r'\1)', c)
c = re.sub(r'(->ft_export[45]\([^,]+,[^,]+,[^,]+,[^,]+),\s*resp\)', r'\1)', c)
write_file(path, c)
print(f"  Updated: {path}")

# src/librt/prep.cpp - rt_plot_solid, rt_del_regtree
print("Updating prep.cpp...")
path = f"{BRLCAD}/src/librt/prep.cpp"
c = read_file(path)
c = c.replace(
    "rt_plot_solid(\n    FILE *fp,\n    struct rt_i *rtip,\n    const struct soltab *stp,\n    struct resource *resp)\n{",
    "rt_plot_solid(\n    FILE *fp,\n    struct rt_i *rtip,\n    const struct soltab *stp)\n{"
)
# Fix the call to rt_vlist_solid within rt_plot_solid
c = c.replace(
    "if (rt_vlist_solid(&vhead, rtip, stp, resp) < 0) {",
    "if (rt_vlist_solid(&vhead, rtip, stp, &rt_uniresource) < 0) {"
)
c = c.replace(
    "rt_del_regtree(struct rt_i *rtip, struct region *delregp, struct resource *resp)\n{",
    "rt_del_regtree(struct rt_i *rtip, struct region *delregp)\n{"
)
c = c.replace(
    "    RT_CK_RESOURCE(resp);\n    RT_CK_REGION(delregp);",
    "    RT_CK_REGION(delregp);"
)
# Fix db_free_tree call in rt_del_regtree
c = c.replace(
    "    db_free_tree(delregp->reg_treetop, resp);",
    "    db_free_tree(delregp->reg_treetop);"
)
# Fix the caller of rt_plot_solid inside prep.cpp
c = c.replace(
    "(void)rt_plot_solid(fp, rtip, stp, resp);",
    "(void)rt_plot_solid(fp, rtip, stp);"
)
write_file(path, c)
print(f"  Updated: {path}")

# src/librt/tree.c - all tree functions
print("Updating tree.c...")
path = f"{BRLCAD}/src/librt/tree.c"
c = read_file(path)

# Read tree.c to find the exact signatures
print(f"  Reading tree.c...")
write_file(path, c)
print(f"  Will update tree.c with regex approach")

# src/librt/primitives/nmg/nmg.c - nmg_booltree_evaluate
print("Updating nmg.c...")
path = f"{BRLCAD}/src/librt/primitives/nmg/nmg.c"
c = read_file(path)
# Find nmg_booltree_evaluate definition
c = re.sub(
    r'(union tree \*\s*\n?nmg_booltree_evaluate\([^,]+,[^,]+,[^,]+),\s*struct resource \*[^)]*\)',
    r'\1)',
    c
)
# Also fix RT_CK_RESOURCE calls in it
write_file(path, c)
print(f"  Updated: {path}")

# src/librt/db_alloc.c - db_alloc_directory_block
print("Updating db_alloc.c...")
path = f"{BRLCAD}/src/librt/db_alloc.c"
c = read_file(path)
write_file(path, c)
print(f"  Will update db_alloc.c separately")

print("\n=== PHASE 1 Headers and core impls done ===")
print("Now doing primitives and tree.c via separate scripts...")
