/// @file minipdf.hpp
/// @brief Minimal self-contained C++17 PDF writer for asciiquack.
///
/// Derived from concepts and data in libharu (https://github.com/libharu/libharu).
/// The font metrics tables (character widths for Helvetica, Helvetica-Bold,
/// and Courier) are taken verbatim from hpdf_fontdef_base14.c.
///
/// Original libharu copyright and license:
///
///   Copyright (c) 1999-2006 Takeshi Kanno
///   Copyright (c) 2007-2009 Antony Dovgal
///
///   Permission to use, copy, modify, distribute and sell this software
///   and its documentation for any purpose is hereby granted without fee,
///   provided that the above copyright notice appear in all copies and
///   that both that copyright notice and this permission notice appear
///   in supporting documentation.
///   It is provided "as is" without express or implied warranty.
///
/// This file implements only the subset of PDF generation needed for
/// asciiquack: base-14 fonts, text placement, word-wrapped text, simple
/// graphics (lines, filled rectangles), and Letter / A4 page sizes.
///
/// Public API summary:
///
///   minipdf::Document doc(minipdf::PageSize::Letter);
///   auto& pg = doc.new_page();
///   pg.fill_rect(50, 700, 500, 14, 0.9f, 0.9f, 0.9f);   // grey box
///   pg.place_text(72, 705, minipdf::FontStyle::Bold, 18, "Hello PDF");
///   pg.draw_hline(72, 680, 540, 0.5f);
///   std::string pdf_bytes = doc.to_string();

#pragma once

// struetype.h is an stb-style header; include only the declarations here.
// The implementation is compiled in minipdf.cpp.
#include "struetype.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace minipdf {

// ─────────────────────────────────────────────────────────────────────────────
// Page sizes (in points, 1 pt = 1/72 inch)
// ─────────────────────────────────────────────────────────────────────────────

enum class PageSize { Letter, A4 };

inline float page_width (PageSize s) { return s == PageSize::A4 ? 595.0f : 612.0f; }
inline float page_height(PageSize s) { return s == PageSize::A4 ? 842.0f : 792.0f; }

// ─────────────────────────────────────────────────────────────────────────────
// Font styles (mapped to the PDF base-14 fonts)
// ─────────────────────────────────────────────────────────────────────────────

enum class FontStyle {
    Regular,      ///< Helvetica
    Bold,         ///< Helvetica-Bold
    Oblique,      ///< Helvetica-Oblique  (italic)
    BoldOblique,  ///< Helvetica-BoldOblique
    Mono,         ///< Courier
    MonoBold,     ///< Courier-Bold
};

/// PDF /Name reference for a font resource (F1..F6).
inline const char* font_res_name(FontStyle s) {
    switch (s) {
        case FontStyle::Regular:     return "F1";
        case FontStyle::Bold:        return "F2";
        case FontStyle::Oblique:     return "F3";
        case FontStyle::BoldOblique: return "F4";
        case FontStyle::Mono:        return "F5";
        case FontStyle::MonoBold:    return "F6";
        default:                     return "F1";
    }
}

