#!/usr/bin/env python3
"""check_pdf_layout.py - Automated PDF layout analysis using OpenCV.

This script converts a PDF to page images and analyses each page for common
layout defects:

  * Grey code-block background rectangles with insufficient spacing before
    the following body text (the "overlap" bug: paragraph ascenders visually
    bleeding into the grey background).
  * Text overflowing the top, bottom, left, or right page margins.

The primary check is a *gap measurement*: for each grey code-block box the
script finds the actual bottom of the grey region and the start of the first
significant body-text run below it.  If the gap is smaller than
MIN_GAP_AFTER_BOX_PX the box is flagged.

At 150 DPI the correct gap (>= 11 pt) produces at least 23 px of clearance.
A gap smaller than MIN_GAP_AFTER_BOX_PX (4 px ~ 1.9 pt) is a defect.

Heading-rule spacing is checked by dedicated unit tests in test_asciiquack.cpp
(test_pdf_heading_rule_not_through_body, test_pdf_heading_rule_position_below_heading).
Attempting to detect heading-rule bars from the rasterized image is not
reliable because table borders (0.5 grey) fall in the same greyscale range and
produce false positives.

Usage
-----
    python3 check_pdf_layout.py [PDF_FILE] [--dpi DPI] [--save-annotated]

If PDF_FILE is omitted the script looks for ``stress_test.pdf`` next to
itself, generating it automatically when an ``asciiquack`` binary is found.

Exit codes
----------
  0 - no defects detected
  1 - one or more defects detected
  2 - usage / environment error
"""

import argparse
import subprocess
import sys
import os
from pathlib import Path

try:
    import cv2
    import numpy as np
    from pdf2image import convert_from_path
except ImportError as exc:
    print(f"ERROR: Missing Python dependency: {exc}")
    print("Install with: pip install opencv-python-headless pdf2image")
    sys.exit(2)

# -----------------------------------------------------------------------------
# Configuration
# -----------------------------------------------------------------------------

# Rendering resolution.  150 DPI gives ~2 pt/pixel, sufficient to detect the
# ~4 pt overlap described in the bug report.
DEFAULT_DPI = 150

# Grey background detection thresholds (0-255 scale).
# The code-block background is rendered at RGB (0.95, 0.95, 0.95) ~= 242/255.
GREY_LO = 210   # lower bound for "light grey"
GREY_HI = 252   # upper bound (pure white >= 253 is excluded)

# Dark text/line detection threshold.
TEXT_DARK = 80   # pixels darker than this are "ink / text"

# Minimum dark pixels in a row for it to count as a "text row".
MIN_TEXT_ROW_PX = 50

# Minimum area (pixels^2) of a grey connected component to be considered a
# code-block background (not a table rule or admonition side-bar).
MIN_GREY_AREA = 300

# Minimum width as a fraction of page width.  Code blocks span nearly the
# full content area; narrow elements are excluded.
MIN_GREY_WIDTH_FRACTION = 0.30

# Minimum rows to scan below the grey box when searching for following text.
MAX_SCAN_ROWS = 80   # ~38 pt at 150 DPI - more than enough

# Gap threshold: the measured gap (in rows) between the grey box bounding
# bottom and the first body-text row below it must exceed this value.
# A gap of 1-3 rows indicates the following text starts immediately adjacent
# to or inside the grey box.  Values of 4+ rows correspond to visually
# separate elements.  The fixed code produces gaps of 6+ rows.
MIN_GAP_AFTER_BOX_PX = 4

# Margin overflow detection.
#
# The PDF left and right margins are each 72 pt.  We check a gutter that is
# *inside* the margin (between the page edge and the content edge):
#   - left gutter:  x in [0, MARGIN_GUTTER_PT / 72 * dpi)
#   - right gutter: x in [page_w - MARGIN_GUTTER_PT / 72 * dpi, page_w)
#
# MARGIN_GUTTER_PT is set to 50 pt (< 72 pt) so we never accidentally flag
# legitimate content at the content edge, only true margin overflows.
MARGIN_GUTTER_PT = 50   # points of each margin to check for overflows
# Minimum dark pixel count in the gutter to flag as "overflow".
MARGIN_OVERFLOW_PX = 30

