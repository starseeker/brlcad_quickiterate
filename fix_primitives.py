#!/usr/bin/env python3
"""
Remove struct resource * from all primitive import/export function definitions.
"""

import re
import os

PRIMITIVES_DIR = "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/librt/primitives"

def read_file(path):
    with open(path, 'r', encoding='utf-8', errors='replace') as f:
        return f.read()

def write_file(path, content):
    with open(path, 'w', encoding='utf-8') as f:
        f.write(content)

count = 0

for root, dirs, files in os.walk(PRIMITIVES_DIR):
    for fname in files:
        if not fname.endswith(('.c', '.cpp', '.cxx')):
            continue
        path = os.path.join(root, fname)
        original = read_file(path)
        c = original
        
        # Pattern 1: Remove ", struct resource *resp" or ", struct resource *UNUSED(resp)"
        # at the end of rt_*_import4/5/export4/5 function parameter lists
        
        # Remove the last param when it's resource from function definitions matching
        # rt_xxx_import4/5 or rt_xxx_export4/5 function signatures
        
        # These functions have the pattern:
        # rt_xxx_import5(
        #     struct rt_db_internal *ip,
        #     const struct bu_external *ep,
        #     const mat_t mat,
        #     const struct db_i *dbip,
        #     struct resource *resp)
        
        # Remove ", struct resource *resp)" or ", struct resource *UNUSED(resp))" from definitions
        # The param can be on its own line preceded by whitespace/tab
        
        # Handle multiline signatures: the resource param on its own line
        c = re.sub(
            r',\s*\n(\s*)struct resource \*(?:UNUSED\([a-zA-Z_]+\)|[a-zA-Z_]*)\)',
            r')',
            c
        )
        
        # Handle single-line: at end of function params
        c = re.sub(
            r',\s*struct resource \*(?:UNUSED\([a-zA-Z_]+\)|[a-zA-Z_]*)\)',
            r')',
            c
        )
        
        # Also remove RT_CK_RESOURCE calls for unused params
        # Only where resp is definitely not used (just checked and discarded)
        # We'll do a targeted removal where the function no longer takes resp
        
        if c != original:
            write_file(path, c)
            count += 1
            print(f"  {path}")

print(f"\nTotal primitives files changed: {count}")

# Also do the same for non-primitives librt files that define import/export
OTHER_DIRS = [
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/librt/binunif",
    "/home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad/src/librt/comb",
]
for d in OTHER_DIRS:
    for root, dirs, files in os.walk(d):
        for fname in files:
            if not fname.endswith(('.c', '.cpp', '.cxx')):
                continue
            path = os.path.join(root, fname)
            original = read_file(path)
            c = original
            c = re.sub(
                r',\s*\n(\s*)struct resource \*(?:UNUSED\([a-zA-Z_]+\)|[a-zA-Z_]*)\)',
                r')',
                c
            )
            c = re.sub(
                r',\s*struct resource \*(?:UNUSED\([a-zA-Z_]+\)|[a-zA-Z_]*)\)',
                r')',
                c
            )
            if c != original:
                write_file(path, c)
                print(f"  {path}")