/// PDF BaseFont name for a font resource.
inline const char* font_base_name(FontStyle s) {
    switch (s) {
        case FontStyle::Regular:     return "Helvetica";
        case FontStyle::Bold:        return "Helvetica-Bold";
        case FontStyle::Oblique:     return "Helvetica-Oblique";
        case FontStyle::BoldOblique: return "Helvetica-BoldOblique";
        case FontStyle::Mono:        return "Courier";
        case FontStyle::MonoBold:    return "Courier-Bold";
        default:                     return "Helvetica";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Font metrics
//
// Character width tables for the ASCII printable range (code points 32–126).
// Values are in units of 1/1000 of a point at 1 pt font size.
// Sourced from libharu's hpdf_fontdef_base14.c (CHAR_DATA_HELVETICA,
// CHAR_DATA_HELVETICA_BOLD).  Courier is fixed-pitch (600 units per char).
// ─────────────────────────────────────────────────────────────────────────────

namespace detail {

/// Helvetica character widths, ASCII 32–126.
static const int HELVETICA_W[95] = {
    278, 278, 355, 556, 556, 889, 667, 222, 333, 333, // 32-41  sp!"#$%&'()
    389, 584, 278, 333, 278, 278,                      // 42-47  *+,-./
    556, 556, 556, 556, 556, 556, 556, 556, 556, 556,  // 48-57  0-9
    278, 278, 584, 584, 584, 556,                      // 58-63  :;<=>?
    1015,                                               // 64     @
    667, 667, 722, 722, 667, 611, 778, 722, 278, 500,  // 65-74  A-J
    667, 556, 833, 722, 778, 667, 778, 722, 667, 611,  // 75-84  K-T
    722, 667, 944, 667, 667, 611,                      // 85-90  U-Z
    278, 278, 278, 469, 556,                           // 91-95  [\]^_
    222,                                               // 96     `
    556, 556, 500, 556, 556, 278, 556, 556, 222, 222,  // 97-106 a-j
    500, 222, 833, 556, 556, 556, 556, 333, 500, 278,  // 107-116 k-t
    556, 500, 722, 500, 500, 500,                      // 117-122 u-z
    334, 260, 334, 584                                 // 123-126 {|}~
};

/// Helvetica-Bold character widths, ASCII 32–126.
static const int HELVETICA_BOLD_W[95] = {
    278, 333, 474, 556, 556, 889, 722, 278, 333, 333, // 32-41
    389, 584, 278, 333, 278, 278,                      // 42-47
    556, 556, 556, 556, 556, 556, 556, 556, 556, 556,  // 48-57
    333, 333, 584, 584, 584, 611,                      // 58-63
    975,                                               // 64
    722, 722, 722, 722, 667, 611, 778, 722, 278, 556,  // 65-74
    722, 611, 833, 722, 778, 667, 778, 722, 667, 611,  // 75-84
    722, 667, 944, 667, 667, 611,                      // 85-90
    333, 278, 333, 584, 556,                           // 91-95
    278,                                               // 96
    556, 611, 556, 611, 556, 333, 611, 611, 278, 278,  // 97-106
    556, 278, 889, 611, 611, 611, 611, 389, 556, 333,  // 107-116
    611, 556, 778, 556, 556, 500,                      // 117-122
    389, 280, 389, 584                                 // 123-126
};

} // namespace detail

// ─────────────────────────────────────────────────────────────────────────────
// TtfFont – optional user-supplied TrueType body font
//
// Uses struetype.h to parse the font file and extract character advance
// widths and vertical metrics in PDF 1000-unit space (1 em = 1000 units).
// The raw bytes are also kept for embedding as a /FontFile2 stream.
//
// TtfFont is non-copyable and non-movable because stt_fontinfo holds a
// raw pointer into bytes_; the object is always managed through shared_ptr.
// ─────────────────────────────────────────────────────────────────────────────

class TtfFont {
public:
    TtfFont(const TtfFont&)            = delete;
    TtfFont& operator=(const TtfFont&) = delete;
    TtfFont(TtfFont&&)                 = delete;
    TtfFont& operator=(TtfFont&&)      = delete;

    /// Load a TrueType font from @p path.
    /// Returns nullptr if the file cannot be opened or parsed.
    [[nodiscard]] static std::shared_ptr<TtfFont> from_file(const std::string& path) {
        if (path.empty()) { return nullptr; }

        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f) { return nullptr; }

        auto file_size = f.tellg();
        if (file_size <= 0 ||
            file_size > static_cast<std::streamoff>(
                static_cast<std::size_t>(std::numeric_limits<int>::max()))) {
            return nullptr;
        }
        std::vector<unsigned char> data(static_cast<std::size_t>(file_size));
        f.seekg(0);
        f.read(reinterpret_cast<char*>(data.data()), file_size);
        if (!f) { return nullptr; }

        // Derive a PDF-safe font name from the filename stem.
        std::string name = path;
        auto slash = name.find_last_of("/\\");
        if (slash != std::string::npos) { name = name.substr(slash + 1); }
        auto dot = name.rfind('.');
        if (dot != std::string::npos) { name = name.substr(0, dot); }

        auto font = std::shared_ptr<TtfFont>(new TtfFont());
        if (!font->init(std::move(data), name)) { return nullptr; }
        return font;
    }

    // ── Metrics (in PDF 1000-unit EM space) ───────────────────────────────────

    /// Advance width for Unicode codepoint @p cp, in 1000-unit EM space.
    [[nodiscard]] float advance_1000(int cp) const {
        int adv = 0, lsb = 0;
        stt_GetCodepointHMetrics(&info_, cp, &adv, &lsb);
        return static_cast<float>(adv) * scale_;
    }

    [[nodiscard]] float ascent_1000()    const { return ascent_; }
    [[nodiscard]] float descent_1000()   const { return descent_; }
    [[nodiscard]] float cap_height_1000() const { return cap_height_; }

    // Bounding box in 1000-unit EM space (integers for PDF integer tokens)
    [[nodiscard]] int bbox_x0() const { return bbox_x0_; }
    [[nodiscard]] int bbox_y0() const { return bbox_y0_; }
    [[nodiscard]] int bbox_x1() const { return bbox_x1_; }
    [[nodiscard]] int bbox_y1() const { return bbox_y1_; }

    /// PDF /BaseFont name (spaces/special chars replaced with '-').
    [[nodiscard]] const std::string& pdf_name() const { return pdf_name_; }

    /// Raw TTF bytes for embedding in a /FontFile2 PDF stream.
    [[nodiscard]] const std::vector<unsigned char>& raw_bytes() const { return bytes_; }

private:
    TtfFont() = default;

    bool init(std::vector<unsigned char> data, const std::string& name) {
        bytes_ = std::move(data);

        int offset = stt_GetFontOffsetForIndex(bytes_.data(), 0);
        if (offset < 0) { return false; }

        const int data_sz = static_cast<int>(bytes_.size());
        if (!stt_InitFont(&info_, bytes_.data(), data_sz, offset)) { return false; }

        // scale_ maps unscaled font units to 1000-unit EM space.
        // stt_ScaleForMappingEmToPixels(info, 1000) = 1000 / units_per_em
        scale_ = stt_ScaleForMappingEmToPixels(&info_, 1000.0f);

        // Vertical metrics: prefer OS/2 typographic values (more accurate for
        // PDF FontDescriptor /Ascent and /Descent) over hhea table values.
        int typo_asc = 0, typo_desc = 0, typo_lgap = 0;
        if (stt_GetFontVMetricsOS2(&info_, &typo_asc, &typo_desc, &typo_lgap)) {
            ascent_  = static_cast<float>(typo_asc)  * scale_;
            descent_ = static_cast<float>(typo_desc) * scale_;
        } else {
            int asc = 0, desc = 0, lgap = 0;
            stt_GetFontVMetrics(&info_, &asc, &desc, &lgap);
            ascent_  = static_cast<float>(asc)  * scale_;
            descent_ = static_cast<float>(desc) * scale_;
        }

        // Cap height: use the top of the 'H' bounding box as an approximation.
        int hx0 = 0, hy0 = 0, hx1 = 0, hy1 = 0;
        if (stt_GetCodepointBox(&info_, 'H', &hx0, &hy0, &hx1, &hy1)) {
            cap_height_ = static_cast<float>(hy1) * scale_;
        } else {
            cap_height_ = ascent_ * 0.7f;
        }

        // Font bounding box
        int fx0 = 0, fy0 = 0, fx1 = 0, fy1 = 0;
        stt_GetFontBoundingBox(&info_, &fx0, &fy0, &fx1, &fy1);
        bbox_x0_ = static_cast<int>(static_cast<float>(fx0) * scale_);
        bbox_y0_ = static_cast<int>(static_cast<float>(fy0) * scale_);
        bbox_x1_ = static_cast<int>(static_cast<float>(fx1) * scale_);
        bbox_y1_ = static_cast<int>(static_cast<float>(fy1) * scale_);

        // PostScript name (nameID=6): try Mac Roman first (platform 1, encoding 0,
        // language 0) since it is ASCII-compatible and the most portable form.
        // Fall back to Microsoft Unicode (platform 3, encoding 1) if not present,
        // filtering to ASCII-printable characters only.
        // If neither yields a usable name, derive one from the filename stem.
        bool got_ps_name = false;

        int ps_len = 0;
        const char* ps_raw = stt_GetFontNameString(
            &info_, &ps_len,
            STT_PLATFORM_ID_MAC, STT_MAC_EID_ROMAN, STT_MAC_LANG_ENGLISH, 6);
        if (ps_raw && ps_len > 0) {
            pdf_name_.assign(ps_raw, static_cast<std::size_t>(ps_len));
            got_ps_name = true;
        } else {
            // Windows platform stores strings as big-endian UTF-16; extract the
            // ASCII bytes (every other byte, starting at offset 1).
            const char* ms_raw = stt_GetFontNameString(
                &info_, &ps_len,
                STT_PLATFORM_ID_MICROSOFT, STT_MS_EID_UNICODE_BMP,
                STT_MS_LANG_ENGLISH, 6);
            if (ms_raw && ps_len > 1) {
                for (int i = 1; i < ps_len; i += 2) {
                    unsigned char hi = static_cast<unsigned char>(ms_raw[i - 1]);
                    unsigned char lo = static_cast<unsigned char>(ms_raw[i]);
                    if (hi == 0 && lo >= 0x20 && lo < 0x80) {
                        pdf_name_ += static_cast<char>(lo);
                        got_ps_name = true;
                    }
                }
            }
        }

        if (!got_ps_name || pdf_name_.empty()) {
            // Final fallback: use the filename stem passed in.
            pdf_name_ = name;
        }

        // Sanitize: PDF name tokens must not contain spaces or special chars.
        for (char& c : pdf_name_) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') {
                c = '-';
            }
        }
        if (pdf_name_.empty()) { pdf_name_ = "CustomFont"; }

        return true;
    }

    std::vector<unsigned char> bytes_;
    stt_fontinfo               info_{};
    float                      scale_      = 1.0f;
    float                      ascent_     = 800.0f;
    float                      descent_    = -200.0f;
    float                      cap_height_ = 700.0f;
    int                        bbox_x0_    = -100;
    int                        bbox_y0_    = -200;
    int                        bbox_x1_    = 1100;
    int                        bbox_y1_    = 900;
    std::string                pdf_name_;
};