# -----------------------------------------------------------------------------
# Helpers
# -----------------------------------------------------------------------------

def find_asciiquack() -> "str | None":
    """Return the path to the asciiquack binary, or None."""
    candidates = [
        Path(__file__).parent.parent / "build" / "asciiquack",
        Path(__file__).parent.parent / "asciiquack",
    ]
    for c in candidates:
        if c.is_file() and os.access(c, os.X_OK):
            return str(c)
    try:
        result = subprocess.run(["which", "asciiquack"],
                                capture_output=True, text=True, check=True)
        return result.stdout.strip()
    except subprocess.CalledProcessError:
        return None


def generate_pdf(adoc_path: Path, pdf_path: Path) -> bool:
    """Run asciiquack to produce a PDF from *adoc_path*."""
    binary = find_asciiquack()
    if binary is None:
        print("ERROR: Cannot find asciiquack binary.  Build the project first.")
        return False
    print(f"Generating {pdf_path} ...")
    result = subprocess.run(
        [binary, "-b", "pdf", str(adoc_path), "-o", str(pdf_path)],
        capture_output=True, text=True,
    )
    if result.returncode != 0:
        print(f"ERROR: asciiquack failed:\n{result.stderr}")
        return False
    return True


def find_grey_boxes(grey_mask: np.ndarray, page_w: int,
                    ) -> "list[tuple[int,int,int,int]]":
    """Return bounding boxes (x, y, w, h) of large light-grey regions.

    Only regions wider than MIN_GREY_WIDTH_FRACTION x page_w are returned so
    that narrow table rules and admonition side-bars are excluded.
    """
    kernel = cv2.getStructuringElement(cv2.MORPH_RECT, (5, 3))
    closed = cv2.morphologyEx(grey_mask, cv2.MORPH_CLOSE, kernel)

    n_labels, _labels, stats, _ = cv2.connectedComponentsWithStats(
        closed, connectivity=8
    )
    boxes = []
    for lbl in range(1, n_labels):
        x, y, w, h, area = stats[lbl]
        if area < MIN_GREY_AREA:
            continue
        if w < MIN_GREY_WIDTH_FRACTION * page_w:
            continue
        boxes.append((int(x), int(y), int(w), int(h)))
    return boxes


def first_text_row_after(dark_mask: np.ndarray,
                          start_row: int,
                          bx: int, bw: int,
                          page_h: int) -> "int | None":
    """Return the first row index >= start_row that has at least
    MIN_TEXT_ROW_PX dark pixels within the x-range [bx, bx+bw].

    Returns None if no such row is found within MAX_SCAN_ROWS.
    """
    for y in range(start_row, min(start_row + MAX_SCAN_ROWS, page_h)):
        row_slice = dark_mask[y, bx : bx + bw]
        if int(np.sum(row_slice > 0)) >= MIN_TEXT_ROW_PX:
            return y
    return None


# -----------------------------------------------------------------------------
# Individual checks
# -----------------------------------------------------------------------------

def check_code_block_gaps(grey_mask: np.ndarray,
                           dark_mask: np.ndarray,
                           page_num: int,
                           page_w: int, page_h: int,
                           annotated: "np.ndarray | None",
                           ) -> "list[dict]":
    """Check that following body text starts far enough below each grey box."""
    defects: "list[dict]" = []

    boxes = find_grey_boxes(grey_mask, page_w)
    for (bx, by, bw, bh) in boxes:
        grey_bot   = by + bh - 1
        first_text = first_text_row_after(dark_mask, grey_bot + 1,
                                           bx, bw, page_h)
        gap = (first_text - grey_bot) if first_text is not None else MAX_SCAN_ROWS

        defect = None
        if gap < MIN_GAP_AFTER_BOX_PX:
            defect = {
                "page": page_num,
                "description": (
                    f"Page {page_num}: grey code-block box bottom row {grey_bot}, "
                    f"next text row {first_text} "
                    f"(gap = {gap} px ~= {gap / (DEFAULT_DPI / 72.0):.1f} pt) "
                    f"- following text too close to code-block background."
                ),
            }
            defects.append(defect)

        if annotated is not None:
            colour = (255, 0, 0) if defect else (0, 200, 0)
            cv2.rectangle(annotated, (bx, by), (bx + bw, by + bh), colour, 2)
            cv2.line(annotated, (bx, grey_bot), (bx + bw, grey_bot),
                     (0, 128, 255), 1)
            if first_text is not None:
                cv2.line(annotated,
                         (bx, first_text), (bx + bw, first_text),
                         (128, 0, 255), 1)
            label = f"cb-gap={gap}px"
            cv2.putText(annotated, label, (bx + 2, max(10, grey_bot - 4)),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.35,
                        (255, 0, 0) if defect else (0, 128, 0), 1, cv2.LINE_AA)

    return defects


