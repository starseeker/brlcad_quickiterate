#!/usr/bin/env python3
"""
Refresh vendor/ulight/ from a local clone of µlight.

Usage
-----
  git clone --depth=1 https://github.com/eisenwave/ulight.git /tmp/ulight
  python3 vendor/tools/amalgamate_ulight.py --src /tmp/ulight

The script copies only the files required to build the embedded static library
(no CLI, no WASM, no tests) and the public C/C++ headers into vendor/ulight/.

It also records the upstream commit SHA in vendor/ulight/README.md.

The resulting layout under vendor/ulight/:
  include/ulight/ulight.h          – public C API  (C++17-compatible)
  include/ulight/ulight.hpp        – C++ wrapper   (C++20-compatible)
  include/ulight/function_ref.hpp
  include/ulight/const.hpp
  include/ulight/impl/**/*.hpp
  include/ulight/impl/platform.h
  src/chars.cpp
  src/io.cpp
  src/parse_utils.cpp
  src/ulight.cpp
  src/lang/*.cpp
  LICENSE
  README.md  (updated with new commit SHA)
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path


def git_head_sha(repo: Path) -> str:
    try:
        result = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=repo,
            capture_output=True,
            text=True,
            check=True,
        )
        return result.stdout.strip()
    except Exception:
        return "(unknown)"


def copy_tree(src_dir: Path, dst_dir: Path, pattern: str = "*") -> list[Path]:
    """Copy files matching *pattern* from src_dir to dst_dir, creating dirs as needed."""
    copied = []
    for src in sorted(src_dir.glob(pattern)):
        if src.is_file():
            dst = dst_dir / src.name
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)
            copied.append(dst)
    return copied


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Refresh vendor/ulight/ from a local µlight clone."
    )
    parser.add_argument(
        "--src",
        default=None,
        help="Path to the µlight repository root (default: /tmp/ulight)",
    )
    parser.add_argument(
        "--out",
        default=None,
        help="Destination directory (default: vendor/ulight/ next to this script)",
    )
    args = parser.parse_args()

    script_dir = Path(__file__).resolve().parent
    repo_root = script_dir.parent.parent  # asciiquack root

    src = Path(args.src).resolve() if args.src else Path("/tmp/ulight")
    dst = Path(args.out).resolve() if args.out else (repo_root / "vendor" / "ulight")

    if not src.is_dir():
        sys.exit(
            f"ERROR: µlight source directory not found: {src}\n"
            "  Clone it with:\n"
            "    git clone --depth=1 https://github.com/eisenwave/ulight.git /tmp/ulight"
        )

    sha = git_head_sha(src)
    print(f"µlight commit: {sha}")
    print(f"Source : {src}")
    print(f"Dest   : {dst}")

    # ── Clean destination ────────────────────────────────────────────────────
    if dst.exists():
        shutil.rmtree(dst)

    # ── Copy public headers ──────────────────────────────────────────────────
    include_src = src / "include" / "ulight"
    include_dst = dst / "include" / "ulight"

    # Top-level headers
    for f in sorted(include_src.glob("*.h")) + sorted(include_src.glob("*.hpp")):
        dest_f = include_dst / f.name
        dest_f.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(f, dest_f)

    # impl/ (flat + subdirs)
    for sub in ["", "algorithm", "lang"]:
        sub_src = include_src / "impl" / sub if sub else include_src / "impl"
        sub_dst = include_dst / "impl" / sub if sub else include_dst / "impl"
        if sub_src.is_dir():
            sub_dst.mkdir(parents=True, exist_ok=True)
            for f in sorted(sub_src.iterdir()):
                if f.is_file() and f.suffix in (".h", ".hpp"):
                    shutil.copy2(f, sub_dst / f.name)

    # ── Copy library source files ─────────────────────────────────────────────
    lib_src_root = src / "src" / "main" / "cpp"
    lib_dst_root = dst / "src"

    # Top-level TUs
    for name in ("chars.cpp", "io.cpp", "parse_utils.cpp", "ulight.cpp"):
        f = lib_src_root / name
        if f.is_file():
            dest_f = lib_dst_root / name
            dest_f.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(f, dest_f)

    # Language-specific TUs
    lang_src = lib_src_root / "lang"
    lang_dst = lib_dst_root / "lang"
    lang_dst.mkdir(parents=True, exist_ok=True)
    for f in sorted(lang_src.glob("*.cpp")):
        shutil.copy2(f, lang_dst / f.name)

    # ── LICENSE ──────────────────────────────────────────────────────────────
    shutil.copy2(src / "LICENSE", dst / "LICENSE")

    # ── README ───────────────────────────────────────────────────────────────
    readme = dst / "README.md"
    readme.write_text(
        f"""\
# Embedded µlight (ulight) — Syntax Highlighter

This directory contains a vendor copy of
[µlight](https://github.com/eisenwave/ulight) (commit `{sha}`),
a zero-dependency, lightweight syntax highlighter written in C++23.

## License

MIT — see `LICENSE`.

## Languages supported

Bash, C, C++, CSS, Diff, EBNF, HTML, JavaScript, JSON, JSON-with-comments,
Kotlin, LaTeX, LLVM IR, Lua, NASM, Python, Rust, TeX, Plaintext, TypeScript, XML

## Regenerating from upstream

```bash
git clone --depth=1 https://github.com/eisenwave/ulight.git /tmp/ulight
python3 vendor/tools/amalgamate_ulight.py --src /tmp/ulight
```

## Build notes

The source files in `src/` require **C++23** to compile.
`CMakeLists.txt` adds a static library target `ulight_vendor`
whose source files are built with `cxx_std_23`.
The public API header `include/ulight/ulight.h` is a plain C header
and can be used from C++17 code without issue.
""",
        encoding="utf-8",
    )

    # ── Summary ───────────────────────────────────────────────────────────────
    all_files = sorted(dst.rglob("*"))
    n_files = sum(1 for f in all_files if f.is_file() and not f.name == "README.md")
    print(f"Copied {n_files} files into {dst}")


if __name__ == "__main__":
    main()
