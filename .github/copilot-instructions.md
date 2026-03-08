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

### Configuring with EXTRADOCS (for DocBook comparison work)

When comparing DocBook-generated HTML/man outputs against asciiquack outputs, configure with `BRLCAD_EXTRADOCS=ON`:

```bash
REPO_ROOT=/home/runner/work/brlcad_quickiterate/brlcad_quickiterate
mkdir -p brlcad_build
cmake -S "$REPO_ROOT/brlcad" -B "$REPO_ROOT/brlcad_build" \
  -DBRLCAD_EXT_DIR="$REPO_ROOT/bext_output" \
  -DBRLCAD_EXTRADOCS=ON \
  -DBRLCAD_ENABLE_STEP=OFF \
  -DBRLCAD_ENABLE_GDAL=OFF \
  -DBRLCAD_ENABLE_QT=OFF
```

This enables the `docbook-*` build targets which generate HTML and man pages from the DocBook XML sources.

## Building BRL-CAD

After a successful configure, build with:

```bash
cmake --build /home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build -j$(nproc)
```

To build only a specific target (e.g. `libbu`):

```bash
cmake --build /home/runner/work/brlcad_quickiterate/brlcad_quickiterate/brlcad_build --target libbu -j$(nproc)
```

### Building DocBook documentation targets

With `BRLCAD_EXTRADOCS=ON`, build individual doc targets or groups:

```bash
REPO_ROOT=/home/runner/work/brlcad_quickiterate/brlcad_quickiterate

# Build all mann pages (mged commands):
cmake --build $REPO_ROOT/brlcad_build --target docbook-docbook-system-mann -j$(nproc)

# Build all man1 pages:
cmake --build $REPO_ROOT/brlcad_build --target docbook-docbook-system-man1 -j$(nproc)

# Build a single man page (e.g. nirt):
cmake --build $REPO_ROOT/brlcad_build --target docbook-system-man1-nirt -j$(nproc)
```

Generated outputs land in:
- Man pages: `$REPO_ROOT/brlcad_build/share/man/man1/`, `.../mann/`, `.../man3/`, etc.
- HTML pages: `$REPO_ROOT/brlcad_build/share/doc/html/man1/`, etc.

## Building and Testing asciiquack

The `asciiquack/` directory contains the AsciiDoc processor used to convert BRL-CAD DocBook sources.  Build it in a separate directory under `/tmp`:

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

## DocBook → AsciiDoc Conversion Workflow

The XSL stylesheet that converts DocBook XML to AsciiDoc is at:
`brlcad/misc/tools/db2adoc/db2adoc.xsl`

The `xsltproc` binary is pre-built at:
`bext_output/noinstall/bin/xsltproc`

### Converting a single file

```bash
REPO_ROOT=/home/runner/work/brlcad_quickiterate/brlcad_quickiterate
LD_LIBRARY_PATH=$REPO_ROOT/bext_output/noinstall/lib \
  $REPO_ROOT/bext_output/noinstall/bin/xsltproc \
  --novalid --xinclude \
  $REPO_ROOT/brlcad/misc/tools/db2adoc/db2adoc.xsl \
  $REPO_ROOT/brlcad/doc/docbook/system/man1/nirt.xml \
  > /tmp/nirt.adoc
```

### Regenerating all committed AsciiDoc files after XSL changes

```bash
REPO_ROOT=/home/runner/work/brlcad_quickiterate/brlcad_quickiterate
XSL="$REPO_ROOT/brlcad/misc/tools/db2adoc/db2adoc.xsl"
XSLTPROC="LD_LIBRARY_PATH=$REPO_ROOT/bext_output/noinstall/lib $REPO_ROOT/bext_output/noinstall/bin/xsltproc --novalid --xinclude"
for dir in articles books devguides lessons lessons/es presentations specifications system/man1 system/man3 system/man5 system/mann; do
  srcdir="$REPO_ROOT/brlcad/doc/docbook/$dir"
  for xml in "$srcdir"/*.xml; do
    [ -f "$xml" ] || continue
    relpath="${xml#$REPO_ROOT/brlcad/doc/docbook/}"
    adoc="$REPO_ROOT/brlcad/doc/asciidoc/${relpath%.xml}.adoc"
    [ -f "$adoc" ] || continue
    new=$(eval $XSLTPROC "$XSL" "$xml" 2>/dev/null)
    current=$(cat "$adoc")
    if [ "$new" != "$current" ]; then
      echo "$new" > "$adoc"
    fi
  done
done
```

## Comparing DocBook vs asciiquack Output

The primary comparison workflow is to render both the DocBook-generated man page and the asciiquack-generated man page as plain text, then diff them.

