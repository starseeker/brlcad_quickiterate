/// @file manpage.hpp
/// @brief Man page (troff/groff) converter for asciiquack.
///
/// Walks the Document AST and produces a troff-formatted man page (.1 / .N
/// file) that is compatible with the output of the Ruby Asciidoctor manpage
/// backend.
///
/// The converter is a pure function: it takes a const Document& and returns
/// a std::string.  All state is held on the call stack.
///
/// Output format: groff/troff man macros (man(7)).
///   .TH   – title header
///   .SH   – section heading (uppercased)
///   .SS   – sub-section heading
///   .PP   – paragraph
///   .B    – bold
///   .I    – italic
///   .nf / .fi – no-fill / fill (for preformatted content)
///   .RS / .RE  – relative indent start / end
///   .IP   – list item with hanging indent
///   .TP   – term–description pair

#pragma once

#include "document.hpp"
#include "substitutors.hpp"
#include "outbuf.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>

namespace asciiquack {

// ─────────────────────────────────────────────────────────────────────────────
// ManpageConverter
// ─────────────────────────────────────────────────────────────────────────────

class ManpageConverter {
public:
    ManpageConverter() = default;

    /// Convert a parsed Document to a troff-formatted man page string.
    [[nodiscard]] std::string convert(const Document& doc) const {
        counters_.clear();
        OutputBuffer out;
        convert_document(doc, out);
        return out.release();
    }

private:
    // ── Per-conversion mutable state ──────────────────────────────────────────
    mutable std::unordered_map<std::string, int> counters_;

    // ── Troff text escaping ───────────────────────────────────────────────────

    /// Escape text for use in troff running text.
    /// Backslashes are doubled; a leading '.' or '\'' is preceded by \& to
    /// prevent it from being interpreted as a troff request.
    [[nodiscard]] static std::string troff_escape(const std::string& text) {
        std::string out;
        out.reserve(text.size() + 8);
        for (std::size_t i = 0; i < text.size(); ++i) {
            char c = text[i];
            if (c == '\\') {
                out += "\\\\";
            } else if (c == '-') {
                // Use \- so that copy-paste produces a real hyphen-minus
                out += "\\-";
            } else {
                out += c;
            }
        }
        // If the line starts with '.' or '\'', prepend \& to avoid macro
        // interpretation.
        if (!out.empty() && (out[0] == '.' || out[0] == '\'')) {
            out.insert(0, "\\&");
        }
        return out;
    }

    /// Convert an AsciiDoc inline text string to troff inline markup.
    /// Replaces *bold*, _italic_, `mono`, and attribute references.
    [[nodiscard]] std::string inline_subs(const std::string& text,
                                          const Document&    doc) const {
        // Apply attribute substitution first
        std::string s = sub_attributes(text, doc.attributes());
        // Apply basic inline markup conversions for troff
        s = troff_inline(s);
        return troff_escape(s);
    }

    /// Apply inline troff markup (bold/italic/mono) to a pre-substituted string.
    [[nodiscard]] static std::string troff_inline(const std::string& text) {
        std::string out;
        out.reserve(text.size() + 32);
        std::size_t i = 0;
        while (i < text.size()) {
            // Unconstrained bold: **...**
            if (i + 1 < text.size() && text[i] == '*' && text[i+1] == '*') {
                auto end = text.find("**", i + 2);
                if (end != std::string::npos) {
                    out += "\\fB";
                    out += text.substr(i + 2, end - i - 2);
                    out += "\\fR";
                    i = end + 2;
                    continue;
                }
            }
            // Constrained bold: *word*
            if (text[i] == '*' && (i == 0 || !std::isalnum(static_cast<unsigned char>(text[i-1])))) {
                auto end = text.find('*', i + 1);
                if (end != std::string::npos && end > i + 1) {
                    out += "\\fB";
                    out += text.substr(i + 1, end - i - 1);
                    out += "\\fR";
                    i = end + 1;
                    continue;
                }
            }
            // Unconstrained italic: __...__
            if (i + 1 < text.size() && text[i] == '_' && text[i+1] == '_') {
                auto end = text.find("__", i + 2);
                if (end != std::string::npos) {
                    out += "\\fI";
                    out += text.substr(i + 2, end - i - 2);
                    out += "\\fR";
                    i = end + 2;
                    continue;
                }
            }
            // Constrained italic: _word_
            if (text[i] == '_' && (i == 0 || !std::isalnum(static_cast<unsigned char>(text[i-1])))) {
                auto end = text.find('_', i + 1);
                if (end != std::string::npos && end > i + 1) {
                    out += "\\fI";
                    out += text.substr(i + 1, end - i - 1);
                    out += "\\fR";
                    i = end + 1;
                    continue;
                }
            }
            // Unconstrained mono: ``...``
            if (i + 1 < text.size() && text[i] == '`' && text[i+1] == '`') {
                auto end = text.find("``", i + 2);
                if (end != std::string::npos) {
                    out += "\\fC";
                    out += text.substr(i + 2, end - i - 2);
                    out += "\\fR";
                    i = end + 2;
                    continue;
                }
            }
            // Constrained mono: `word`
            if (text[i] == '`' && (i == 0 || !std::isalnum(static_cast<unsigned char>(text[i-1])))) {
                auto end = text.find('`', i + 1);
                if (end != std::string::npos && end > i + 1) {
                    out += "\\fC";
                    out += text.substr(i + 1, end - i - 1);
                    out += "\\fR";
                    i = end + 1;
                    continue;
                }
            }
            out += text[i];
            ++i;
        }
        return out;
    }

