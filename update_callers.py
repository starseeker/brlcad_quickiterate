#!/usr/bin/env python3
"""
Update all call sites throughout the entire BRL-CAD source tree.
Remove the resource * argument from all function calls.
"""

import re
import os
import glob as g

BRLCAD = "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad"
SRC = f"{BRLCAD}/src"

def read_file(path):
    with open(path, 'r', encoding='utf-8', errors='replace') as f:
        return f.read()

def write_file(path, content):
    with open(path, 'w', encoding='utf-8') as f:
        f.write(content)

def all_source_files():
    """Yield all .c, .cpp, .h files in the source tree."""
    for root, dirs, files in os.walk(BRLCAD):
        # Skip build dirs
        dirs[:] = [d for d in dirs if d not in ['.git', 'CMakeFiles', 'build']]
        for f in files:
            if f.endswith(('.c', '.cpp', '.h', '.cxx')):
                yield os.path.join(root, f)

def remove_last_arg(content, func_name):
    """Remove the last argument from calls to func_name."""
    pattern = r'\b' + re.escape(func_name) + r'\s*\('
    result = []
    i = 0
    while i < len(content):
        m = re.search(pattern, content[i:])
        if not m:
            result.append(content[i:])
            break
        start = i + m.start()
        paren_start = i + m.end() - 1  # position of opening (
        result.append(content[i:start + len(func_name)])
        # Now find matching close paren, tracking nesting
        depth = 0
        j = paren_start
        args = []
        current_arg_start = paren_start + 1
        in_string = False
        in_char = False
        while j < len(content):
            c = content[j]
            if c == '\\' and (in_string or in_char):
                j += 2
                continue
            if c == '"' and not in_char:
                in_string = not in_string
            elif c == "'" and not in_string:
                in_char = not in_char
            elif not in_string and not in_char:
                if c in '([{':
                    depth += 1
                elif c in ')]}':
                    depth -= 1
                    if depth == 0:
                        args.append(content[current_arg_start:j])
                        break
                elif c == ',' and depth == 1:
                    args.append(content[current_arg_start:j])
                    current_arg_start = j + 1
            j += 1
        # Remove last arg
        if len(args) > 1:
            args = args[:-1]
            # Strip trailing whitespace/newline from last remaining arg
            args[-1] = args[-1].rstrip()
        result.append('(' + ','.join(args) + ')')
        i = j + 1
    return ''.join(result)

def remove_nth_arg(content, func_name, n):
    """Remove the nth argument (0-indexed) from calls to func_name."""
    pattern = r'\b' + re.escape(func_name) + r'\s*\('
    result = []
    i = 0
    while i < len(content):
        m = re.search(pattern, content[i:])
        if not m:
            result.append(content[i:])
            break
        start = i + m.start()
        paren_start = i + m.end() - 1
        result.append(content[i:start + len(func_name)])
        depth = 0
        j = paren_start
        args = []
        current_arg_start = paren_start + 1
        in_string = False
        in_char = False
        while j < len(content):
            c = content[j]
            if c == '\\' and (in_string or in_char):
                j += 2
                continue
            if c == '"' and not in_char:
                in_string = not in_string
            elif c == "'" and not in_string:
                in_char = not in_char
            elif not in_string and not in_char:
                if c in '([{':
                    depth += 1
                elif c in ')]}':
                    depth -= 1
                    if depth == 0:
                        args.append(content[current_arg_start:j])
                        break
                elif c == ',' and depth == 1:
                    args.append(content[current_arg_start:j])
                    current_arg_start = j + 1
            j += 1
        if n < len(args):
            del args[n]
        result.append('(' + ','.join(args) + ')')
        i = j + 1
    return ''.join(result)

# Functions where we remove the LAST argument
REMOVE_LAST_FUNCS = [
    "rt_db_get_internal",
    "rt_db_put_internal",
    "rt_db_lookup_internal",
    "rt_matrix_transform",
    "rt_obj_import",
    "rt_obj_export",
    "rt_plot_solid",
    "rt_del_regtree",
    "db_tree_parse",
    "db_flatten_tree",
    "db_free_tree",
    "db_dup_subtree",
    "db_non_union_push",
    "db_tree_del_lhs",
    "db_tree_del_rhs",
    "db_tally_subtree_regions",
    "db_tree_flatten_describe",
    "rt_tree_elim_nops",
    "db_init_db_tree_state",
    "nmg_booltree_evaluate",
    "rt_db_get_internal5",
    "rt_db_external5_to_internal5",
    # Function pointer calls on ft_* structs
]

# Special function pointer patterns
FT_PATTERNS = [
    # import5/export5/import4/export4 via ft_* pointer
    (r'((?:->|\.)\s*ft_import5\s*\([^,]+,\s*[^,]+,\s*[^,]+,\s*[^,]+),\s*[^)]+\)', r'\1)'),
    (r'((?:->|\.)\s*ft_export5\s*\([^,]+,\s*[^,]+,\s*[^,]+,\s*[^,]+),\s*[^)]+\)', r'\1)'),
    (r'((?:->|\.)\s*ft_import4\s*\([^,]+,\s*[^,]+,\s*[^,]+,\s*[^,]+),\s*[^)]+\)', r'\1)'),
    (r'((?:->|\.)\s*ft_export4\s*\([^,]+,\s*[^,]+,\s*[^,]+,\s*[^,]+),\s*[^)]+\)', r'\1)'),
    # rt_db_put_internal5 - also has resp removed (it's not in REMOVE_LAST list since it also changes)
    (r'rt_db_put_internal5\(([^,]+),\s*([^,]+),\s*([^,]+),\s*(?:resp|&rt_uniresource|resource|[a-z_]+_resp|resp_ptr),\s*([^)]+)\)',
     r'rt_db_put_internal5(\1, \2, \3, \4)'),
]

total_files_changed = 0

for path in all_source_files():
    try:
        original = read_file(path)
    except Exception:
        continue
    
    content = original
    
    # Apply simple last-arg removal
    for func in REMOVE_LAST_FUNCS:
        if func in content:
            content = remove_last_arg(content, func)
    
    # Special case: db_tree_del_dbleaf - remove 3rd arg (resp), keep 4th (nflag)
    if 'db_tree_del_dbleaf' in content:
        content = remove_nth_arg(content, 'db_tree_del_dbleaf', 2)
    
    # Special case: db_functree - remove 5th arg (resp), keep 6th (client_data)
    if 'db_functree' in content:
        content = remove_nth_arg(content, 'db_functree', 4)
    
    # Apply FT pattern fixups
    for pattern, replacement in FT_PATTERNS:
        new_content = re.sub(pattern, replacement, content)
        if new_content != content:
            content = new_content
    
    if content != original:
        write_file(path, content)
        total_files_changed += 1
        print(f"  {path}")

print(f"\nTotal files changed: {total_files_changed}")
