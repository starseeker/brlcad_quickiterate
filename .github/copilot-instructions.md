# BRL-CAD Quick Iterate – Copilot Agent Instructions

## Repository Layout

```
brlcad_quickiterate/
├── brlcad/          # BRL-CAD source tree
├── bext_output/     # Pre-built BRL-CAD external dependencies (bext)
│   ├── install/     # Runtime-installed dependency artifacts
│   └── noinstall/   # Build-time-only dependency artifacts
├── asciiquack/      # AsciiDoc converter/processor sources + tests
└── .github/
    └── copilot-instructions.md   # This file
```

## Configuring BRL-CAD

Use the pre-built dependencies in `bext_output/` together with the flags below to minimize configure and build time.  Run these commands from the **repository root**:

```bash
REPO_ROOT=/home/runner/work/brlcad_quickiterate/brlcad_quickiterate
mkdir -p brlcad_build
cmake -S "$REPO_ROOT/brlcad" -B "$REPO_ROOT/brlcad_build" \
  -DBRLCAD_EXT_DIR="$REPO_ROOT/bext_output" \
  -DBRLCAD_EXTRADOCS=OFF \
  -DBRLCAD_ENABLE_STEP=OFF \
  -DBRLCAD_ENABLE_GDAL=OFF \
  -DBRLCAD_ENABLE_QT=OFF
```

Expected configure time: ~55 seconds on a fresh build directory (a few seconds on a re-configure).

`BRLCAD_EXTRADOCS` controls whether the AsciiDoc documentation (man pages and HTML) is
built via **asciiquack**.  It defaults to ON (when `BRLCAD_ENABLE_TARGETS > 2`).  Pass
`-DBRLCAD_EXTRADOCS=OFF` to skip documentation building and save time.

## Building BRL-CAD

After a successful configure, build with:

```bash
cmake --build /home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build -j$(nproc)
```

To build only a specific target (e.g. `libbu`):

```bash
cmake --build /home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build --target libbu -j$(nproc)
```

### Building AsciiDoc documentation targets

Build individual asciidoc doc targets or groups:

```bash
REPO_ROOT=/home/runner/work/brlcad_quickiterate/brlcad_quickiterate

# Build all mann pages (mged commands):
cmake --build $REPO_ROOT/brlcad_build --target asciidoc-asciidoc-system-mann -j$(nproc)

# Build all man1 pages:
cmake --build $REPO_ROOT/brlcad_build --target asciidoc-asciidoc-system-man1 -j$(nproc)
```

Generated outputs land in:
- Man pages: `$REPO_ROOT/brlcad_build/share/man/man1/`, `.../mann/`, `.../man3/`, etc.
- HTML pages: `$REPO_ROOT/brlcad_build/share/doc/html/asciidoc/`, etc.

## Building and Testing asciiquack

The `asciiquack/` directory contains the AsciiDoc processor used to build BRL-CAD documentation.  Build it in a separate directory under `/tmp`:

```bash
REPO_ROOT=/home/runner/work/brlcad_quickiterate/brlcad_quickiterate
mkdir -p /tmp/aq_build
cmake -S $REPO_ROOT/asciiquack -B /tmp/aq_build
cmake --build /tmp/aq_build -j$(nproc)
```

Run the test suite:

```bash
/tmp/aq_build/asciiquack_tests
```

The test binary reports `All N tests passed.` on success.

The asciiquack binary itself lives at `/tmp/aq_build/asciiquack`.  A pre-built copy is also committed at `bext_output/noinstall/bin/asciiquack` and is updated by the `report_progress` commits.

## Comparing asciidoctor vs asciiquack Output

The active comparison workflow uses **asciidoctor** as the reference renderer.
Both `.adoc` files are rendered to man page troff via their respective tools, then rendered
to plain text with `groff -Tascii | col -b`, normalized to strip terminal formatting codes,
and diffed.

### Accepted differences (filtered from counts)

The following differences are **known and accepted** – do not try to "fix" them:

1. **NAME/description italic markup**: asciidoctor preserves `_word_` as literal underscores
   in man page output; asciiquack converts them to `\fIword\fP` italic.  We keep asciiquack's
   behaviour.  Normalised away by stripping `[0-9]+m` codes and boundary-anchored `_phrase_`
   pairs (single underscores, no internal underscores, bounded by space/punctuation).
   Double-underscore `__word__` patterns (e.g. `__old_bot_primitive__`) are also stripped.
2. **Email address colour/angle-brackets**: asciidoctor wraps email addresses in colour codes
   `34m…0m` or `<...>` angle brackets; asciiquack emits them plain.
   Filtered by `devs@brlcad` and `bugs@brlcad`.