// ─────────────────────────────────────────────────────────────────────────────

/// Return width of ASCII character c in 1/1000 pt units at 1 pt font size.
inline float char_width_units(char c, FontStyle style) {
    auto uc = static_cast<unsigned char>(c);
    if (uc < 32 || uc > 126) { return 556.0f; }  // fallback for out-of-range
    int idx = static_cast<int>(uc) - 32;
    switch (style) {
        case FontStyle::Bold:
        case FontStyle::BoldOblique:
            return static_cast<float>(detail::HELVETICA_BOLD_W[idx]);
        case FontStyle::Mono:
        case FontStyle::MonoBold:
            return 600.0f;
        default:
            return static_cast<float>(detail::HELVETICA_W[idx]);
    }
}

/// Return the rendered width of a string in points at the given font size.
inline float text_width(const std::string& text, FontStyle style, float size) {
    float w = 0.0f;
    for (char c : text) {
        w += char_width_units(c, style);
    }
    return w * size / 1000.0f;
}

// ─────────────────────────────────────────────────────────────────────────────
// PDF string escaping
// ─────────────────────────────────────────────────────────────────────────────

/// Escape a string for use as a PDF literal string (inside parentheses).
inline std::string pdf_escape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (char raw : text) {
        auto c = static_cast<unsigned char>(raw);
        if (c == '(' || c == ')' || c == '\\') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c < 0x20 || c >= 0x80) {
            char buf[6];
            std::snprintf(buf, sizeof(buf), "\\%03o", static_cast<unsigned int>(c));
            out += buf;
        } else {
            out += static_cast<char>(c);
        }
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Number formatting helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Format a float with two decimal places for use in a PDF content stream.
inline std::string fmtf(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(v));
    return buf;
}

