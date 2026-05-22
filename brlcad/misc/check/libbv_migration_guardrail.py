#!/usr/bin/env python3
"""Generate and enforce libbv migration guardrail inventories."""

from __future__ import annotations

import argparse
import datetime
import pathlib
import sys
from dataclasses import dataclass


@dataclass(frozen=True)
class Query:
    key: str
    description: str
    pattern: str
    fixed: bool = True


QUERIES: tuple[Query, ...] = (
    Query("include_bv_slash_angle", "#include <bv/...>", "<bv/"),
    Query("include_bv_slash_quote", "#include \"bv/...\"", "\"bv/"),
    Query("include_bv_h_angle", "#include <bv.h>", "<bv.h>"),
    Query("include_bv_h_quote", "#include \"bv.h\"", "\"bv.h\""),
    Query("bv_scene_obj", "bv_scene_obj references", "bv_scene_obj"),
    Query("struct_bview", "struct bview references", "struct bview"),
    Query("struct_bview_settings", "struct bview_settings references", "struct bview_settings"),
    Query("struct_bview_set", "struct bview_set references", "struct bview_set"),
    Query("libbv", "libbv references", "libbv"),
    Query("LIBBV", "LIBBV references", "LIBBV"),
    Query("BV_EXPORT", "BV_EXPORT references", "BV_EXPORT"),
    Query("BV_DLL", "BV_DLL references", "BV_DLL"),
)


def _run_rg(source_root: pathlib.Path, query: Query) -> list[str]:
    files: list[str] = []
    roots = [source_root / "include", source_root / "src"]
    root_cmake = source_root / "CMakeLists.txt"
    if root_cmake.exists():
        roots.append(root_cmake)

    def _matches(path: pathlib.Path) -> bool:
        if not path.is_file():
            return False
        try:
            content = path.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            return False
        if query.fixed:
            return query.pattern in content
        return False

    for r in roots:
        if r.is_file():
            if _matches(r):
                files.append(str(r.resolve().relative_to(source_root.resolve())))
            continue
        for path in r.rglob("*"):
            if _matches(path):
                files.append(str(path.resolve().relative_to(source_root.resolve())))

    return sorted(set(files))


def collect(source_root: pathlib.Path) -> dict[str, list[str]]:
    return {q.key: _run_rg(source_root, q) for q in QUERIES}


def write_inventory(path: pathlib.Path, source_root: pathlib.Path, data: dict[str, list[str]]) -> None:
    now = datetime.datetime.now(datetime.UTC).strftime("%Y-%m-%dT%H:%M:%SZ")
    lines: list[str] = []
    lines.append("libbv Migration Reference Inventory")
    lines.append("==================================")
    lines.append("")
    lines.append(f"Generated: {now}")
    lines.append("Generator: misc/check/libbv_migration_guardrail.py")
    lines.append(f"Source root: {source_root}")
    lines.append("")
    lines.append("This file is machine-generated. Refresh with:")
    lines.append("  python3 brlcad/misc/check/libbv_migration_guardrail.py --refresh --source-root /abs/path/to/brlcad")
    lines.append("")
    for query in QUERIES:
        files = data[query.key]
        lines.append(query.description)
        lines.append("-" * len(query.description))
        lines.append(f"Key: {query.key}")
        lines.append(f"Pattern: {query.pattern}")
        lines.append(f"Match files: {len(files)}")
        if files:
            lines.append("")
            for f in files:
                lines.append(f"- {f}")
        lines.append("")

    path.write_text("\n".join(lines), encoding="utf-8")


def write_whitelist(path: pathlib.Path, data: dict[str, list[str]]) -> None:
    now = datetime.datetime.now(datetime.UTC).strftime("%Y-%m-%dT%H:%M:%SZ")
    lines: list[str] = []
    lines.append("# libbv migration whitelist")
    lines.append("# Generated file: do not edit by hand.")
    lines.append("# Format: <key>\t<relative_path>")
    lines.append(f"# Generated: {now}")
    lines.append("# Generator: misc/check/libbv_migration_guardrail.py")
    lines.append("")
    for query in QUERIES:
        for f in data[query.key]:
            lines.append(f"{query.key}\t{f}")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def read_whitelist(path: pathlib.Path) -> dict[str, set[str]]:
    allowed: dict[str, set[str]] = {q.key: set() for q in QUERIES}
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split("\t", 1)
        if len(parts) != 2:
            raise ValueError(f"Invalid whitelist line: {line}")
        key, rel = parts
        if key not in allowed:
            raise ValueError(f"Unknown whitelist key '{key}' in {path}")
        allowed[key].add(rel)
    return allowed


def check_against_whitelist(data: dict[str, list[str]], whitelist: dict[str, set[str]]) -> int:
    errors: list[str] = []
    for query in QUERIES:
        key = query.key
        actual = set(data[key])
        allowed = whitelist[key]
        unexpected = sorted(actual - allowed)
        if unexpected:
            errors.append(f"{key} has {len(unexpected)} unexpected file(s):")
            errors.extend([f"  - {f}" for f in unexpected])

    if errors:
        print("libbv migration guardrail failed.")
        print("Unexpected references outside the approved whitelist were found:")
        print("\n".join(errors))
        return 1

    print("libbv migration guardrail passed.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source-root",
        type=pathlib.Path,
        default=pathlib.Path(__file__).resolve().parents[2],
        help="Path to BRL-CAD source root (default: inferred from script location)",
    )
    parser.add_argument(
        "--inventory",
        type=pathlib.Path,
        default=None,
        help="Inventory output path (default: <source-root>/doc/notes/libbv_migration_inventory.txt)",
    )
    parser.add_argument(
        "--whitelist",
        type=pathlib.Path,
        default=None,
        help="Whitelist path (default: <source-root>/doc/notes/libbv_migration_whitelist.txt)",
    )
    parser.add_argument("--refresh", action="store_true", help="Regenerate inventory and whitelist")
    parser.add_argument("--check", action="store_true", help="Check current references against whitelist")

    args = parser.parse_args()
    source_root = args.source_root.resolve()
    inventory_path = (args.inventory or (source_root / "doc/notes/libbv_migration_inventory.txt")).resolve()
    whitelist_path = (args.whitelist or (source_root / "doc/notes/libbv_migration_whitelist.txt")).resolve()

    if not args.refresh and not args.check:
        parser.error("Specify at least one of --refresh or --check")

    data = collect(source_root)

    if args.refresh:
        write_inventory(inventory_path, source_root, data)
        write_whitelist(whitelist_path, data)

    if args.check:
        if not whitelist_path.exists():
            print(f"Whitelist file not found: {whitelist_path}", file=sys.stderr)
            return 2
        whitelist = read_whitelist(whitelist_path)
        return check_against_whitelist(data, whitelist)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