3. **Date in footer**: asciidoctor adds a `2026-03-08`-style date to the footer; asciiquack
   does not.  Filtered by `BRL-CAD.*[0-9]` and `BRL-CAD\t`.
4. **asciidoctor `#` mangling bug**: `*-C #/#/#*` → asciidoctor emits `-C //#` (wrong);
   asciiquack emits `-C #/#/#` (correct).  Same bug affects `-T #,#`, `-#`, etc.
   Filtered by `//#`.

### Normalisation function

```bash
normalize_groff() {
    # Strip groff -Tascii terminal bold/italic codes.
    # [0-9]+m covers: 1m (bold-on), 22m (bold-off), 4m (italic-on), 24m (italic-off),
    # 0m (reset), 34m (link colour), etc.
    # Strip __double_underscore__ italic (asciidoctor preserves in NAME section).
    # Strip boundary-anchored _phrase_ italic (space/punct bounded, no internal underscores).
    # Strip *word* bold markup preserved by asciidoctor in NAME section.
    sed 's/[0-9]\+m//g' | \
    sed 's/__\([a-zA-Z][a-zA-Z0-9_]*\)__/\1/g' | \
    sed 's/\(^\|[ (]\)_\([^_]*\)_\([ ,.:;!?>)\n]\|$\)/\1\2\3/g' | \
    sed 's/\*\([^*]*\)\*/\1/g'
}
```

### Single-file comparison (asciidoctor vs asciiquack)

```bash
REPO_ROOT=/home/runner/work/brlcad_quickiterate/brlcad_quickiterate
AQ=/tmp/aq_build/asciiquack   # or $REPO_ROOT/bext_output/noinstall/bin/asciiquack

normalize_groff() {
    sed 's/[0-9]\+m//g' | \
    sed 's/__\([a-zA-Z][a-zA-Z0-9_]*\)__/\1/g' | \
    sed 's/\(^\|[ (]\)_\([^_]*\)_\([ ,.:;!?>)\n]\|$\)/\1\2\3/g' | \
    sed 's/\*\([^*]*\)\*/\1/g'
}

adoc="$REPO_ROOT/brlcad/doc/asciidoc/system/man1/nirt.adoc"
asciidoctor -b manpage -o /tmp/ad_nirt.1 "$adoc" 2>/dev/null
$AQ -b manpage "$adoc" -o /tmp/aq_nirt.1 2>/dev/null
diff <(groff -t -Tascii -man /tmp/ad_nirt.1 2>/dev/null | col -b | normalize_groff) \
     <(groff -t -Tascii -man /tmp/aq_nirt.1 2>/dev/null | col -b | normalize_groff) | \
  grep "^[<>]" | grep -Pv "BRL-CAD.*[0-9]|BRL-CAD\t|devs@brlcad|bugs@brlcad|//#"
```

### Bulk comparison (mann + man1, asciidoctor vs asciiquack)

```bash
REPO_ROOT=/home/runner/work/brlcad_quickiterate/brlcad_quickiterate
AQ=$REPO_ROOT/bext_output/noinstall/bin/asciiquack

normalize_groff() {
    sed 's/[0-9]\+m//g' | \
    sed 's/__\([a-zA-Z][a-zA-Z0-9_]*\)__/\1/g' | \
    sed 's/\(^\|[ (]\)_\([^_]*\)_\([ ,.:;!?>)\n]\|$\)/\1\2\3/g' | \
    sed 's/\*\([^*]*\)\*/\1/g'
}

fixed=0; minor=0; large=0; total=0
for adoc in $REPO_ROOT/brlcad/doc/asciidoc/system/mann/*.adoc \
            $REPO_ROOT/brlcad/doc/asciidoc/system/man1/*.adoc; do
  total=$((total+1))
  asciidoctor -b manpage -o /tmp/ad_cmp.nged "$adoc" 2>/dev/null
  $AQ -b manpage "$adoc" -o /tmp/aq_cmp.nged 2>/dev/null
  ndiff=$(diff \
    <(groff -t -Tascii -man /tmp/ad_cmp.nged 2>/dev/null | col -b | normalize_groff) \
    <(groff -t -Tascii -man /tmp/aq_cmp.nged 2>/dev/null | col -b | normalize_groff) | \
    grep "^[<>]" | grep -Pcv "BRL-CAD.*[0-9]|BRL-CAD\t|devs@brlcad|bugs@brlcad|//#")
  if   [ "$ndiff" -le 0 ]; then fixed=$((fixed+1))
  elif [ "$ndiff" -le 6 ]; then minor=$((minor+1))
  else                          large=$((large+1)); fi
done
echo "Total: $total  Exact=$fixed  Minor(1-6)=$minor  Larger=$large"
```