// ─────────────────────────────────────────────────────────────────────────────
// Page – content stream builder
// ─────────────────────────────────────────────────────────────────────────────

/// Represents a single PDF page.  Content is accumulated in a content stream
/// string that is later embedded in the PDF file.
class Page {
public:
    float width;   ///< page width in points
    float height;  ///< page height in points

    explicit Page(float w, float h) : width(w), height(h) {}

    // ── Graphics operations ───────────────────────────────────────────────────

    /// Set the current non-stroking (fill) colour to an RGB triple (0.0–1.0).
    void set_fill_color(float r, float g, float b) {
        content_ += fmtf(r) + " " + fmtf(g) + " " + fmtf(b) + " rg\n";
    }

    /// Set the current stroking colour to an RGB triple (0.0–1.0).
    void set_stroke_color(float r, float g, float b) {
        content_ += fmtf(r) + " " + fmtf(g) + " " + fmtf(b) + " RG\n";
    }

    /// Draw a filled rectangle.  y is from the bottom of the page.
    void fill_rect(float x, float y, float w, float h,
                   float r, float g, float b) {
        // Save / modify / restore so we don't pollute the colour state.
        content_ += "q\n";
        set_fill_color(r, g, b);
        content_ += fmtf(x) + " " + fmtf(y) + " "
                  + fmtf(w) + " " + fmtf(h) + " re f\n";
        content_ += "Q\n";
    }