def check_left_margin_overflow(grey_img: np.ndarray,
                                page_num: int,
                                dpi: int,
                                annotated: "np.ndarray | None",
                                ) -> "list[dict]":
    """Detect text/ink pixels in the left gutter (inside the left margin).

    The gutter width is MARGIN_GUTTER_PT points from the page edge, which
    must be less than the actual page margin (72 pt) so that legitimate
    content at the margin edge is not flagged.
    """
    gutter_w = int(MARGIN_GUTTER_PT / 72.0 * dpi)
    gutter   = grey_img[:, :gutter_w]
    dark_ct  = int(np.sum(gutter < TEXT_DARK))
    if dark_ct <= MARGIN_OVERFLOW_PX:
        return []
    defect = {
        "page": page_num,
        "description": (
            f"Page {page_num}: {dark_ct} dark pixels in left gutter "
            f"(leftmost {gutter_w}px = {MARGIN_GUTTER_PT}pt from left edge) "
            f"- possible left-margin overflow."
        ),
    }
    if annotated is not None:
        cv2.rectangle(annotated, (0, 0), (gutter_w, annotated.shape[0] - 1),
                      (255, 128, 0), 2)
    return [defect]


def check_right_margin_overflow(grey_img: np.ndarray,
                                 page_num: int,
                                 page_w: int,
                                 dpi: int,
                                 annotated: "np.ndarray | None",
                                 ) -> "list[dict]":
    """Detect text/ink pixels in the right gutter (inside the right margin).

    The gutter width is MARGIN_GUTTER_PT points from the right page edge.
    """
    gutter_w = int(MARGIN_GUTTER_PT / 72.0 * dpi)
    gutter   = grey_img[:, page_w - gutter_w:]
    dark_ct  = int(np.sum(gutter < TEXT_DARK))
    if dark_ct <= MARGIN_OVERFLOW_PX:
        return []
    defect = {
        "page": page_num,
        "description": (
            f"Page {page_num}: {dark_ct} dark pixels in right gutter "
            f"(rightmost {gutter_w}px = {MARGIN_GUTTER_PT}pt from right edge) "
            f"- possible right-margin overflow."
        ),
    }
    if annotated is not None:
        cv2.rectangle(annotated,
                      (page_w - gutter_w, 0),
                      (page_w - 1, annotated.shape[0] - 1),
                      (255, 128, 0), 2)
    return [defect]


def check_bottom_margin(grey_img: np.ndarray, page_num: int,
                         page_h: int,
                         dpi: int,
                         margin_pt: int = 50) -> "list[dict]":
    """Detect text below the bottom margin."""
    margin_px = int(margin_pt / 72.0 * dpi)
    dark = int(np.sum(grey_img[page_h - margin_px:, :] < TEXT_DARK))
    if dark > 50:
        return [{
            "page": page_num,
            "description": (
                f"Page {page_num}: {dark} dark pixels in the bottom "
                f"{margin_px}px ({margin_pt}pt) margin (text overflow?)."
            ),
        }]
    return []


def check_top_margin(grey_img: np.ndarray, page_num: int,
                      dpi: int,
                      margin_pt: int = 50) -> "list[dict]":
    """Detect text above the top margin."""
    margin_px = int(margin_pt / 72.0 * dpi)
    dark = int(np.sum(grey_img[:margin_px, :] < TEXT_DARK))
    if dark > 50:
        return [{
            "page": page_num,
            "description": (
                f"Page {page_num}: {dark} dark pixels in the top "
                f"{margin_px}px ({margin_pt}pt) margin (text overflow?)."
            ),
        }]
    return []