    /// Emit a troff section heading (.SH).  The title is uppercased.
    static void emit_sh(const std::string& title, OutputBuffer& out) {
        std::string upper;
        upper.reserve(title.size());
        for (char c : title) { upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
        out << ".SH " << upper << '\n';
    }

    /// Emit a troff sub-section heading (.SS).
    static void emit_ss(const std::string& title, OutputBuffer& out) {
        out << ".SS " << title << '\n';
    }

    // ── Document ──────────────────────────────────────────────────────────────

    void convert_document(const Document& doc, OutputBuffer& out) const {
        // Comment / source hint
        out << "'\\\" t\n";

        // .TH TITLE SECTION DATE [SOURCE [MANUAL]]
        const std::string& manname   = doc.attr("manname");
        const std::string& manvolnum = doc.attr("manvolnum", "1");
        const std::string& date      = doc.attr("revdate", doc.attr("docdate"));
        const std::string& source    = doc.attr("source");
        const std::string& manual    = doc.attr("manual");

        // Uppercase the command name for the title
        std::string manname_upper;
        for (char c : manname) {
            manname_upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        if (manname_upper.empty()) {
            // Fall back to the document title
            for (char c : doc.doctitle()) {
                manname_upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
        }

        out << ".TH \"" << manname_upper << "\" \"" << manvolnum << "\" \""
            << date << "\"";
        if (!source.empty()) {
            out << " \"" << source << "\"";
            if (!manual.empty()) {
                out << " \"" << manual << "\"";
            }
        }
        out << '\n';

        // Global troff settings
        out << ".ie \\n(.g .ds Aq \\(aq\n"
            << ".el       .ds Aq '\n"
            << ".ss \\n[.ss] 0\n"
            << ".nh\n"
            << ".ad l\n"
            << ".de URL\n"
            << "\\\\$2 \\(la\\\\$1\\(ra\\\\$3\n"
            << "..\n"
            << ".als MTO URL\n"
            << ".if \\n[.g] \\{\\\n"
            << ".  mso www.tmac\n"
            << ".\\}\n";

        // Render blocks
        for (const auto& child : doc.blocks()) {
            convert_block(*child, doc, out);
        }
    }

    // ── Block dispatcher ──────────────────────────────────────────────────────

    void convert_block(const Block& block, const Document& doc,
                       OutputBuffer& out) const {
        switch (block.context()) {
            case BlockContext::Section:
                convert_section(dynamic_cast<const Section&>(block), doc, out);
                break;
            case BlockContext::Paragraph:
                convert_paragraph(block, doc, out);
                break;
            case BlockContext::Listing:
            case BlockContext::Literal:
                convert_verbatim(block, out);
                break;
            case BlockContext::Ulist:
                convert_ulist(dynamic_cast<const List&>(block), doc, out);
                break;
            case BlockContext::Olist:
                convert_olist(dynamic_cast<const List&>(block), doc, out);
                break;
            case BlockContext::Dlist:
                convert_dlist(dynamic_cast<const List&>(block), doc, out);
                break;
            case BlockContext::Admonition:
                convert_admonition(block, doc, out);
                break;
            case BlockContext::Example:
            case BlockContext::Quote:
            case BlockContext::Verse:
            case BlockContext::Sidebar:
                convert_compound(block, doc, out);
                break;
            case BlockContext::Pass:
                // Raw passthrough: emit as-is (strip HTML if present)
                out << block.source() << '\n';
                break;
            case BlockContext::ThematicBreak:
                out << ".sp\n"
                    << "\\l'\\n(.lu'\n";
                break;
            case BlockContext::PageBreak:
                out << ".bp\n";
                break;
            case BlockContext::Image:
                // Images not representable in man pages; omit with comment
                out << "\\# [image: " << block.attr("target") << "]\n";
                break;
            default:
                for (const auto& child : block.blocks()) {
                    convert_block(*child, doc, out);
                }
                break;
        }
    }

    // ── Section ───────────────────────────────────────────────────────────────

    void convert_section(const Section& sect, const Document& doc,
                         OutputBuffer& out) const {
        int level = sect.level();  // 1 = ==, 2 = ===, …

        if (level == 1) {
            emit_sh(sect.title(), out);
        } else {
            emit_ss(sect.title(), out);
        }

        for (const auto& child : sect.blocks()) {
            convert_block(*child, doc, out);
        }
    }

    // ── Paragraph ─────────────────────────────────────────────────────────────

    void convert_paragraph(const Block& block, const Document& doc,
                           OutputBuffer& out) const {
        if (block.has_title()) {
            out << ".PP\n"
                << "\\fB" << troff_escape(block.title()) << "\\fR\n";
        }
        out << ".PP\n"
            << inline_subs(block.source(), doc) << '\n';
    }

    // ── Verbatim (listing / literal) ──────────────────────────────────────────

    static void convert_verbatim(const Block& block, OutputBuffer& out) {
        out << ".if n .RS 4\n"
            << ".nf\n"
            << ".fam C\n";
        // Emit each line with leading period protection
        std::istringstream ss(block.source());
        std::string line;
        while (std::getline(ss, line)) {
            if (!line.empty() && (line[0] == '.' || line[0] == '\'')) {
                out << "\\&";
            }
            // Escape backslashes
            for (char c : line) {
                if (c == '\\') { out << "\\\\"; }
                else           { out << c; }
            }
            out << '\n';
        }
        out << ".fam\n"
            << ".fi\n"
            << ".if n .RE\n";
    }

    // ── Admonition ────────────────────────────────────────────────────────────

    void convert_admonition(const Block& block, const Document& doc,
                            OutputBuffer& out) const {
        const std::string& name = block.attr("name", "note");
        std::string label = name;
        // Title-case
        if (!label.empty()) {
            label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
        }

        out << ".sp\n"
            << "\\fB" << label << "\\fR\n"
            << ".RS 4\n";

        if (block.content_model() == ContentModel::Simple) {
            out << inline_subs(block.source(), doc) << '\n';
        } else {
            for (const auto& child : block.blocks()) {
                convert_block(*child, doc, out);
            }
        }
        out << ".RE\n";
    }

    // ── Compound block (example, quote, sidebar) ──────────────────────────────

    void convert_compound(const Block& block, const Document& doc,
                          OutputBuffer& out) const {
        if (block.has_title()) {
            out << ".PP\n"
                << "\\fB" << troff_escape(block.title()) << "\\fR\n";
        }
        out << ".RS 4\n";
        for (const auto& child : block.blocks()) {
            convert_block(*child, doc, out);
        }
        out << ".RE\n";
    }

    // ── Unordered list ────────────────────────────────────────────────────────

    void convert_ulist(const List& list, const Document& doc,
                       OutputBuffer& out) const {
        if (list.has_title()) {
            out << ".PP\n"
                << "\\fB" << troff_escape(list.title()) << "\\fR\n";
        }
        out << ".sp\n"
            << ".RS 4\n";
        for (const auto& item : list.items()) {
            out << ".ie n \\{\\[bu]\\ \n";
            out << ".el \\{\\[bu]\\ \n";
            out << ".\\}\n";
            out << inline_subs(item->source(), doc) << '\n';
            // Sub-blocks
            for (const auto& child : item->blocks()) {
                convert_block(*child, doc, out);
            }
        }
        out << ".RE\n";
    }

    // ── Ordered list ──────────────────────────────────────────────────────────

    void convert_olist(const List& list, const Document& doc,
                       OutputBuffer& out) const {
        if (list.has_title()) {
            out << ".PP\n"
                << "\\fB" << troff_escape(list.title()) << "\\fR\n";
        }
        out << ".sp\n"
            << ".RS 4\n";
        int n = 1;
        for (const auto& item : list.items()) {
            out << ".IP " << n << ". 4\n";
            out << inline_subs(item->source(), doc) << '\n';
            for (const auto& child : item->blocks()) {
                convert_block(*child, doc, out);
            }
            ++n;
        }
        out << ".RE\n";
    }

    // ── Description list ──────────────────────────────────────────────────────

    void convert_dlist(const List& list, const Document& doc,
                       OutputBuffer& out) const {
        if (list.has_title()) {
            out << ".PP\n"
                << "\\fB" << troff_escape(list.title()) << "\\fR\n";
        }
        for (const auto& item : list.items()) {
            out << ".TP\n"
                << "\\fB" << inline_subs(item->term(), doc) << "\\fR\n";
            if (!item->source().empty()) {
                out << inline_subs(item->source(), doc) << '\n';
            }
            for (const auto& child : item->blocks()) {
                convert_block(*child, doc, out);
            }
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Free function convenience wrapper
// ─────────────────────────────────────────────────────────────────────────────

/// Convert a parsed Document to a troff man page string.
[[nodiscard]] inline std::string convert_to_manpage(const Document& doc) {
    return ManpageConverter{}.convert(doc);
}

} // namespace asciiquack