    /// Draw a horizontal line from (x1, y) to (x2, y).
    void draw_hline(float x1, float y, float x2, float line_width = 0.5f,
                    float r = 0.0f, float g = 0.0f, float b = 0.0f) {
        content_ += "q\n";
        content_ += fmtf(line_width) + " w\n";
        set_stroke_color(r, g, b);
        content_ += fmtf(x1) + " " + fmtf(y) + " m\n";
        content_ += fmtf(x2) + " " + fmtf(y) + " l\n";
        content_ += "S\n";
        content_ += "Q\n";
    }

    // ── Text operations ───────────────────────────────────────────────────────

    /// Place a single string of text at absolute position (x, y).
    /// y is measured from the bottom of the page (PDF coordinate system).
    void place_text(float x, float y, FontStyle style, float size,
                    const std::string& text,
                    float r = 0.0f, float g = 0.0f, float b = 0.0f) {
        if (text.empty()) { return; }
        content_ += "BT\n";
        if (r != 0.0f || g != 0.0f || b != 0.0f) {
            // non-black text fill colour
            content_ += fmtf(r) + " " + fmtf(g) + " " + fmtf(b) + " rg\n";
        }
        content_ += "/" + std::string(font_res_name(style)) + " "
                  + fmtf(size) + " Tf\n";
        content_ += "1 0 0 1 " + fmtf(x) + " " + fmtf(y) + " Tm\n";
        content_ += "(" + pdf_escape(text) + ") Tj\n";
        content_ += "ET\n";
    }

    /// Place multiple adjacent text spans on one line, starting at (x, y).
    /// Each span can have an independent font style.  x advances between spans.
    struct Span {
        std::string  text;
        FontStyle    style = FontStyle::Regular;
        float        size  = 11.0f;
    };

    void place_spans(float x, float y,
                     const std::vector<Span>& spans,
                     float r = 0.0f, float g = 0.0f, float b = 0.0f) {
        if (spans.empty()) { return; }
        for (const auto& sp : spans) {
            if (sp.text.empty()) { continue; }
            place_text(x, y, sp.style, sp.size, sp.text, r, g, b);
            x += text_width(sp.text, sp.style, sp.size);
        }
    }

    // ── Internal access for Document::to_string() ─────────────────────────────
    [[nodiscard]] const std::string& raw_content() const { return content_; }

private:
    std::string content_;   ///< accumulated PDF content stream operators
};

// ─────────────────────────────────────────────────────────────────────────────
// Document – manages pages and serialises to a PDF byte string
// ─────────────────────────────────────────────────────────────────────────────

class Document {
public:
    explicit Document(PageSize size = PageSize::Letter)
        : pw_(size == PageSize::A4 ? 595.0f : 612.0f),
          ph_(size == PageSize::A4 ? 842.0f : 792.0f) {}

    explicit Document(float w, float h) : pw_(w), ph_(h) {}

    /// Add a new blank page and return a reference to it.
    Page& new_page() {
        pages_.emplace_back(pw_, ph_);
        return pages_.back();
    }

    /// Return the current (last) page, creating one if none exist.
    Page& current_page() {
        if (pages_.empty()) { return new_page(); }
        return pages_.back();
    }

    [[nodiscard]] float page_width()  const { return pw_; }
    [[nodiscard]] float page_height() const { return ph_; }
    [[nodiscard]] std::size_t page_count() const { return pages_.size(); }

    /// Attach an optional TrueType body font.  When set, F1 (regular text)
    /// becomes an embedded TrueType font instead of the base-14 Helvetica.
    void set_body_font(std::shared_ptr<TtfFont> font) { body_font_ = std::move(font); }
    [[nodiscard]] const std::shared_ptr<TtfFont>& body_font() const { return body_font_; }

    // ── PDF serialisation ─────────────────────────────────────────────────────