```bash
REPO_ROOT=/home/runner/work/brlcad_quickiterate/brlcad_quickiterate
AQ=/tmp/aq_build/asciiquack   # or $REPO_ROOT/bext_output/noinstall/bin/asciiquack

# Generate asciiquack man page from the committed .adoc file
$AQ -b manpage "$REPO_ROOT/brlcad/doc/asciidoc/system/man1/nirt.adoc" -o /tmp/nirt_aq.1

# Render both to plain text and diff
man -l $REPO_ROOT/brlcad_build/share/man/man1/nirt.1 2>/dev/null | col -b > /tmp/nirt_db.txt
man -l /tmp/nirt_aq.1 2>/dev/null | col -b > /tmp/nirt_aq.txt
diff /tmp/nirt_db.txt /tmp/nirt_aq.txt
```

### Bulk comparison of all mann pages

```bash
REPO_ROOT=/home/runner/work/brlcad_quickiterate/brlcad_quickiterate
AQ=$REPO_ROOT/bext_output/noinstall/bin/asciiquack

mismatches=0; total=0; issues=""
for db_man in $REPO_ROOT/brlcad_build/share/man/mann/*.nged; do
  base=$(basename "$db_man" .nged)
  adoc="$REPO_ROOT/brlcad/doc/asciidoc/system/mann/${base}.adoc"
  [ -f "$adoc" ] || continue
  $AQ -b manpage "$adoc" -o /tmp/aq_compare.nged 2>/dev/null
  db_text=$(man -l "$db_man" 2>/dev/null | col -b)
  aq_text=$(man -l /tmp/aq_compare.nged 2>/dev/null | col -b)
  total=$((total+1))
  if [ "$db_text" != "$aq_text" ]; then
    mismatches=$((mismatches+1)); issues="$issues $base"
  fi
done
echo "Total: $total, Mismatches: $mismatches"
echo "Mismatched: $issues"
```

## Current Work: DocBook vs asciiquack Comparison (PR: copilot/analyze-output-differences)

The active work stream is in the `copilot/analyze-output-differences` branch.  Each session should:

1. Build/rebuild the asciiquack binary from `asciiquack/` sources in `/tmp/aq_build/`
2. Configure brlcad_build with `BRLCAD_EXTRADOCS=ON` and build relevant docbook targets
3. Use the bulk comparison script above to find mismatches
4. Investigate and fix issues in either `brlcad/misc/tools/db2adoc/db2adoc.xsl` (XSL converter) or `asciiquack/` sources (man page backend, HTML backend)
5. Add tests to `asciiquack/test_asciiquack.cpp` for new fixes
6. Regenerate committed `.adoc` files using the regeneration script above
7. Run `asciiquack_tests` to confirm all tests pass
8. Use `report_progress` to commit

### Issues fixed so far

- Inline element trailing/leading space (word boundary rules)
- Adjacent inline elements losing space between them
- `db:footnote` with `<para>` children emitting `\n\n` inside `footnote:[...]`
- `<userinput>` with nested element children producing malformed bold markers
- Synopsis `<replaceable>` emitting `<name>` instead of bare `name`
- `funcsynopsis` whitespace: `normalize-space()` stripping spaces inside type names
- `funcsynopsis` paren placement: `paramdef` `position()=1/last()` counting wrong
- man page backend: double-escaping of troff macros (`\\fB` instead of `\fB`)
- man page backend: nested inline markup (e.g. `*-A _attr_*` → italic inside bold)
- man page backend: constrained marker end-detection (underscore in middle of word)

### Known remaining differences (investigate next session)

- **Example block titles**: DocBook renders `Example N. Title` with a numbered prefix; asciiquack renders just `Title` (the `[example]` block title in AsciiDoc doesn't number)
- **dlist (.TP) indentation style**: DocBook uses `.RS 4`/`.RE` for consistent indent; asciiquack uses standard `.TP` which groff formats slightly differently (minor)
- **Synopsis spacing**: minor whitespace differences in rendered synopsis lines

## Important Notes

- **bext_output is pre-built** – do not delete or rebuild it unless strictly necessary; rebuilding bext from source takes a very long time.
- **Build directory is outside the source tree** – always configure with a separate build directory (e.g. `brlcad_build/` at the repo root) so that source-tree integrity checks inside CMake pass.
- **distcheck.yml** – BRL-CAD's cmake system validates that `brlcad/.github/workflows/distcheck.yml` is present and up to date.  The copy committed in this repo was generated from the current source tree; if the source tree is updated you may need to re-generate it by running cmake once, letting it fail, then copying the generated file from `<build_dir>/CMakeTmp/distcheck.yml` into `brlcad/.github/workflows/distcheck.yml`.
- **Ninja is available** but the default Unix Makefiles generator was measured to be faster in this environment for fresh configures.  Either generator works for builds.
- BRL-CAD enforces strict compiler warnings (including `-Werror`) by default, so compiler version matters.  The environment provides GCC 13.
