/// @file pdf.hpp
/// @brief PDF converter for asciiquack.
///
/// Walks the Document AST and produces a PDF byte string using minipdf.hpp.
/// Supports Letter (8.5"×11") and A4 page sizes; outputs documents with
/// correct heading hierarchy, body text, bullet and numbered lists, code
/// blocks, admonition blocks, inline bold / italic / monospace text, and
/// basic horizontal rules.
///
/// Custom TrueType fonts can be specified per-style via the FontSet struct
/// (attributes: pdf-font, pdf-font-bold, pdf-font-italic, pdf-font-bold-italic,
/// pdf-font-mono, pdf-font-mono-bold).  When a style has no custom font the
/// corresponding PDF base-14 fallback (Helvetica family / Courier) is used.
///
/// Usage:
///   auto doc = Parser::parse_string(src, opts);
///   std::string pdf_bytes = asciiquack::convert_to_pdf(*doc);             // Letter, Helvetica
///   std::string pdf_bytes = asciiquack::convert_to_pdf(*doc, true);       // A4
///   std::string pdf_bytes = asciiquack::convert_to_pdf(*doc, false,
///                               "/usr/share/fonts/myfont.ttf");  // custom body font
///
///   asciiquack::FontSet fs;
///   fs.regular    = "/path/to/NotoSans-Regular.ttf";
///   fs.bold       = "/path/to/NotoSans-Bold.ttf";
///   fs.italic     = "/path/to/NotoSans-Italic.ttf";
///   fs.bold_italic = "/path/to/NotoSans-BoldItalic.ttf";
///   fs.mono       = "/path/to/NotoSansMono-Regular.ttf";
///   fs.mono_bold  = "/path/to/NotoSansMono-Bold.ttf";
///   std::string pdf_bytes = asciiquack::convert_to_pdf(*doc, false, fs);

#pragma once

#include "document.hpp"
#include "minipdf.hpp"
#include "substitutors.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace asciiquack {

// ─────────────────────────────────────────────────────────────────────────────
// Font set – paths to optional TrueType fonts for each style slot
// ─────────────────────────────────────────────────────────────────────────────

/// Paths to TrueType (.ttf) font files for each text style used in the PDF
/// output.  Leave any field empty to fall back to the corresponding PDF
/// base-14 font (Helvetica family for body text, Courier for monospace).
struct FontSet {
    std::string regular;     ///< pdf-font              – body text (F1)
    std::string bold;        ///< pdf-font-bold         – bold text (F2)
    std::string italic;      ///< pdf-font-italic       – italic text (F3)
    std::string bold_italic; ///< pdf-font-bold-italic  – bold-italic text (F4)
    std::string mono;        ///< pdf-font-mono         – monospace text (F5)
    std::string mono_bold;   ///< pdf-font-mono-bold    – bold monospace text (F6)
};

// ─────────────────────────────────────────────────────────────────────────────
// Inline text spans
// ─────────────────────────────────────────────────────────────────────────────

struct TextSpan {
    std::string        text;
    minipdf::FontStyle style = minipdf::FontStyle::Regular;
    float              size  = 11.0f;  ///< font size in points (0 = use context default)
};

/// Strip AsciiDoc markup that we cannot render (links, macros, etc.) and
/// leave the visible text only.  This is a best-effort plain-text extractor
/// used so that the raw AsciiDoc source is never mis-rendered as markup.
static std::string strip_markup(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    std::size_t i = 0;
    const std::size_t n = s.size();
    while (i < n) {
        // link:url[label] or http(s)://url[label]
        if (s.compare(i, 4, "http") == 0 || s.compare(i, 5, "link:") == 0) {
            auto ob = s.find('[', i);
            auto cb = (ob != std::string::npos) ? s.find(']', ob) : std::string::npos;
            if (ob != std::string::npos && cb != std::string::npos) {
                // Use the link label if non-empty, else the bare URL
                std::string label = s.substr(ob + 1, cb - ob - 1);
                if (!label.empty()) {
                    out += label;
                } else {
                    auto url_end = ob;
                    auto url_start = (s[i] == 'l') ? i + 5 : i;
                    out += s.substr(url_start, url_end - url_start);
                }
                i = cb + 1;
                continue;
            }
        }
        // kbd:[key], btn:[label], menu:X[Y] – drop markers, keep content
        if (s.compare(i, 4, "kbd:") == 0 ||
            s.compare(i, 4, "btn:") == 0 ||
            s.compare(i, 5, "menu:") == 0) {
            auto ob = s.find('[', i);
            auto cb = (ob != std::string::npos) ? s.find(']', ob) : std::string::npos;
            if (ob != std::string::npos && cb != std::string::npos) {
                out += s.substr(ob + 1, cb - ob - 1);
                i = cb + 1;
                continue;
            }
        }
        // image:target[alt] – use alt text
        if (s.compare(i, 6, "image:") == 0) {
            auto ob = s.find('[', i);
            auto cb = (ob != std::string::npos) ? s.find(']', ob) : std::string::npos;
            if (ob != std::string::npos && cb != std::string::npos) {
                std::string alt = s.substr(ob + 1, cb - ob - 1);
                if (!alt.empty()) { out += "[" + alt + "]"; }
                i = cb + 1;
                continue;
            }
        }
        // footnote:[text] – drop footnote markers
        if (s.compare(i, 9, "footnote:") == 0) {
            auto ob = s.find('[', i);
            auto cb = (ob != std::string::npos) ? s.find(']', ob) : std::string::npos;
            if (ob != std::string::npos && cb != std::string::npos) {
                i = cb + 1;
                continue;
            }
        }
        out += s[i++];
    }
    return out;
}