    /// Serialise the document to a PDF byte string.
    ///
    /// Object ID layout when body_font_ is **not** set (default, base-14 only):
    ///   1   Catalog
    ///   2   Pages tree
    ///   3–8 F1–F6 font dicts (Helvetica … Courier-Bold, /Type1 base-14)
    ///   9+  (content, page) pairs
    ///
    /// Object ID layout when body_font_ **is** set (TrueType body font):
    ///   1   Catalog
    ///   2   Pages tree
    ///   3   TrueType font data stream (/FontFile2)
    ///   4   FontDescriptor
    ///   5   F1 font dict (/TrueType, references 3 and 4)
    ///   6–10 F2–F6 font dicts (Helvetica-Bold … Courier-Bold, /Type1 base-14)
    ///   11+ (content, page) pairs
    [[nodiscard]] std::string to_string() const {
        const bool has_ttf = body_font_ != nullptr;

        // Object ID assignment
        const int TTF_DATA_ID  = has_ttf ? 3 : 0;   // font data stream
        const int TTF_FD_ID    = has_ttf ? 4 : 0;   // FontDescriptor
        const int FONT_BASE_ID = has_ttf ? 5 : 3;   // first font dict (F1)
        constexpr int N_FONTS  = 6;
        const int BODY_BASE_ID = FONT_BASE_ID + N_FONTS;  // first page pair

        const int n_pages    = static_cast<int>(pages_.size());
        const int total_objs = BODY_BASE_ID + n_pages * 2;

        const std::size_t ttf_reserve =
            has_ttf ? body_font_->raw_bytes().size() : 0;
        std::string buf;
        buf.reserve(64 * 1024 + ttf_reserve);

        // Byte-offset table (index = object ID, value = file offset).
        std::vector<std::size_t> offsets(static_cast<std::size_t>(total_objs + 1), 0);

        // PDF header
        buf += "%PDF-1.4\n";
        buf += "%\xe2\xe3\xcf\xd3\n";  // binary marker (4 bytes > 127)

        // ── Object 1: Catalog ─────────────────────────────────────────────────
        offsets[1] = buf.size();
        buf += "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n";

        // ── Object 2: Pages tree ──────────────────────────────────────────────
        offsets[2] = buf.size();
        {
            std::string kids = "[";
            for (int i = 0; i < n_pages; ++i) {
                if (i) { kids += ' '; }
                kids += std::to_string(BODY_BASE_ID + i * 2 + 1) + " 0 R";
            }
            kids += "]";
            buf += "2 0 obj\n<< /Type /Pages /Kids " + kids
                   + " /Count " + std::to_string(n_pages) + " >>\nendobj\n";
        }

        // ── Objects 3 & 4: TrueType font stream + FontDescriptor (optional) ───
        if (has_ttf) {
            const auto& ttf_bytes = body_font_->raw_bytes();
            const auto  ttf_len   = ttf_bytes.size();

            // Object 3: Font data stream
            offsets[static_cast<std::size_t>(TTF_DATA_ID)] = buf.size();
            buf += std::to_string(TTF_DATA_ID) + " 0 obj\n"
                   "<< /Length "  + std::to_string(ttf_len)
                   + " /Length1 " + std::to_string(ttf_len) + " >>\nstream\n";
            buf.append(reinterpret_cast<const char*>(ttf_bytes.data()), ttf_len);
            buf += "\nendstream\nendobj\n";

            // Object 4: FontDescriptor
            offsets[static_cast<std::size_t>(TTF_FD_ID)] = buf.size();
            buf += std::to_string(TTF_FD_ID) + " 0 obj\n"
                   "<< /Type /FontDescriptor\n"
                   "   /FontName /" + body_font_->pdf_name() + "\n"
                   "   /Flags 32\n"
                   "   /FontBBox ["
                   + std::to_string(body_font_->bbox_x0()) + " "
                   + std::to_string(body_font_->bbox_y0()) + " "
                   + std::to_string(body_font_->bbox_x1()) + " "
                   + std::to_string(body_font_->bbox_y1()) + "]\n"
                   "   /ItalicAngle 0\n"
                   "   /Ascent "    + std::to_string(static_cast<int>(body_font_->ascent_1000()))    + "\n"
                   "   /Descent "   + std::to_string(static_cast<int>(body_font_->descent_1000()))   + "\n"
                   "   /CapHeight " + std::to_string(static_cast<int>(body_font_->cap_height_1000())) + "\n"
                   "   /StemV 80\n"
                   "   /FontFile2 " + std::to_string(TTF_DATA_ID) + " 0 R\n"
                   ">>\nendobj\n";
        }

        // ── Font objects F1–F6 ────────────────────────────────────────────────
        static const FontStyle FONT_STYLE_ORDER[N_FONTS] = {
            FontStyle::Regular, FontStyle::Bold, FontStyle::Oblique,
            FontStyle::BoldOblique, FontStyle::Mono, FontStyle::MonoBold
        };
        for (int i = 0; i < N_FONTS; ++i) {
            int fid = FONT_BASE_ID + i;
            offsets[static_cast<std::size_t>(fid)] = buf.size();

            if (i == 0 && has_ttf) {
                // F1 = embedded TrueType; build /Widths array for chars 32–255
                std::string widths = "[";
                for (int ch = 32; ch <= 255; ++ch) {
                    if (ch > 32) { widths += ' '; }
                    widths += std::to_string(
                        static_cast<int>(body_font_->advance_1000(ch)));
                }
                widths += "]";

                buf += std::to_string(fid) + " 0 obj\n"
                       "<< /Type /Font\n"
                       "   /Subtype /TrueType\n"
                       "   /BaseFont /" + body_font_->pdf_name() + "\n"
                       "   /FirstChar 32\n"
                       "   /LastChar 255\n"
                       "   /Widths " + widths + "\n"
                       "   /FontDescriptor " + std::to_string(TTF_FD_ID) + " 0 R\n"
                       "   /Encoding /WinAnsiEncoding\n"
                       ">>\nendobj\n";
            } else {
                // Base-14 Type1 font (unchanged path)
                buf += std::to_string(fid) + " 0 obj\n"
                       "<< /Type /Font /Subtype /Type1 /BaseFont /"
                       + std::string(font_base_name(FONT_STYLE_ORDER[i]))
                       + " /Encoding /WinAnsiEncoding >>\nendobj\n";
            }
        }

        // Build the /Font sub-dictionary string (shared by all pages)
        std::string font_dict = "<< ";
        for (int i = 0; i < N_FONTS; ++i) {
            font_dict += "/F" + std::to_string(i + 1) + " "
                       + std::to_string(FONT_BASE_ID + i) + " 0 R ";
        }
        font_dict += ">>";

        // ── Per-page objects ──────────────────────────────────────────────────
        for (int i = 0; i < n_pages; ++i) {
            const Page& pg = pages_[static_cast<std::size_t>(i)];
            int cid = BODY_BASE_ID + i * 2;       // content stream id
            int pid = BODY_BASE_ID + i * 2 + 1;   // page id

            // Content stream
            const std::string& cs = pg.raw_content();
            offsets[static_cast<std::size_t>(cid)] = buf.size();
            buf += std::to_string(cid) + " 0 obj\n"
                   "<< /Length " + std::to_string(cs.size()) + " >>\n"
                   "stream\n"
                   + cs
                   + "endstream\nendobj\n";

            // Page dictionary
            offsets[static_cast<std::size_t>(pid)] = buf.size();
            buf += std::to_string(pid) + " 0 obj\n"
                   "<< /Type /Page\n"
                   "   /Parent 2 0 R\n"
                   "   /MediaBox [0 0 " + fmtf(pg.width) + " " + fmtf(pg.height) + "]\n"
                   "   /Resources << /Font " + font_dict + " >>\n"
                   "   /Contents " + std::to_string(cid) + " 0 R\n"
                   ">>\nendobj\n";
        }

        // ── Cross-reference table ─────────────────────────────────────────────
        std::size_t xref_offset = buf.size();
        buf += "xref\n";
        buf += "0 " + std::to_string(total_objs) + "\n";
        buf += "0000000000 65535 f \n";
        for (int i = 1; i < total_objs; ++i) {
            char entry[24];
            std::snprintf(entry, sizeof(entry), "%010zu 00000 n \n",
                          offsets[static_cast<std::size_t>(i)]);
            buf += entry;
        }

        // ── Trailer ───────────────────────────────────────────────────────────
        buf += "trailer\n"
               "<< /Size " + std::to_string(total_objs)
               + " /Root 1 0 R >>\n"
               "startxref\n"
               + std::to_string(xref_offset) + "\n"
               "%%EOF\n";

        return buf;
    }

private:
    float                    pw_;         ///< page width  (pts)
    float                    ph_;         ///< page height (pts)
    std::vector<Page>        pages_;
    std::shared_ptr<TtfFont> body_font_;  ///< optional embedded TrueType body font
};

} // namespace minipdf
