```
= asciiquack
:doctype: duck
:quack: true

// parsing...

    ==(o )===
      ( ._> ::
      `---'//

----
quack:: true
duck:: ascii
----
```

A C++17 translation of asciidoctor.

Currently a work in progress.  The desired end state is a clean,
self-contained, strictly C++17 compliant codebase that can handle
most of what real-world asciidoc use would entail - we'll see if
we get there.

Command line option via https://github.com/jarro2783/cxxopts

## Building

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
./asciiquack_tests        # run test suite
./bench_asciiquack [file] [iterations]
```

### Regex backend options

| CMake flags | Backend | Notes |
|---|---|---|
| *(default)* | System `libpcre2-8` if found; embedded PCRE2 otherwise | Automatic |
| `-DUSE_SYSTEM_PCRE2=OFF` | Embedded PCRE2 vendor subset | No external dependency |
| `-DUSE_PCRE2=OFF` | `std::regex` | Slowest; always available |

The embedded PCRE2 subset (`vendor/pcre2_embed.h`) is a single-header
amalgamation of PCRE2 10.42 (~54 000 lines): no JIT compiler, no Unicode
property support (`\p{}`), no DFA engine.  Include `vendor/pcre2_impl.c` in
your build to instantiate it.  Install `libpcre2-dev` to get the full
JIT-enabled system library.

## Performance

Benchmark: 1 000 in-process iterations on `benchmark/sample-data/mdbasics.adoc`
(335 lines, ~9 KB), 10-iteration warm-up, GCC 13 `-O2`.

| Backend | Avg / iter | Conv / sec | vs std::regex |
|---|---|---|---|
| Ruby Asciidoctor 2.1.0 | ~2.3 ms | ~440 | reference |
| asciiquack / `std::regex` | ~3.1 ms | ~321 | — |
| asciiquack / embedded PCRE2 (no JIT) | ~0.77 ms | ~1 291 | **~4× faster** |
| asciiquack / system PCRE2 (JIT) | ~0.65 ms | ~1 541 | **~4.8× faster** |

### Why not RE2?

RE2 was evaluated but cannot serve as a drop-in backend because several
patterns require features RE2 intentionally omits:

- **Backreferences** – e.g. `([-*_])…\1` (thematic-break detection)
- **Lookahead assertions** – e.g. `(?=[^*\w]|$)` (constrained inline quotes)
- **Negative lookahead** – e.g. `(?!//[^/])` (description-list guard)

PCRE2 is equally fast in practice and supports the full pattern set.
