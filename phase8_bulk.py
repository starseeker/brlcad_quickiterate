#!/usr/bin/env python3
"""
Phase 8 bulk transformations: remove struct resource, rt_uniresource,
and all related infrastructure from BRL-CAD source files.
"""

import re
import os
import sys

BRLCAD_ROOT = "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad"

def read_file(path):
    with open(path, 'r', encoding='utf-8', errors='replace') as f:
        return f.read()

def write_file(path, content):
    with open(path, 'w', encoding='utf-8') as f:
        f.write(content)

def transform(c, path):
    original = c

    # ── Remove RT_CK_RESOURCE(...); lines ──────────────────────────────────
    c = re.sub(r'[ \t]*RT_CK_RESOURCE\s*\([^)]+\)\s*;\s*\n', '', c)

    # ── Remove rt_init_resource(...); calls ────────────────────────────────
    c = re.sub(r'[ \t]*rt_init_resource\s*\([^)]+\)\s*;\s*\n', '', c)

    # ── Remove rt_clean_resource*(...); calls ─────────────────────────────
    c = re.sub(r'[ \t]*rt_clean_resource\w*\s*\([^)]+\)\s*;\s*\n', '', c)

    # ── Remove rt_add_res_stats / rt_zero_res_stats calls ─────────────────
    c = re.sub(r'[ \t]*rt_add_res_stats\s*\([^)]+\)\s*;\s*\n', '', c)
    c = re.sub(r'[ \t]*rt_zero_res_stats\s*\([^)]+\)\s*;\s*\n', '', c)

    # ── Remove "struct resource rt_uniresource = ...;" global ─────────────
    c = re.sub(r'[ \t]*struct\s+resource\s+rt_uniresource\s*=\s*[^;]+;\s*\n', '', c)

    # ── Remove local struct resource variable declarations ─────────────────
    # Matches:  struct resource  resp;   struct resource *resp = NULL; etc.
    c = re.sub(r'[ \t]*struct\s+resource\s+\*?\s*\w+\s*=\s*[^;]+;\s*\n', '', c)
    c = re.sub(r'[ \t]*struct\s+resource\s+\*?\s*\w+\s*;\s*\n', '', c)

    # ── Replace RESOURCE_NULL → NULL ──────────────────────────────────────
    c = c.replace('RESOURCE_NULL', 'NULL')

    # ── Remove ap->a_resource = ... assignments ────────────────────────────
    c = re.sub(r'[ \t]*ap\s*->\s*a_resource\s*=\s*[^;]+;\s*\n', '', c)
    c = re.sub(r'[ \t]*application\s*\.\s*a_resource\s*=\s*[^;]+;\s*\n', '', c)
    c = re.sub(r'[ \t]*\w+\.a_resource\s*=\s*[^;]+;\s*\n', '', c)

    # ── Replace ap->a_resource->re_cpu → ap->a_cpu ────────────────────────
    c = re.sub(r'ap\s*->\s*a_resource\s*->\s*re_cpu', 'ap->a_cpu', c)

    # ── Remove ts_resp = ... assignments ──────────────────────────────────
    c = re.sub(r'[ \t]*\w+\s*->\s*ts_resp\s*=\s*[^;]+;\s*\n', '', c)
    c = re.sub(r'[ \t]*\w+\.ts_resp\s*=\s*[^;]+;\s*\n', '', c)

    # ── Remove wdb_resp = ... assignments ─────────────────────────────────
    c = re.sub(r'[ \t]*\w+\s*->\s*wdb_resp\s*=\s*[^;]+;\s*\n', '', c)
    c = re.sub(r'[ \t]*\w+\.wdb_resp\s*=\s*[^;]+;\s*\n', '', c)

    # ── db_update_nref(x, y) → db_update_nref(x) ─────────────────────────
    # Remove second argument (the resource pointer)
    c = re.sub(
        r'db_update_nref\s*\(\s*([^,)]+)\s*,\s*[^)]+\)',
        r'db_update_nref(\1)',
        c
    )

    # ── db_path_to_mat(a,b,c,d,e) → db_path_to_mat(a,b,c,d) ──────────────
    def strip_last_arg(m):
        args = m.group(1)
        # Split on commas, but respect nesting
        parts = []
        depth = 0
        cur = []
        for ch in args:
            if ch in '([{':
                depth += 1
            elif ch in ')]}':
                depth -= 1
            if ch == ',' and depth == 0:
                parts.append(''.join(cur).strip())
                cur = []
            else:
                cur.append(ch)
        if cur:
            parts.append(''.join(cur).strip())
        if len(parts) > 1:
            return m.group(0).split('(')[0] + '(' + ', '.join(parts[:-1]) + ')'
        return m.group(0)

    c = re.sub(
        r'db_path_to_mat\s*\(([^)]*)\)',
        strip_last_arg,
        c
    )
    c = re.sub(
        r'db_fp_op\s*\(([^)]*)\)',
        strip_last_arg,
        c
    )
    c = re.sub(
        r'db_full_path_color\s*\(([^)]*)\)',
        strip_last_arg,
        c
    )

    # ── rt_optim_tree(tp, resp) → rt_optim_tree(tp) ───────────────────────
    c = re.sub(
        r'rt_optim_tree\s*\(([^,)]+)\s*,\s*[^)]+\)',
        r'rt_optim_tree(\1)',
        c
    )

    # ── rt_get_solidbitv(n, resp) → rt_get_solidbitv(n) ───────────────────
    c = re.sub(
        r'rt_get_solidbitv\s*\(([^,)]+)\s*,\s*[^)]+\)',
        r'rt_get_solidbitv(\1)',
        c
    )

    # ── rt_find_paths(a,b,c,d,e) → rt_find_paths(a,b,c,d) ────────────────
    c = re.sub(
        r'rt_find_paths\s*\(([^)]*)\)',
        strip_last_arg,
        c
    )

    # ── rt_vlist_solid(a,b,c,d) → rt_vlist_solid(a,b,c) ──────────────────
    c = re.sub(
        r'rt_vlist_solid\s*\(([^)]*)\)',
        strip_last_arg,
        c
    )

    # ── rt_plot_all_solids(a,b,c) → rt_plot_all_solids(a,b) ──────────────
    c = re.sub(
        r'rt_plot_all_solids\s*\(([^)]*)\)',
        strip_last_arg,
        c
    )

    # ── fill_out_bsp(a,b,c,d) → fill_out_bsp(a,b,d) ──────────────────────
    # (remove the 3rd arg which is resp - tricky, skip; handled manually below)

    # ── GED_DB_GET_INTERNAL(a,b,c,d,e,f) → GED_DB_GET_INTERNAL(a,b,c,d,f) ─
    # Remove 5th of 6 args
    def ged_get_internal_fix(m):
        args_str = m.group(1)
        parts = []
        depth = 0
        cur = []
        for ch in args_str:
            if ch in '([{':
                depth += 1
            elif ch in ')]}':
                depth -= 1
            if ch == ',' and depth == 0:
                parts.append(''.join(cur).strip())
                cur = []
            else:
                cur.append(ch)
        if cur:
            parts.append(''.join(cur).strip())
        if len(parts) == 6:
            # remove index 4 (the _resource arg)
            del parts[4]
        return 'GED_DB_GET_INTERNAL(' + ', '.join(parts) + ')'

    c = re.sub(r'GED_DB_GET_INTERNAL\s*\(([^)]*)\)', ged_get_internal_fix, c)

    # ── GED_DB_PUT_INTERNAL(a,b,c,d,e) → GED_DB_PUT_INTERNAL(a,b,c,d) ────
    def ged_put_internal_fix(m):
        args_str = m.group(1)
        parts = []
        depth = 0
        cur = []
        for ch in args_str:
            if ch in '([{':
                depth += 1
            elif ch in ')]}':
                depth -= 1
            if ch == ',' and depth == 0:
                parts.append(''.join(cur).strip())
                cur = []
            else:
                cur.append(ch)
        if cur:
            parts.append(''.join(cur).strip())
        if len(parts) == 5:
            # remove index 3 (the _resource arg)
            del parts[3]
        return 'GED_DB_PUT_INTERNAL(' + ', '.join(parts) + ')'

    c = re.sub(r'GED_DB_PUT_INTERNAL\s*\(([^)]*)\)', ged_put_internal_fix, c)

    # ── rt_unprep(a,b,c) → rt_unprep(a,b) ────────────────────────────────
    c = re.sub(
        r'rt_unprep\s*\(([^)]*)\)',
        strip_last_arg,
        c
    )

    # ── rt_reprep(a,b,c) → rt_reprep(a,b) ────────────────────────────────
    c = re.sub(
        r'rt_reprep\s*\(([^)]*)\)',
        strip_last_arg,
        c
    )

    # ── re_prep_solids(a,b,c,d) → re_prep_solids(a,b,c) ──────────────────
    c = re.sub(
        r're_prep_solids\s*\(([^)]*)\)',
        strip_last_arg,
        c
    )

    return c

def should_skip(path):
    # Skip generated files, build dirs, and our own scripts
    skip_fragments = ['/CMakeFiles/', '/.git/', 'brlcad_build/', 'bext_output/']
    for frag in skip_fragments:
        if frag in path:
            return True
    return False

EXTENSIONS = ('.c', '.cpp', '.cxx', '.cc', '.h')

changed = 0
total = 0

src_dirs = [
    os.path.join(BRLCAD_ROOT, 'src'),
    os.path.join(BRLCAD_ROOT, 'include'),
]

for src_dir in src_dirs:
    for root, dirs, files in os.walk(src_dir):
        # Skip hidden dirs
        dirs[:] = [d for d in dirs if not d.startswith('.')]
        for fname in files:
            if not any(fname.endswith(ext) for ext in EXTENSIONS):
                continue
            path = os.path.join(root, fname)
            if should_skip(path):
                continue
            try:
                original = read_file(path)
            except Exception as e:
                print(f"Error reading {path}: {e}")
                continue
            c = transform(original, path)
            if c != original:
                write_file(path, c)
                changed += 1
                if '--verbose' in sys.argv:
                    print(f"Modified: {path}")
            total += 1

print(f"Processed {total} files, modified {changed}")