# -----------------------------------------------------------------------------
# Per-page analysis
# -----------------------------------------------------------------------------

def check_page(image: np.ndarray, page_num: int,
               dpi: int,
               save_annotated: bool, out_dir: Path) -> "list[dict]":
    """Analyse a single page image and return a list of defect dicts."""
    page_h, page_w = image.shape[:2]

    grey_img = cv2.cvtColor(image, cv2.COLOR_RGB2GRAY)

    grey_mask = np.zeros(grey_img.shape, dtype=np.uint8)
    grey_mask[(grey_img >= GREY_LO) & (grey_img <= GREY_HI)] = 255

    dark_mask = np.zeros(grey_img.shape, dtype=np.uint8)
    dark_mask[grey_img < TEXT_DARK] = 255

    annotated = image.copy() if save_annotated else None

    defects: "list[dict]" = []

    defects += check_code_block_gaps(
        grey_mask, dark_mask, page_num, page_w, page_h, annotated)

    defects += check_left_margin_overflow(grey_img, page_num, dpi, annotated)
    defects += check_right_margin_overflow(grey_img, page_num, page_w, dpi, annotated)
    defects += check_bottom_margin(grey_img, page_num, page_h, dpi)
    defects += check_top_margin(grey_img, page_num, dpi)

    if save_annotated and annotated is not None:
        out_path = out_dir / f"page_{page_num:03d}_annotated.png"
        cv2.imwrite(str(out_path), cv2.cvtColor(annotated, cv2.COLOR_RGB2BGR))
        print(f"  Saved annotated page {page_num} -> {out_path}")

    return defects


# -----------------------------------------------------------------------------
# Entry point
# -----------------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(
        description="Analyse a PDF for layout defects using OpenCV."
    )
    parser.add_argument(
        "pdf", nargs="?",
        help="Path to the PDF file (default: examples/stress_test.pdf)",
    )
    parser.add_argument(
        "--dpi", type=int, default=DEFAULT_DPI,
        help=f"Rendering DPI (default: {DEFAULT_DPI})",
    )
    parser.add_argument(
        "--save-annotated", action="store_true",
        help="Save annotated PNG images alongside the PDF.",
    )
    args = parser.parse_args()

    script_dir = Path(__file__).parent

    if args.pdf:
        pdf_path = Path(args.pdf)
    else:
        pdf_path = script_dir / "stress_test.pdf"

    # Auto-generate the PDF from the stress-test adoc if it doesn't exist
    if not pdf_path.exists():
        adoc_path = pdf_path.with_suffix(".adoc")
        if not adoc_path.exists():
            print(f"ERROR: Neither {pdf_path} nor {adoc_path} exists.")
            return 2
        if not generate_pdf(adoc_path, pdf_path):
            return 2

    if not pdf_path.exists():
        print(f"ERROR: PDF not found at {pdf_path}")
        return 2

    print(f"Analysing {pdf_path} at {args.dpi} DPI ...")

    try:
        pages = convert_from_path(str(pdf_path), dpi=args.dpi)
    except Exception as exc:
        print(f"ERROR: Failed to convert PDF to images: {exc}")
        return 2

    print(f"  {len(pages)} page(s) to analyse.")
    out_dir = pdf_path.parent
    all_defects: "list[dict]" = []

    for page_num, page_img in enumerate(pages, start=1):
        img = np.array(page_img)  # PIL RGB -> numpy
        defects = check_page(img, page_num, args.dpi, args.save_annotated, out_dir)
        all_defects.extend(defects)

    print()
    if all_defects:
        print(f"DEFECTS FOUND: {len(all_defects)}")
        for d in all_defects:
            print(f"  [!] {d['description']}")
        return 1
    else:
        print(f"No layout defects detected across {len(pages)} page(s). [OK]")
        return 0


if __name__ == "__main__":
    sys.exit(main())

