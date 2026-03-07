/// @file pdf.hpp
/// @brief PDF converter for asciiquack.
///
/// Walks the Document AST and produces a PDF byte string using minipdf.hpp.
/// Supports Letter (8.5"×11") and A4 page sizes; outputs documents with
/// correct heading hierarchy, body text, bullet and numbered lists, code
/// blocks, admonition blocks, inline bold / italic / monospace text, and
/// basic horizontal rules.
///
/// An optional TrueType body font can be specified via the `pdf-font` document
/// attribute (absolute or relative path to a .ttf file).  When provided it is
/// embedded in the PDF and used for all regular/body text; bold, italic and
/// monospace text continue to use the PDF base-14 fonts.
///
/// Usage:
///   auto doc = Parser::parse_string(src, opts);
///   std::string pdf_bytes = asciiquack::convert_to_pdf(*doc);             // Letter, Helvetica
///   std::string pdf_bytes = asciiquack::convert_to_pdf(*doc, true);       // A4
///   std::string pdf_bytes = asciiquack::convert_to_pdf(*doc, false,
///                               "/usr/share/fonts/myfont.ttf");  // custom body font

#pragma once

#include "document.hpp"
#include "minipdf.hpp"
#include "substitutors.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace asciiquack {

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
    static constexpr float BODY_SIZE  = 11.0f;
    static constexpr float CODE_SIZE  = 10.0f;
    static constexpr float LINE_RATIO = 1.35f;   ///< line height = size × ratio

    explicit PdfLayout(minipdf::Document& doc) : doc_(doc) {
        body_font_ = doc.body_font().get();  // non-owning; Document owns the shared_ptr
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

        // Extra space before heading (except at very top of first page)
        float space_before = (cursor_y_ < doc_.page_height() - MARGIN_TOP - 2.0f)
                             ? sz * 1.2f : 0.0f;
        ensure_space(space_before + lh + 2.0f);
        cursor_y_ -= space_before;

        // Draw a coloured rule under top-level headings
        if (level <= 1) {
            float bar_h = (level == 0) ? 3.0f : 1.5f;
            float bar_y = cursor_y_ - lh - 2.0f;
            // Light grey for level 0, lighter for level 1
            float c = (level == 0) ? 0.3f : 0.6f;
            page_->fill_rect(MARGIN_LEFT, bar_y,
                             content_w_, bar_h,
                             c, c, c);
        }

        auto spans = parse_spans(title, minipdf::FontStyle::Bold);
        // Force heading size onto all spans
        for (auto& sp : spans) { sp.size = sz; }
        spans = merge_spans(std::move(spans));
        wrap_spans(spans, lh, 0.0f);

        // Small gap after heading rule
        cursor_y_ -= (level <= 1) ? 4.0f : 2.0f;
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
            page_->place_text(MARGIN_LEFT + indent, cursor_y_,
                               minipdf::FontStyle::Mono, CODE_SIZE, line);
            cursor_y_ -= lh;
        }
        cursor_y_ -= CODE_SIZE * 0.5f;
    }

    // ── Horizontal rule ───────────────────────────────────────────────────────

    void hrule() {
        ensure_space(8.0f);
        cursor_y_ -= 4.0f;
        page_->draw_hline(MARGIN_LEFT, cursor_y_,
                          MARGIN_LEFT + content_w_, 0.5f,
                          0.5f, 0.5f, 0.5f);
        cursor_y_ -= 4.0f;
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
        float indent = 54.0f;  // leave room for the label on the left

        ensure_space(lh * 2.0f);

        // Label (NOTE, TIP, etc.) in bold
        std::string lbl = label;
        for (char& c : lbl) {
            c = static_cast<char>(
                    std::toupper(static_cast<unsigned char>(c)));
        }
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

private:
    /// Measure the rendered width of @p text in the given style/size, using
    /// the custom body font for Regular text when one is attached.
    [[nodiscard]] float tw(const std::string& text,
                           minipdf::FontStyle style, float size) const {
        if (body_font_ && style == minipdf::FontStyle::Regular) {
            float w = 0.0f;
            for (char c : text) {
                w += body_font_->advance_1000(static_cast<unsigned char>(c));
            }
            return w * size / 1000.0f;
        }
        return minipdf::text_width(text, style, size);
    }

    minipdf::Document&      doc_;
    minipdf::Page*          page_      = nullptr;
    float                   cursor_y_  = 0.0f;
    float                   content_w_ = 0.0f;
    const minipdf::TtfFont* body_font_ = nullptr;  ///< non-owning; may be nullptr
};

// ─────────────────────────────────────────────────────────────────────────────
// PdfConverter – walks the Document AST
// ─────────────────────────────────────────────────────────────────────────────

class PdfConverter {
public:
    explicit PdfConverter(bool a4 = false, const std::string& font_path = "")
        : page_size_(a4 ? minipdf::PageSize::A4 : minipdf::PageSize::Letter),
          body_font_(minipdf::TtfFont::from_file(font_path)) {}

    [[nodiscard]] std::string convert(const Document& doc) const {
        minipdf::Document pdf(page_size_);
        if (body_font_) { pdf.set_body_font(body_font_); }
        PdfLayout layout(pdf);

        render_document(doc, layout);

        return pdf.to_string();
    }

private:
    minipdf::PageSize               page_size_;
    std::shared_ptr<minipdf::TtfFont> body_font_;

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

            case BlockContext::Image:
                // Images not yet supported; emit a placeholder
                layout.paragraph("[image: " + blk.attr("target") + "]");
                break;

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
        std::string attribution = blk.attr("attribution");
        std::string citetitle   = blk.attr("citetitle");
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
};

// ─────────────────────────────────────────────────────────────────────────────
// Free function convenience wrapper
// ─────────────────────────────────────────────────────────────────────────────

/// Convert a parsed Document to a PDF byte string.
/// @param a4        When true, use A4 page size; otherwise Letter (default).
/// @param font_path Optional path to a TrueType (.ttf) file to use as the
///                  body text font.  When empty (default) or unreadable,
///                  the PDF base-14 font Helvetica is used instead.
[[nodiscard]] inline std::string convert_to_pdf(const Document& doc,
                                                 bool a4 = false,
                                                 const std::string& font_path = "") {
    return PdfConverter{a4, font_path}.convert(doc);
}

} // namespace asciiquack
