# BRL-CAD Documentation Build Benchmark: DocBook vs AsciiDoc/asciiquack

## Summary

Switching from the DocBook toolchain to AsciiDoc/asciiquack delivers a
**22–31× speedup** in documentation generation wall time when generating
both HTML and man pages per source file (apples-to-apples).

---

## System / Environment

| | |
|---|---|
| **OS** | Linux 6.14.0-1017-azure x86_64 |
| **CPUs** | 4 cores (Azure-hosted runner) |
| **Compiler** | GCC 13 |
| **DocBook tool** | xsltproc (bundled in bext\_output/noinstall/bin) |
| **AsciiDoc tool** | asciiquack 0.1.0 (bundled in bext\_output/noinstall/bin) |
| **Source corpus** | 183 man1 pages + 266 mann pages = **449 files** |
| **Output formats** | Man page **and** HTML per source file (both pipelines) |

---

## Benchmark Results

Both pipelines generate the same two output formats per source file:
a man page (troff) and an HTML page.

| Measurement | DocBook | asciiquack | Speedup |
|---|---:|---:|---:|
| cmake configure (EXTRADOCS=ON) | **69.5 s** | *not needed* | — |
| cmake xsl-expand (extract XSL stylesheets) | **~3 s** | *not needed* | — |
| Sequential HTML+man gen, 1 job (449 files) | **136.0 s** | 4.4 s | **31.2×** |
| Parallel HTML+man gen, -j4 (449 files) | **53.6 s** | 2.4 s | **22.4×** |
| Per-file latency, 2 formats (sequential) | **302.8 ms/file** | 9.7 ms/file | **31.2×** |
| cmake mann build, -j4 (266 files, HTML+man) | **48.0 s** | 2.9 s | **16.8×** |
| cmake man1 build, -j4 (183 files, HTML+man) | *(no grouped target)* | 2.4 s | — |
| In-process throughput (bench\_asciiquack, no startup overhead) | — | **2,759 files/s, 362 µs/file** | — |

---

## Setup Cost Comparison

The DocBook pipeline has a **one-time build-directory setup cost** that
asciiquack completely avoids:

| Setup step | DocBook | asciiquack |
|---|---:|---:|
| cmake configure (generates .xsl from .xsl.in templates) | ~70 s | *none* |
| xsl-expand (extract 12 MB docbook-xsl-ns.tar.bz2) | ~3 s | *none* |
| **Total setup** | **~73 s** | **0 s** |

asciiquack is a self-contained binary.  No XSL stylesheets to extract, no
catalog files, no CMake configure step needed for the documentation build.

---

## What Each Pipeline Generates Per Source File

| Output | DocBook tool invocation | asciiquack tool invocation |
|---|---|---|
| Man page | `xsltproc STYLESHEET XML` | `asciiquack -b manpage -d manpage` |
| HTML page | `xsltproc XHTML_STYLESHEET XML` | `asciiquack -b html5 -d article` |

Both pipelines process the same 449 source files and produce the same two
output formats.  The timing numbers above measure both formats together.

---

## CMake Build Target Comparison

| Pipeline | Target | Formats built |
|---|---|---|
| DocBook | `docbook-docbook-system-mann` | HTML + man page |
| AsciiDoc | `asciidoc-asciidoc-system-mann` | HTML + man page |
| AsciiDoc | `asciidoc-asciidoc-system-man1` | HTML + man page |

Note: DocBook man1 pages are each defined individually in CMakeLists.txt (one
`add_docbook()` call per file) so there is no single grouped
`docbook-docbook-system-man1` target.  The direct-tool numbers cover man1.

---

## Where the Speedup Comes From

| Factor | Detail |
|---|---|
| **Process startup per file** | Each `xsltproc` invocation loads libxslt, libxml2, and parses the full DocBook XSL stylesheet tree (~150 KB of XSLT) before processing a single source line.  Two invocations per file (man + HTML) means double the startup overhead.  asciiquack starts up and completes both formats in the time xsltproc finishes loading once. |
| **XSLT stylesheet complexity** | The DocBook man-page stylesheets import the upstream DocBook XSL library (hundreds of named templates evaluated for every file). |
| **Compiled vs interpreted** | asciiquack is compiled C++ with an optimised hand-written block scanner.  xsltproc interprets XSLT 1.0, a tree-transformation language. |
| **Single binary, multiple backends** | asciiquack switches output format (manpage vs html5) in the same process; each format costs roughly the same (~5 ms/file).  xsltproc must load an entirely different stylesheet tree for each format (~150 ms/file each). |

---

## Benchmark Methodology

Run `benchmark_docs.sh` from the repository root:

```bash
# First run: also times cmake configure + xsl-expand steps
./benchmark_docs.sh --build-dir /tmp/brlcad_bench_build

# Subsequent runs (configure already done):
./benchmark_docs.sh --skip-configure --build-dir /tmp/brlcad_bench_build
```

The script:
1. Configures a cmake build with `BRLCAD_EXTRADOCS=ON`
2. Builds the `xsl-expand` target to extract bundled DocBook XSL stylesheets
3. Times sequential HTML+man generation: two `xsltproc` calls per file (one with
   `brlcad-man-stylesheet.xsl` → man page, one with `brlcad-man-xhtml-stylesheet.xsl`
   → HTML) vs two `asciiquack` calls per file (`-b manpage` and `-b html5`)
4. Repeats the same measurement in parallel (`-j N` via `xargs -P`)
5. Times cmake grouped targets: `docbook-docbook-system-mann` vs
   `asciidoc-asciidoc-system-mann` and `asciidoc-asciidoc-system-man1`
   (all now generating HTML + man pages)
6. Runs `bench_asciiquack` for the in-process throughput micro-benchmark

All times are wall-clock seconds measured with `date +%s%3N` before and after
each phase.

---

*Generated by `benchmark_docs.sh` on 2026-03-13 on a 4-core Azure runner.*
