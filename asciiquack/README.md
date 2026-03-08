```
= asciiquack
:doctype: duck
:quack: true

// parsing...

     __
   <(o )___
    ( ._> /
     `---'

----
quack:: true
duck:: ascii
----
```

A C++17 translation of (most of) asciidoctor.

This is intending to be a minimalist, self-contained tool that can be used to
produce output along the lines of asciidoctor, but without some of its most
complex features - for example, our PDF output is quite basic since most full
featured solutions to that problem are also extremely heavy.


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

### Other dependencies

- Command line options: cxxopts (https://github.com/jarro2783/cxxopts)
- PDF writing: minimal subset of libharu (https://github.com/libharu/libharu)
- Font support: struetype fork of stb_truetype (https://github.com/starseeker/struetype)
- PNG support: LodePNG (https://github.com/lvandeve/lodepng)
- fonts: Noto (https://fonts.google.com/noto) 
- Syntax highlighting (if C++23 available): µlight (https://github.com/eisenwave/ulight)