To list large-diff pages:
```bash
for adoc in ...; do
  base=$(basename "$adoc" .adoc)
  ...
  if [ "$ndiff" -gt 6 ]; then echo "$ndiff $base"; fi
done | sort -rn | head -20
```

## Current Work: asciidoctor vs asciiquack Comparison (PR: copilot/validate-asciiquack-outputs)

The active work stream is in the `copilot/validate-asciiquack-outputs` branch.  Each session should:

1. Build/rebuild the asciiquack binary from `asciiquack/` sources in `/tmp/aq_build/`
2. Run `sudo gem install asciidoctor` if asciidoctor is not available
3. Use the bulk comparison script above to find mismatches
4. Investigate and fix issues in `asciiquack/` sources (man page backend, parser)
5. Add tests to `asciiquack/test_asciiquack.cpp` for new fixes
6. Run `asciiquack_tests` to confirm all tests pass
7. Use `report_progress` to commit

### Issues fixed so far (copilot/validate-asciiquack-outputs)

- Inline element trailing/leading space (word boundary rules)
- Adjacent inline elements losing space between them
- man page backend: double-escaping of troff macros (`\\fB` instead of `\fB`)
- man page backend: nested inline markup (e.g. `*-A _attr_*` → italic inside bold)
- man page backend: constrained marker end-detection (underscore in middle of word)
- Verbatim blocks: `.sp` prefix, tab→8-space expansion, trim leading/trailing blank lines
- Nested dlist inside dlist item: child dlist rendered OUTSIDE parent `.RS 4` (pnts page)
- Single-character dlist terms (`1::`, `2::`) now parsed correctly
- xref `<<anchor>>` guard: anchor part must not contain spaces (fixes `"^<<"` in text)
- dlist term auto-bold removed: plain terms no longer wrapped in `\fB...\fP`
- Compact dlist: consecutive empty-body items joined with `, ` on one term line
- Untitled example blocks: indented with `.RS 4`/`.RE` but no "Example N." prefix
- Example N. numbering only applied to titled example blocks

### Known remaining differences (investigate next session)

- **Multi-term dlist format**: asciidoctor emits each preceding term on its own line then
  `last-term:: body-inline`; asciiquack joins all terms on one line then indents body with
  `.RS 4`.  Affects ~8 pages (lc, make_pnts, comgeom-g, rtg3, gr, etc.)
- **Nested inline `_*bold*_` italic-containing-bold**: asciidoctor emits `\fI*word*\fB\fP`
  (leaving `*` literal); asciiquack emits `\fI\fBword\fP\fP` (proper nesting).  Affects
  g2asc, asc2g.  asciiquack's rendering is arguably more correct.
- **saveview/remrt verbatim blocks**: leading-spaces/indentation differences in shell script
  blocks.
- **gqa/search/comb/rtwizard**: large diffs – investigate (likely dlist continuation `+`
  paragraphs rendered with double indentation vs single, and nested `.RS 4` issues).
- **Line wrapping**: asciidoctor and asciiquack wrap at slightly different widths in some
  cases (different `.RS`/`.RE` depth affects available width). Results in same content
  split differently across lines (cat, cpi, db_glob, e, decompose, status, vdraw, etc.).
  These are minor cosmetic differences (2-4 diff lines).
- **Double-space after period**: `Endianness flipped.  Converting` (two spaces) vs one
  space (dbupgrade). Minor formatting quirk.

## Important Notes

- **bext_output is pre-built** – do not delete or rebuild it unless strictly necessary; rebuilding bext from source takes a very long time.
- **Build directory is outside the source tree** – always configure with a separate build directory (e.g. `brlcad_build/` at the repo root) so that source-tree integrity checks inside CMake pass.
- **distcheck.yml** – BRL-CAD's cmake system validates that `brlcad/.github/workflows/distcheck.yml` is present and up to date.  The copy committed in this repo was generated from the current source tree; if the source tree is updated you may need to re-generate it by running cmake once, letting it fail, then copying the generated file from `<build_dir>/CMakeTmp/distcheck.yml` into `brlcad/.github/workflows/distcheck.yml`.
- **Ninja is available** but the default Unix Makefiles generator was measured to be faster in this environment for fresh configures.  Either generator works for builds.
- BRL-CAD enforces strict compiler warnings (including `-Werror`) by default, so compiler version matters.  The environment provides GCC 13.