/// Parse a plain-text string (after markup has been stripped) into a vector
/// of TextSpans, interpreting *bold*, _italic_, `mono`, **bold**, __italic__,
/// ``mono`` inline markup.
static std::vector<TextSpan> parse_spans(const std::string& raw,
                                         minipdf::FontStyle base_style
                                             = minipdf::FontStyle::Regular) {
    std::string text = strip_markup(raw);

    std::vector<TextSpan> spans;
    std::string current;
    std::size_t i = 0;
    const std::size_t n = text.size();

    // Helpers to flush accumulated plain text
    auto flush = [&]() {
        if (!current.empty()) {
            spans.push_back({current, base_style});
            current.clear();
        }
    };

    // Determine bold/oblique variant of base_style
    auto bold_of = [&]() {
        switch (base_style) {
            case minipdf::FontStyle::Oblique:
                return minipdf::FontStyle::BoldOblique;
            default:
                return minipdf::FontStyle::Bold;
        }
    };
    auto italic_of = [&]() {
        switch (base_style) {
            case minipdf::FontStyle::Bold:
                return minipdf::FontStyle::BoldOblique;
            default:
                return minipdf::FontStyle::Oblique;
        }
    };

    while (i < n) {
        // ── Unconstrained bold: **...**
        if (i + 1 < n && text[i] == '*' && text[i+1] == '*') {
            auto end = text.find("**", i + 2);
            if (end != std::string::npos) {
                flush();
                spans.push_back({text.substr(i + 2, end - i - 2), bold_of()});
                i = end + 2;
                continue;
            }
        }
        // ── Constrained bold: *word*
        if (text[i] == '*' &&
            (i == 0 || !std::isalnum(static_cast<unsigned char>(text[i-1])))) {
            auto end = text.find('*', i + 1);
            if (end != std::string::npos && end > i + 1) {
                flush();
                spans.push_back({text.substr(i + 1, end - i - 1), bold_of()});
                i = end + 1;
                continue;
            }
        }
        // ── Unconstrained italic: __...__
        if (i + 1 < n && text[i] == '_' && text[i+1] == '_') {
            auto end = text.find("__", i + 2);
            if (end != std::string::npos) {
                flush();
                spans.push_back({text.substr(i + 2, end - i - 2), italic_of()});
                i = end + 2;
                continue;
            }
        }
        // ── Constrained italic: _word_
        if (text[i] == '_' &&
            (i == 0 || !std::isalnum(static_cast<unsigned char>(text[i-1])))) {
            auto end = text.find('_', i + 1);
            if (end != std::string::npos && end > i + 1) {
                flush();
                spans.push_back({text.substr(i + 1, end - i - 1), italic_of()});
                i = end + 1;
                continue;
            }
        }
        // ── Unconstrained mono: ``...``
        if (i + 1 < n && text[i] == '`' && text[i+1] == '`') {
            auto end = text.find("``", i + 2);
            if (end != std::string::npos) {
                flush();
                spans.push_back({text.substr(i + 2, end - i - 2),
                                  minipdf::FontStyle::Mono});
                i = end + 2;
                continue;
            }
        }
        // ── Constrained mono: `word`
        if (text[i] == '`' &&
            (i == 0 || !std::isalnum(static_cast<unsigned char>(text[i-1])))) {
            auto end = text.find('`', i + 1);
            if (end != std::string::npos && end > i + 1) {
                flush();
                spans.push_back({text.substr(i + 1, end - i - 1),
                                  minipdf::FontStyle::Mono});
                i = end + 1;
                continue;
            }
        }
        current += text[i++];
    }
    flush();
    return spans;
}

