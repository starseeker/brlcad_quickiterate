# Embedded µlight (ulight) — Syntax Highlighter

This directory contains a vendor copy of
[µlight](https://github.com/eisenwave/ulight) (commit 559846a),
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