/// Collapse spans with consecutive identical styles into a single span.
static std::vector<TextSpan> merge_spans(std::vector<TextSpan> spans) {
    std::vector<TextSpan> out;
    for (auto& sp : spans) {
        if (!out.empty() && out.back().style == sp.style) {
            out.back().text += sp.text;
        } else {
            out.push_back(std::move(sp));
        }
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// PdfLayout – stateful text-flow engine
//
// Manages the current position on the page and automatically starts new pages
// when there is insufficient vertical space.
// ─────────────────────────────────────────────────────────────────────────────

class PdfLayout {
public:
    // ── Page geometry (points) ────────────────────────────────────────────────
    static constexpr float MARGIN_LEFT   = 72.0f;
    static constexpr float MARGIN_RIGHT  = 72.0f;
    static constexpr float MARGIN_TOP    = 72.0f;
    static constexpr float MARGIN_BOTTOM = 72.0f;

    // ── Default typography ────────────────────────────────────────────────────
    static constexpr float BODY_SIZE          = 11.0f;
    static constexpr float CODE_SIZE          = 10.0f;
    static constexpr float LINE_RATIO         = 1.35f;   ///< line height = size × ratio
    static constexpr float CODE_RIGHT_PADDING = 4.0f;    ///< right padding inside code block (pt)

    explicit PdfLayout(minipdf::Document& doc) : doc_(doc) {
        content_w_ = doc.page_width() - MARGIN_LEFT - MARGIN_RIGHT;
        new_page();
    }

    // ── Page management ───────────────────────────────────────────────────────

    void new_page() {
        page_ = &doc_.new_page();
        cursor_y_ = doc_.page_height() - MARGIN_TOP;
    }

    /// Ensure at least @p pts vertical space remains; if not, break page.
    void ensure_space(float pts) {
        if (cursor_y_ - pts < MARGIN_BOTTOM) {
            new_page();
        }
    }

    /// Advance cursor down by @p pts (vertical skip).
    void skip(float pts) {
        cursor_y_ -= pts;
        if (cursor_y_ < MARGIN_BOTTOM) { new_page(); }
    }

    // ── Word-wrapped paragraph ────────────────────────────────────────────────

    /// Render @p spans word-wrapped into lines within the content width minus
    /// @p x_indent.  @p line_h is the per-line advance.
    void wrap_spans(const std::vector<TextSpan>& spans,
                    float line_h,
                    float x_indent = 0.0f) {
        if (spans.empty()) { return; }
        float avail = content_w_ - x_indent;

        // Tokenise the spans into words, preserving span metadata.
        // A "word" is a run of non-space characters in a single span.
        struct Word {
            std::string        text;
            minipdf::FontStyle style = minipdf::FontStyle::Regular;
            float              size  = BODY_SIZE;
            bool               space_before = false;  ///< was preceded by space
        };

        std::vector<Word> words;
        for (const auto& sp : spans) {
            std::istringstream ss(sp.text);
            std::string tok;
            bool first = true;
            while (std::getline(ss, tok, ' ')) {
                if (tok.empty()) {
                    first = false;
                    continue;
                }
                Word w;
                w.text  = tok;
                w.style = sp.style;
                w.size  = sp.size;
                w.space_before = !first;
                words.push_back(w);
                first = false;
            }
        }

        if (words.empty()) { return; }

        // Build lines
        struct Line {
            std::vector<Word> words;
            float             total_w = 0.0f;
        };

        float space_w = tw(" ", minipdf::FontStyle::Regular, BODY_SIZE);
        std::vector<Line> lines;
        lines.push_back({});

        for (const auto& w : words) {
            float ww = tw(w.text, w.style, w.size);
            float need = lines.back().words.empty() ? ww
                         : lines.back().total_w + space_w + ww;

            if (need > avail && !lines.back().words.empty()) {
                lines.push_back({});
            }
            float add_w = lines.back().words.empty() ? ww : space_w + ww;
            lines.back().total_w += add_w;
            lines.back().words.push_back(w);
        }

        // Render each line
        for (const auto& line : lines) {
            ensure_space(line_h);
            float x = MARGIN_LEFT + x_indent;
            bool  first_word = true;
            for (const auto& w : line.words) {
                if (!first_word) {
                    x += space_w;
                }
                page_->place_text(x, cursor_y_, w.style, w.size, w.text);
                x += tw(w.text, w.style, w.size);
                first_word = false;
            }
            cursor_y_ -= line_h;
        }
    }

    // ── Heading ───────────────────────────────────────────────────────────────

    /// Render a section heading at the given level (0 = document title).
    void heading(const std::string& title, int level) {
        static const float SIZES[] = {26.0f, 22.0f, 18.0f, 15.0f, 13.0f, 12.0f};
        float sz = SIZES[std::min(level, 5)];
        float lh = sz * LINE_RATIO;
        float bar_h = (level == 0) ? 3.0f : (level == 1) ? 1.5f : 0.0f;

        // Extra space before heading (except at very top of first page)
        float space_before = (cursor_y_ < doc_.page_height() - MARGIN_TOP - 2.0f)
                             ? sz * 1.2f : 0.0f;

        // For ruled headings the space consumed is:
        //   space_before + lh (one heading line) + bar_offset + bar_h + gap_below
        // gap_below must be large enough that body-text ascenders (≈ BODY_SIZE)
        // do not reach up into the bar.  A gap of BODY_SIZE * 1.5 is sufficient.
        const float gap_below = (level <= 1) ? BODY_SIZE * 1.5f : 2.0f;
        // bar_offset = distance from last heading line baseline to bar bottom
        // ≈ sz * 0.35 (just past heading descenders)
        const float bar_offset = sz * 0.35f;
        ensure_space(space_before + lh +
                     (bar_h > 0.0f ? bar_offset + bar_h + gap_below : gap_below));
        cursor_y_ -= space_before;

        auto spans = parse_spans(title, minipdf::FontStyle::Bold);
        // Force heading size onto all spans
        for (auto& sp : spans) { sp.size = sz; }
        spans = merge_spans(std::move(spans));
        wrap_spans(spans, lh, 0.0f);

        // Draw a coloured rule after the heading text.  The bar is placed just
        // below the last heading line's descenders, so it acts as a visual
        // separator between the heading block and the following body text.
        if (level <= 1) {
            // cursor_y_ is now at last_line_baseline - lh; recover last baseline
            float last_baseline = cursor_y_ + lh;
            float bar_bottom = last_baseline - bar_offset;
            float c = (level == 0) ? 0.3f : 0.6f;
            page_->fill_rect(MARGIN_LEFT, bar_bottom, content_w_, bar_h, c, c, c);
            // Advance cursor to below the bar with enough room for body text
            cursor_y_ = bar_bottom - bar_h - gap_below;
        } else {
            cursor_y_ -= gap_below;
        }
    }

    // ── Paragraph ─────────────────────────────────────────────────────────────

    void paragraph(const std::string& text, float x_indent = 0.0f) {
        paragraph_spans(parse_spans(text), x_indent);
    }

    void paragraph_spans(std::vector<TextSpan> spans, float x_indent = 0.0f) {
        spans = merge_spans(std::move(spans));
        float lh = BODY_SIZE * LINE_RATIO;
        wrap_spans(spans, lh, x_indent);
        cursor_y_ -= BODY_SIZE * 0.5f;  // paragraph spacing
    }

    // ── Code block ────────────────────────────────────────────────────────────

    void code_block(const std::string& source) {
        float lh = CODE_SIZE * LINE_RATIO;
        float indent = 8.0f;

        // Count lines to determine background height
        int n_lines = 0;
        {
            std::istringstream ss(source);
            std::string line;
            while (std::getline(ss, line)) { ++n_lines; }
        }
        float block_h = static_cast<float>(n_lines) * lh + 8.0f;

        ensure_space(block_h);

        // Light grey background
        page_->fill_rect(MARGIN_LEFT - 4.0f,
                         cursor_y_ - block_h + lh * 0.25f,
                         content_w_ + 8.0f, block_h,
                         0.95f, 0.95f, 0.95f);

        cursor_y_ -= 4.0f;  // top padding

        std::istringstream ss(source);
        std::string line;
        while (std::getline(ss, line)) {
            ensure_space(lh);
            // Clip lines that would overflow the right margin.  Truncate
            // characters from the end and append "..." to signal clipping.
            float avail_w = content_w_ - indent - CODE_RIGHT_PADDING;
            float line_w  = tw(line, minipdf::FontStyle::Mono, CODE_SIZE);
            if (line_w > avail_w) {
                const std::string ellipsis = "...";
                float ell_w = tw(ellipsis, minipdf::FontStyle::Mono, CODE_SIZE);
                while (!line.empty() &&
                       tw(line, minipdf::FontStyle::Mono, CODE_SIZE)
                           + ell_w > avail_w) {
                    line.pop_back();
                }
                line += ellipsis;
            }
            page_->place_text(MARGIN_LEFT + indent, cursor_y_,
                               minipdf::FontStyle::Mono, CODE_SIZE, line);
            cursor_y_ -= lh;
        }
        // Gap below the code block must be large enough that the ascenders of
        // the following paragraph (≈ BODY_SIZE × 0.75) do not reach up into the
        // grey background rectangle.  The minimum required is ~8.9 pt; use
        // BODY_SIZE (11 pt) for a comfortable, visually consistent separation.
        cursor_y_ -= BODY_SIZE;
    }

    // ── Horizontal rule ───────────────────────────────────────────────────────

    void hrule() {
        // Leave room above the rule (4 pt) and enough room below so that
        // ascenders of the following text (≈ BODY_SIZE × 0.85) do not reach
        // up through the rule line.
        const float gap_below = BODY_SIZE * 0.85f;
        ensure_space(4.0f + gap_below);
        cursor_y_ -= 4.0f;
        page_->draw_hline(MARGIN_LEFT, cursor_y_,
                          MARGIN_LEFT + content_w_, 0.5f,
                          0.5f, 0.5f, 0.5f);
        cursor_y_ -= gap_below;
    }

    // ── Image block ───────────────────────────────────────────────────────────

    /// Embed a raster image (JPEG or PNG) at the current cursor position.
    ///
    /// The image is scaled to fit within the content area while preserving its
    /// aspect ratio.  When @p hint_w is non-zero it is used as the requested
    /// display width in points; otherwise the image fills the content width.
    /// @p hint_h is an optional height override (0 = maintain aspect ratio).
    ///
    /// When the image cannot be loaded (missing file, unsupported format) a
    /// plain-text placeholder is emitted instead.
    void image_block(const std::string& path,
                     float hint_w = 0.0f, float hint_h = 0.0f) {
        auto img = minipdf::PdfImage::from_file(path);
        if (!img) {
            // Fall back to a text placeholder
            paragraph("[image: " + path + "]");
            return;
        }

        // Determine display dimensions
        float img_w = (img->width()  > 0) ? static_cast<float>(img->width())  : 1.0f;
        float img_h = (img->height() > 0) ? static_cast<float>(img->height()) : 1.0f;
        float aspect = img_h / img_w;

        float disp_w = (hint_w > 0.0f) ? hint_w : content_w_;
        disp_w = std::min(disp_w, content_w_);  // never overflow margin

        float disp_h = (hint_h > 0.0f) ? hint_h : disp_w * aspect;

        ensure_space(disp_h + BODY_SIZE);  // image height + one body line below
        if (disp_h > (cursor_y_ - MARGIN_BOTTOM)) {
            // Still too tall after a possible page break; clamp to page
            disp_h = cursor_y_ - MARGIN_BOTTOM - BODY_SIZE;
            if (disp_h <= 0.0f) { disp_h = BODY_SIZE; }
            disp_w = disp_h / aspect;
        }

        // Add image to document and place it
        std::string res = doc_.add_image(img);
        // PDF y is from the bottom; cursor_y_ is the TOP-LEFT origin in our model
        float img_y = cursor_y_ - disp_h;
        page_->place_image(MARGIN_LEFT, img_y, disp_w, disp_h, res);
        cursor_y_ = img_y - BODY_SIZE * 0.5f;  // small gap below image
    }

    // ── Page break ────────────────────────────────────────────────────────────

    void page_break() {
        new_page();
    }

    // ── Author / revision line ────────────────────────────────────────────────

    void meta_line(const std::string& text) {
        float lh = BODY_SIZE * LINE_RATIO;
        auto spans = parse_spans(text, minipdf::FontStyle::Oblique);
        for (auto& sp : spans) { sp.size = BODY_SIZE; }
        spans = merge_spans(std::move(spans));
        ensure_space(lh);
        wrap_spans(spans, lh, 0.0f);
    }

    // ── Admonition block ──────────────────────────────────────────────────────

    void admonition(const std::string& label, const std::string& body_text) {
        float lh = BODY_SIZE * LINE_RATIO;

        // Label (NOTE, TIP, etc.) in bold
        std::string lbl = label;
        for (char& c : lbl) {
            c = static_cast<char>(
                    std::toupper(static_cast<unsigned char>(c)));
        }

        // Indent body text past the label.  Computed dynamically so that long
        // labels like "IMPORTANT:" don't overflow into the body text area.
        float indent = tw(lbl + ":", minipdf::FontStyle::Bold, BODY_SIZE)
                       + BODY_SIZE;  ///< label width + one-em gap

        ensure_space(lh * 2.0f);

        page_->place_text(MARGIN_LEFT, cursor_y_,
                          minipdf::FontStyle::Bold, BODY_SIZE, lbl + ":");

        // Body text indented
        paragraph(body_text, indent);
    }

    // ── Bullet list item ──────────────────────────────────────────────────────

    void bullet_item(const std::string& text, int depth = 0) {
        float lh    = BODY_SIZE * LINE_RATIO;
        float indent = static_cast<float>(depth) * 18.0f + 18.0f;

        ensure_space(lh);

        // Bullet character: use WinAnsiEncoding 0x95 = bullet (•)
        std::string bullet = (depth % 2 == 0) ? "\x95" : "-";
        page_->place_text(MARGIN_LEFT + indent - 12.0f, cursor_y_,
                          minipdf::FontStyle::Regular, BODY_SIZE, bullet);

        // Item text
        auto spans = parse_spans(text);
        for (auto& sp : spans) { sp.size = BODY_SIZE; }
        spans = merge_spans(std::move(spans));
        float saved_y = cursor_y_;
        wrap_spans(spans, lh, indent);

        // Restore any extra advance for multi-line items - already handled
        (void)saved_y;
    }

    // ── Ordered list item ─────────────────────────────────────────────────────

    void ordered_item(int number, const std::string& text, int depth = 0) {
        float lh     = BODY_SIZE * LINE_RATIO;
        float indent = static_cast<float>(depth) * 18.0f + 22.0f;

        ensure_space(lh);

        std::string label = std::to_string(number) + ".";
        page_->place_text(MARGIN_LEFT + indent - static_cast<float>(label.size()) * 6.0f - 4.0f,
                          cursor_y_,
                          minipdf::FontStyle::Regular, BODY_SIZE, label);

        auto spans = parse_spans(text);
        for (auto& sp : spans) { sp.size = BODY_SIZE; }
        spans = merge_spans(std::move(spans));
        wrap_spans(spans, lh, indent);
    }

    // ── Description list item ─────────────────────────────────────────────────

    void dlist_item(const std::string& term, const std::string& body) {
        float lh = BODY_SIZE * LINE_RATIO;
        ensure_space(lh);
        auto term_spans = parse_spans(term, minipdf::FontStyle::Bold);
        for (auto& sp : term_spans) { sp.size = BODY_SIZE; }
        term_spans = merge_spans(std::move(term_spans));
        wrap_spans(term_spans, lh, 0.0f);
        if (!body.empty()) {
            paragraph(body, 18.0f);
        }
    }

    // ── Block title ───────────────────────────────────────────────────────────

    void block_title(const std::string& title) {
        float lh = BODY_SIZE * LINE_RATIO;
        ensure_space(lh);
        page_->place_text(MARGIN_LEFT, cursor_y_,
                          minipdf::FontStyle::BoldOblique, BODY_SIZE, title);
        cursor_y_ -= lh;
    }

    // ── Quote / verse / sidebar (indented block) ──────────────────────────────

    void quoted_block(const std::string& text, const std::string& attribution) {
        float lh = BODY_SIZE * LINE_RATIO;
        ensure_space(lh);

        // Draw left vertical bar
        float bar_x = MARGIN_LEFT + 4.0f;
        float top_y = cursor_y_ + lh;

        // Estimate wrapped height (rough)
        auto spans = parse_spans(text);
        for (auto& sp : spans) { sp.size = BODY_SIZE; }
        spans = merge_spans(std::move(spans));
        float avail = content_w_ - 24.0f;
        // Word count estimate
        int word_count = 1;
        for (char c : text) { if (c == ' ') { ++word_count; } }
        float avg_word_w = tw("word ", minipdf::FontStyle::Regular, BODY_SIZE);
        float est_lines = std::max(1.0f,
            std::ceil(static_cast<float>(word_count) * avg_word_w / avail));
        float bar_h = est_lines * lh + (attribution.empty() ? 0 : lh);

        page_->fill_rect(bar_x - 2.0f, top_y - bar_h, 3.0f, bar_h,
                         0.4f, 0.4f, 0.4f);

        wrap_spans(spans, lh, 18.0f);
        if (!attribution.empty()) {
            // em-dash: WinAnsiEncoding 0x97
            auto attr_spans = parse_spans("\x97 " + attribution,
                                          minipdf::FontStyle::Oblique);
            for (auto& sp : attr_spans) { sp.size = BODY_SIZE; }
            attr_spans = merge_spans(std::move(attr_spans));
            wrap_spans(attr_spans, lh, 36.0f);
        }
        cursor_y_ -= 4.0f;
    }

    [[nodiscard]] float cursor_y() const { return cursor_y_; }
    [[nodiscard]] float content_w() const { return content_w_; }

    // ── Table ─────────────────────────────────────────────────────────────────

    /// One pre-processed table row passed to table_block().
    struct TableRowData {
        std::vector<std::string> cells;  ///< pre-substituted cell text
        bool                     header; ///< true → bold text + shaded background
    };

    /// Render a grid table.
    ///
    /// Column widths are given as absolute points and must sum to roughly
    /// content_w_.  Each row is a TableRowData with pre-substituted cell text.
    /// An optional block @p title is rendered above the table.
    ///
    /// The implementation:
    ///   - draws a light-grey background for header rows;
    ///   - word-wraps cell text to fit within each column;
    ///   - calls ensure_space() once per row so the table can span pages;
    ///   - draws a thin (0.5 pt) grid around every cell.
    void table_block(const std::vector<float>& col_w,
                     const std::vector<TableRowData>& rows,
                     const std::string& title = "") {
        if (col_w.empty() || rows.empty()) { return; }

        const std::size_t ncols    = col_w.size();
        const float       lh       = BODY_SIZE * LINE_RATIO;
        const float       pad_x    = 4.0f;          ///< horizontal cell padding (pts)
        // Vertical padding must clear the text ascenders / descenders.
        // cursor_y_ is the text BASELINE; ascenders extend roughly
        // 0.75 × BODY_SIZE above the baseline, descenders ~0.25 × below.
        const float       pad_top  = BODY_SIZE * 0.85f; ///< baseline → top border gap
        const float       pad_bot  = BODY_SIZE * 0.35f; ///< last-baseline → bottom gap
        const float       bdr      = 0.5f;          ///< border line width (pts)
        const float       bdr_grey = 0.5f;           ///< border grey level

        if (!title.empty()) { block_title(title); }

        // ── helpers ──────────────────────────────────────────────────────────

        // Count how many wrapped lines are needed to fit @p spans in @p avail_w.
        auto count_lines = [&](const std::vector<TextSpan>& spans,
                               float avail_w) -> int {
            if (spans.empty()) { return 1; }
            float space_w  = tw(" ", minipdf::FontStyle::Regular, BODY_SIZE);
            int   lines    = 1;
            float line_w   = 0.0f;
            for (const auto& sp : spans) {
                std::istringstream ss(sp.text);
                std::string        tok;
                while (std::getline(ss, tok, ' ')) {
                    if (tok.empty()) { continue; }
                    float ww   = tw(tok, sp.style, sp.size);
                    float need = (line_w == 0.0f) ? ww : line_w + space_w + ww;
                    if (need > avail_w && line_w > 0.0f) { ++lines; line_w = ww; }
                    else                                  { line_w = need; }
                }
            }
            return lines;
        };

        // ── row rendering ────────────────────────────────────────────────────

        float table_x = MARGIN_LEFT;
        float table_w = content_w_;

        for (const auto& row : rows) {
            const auto base = row.header ? minipdf::FontStyle::Bold
                                         : minipdf::FontStyle::Regular;

            // Compute row height: tallest cell drives the row.
            // row_h = pad_top (above first baseline) + nl × lh + pad_bot (below last baseline)
            float row_h = pad_top + lh + pad_bot;
            for (std::size_t ci = 0; ci < ncols; ++ci) {
                const std::string& txt = (ci < row.cells.size())
                                          ? row.cells[ci] : "";
                if (txt.empty()) { continue; }
                auto spans = merge_spans(parse_spans(txt, base));
                for (auto& sp : spans) { sp.size = BODY_SIZE; }
                float avail_w = col_w[ci] - 2.0f * pad_x;
                int   nl      = count_lines(spans, avail_w);
                float cell_h  = pad_top + static_cast<float>(nl) * lh + pad_bot;
                row_h = std::max(row_h, cell_h);
            }

            ensure_space(row_h + bdr);

            float row_top    = cursor_y_;
            float row_bottom = row_top - row_h;

            // Header rows: light-grey fill.
            if (row.header) {
                page_->fill_rect(table_x, row_bottom, table_w, row_h,
                                 0.88f, 0.88f, 0.88f);
            }

            // Render cell text – cursor is temporarily manipulated per cell and
            // restored afterwards so all cells in the same row share row_top.
            float x_cell = table_x;
            for (std::size_t ci = 0; ci < ncols; ++ci) {
                const std::string& txt = (ci < row.cells.size())
                                          ? row.cells[ci] : "";
                if (!txt.empty()) {
                    auto spans = merge_spans(parse_spans(txt, base));
                    for (auto& sp : spans) { sp.size = BODY_SIZE; }
                    float saved  = cursor_y_;
                    cursor_y_    = row_top - pad_top;
                    wrap_spans_in_cell(spans, lh,
                                       x_cell + pad_x,
                                       col_w[ci] - 2.0f * pad_x);
                    cursor_y_ = saved;
                }
                x_cell += col_w[ci];
            }

            // Top border for this row.
            page_->draw_hline(table_x, row_top, table_x + table_w,
                              bdr, bdr_grey, bdr_grey, bdr_grey);

            // Vertical column separators (drawn as thin filled rectangles).
            {
                float vx = table_x;
                for (std::size_t ci = 0; ci <= ncols; ++ci) {
                    page_->fill_rect(vx - bdr * 0.5f, row_bottom,
                                     bdr, row_h + bdr,
                                     bdr_grey, bdr_grey, bdr_grey);
                    if (ci < ncols) { vx += col_w[ci]; }
                }
            }

            cursor_y_ = row_bottom;
        }

        // Bottom border of the last row.
        page_->draw_hline(table_x, cursor_y_, table_x + table_w,
                          bdr, bdr_grey, bdr_grey, bdr_grey);

        cursor_y_ -= BODY_SIZE * 1.2f;   // gap below table
    }

private:
    /// Word-wrap @p spans into lines and place them starting at the absolute
    /// position (x_start, cursor_y_), clipped to @p avail_w points wide.
    ///
    /// Unlike wrap_spans(), this helper does NOT call ensure_space() – it is the
    /// caller's responsibility to have already guaranteed sufficient vertical
    /// space for the entire row before invoking this function.
    void wrap_spans_in_cell(const std::vector<TextSpan>& spans,
                            float line_h,
                            float x_start,
                            float avail_w) {
        if (spans.empty()) { return; }

        struct Word {
            std::string        text;
            minipdf::FontStyle style;
            float              size;
        };

        float space_w = tw(" ", minipdf::FontStyle::Regular, BODY_SIZE);

        std::vector<Word> words;
        for (const auto& sp : spans) {
            std::istringstream ss(sp.text);
            std::string        tok;
            while (std::getline(ss, tok, ' ')) {
                if (!tok.empty()) {
                    words.push_back({tok, sp.style, sp.size});
                }
            }
        }
        if (words.empty()) { return; }

        // Build wrapped lines.
        struct Line {
            std::vector<Word> words;
            float             total_w = 0.0f;
        };
        std::vector<Line> lines;
        lines.push_back({});

        for (const auto& w : words) {
            float ww   = tw(w.text, w.style, w.size);
            float need = lines.back().words.empty()
                             ? ww
                             : lines.back().total_w + space_w + ww;
            if (need > avail_w && !lines.back().words.empty()) {
                lines.push_back({});
            }
            float add = lines.back().words.empty() ? ww : space_w + ww;
            lines.back().total_w += add;
            lines.back().words.push_back(w);
        }

        // Render – no ensure_space; caller already reserved the row height.
        for (const auto& line : lines) {
            float x     = x_start;
            bool  first = true;
            for (const auto& w : line.words) {
                if (!first) { x += space_w; }
                page_->place_text(x, cursor_y_, w.style, w.size, w.text);
                x += tw(w.text, w.style, w.size);
                first = false;
            }
            cursor_y_ -= line_h;
        }
    }

    /// Measure the rendered width of @p text in the given style/size, using
    /// the custom font for that style when one is attached.
    [[nodiscard]] float tw(const std::string& text,
                           minipdf::FontStyle style, float size) const {
        const auto& ttf = doc_.get_font(style);
        if (ttf) {
            float w = 0.0f;
            for (char c : text) {
                w += ttf->advance_1000(static_cast<unsigned char>(c));
            }
            return w * size / 1000.0f;
        }
        return minipdf::text_width(text, style, size);
    }

    minipdf::Document&      doc_;
    minipdf::Page*          page_      = nullptr;
    float                   cursor_y_  = 0.0f;
    float                   content_w_ = 0.0f;
};

// ─────────────────────────────────────────────────────────────────────────────
// PdfConverter – walks the Document AST
// ─────────────────────────────────────────────────────────────────────────────

class PdfConverter {
public:
    explicit PdfConverter(bool a4 = false, const FontSet& fonts = {},
                          const std::string& images_dir = "")
        : page_size_(a4 ? minipdf::PageSize::A4 : minipdf::PageSize::Letter),
          images_dir_(images_dir) {
        using FS = minipdf::FontStyle;
        auto load = [](const std::string& path) {
            return minipdf::TtfFont::from_file(path);
        };
        fonts_[static_cast<std::size_t>(FS::Regular)]     = load(fonts.regular);
        fonts_[static_cast<std::size_t>(FS::Bold)]        = load(fonts.bold);
        fonts_[static_cast<std::size_t>(FS::Oblique)]     = load(fonts.italic);
        fonts_[static_cast<std::size_t>(FS::BoldOblique)] = load(fonts.bold_italic);
        fonts_[static_cast<std::size_t>(FS::Mono)]        = load(fonts.mono);
        fonts_[static_cast<std::size_t>(FS::MonoBold)]    = load(fonts.mono_bold);
    }

    [[nodiscard]] std::string convert(const Document& doc) const {
        minipdf::Document pdf(page_size_);
        using FS = minipdf::FontStyle;
        static constexpr FS styles[] = {
            FS::Regular, FS::Bold, FS::Oblique,
            FS::BoldOblique, FS::Mono, FS::MonoBold
        };
        for (auto s : styles) {
            const auto& f = fonts_[static_cast<std::size_t>(s)];
            if (f) { pdf.set_font(s, f); }
        }
        PdfLayout layout(pdf);

        render_document(doc, layout);

        return pdf.to_string();
    }

private:
    minipdf::PageSize                              page_size_;
    std::array<std::shared_ptr<minipdf::TtfFont>, 6> fonts_{};
    std::string                                    images_dir_;

    // ── Apply attribute substitution (document header and simple paragraphs)
    [[nodiscard]] static std::string attrs(const std::string& text,
                                           const Document& doc) {
        return sub_attributes(text, doc.attributes());
    }

    // ── Document ──────────────────────────────────────────────────────────────

    void render_document(const Document& doc, PdfLayout& layout) const {
        // Title, author, revision
        const DocumentHeader& hdr = doc.header();
        if (hdr.has_header && !hdr.title.empty()) {
            layout.heading(attrs(hdr.title, doc), 0);

            // Author line
            if (!hdr.authors.empty()) {
                const auto& a = hdr.authors[0];
                std::string auth;
                if (!a.firstname.empty()) { auth += a.firstname; }
                if (!a.middlename.empty()) { auth += " " + a.middlename; }
                if (!a.lastname.empty())  { auth += " " + a.lastname;  }
                if (!a.email.empty())     { auth += " <" + a.email + ">"; }
                layout.meta_line(auth);
            }

            // Revision line
            const auto& rev = hdr.revision;
            if (!rev.number.empty() || !rev.date.empty()) {
                std::string revline;
                if (!rev.number.empty()) { revline += "v" + rev.number; }
                if (!rev.date.empty()) {
                    if (!revline.empty()) { revline += ", "; }
                    revline += rev.date;
                }
                if (!rev.remark.empty()) { revline += " — " + rev.remark; }
                layout.meta_line(revline);
            }
            layout.skip(8.0f);
        }

        for (const auto& child : doc.blocks()) {
            render_block(*child, doc, layout, 0);
        }
    }

    // ── Block dispatcher ──────────────────────────────────────────────────────

    void render_block(const Block& blk, const Document& doc,
                      PdfLayout& layout, int list_depth) const {
        switch (blk.context()) {

            case BlockContext::Section:
                render_section(dynamic_cast<const Section&>(blk), doc, layout);
                break;

            case BlockContext::Paragraph:
                if (blk.has_title()) {
                    layout.block_title(attrs(blk.title(), doc));
                }
                layout.paragraph(attrs(blk.source(), doc));
                break;

            case BlockContext::Listing:
            case BlockContext::Literal:
                if (blk.has_title()) {
                    layout.block_title(attrs(blk.title(), doc));
                }
                layout.code_block(blk.source());
                break;

            case BlockContext::Ulist:
                render_ulist(dynamic_cast<const List&>(blk), doc, layout, list_depth);
                break;

            case BlockContext::Olist:
                render_olist(dynamic_cast<const List&>(blk), doc, layout, list_depth);
                break;

            case BlockContext::Dlist:
                render_dlist(dynamic_cast<const List&>(blk), doc, layout);
                break;

            case BlockContext::Admonition:
                render_admonition(blk, doc, layout);
                break;

            case BlockContext::Quote:
            case BlockContext::Verse:
                render_quote(blk, doc, layout);
                break;

            case BlockContext::Example:
            case BlockContext::Sidebar:
                render_compound(blk, doc, layout, list_depth);
                break;

            case BlockContext::Table:
                render_table(dynamic_cast<const Table&>(blk), doc, layout);
                break;

            case BlockContext::Pass:
                // Raw pass-through: emit as plain text (PDF cannot render HTML)
                layout.paragraph(blk.source());
                break;

            case BlockContext::ThematicBreak:
                layout.hrule();
                break;

            case BlockContext::PageBreak:
                layout.page_break();
                break;

            case BlockContext::Image: {                // Resolve the image target to a file path.
                // Try, in order:
                //   1. target as-is (absolute or relative to cwd)
                //   2. images_dir_ / target
                //   3. imagesdir document attribute + "/" + target
                std::string target = blk.attr("target");
                std::string resolved;
                {
                    auto try_path = [](const std::string& p) -> bool {
                        if (p.empty()) { return false; }
                        std::ifstream tf(p, std::ios::binary);
                        return static_cast<bool>(tf);
                    };
                    if (try_path(target)) {
                        resolved = target;
                    } else if (!images_dir_.empty()) {
                        std::string candidate = images_dir_ + "/" + target;
                        if (try_path(candidate)) { resolved = candidate; }
                    }
                    if (resolved.empty()) {
                        // Try document imagesdir attribute
                        std::string idir = doc.attr("imagesdir");
                        if (!idir.empty()) {
                            std::string candidate = idir + "/" + target;
                            if (try_path(candidate)) { resolved = candidate; }
                        }
                    }
                    if (resolved.empty()) { resolved = target; }
                }

                // Parse optional width/height hints from block attributes.
                // Invalid or non-numeric values are silently treated as 0 (auto).
                float hint_w = 0.0f, hint_h = 0.0f;
                {
                    const std::string& ws = blk.attr("width");
                    const std::string& hs = blk.attr("height");
                    if (!ws.empty()) {
                        try { hint_w = std::stof(ws); }
                        catch (...) { hint_w = 0.0f; }  // non-numeric → auto
                    }
                    if (!hs.empty()) {
                        try { hint_h = std::stof(hs); }
                        catch (...) { hint_h = 0.0f; }  // non-numeric → auto
                    }
                }

                layout.image_block(resolved, hint_w, hint_h);
                break;
            }

            case BlockContext::Preamble:
            default:
                for (const auto& child : blk.blocks()) {
                    render_block(*child, doc, layout, list_depth);
                }
                break;
        }
    }

    // ── Section ───────────────────────────────────────────────────────────────

    void render_section(const Section& sect, const Document& doc,
                        PdfLayout& layout) const {
        layout.heading(attrs(sect.title(), doc), sect.level());
        for (const auto& child : sect.blocks()) {
            render_block(*child, doc, layout, 0);
        }
    }

    // ── Admonition ────────────────────────────────────────────────────────────

    void render_admonition(const Block& blk, const Document& doc,
                           PdfLayout& layout) const {
        std::string label = blk.attr("name");
        if (label.empty()) { label = "note"; }

        if (blk.content_model() == ContentModel::Simple) {
            layout.admonition(label, attrs(blk.source(), doc));
        } else {
            // Render label then indented content
            std::string lbl = label;
            for (char& c : lbl) {
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
            layout.block_title(lbl + ":");
            for (const auto& child : blk.blocks()) {
                render_block(*child, doc, layout, 0);
            }
        }
    }

    // ── Quote / Verse ─────────────────────────────────────────────────────────

    void render_quote(const Block& blk, const Document& doc,
                      PdfLayout& layout) const {
        std::string attribution = !blk.attr("attribution").empty()
                                  ? blk.attr("attribution") : blk.attr("2");
        std::string citetitle   = !blk.attr("citetitle").empty()
                                  ? blk.attr("citetitle") : blk.attr("3");
        if (!citetitle.empty()) {
            if (!attribution.empty()) { attribution += ", "; }
            attribution += citetitle;
        }

        if (blk.content_model() == ContentModel::Simple) {
            layout.quoted_block(attrs(blk.source(), doc), attribution);
        } else {
            // Extract first paragraph for the quote body
            for (const auto& child : blk.blocks()) {
                if (child->context() == BlockContext::Paragraph) {
                    layout.quoted_block(attrs(child->source(), doc), attribution);
                } else {
                    render_block(*child, doc, layout, 0);
                }
            }
        }
    }

    // ── Compound block (example, sidebar) ────────────────────────────────────

    void render_compound(const Block& blk, const Document& doc,
                         PdfLayout& layout, int list_depth) const {
        if (blk.has_title()) {
            layout.block_title(attrs(blk.title(), doc));
        }
        layout.skip(4.0f);
        for (const auto& child : blk.blocks()) {
            render_block(*child, doc, layout, list_depth);
        }
        layout.skip(4.0f);
    }

    // ── Unordered list ────────────────────────────────────────────────────────

    void render_ulist(const List& list, const Document& doc,
                      PdfLayout& layout, int depth) const {
        if (list.has_title()) {
            layout.block_title(attrs(list.title(), doc));
        }
        for (const auto& item : list.items()) {
            layout.bullet_item(attrs(item->source(), doc), depth);
            // Nested lists or sub-blocks
            for (const auto& child : item->blocks()) {
                render_block(*child, doc, layout, depth + 1);
            }
        }
        layout.skip(4.0f);
    }

    // ── Ordered list ──────────────────────────────────────────────────────────

    void render_olist(const List& list, const Document& doc,
                      PdfLayout& layout, int depth) const {
        if (list.has_title()) {
            layout.block_title(attrs(list.title(), doc));
        }
        int n = 1;
        for (const auto& item : list.items()) {
            layout.ordered_item(n, attrs(item->source(), doc), depth);
            for (const auto& child : item->blocks()) {
                render_block(*child, doc, layout, depth + 1);
            }
            ++n;
        }
        layout.skip(4.0f);
    }

    // ── Description list ──────────────────────────────────────────────────────

    void render_dlist(const List& list, const Document& doc,
                      PdfLayout& layout) const {
        if (list.has_title()) {
            layout.block_title(attrs(list.title(), doc));
        }
        for (const auto& item : list.items()) {
            layout.dlist_item(attrs(item->term(), doc),
                              attrs(item->source(), doc));
            for (const auto& child : item->blocks()) {
                render_block(*child, doc, layout, 0);
            }
        }
        layout.skip(4.0f);
    }

    // ── Table ─────────────────────────────────────────────────────────────────
    //
    // Converts the Table AST node into the PdfLayout::TableRowData intermediate
    // representation and delegates rendering to PdfLayout::table_block().
    //
    // Column widths are computed proportionally from the ColumnSpec weights.
    // When no specs are present every column receives an equal share of the
    // content width.

    void render_table(const Table& tbl, const Document& doc,
                      PdfLayout& layout) const {
        const auto& specs = tbl.column_specs();

        // Determine number of columns.
        std::size_t ncols = specs.size();
        if (ncols == 0) {
            for (const auto* sec : {&tbl.head_rows(),
                                    &tbl.body_rows(),
                                    &tbl.foot_rows()}) {
                if (!sec->empty()) { ncols = sec->front().cells().size(); break; }
            }
        }
        if (ncols == 0) { return; }

        // Proportional column widths.
        int total_weight = 0;
        for (std::size_t i = 0; i < ncols; ++i) {
            total_weight += (i < specs.size() && specs[i].width > 0)
                                ? specs[i].width : 1;
        }
        std::vector<float> col_w(ncols);
        for (std::size_t i = 0; i < ncols; ++i) {
            int w = (i < specs.size() && specs[i].width > 0) ? specs[i].width : 1;
            col_w[i] = layout.content_w()
                       * static_cast<float>(w) / static_cast<float>(total_weight);
        }

        // Build row data with attribute-substituted cell text.
        std::vector<PdfLayout::TableRowData> rows;
        auto append = [&](const std::vector<TableRow>& src, bool header) {
            for (const auto& row : src) {
                PdfLayout::TableRowData rd;
                rd.header = header;
                for (const auto& cell : row.cells()) {
                    rd.cells.push_back(attrs(cell->source(), doc));
                }
                rows.push_back(std::move(rd));
            }
        };
        append(tbl.head_rows(), true);
        append(tbl.body_rows(), false);
        append(tbl.foot_rows(), false);

        std::string title = tbl.has_title() ? attrs(tbl.title(), doc) : "";
        layout.table_block(col_w, rows, title);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Free function convenience wrappers
// ─────────────────────────────────────────────────────────────────────────────

/// Convert a parsed Document to a PDF byte string using a FontSet.
/// @param a4         When true, use A4 page size; otherwise Letter (default).
/// @param fonts      Paths to TrueType font files for each text style.  Any empty
///                   path falls back to the corresponding PDF base-14 font.
/// @param images_dir Base directory to search for image files referenced by
///                   image:: blocks.  When empty, only the target path as written
///                   in the source and the document's imagesdir attribute are tried.
[[nodiscard]] inline std::string convert_to_pdf(const Document& doc,
                                                 bool a4,
                                                 const FontSet& fonts,
                                                 const std::string& images_dir = "") {
    return PdfConverter{a4, fonts, images_dir}.convert(doc);
}

/// Convert a parsed Document to a PDF byte string.
/// @param a4        When true, use A4 page size; otherwise Letter (default).
/// @param font_path Optional path to a TrueType (.ttf) file to use as the
///                  body text font (FontStyle::Regular / F1).  When empty
///                  (default) or unreadable, Helvetica is used instead.
[[nodiscard]] inline std::string convert_to_pdf(const Document& doc,
                                                 bool a4 = false,
                                                 const std::string& font_path = "") {
    FontSet fs;
    fs.regular = font_path;
    return PdfConverter{a4, fs}.convert(doc);
}

} // namespace asciiquack
