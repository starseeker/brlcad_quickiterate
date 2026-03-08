/// @file substitutors.hpp
/// @brief Text substitution engine for asciiquack.
///
/// Mirrors the Ruby Asciidoctor Substitutors module.
///
/// Substitution pipeline (applied in order for "normal" subs):
///   0. pass macros       – extract pass:[] regions into protected stash
///   1. specialcharacters  – escape &, <, >
///   2. quotes             – bold, italic, monospace, …
///   3. attributes         – {name} references  (+ counter: macros)
///   4. replacements       – (C), (R), (TM), --, …, '
///   5. macros             – link:, image:, <<xref>>, footnote:, etc.
///   6. post_replacements  – hard line-break marker (" +")
///   9. pass restore       – restore protected stash regions
///
/// Each function is a pure transformation: it takes a string and returns a
/// new string with the substitution applied.

#pragma once

#include "aqregex.hpp"
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace asciiquack {

// ─────────────────────────────────────────────────────────────────────────────
// InlineContext  –  mutable state shared across inline substitutions
// ─────────────────────────────────────────────────────────────────────────────

/// Holds per-conversion mutable state needed by the inline substitution
/// pipeline: attribute map reference, named counters, and collected footnotes.
struct FootnoteEntry {
    int         number;   ///< sequential number (1-based)
    std::string id;       ///< named anchor id (empty for anonymous footnotes)
    std::string text;     ///< footnote body text
};

/// Context passed through the inline substitution pipeline.
/// The caller (Html5Converter) owns this and passes it by pointer.
struct InlineContext {
    const std::unordered_map<std::string, std::string>& attrs;

    /// Mutable counter map.  May be nullptr if counter: macros are not needed.
    std::unordered_map<std::string, int>* counters = nullptr;

    /// Collected footnotes.  May be nullptr if footnotes are not needed.
    std::vector<FootnoteEntry>* footnotes = nullptr;
};

// ─────────────────────────────────────────────────────────────────────────────
// 0. Pass macro extraction / restore  (must run before all other subs)
// ─────────────────────────────────────────────────────────────────────────────
//
// pass:[content]   – emit content with NO substitutions
// pass:q[content]  – apply only the quotes substitution to content
// pass:c[content]  – apply only the specialchars substitution to content
//
// We use STX (\x02) and ETX (\x03) as placeholder delimiters.  They don't
// appear in normal text and survive all subsequent substitution steps.

/// Extract pass:[] macros from @p text, store their (possibly processed) content
/// in @p stash, and return the text with each macro replaced by a numeric
/// placeholder ("\x02<index>\x03").  @p stash may be passed to restore_pass_macros()
/// after all other substitutions have been applied to the placeholder text.
[[nodiscard]] inline std::string extract_pass_macros(
        const std::string& text,
        std::vector<std::string>& stash)
{
    // Match:  pass:<subs>[<content>]
    // <subs> is optional letters (c, q, …); <content> may not contain ']'.
    // We also handle the single-plus passthrough (+mono+ is already handled by
    // sub_quotes, but triple-plus +++raw+++ needs pass treatment).
    static const aqrx::regex rx(
        R"(pass:([a-z]*)(\[(?:[^\]\\]|\\.)*\]))",
        aqrx::ECMAScript | aqrx::optimize);

    std::string out;
    out.reserve(text.size());

    auto begin = aqrx::sregex_iterator(text.begin(), text.end(), rx);
    auto end   = aqrx::sregex_iterator{};
    std::size_t last = 0;

    for (auto it = begin; it != end; ++it) {
        const aqrx::smatch& m = *it;
        auto pos   = static_cast<std::size_t>(m.position());
        auto len   = static_cast<std::size_t>(m.length());
        out.append(text, last, pos - last);

        const std::string subs_spec = m[1].str();   // "c", "q", "", …
        // Extract content: strip surrounding [ ]
        std::string content_raw = m[2].str();
        if (content_raw.size() >= 2) {
            content_raw = content_raw.substr(1, content_raw.size() - 2);
        }
        // Unescape any \] inside the content
        std::string content;
        for (std::size_t i = 0; i < content_raw.size(); ++i) {
            if (content_raw[i] == '\\' && i + 1 < content_raw.size() &&
                content_raw[i + 1] == ']') {
                content += ']';
                ++i;
            } else {
                content += content_raw[i];
            }
        }

        // Apply requested substitutions to the pass content
        // (specialchars is default for pass:c[]; quotes for pass:q[])
        if (!subs_spec.empty()) {
            if (subs_spec.find('c') != std::string::npos) {
                // specialchars only
                std::string esc;
                esc.reserve(content.size());
                for (char c : content) {
                    switch (c) {
                        case '&': esc += "&amp;";  break;
                        case '<': esc += "&lt;";   break;
                        case '>': esc += "&gt;";   break;
                        default:  esc += c;        break;
                    }
                }
                content = std::move(esc);
            }
            // pass:q[]: quotes substitution applied after restore (see note below)
            // We just stash the raw content and mark it for q-subs
            if (subs_spec.find('q') != std::string::npos) {
                // Mark as needing quotes by prefixing with a sentinel
                content = "\x05q\x05" + content;
            }
        }

        // Build placeholder:  STX index ETX
        out += '\x02';
        out += std::to_string(stash.size());
        out += '\x03';
        stash.push_back(std::move(content));

        last = pos + len;
    }

    out.append(text, last);
    return out;
}

/// Replace placeholders with their stashed content.
[[nodiscard]] inline std::string restore_pass_macros(
        const std::string& text,
        const std::vector<std::string>& stash);  // defined after sub_quotes

// ─────────────────────────────────────────────────────────────────────────────
// 1. Special characters
// ─────────────────────────────────────────────────────────────────────────────

/// Escape HTML special characters: & < >
[[nodiscard]] inline std::string sub_specialchars(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 16);
    for (char c : text) {
        switch (c) {
            case '&': out += "&amp;";  break;
            case '<': out += "&lt;";   break;
            case '>': out += "&gt;";   break;
            default:  out += c;        break;
        }
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// 2. Inline quote formatting
// ─────────────────────────────────────────────────────────────────────────────
//
// Constrained  (*word*, _word_, `word`, #word#, ^word^, ~word~)
//   – may not be immediately preceded / followed by a word character
// Unconstrained (**text**, __text__, ``text``, ##text##, ^^text^^, ~~text~~)
//   – no boundary restriction

namespace detail {

/// Replace the first capture group of a regex with open+text+close.
inline std::string apply_quote_rx(
        const std::string& text,
        const aqrx::regex&  rx,
        const std::string& open_tag,
        const std::string& close_tag)
{
    return aqrx::regex_replace(text, rx,
        open_tag + "$1" + close_tag);
}

} // namespace detail

/// Apply inline quote substitutions (*bold*, _italic_, etc.).
[[nodiscard]] inline std::string sub_quotes(const std::string& text) {
    std::string out = text;

    // --- Unconstrained (double markers, no boundary restriction) -------------

    // **strong**
    {
        static const aqrx::regex rx(R"(\*\*(.+?)\*\*)",
                                   aqrx::ECMAScript | aqrx::optimize);
        out = aqrx::regex_replace(out, rx, "<strong>$1</strong>");
    }
    // __emphasis__
    {
        static const aqrx::regex rx(R"(__(.+?)__)",
                                   aqrx::ECMAScript | aqrx::optimize);
        out = aqrx::regex_replace(out, rx, "<em>$1</em>");
    }
    // ``monospace``
    {
        static const aqrx::regex rx(R"(``(.+?)``)",
                                   aqrx::ECMAScript | aqrx::optimize);
        out = aqrx::regex_replace(out, rx, "<code>$1</code>");
    }
    // ##highlight##
    {
        static const aqrx::regex rx(R"(##(.+?)##)",
                                   aqrx::ECMAScript | aqrx::optimize);
        out = aqrx::regex_replace(out, rx, "<mark>$1</mark>");
    }
    // ^^superscript^^
    {
        static const aqrx::regex rx(R"(\^\^(.+?)\^\^)",
                                   aqrx::ECMAScript | aqrx::optimize);
        out = aqrx::regex_replace(out, rx, "<sup>$1</sup>");
    }
    // ~~subscript~~
    {
        static const aqrx::regex rx(R"(~~(.+?)~~)",
                                   aqrx::ECMAScript | aqrx::optimize);
        out = aqrx::regex_replace(out, rx, "<sub>$1</sub>");
    }

    // --- Constrained (single markers, requires non-word boundary) ------------
    //
    // aqrx::regex (ECMAScript) does not support lookbehind, so we capture the
    // character before the opening marker in group $1 and re-emit it.
    // The lookahead (?=[^*a-zA-Z0-9]|$) for the closing boundary IS supported.
    //
    // Bug #4 fix: exclude '/' and ':' as the preceding character to avoid
    // false positives in URLs (e.g. https://*host*/path).
    //
    // NOTE: We intentionally use [a-zA-Z0-9] rather than \w in boundary checks.
    // \w includes '_', which is also the italic delimiter.  Using \w would
    // prevent adjacent spans like *bold*_italic_ or _italic_*bold* from
    // matching, because the closing '*' followed by '_' (or opening '*'
    // preceded by '_') would be wrongly rejected as being "inside a word".

    // *strong*
    {
        static const aqrx::regex rx(
            R"((^|[^*a-zA-Z0-9/:])\*(\S|\S.*?\S)\*(?=[^*a-zA-Z0-9]|$))",
            aqrx::ECMAScript | aqrx::optimize);
        out = aqrx::regex_replace(out, rx, "$1<strong>$2</strong>");
    }
    // _emphasis_
    {
        static const aqrx::regex rx(
            R"((^|[^_a-zA-Z0-9])_(\S|\S.*?\S)_(?=[^_a-zA-Z0-9]|$))",
            aqrx::ECMAScript | aqrx::optimize);
        out = aqrx::regex_replace(out, rx, "$1<em>$2</em>");
    }
    // `monospace`
    {
        static const aqrx::regex rx(
            R"((^|[^`a-zA-Z0-9])`(\S|\S.*?\S)`(?=[^`a-zA-Z0-9]|$))",
            aqrx::ECMAScript | aqrx::optimize);
        out = aqrx::regex_replace(out, rx, "$1<code>$2</code>");
    }
    // +monospace+ (legacy)
    {
        static const aqrx::regex rx(
            R"((^|[^+a-zA-Z0-9])\+(\S|\S.*?\S)\+(?=[^+a-zA-Z0-9]|$))",
            aqrx::ECMAScript | aqrx::optimize);
        out = aqrx::regex_replace(out, rx, "$1<code>$2</code>");
    }
    // #highlight#
    // Content must start and end with an alphanumeric character to avoid
    // false positives on option values like "#/#/#" or "#,#".
    {
        static const aqrx::regex rx(
            R"((^|[^#a-zA-Z0-9])#([a-zA-Z0-9][^#\n]*[a-zA-Z0-9]|[a-zA-Z0-9])#(?=[^#a-zA-Z0-9]|$))",
            aqrx::ECMAScript | aqrx::optimize);
        out = aqrx::regex_replace(out, rx, "$1<mark>$2</mark>");
    }
    // ^superscript^
    {
        static const aqrx::regex rx(R"(\^(\S+?)\^)",
                                   aqrx::ECMAScript | aqrx::optimize);
        out = aqrx::regex_replace(out, rx, "<sup>$1</sup>");
    }
    // ~subscript~
    {
        static const aqrx::regex rx(R"(~(\S+?)~)",
                                   aqrx::ECMAScript | aqrx::optimize);
        out = aqrx::regex_replace(out, rx, "<sub>$1</sub>");
    }

    return out;
}

/// Replace placeholders (inserted by extract_pass_macros) with stashed content.
[[nodiscard]] inline std::string restore_pass_macros(
        const std::string& text,
        const std::vector<std::string>& stash)
{
    if (stash.empty()) { return text; }

    std::string out;
    out.reserve(text.size());
    const std::size_t n = text.size();

    for (std::size_t i = 0; i < n; ) {
        if (text[i] == '\x02') {
            // Find the closing ETX
            std::size_t j = i + 1;
            while (j < n && text[j] != '\x03') { ++j; }
            if (j < n) {
                std::string idx_str(text, i + 1, j - i - 1);
                try {
                    std::size_t idx = std::stoul(idx_str);
                    if (idx < stash.size()) {
                        std::string content = stash[idx];
                        // Check for q-subs sentinel (\x05 q \x05 prefix)
                        if (content.size() >= 3 &&
                            content[0] == '\x05' && content[1] == 'q' &&
                            content[2] == '\x05') {
                            content = sub_quotes(content.substr(3));
                        }
                        out += content;
                    }
                } catch (...) {}
                i = j + 1;
                continue;
            }
        }
        out += text[i++];
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// 3. Attribute references
// ─────────────────────────────────────────────────────────────────────────────

/// Expand {attribute-name} references using the supplied attribute map.
/// Behaviour for unknown attributes is controlled by the 'attribute-missing'
/// entry in @p attrs:
///   skip (default) – leave the reference as-is
///   warn           – leave as-is and emit a WARNING to stderr
///   drop           – replace the reference with an empty string
[[nodiscard]] inline std::string sub_attributes(
        const std::string&                                    text,
        const std::unordered_map<std::string, std::string>&   attrs)
{
    // Fast path: no opening brace → nothing to do.
    if (text.find('{') == std::string::npos) { return text; }

    static const aqrx::regex rx(R"(\{([\w][\w\-]*)\})",
                                aqrx::ECMAScript | aqrx::optimize);

    // Resolve attribute-missing policy (default: skip)
    std::string missing_policy = "skip";
    {
        auto it = attrs.find("attribute-missing");
        if (it != attrs.end()) { missing_policy = it->second; }
    }

    std::string out;
    out.reserve(text.size());

    auto begin = aqrx::sregex_iterator(text.begin(), text.end(), rx);
    auto end   = aqrx::sregex_iterator{};

    std::size_t last_pos = 0;
    for (auto it = begin; it != end; ++it) {
        const aqrx::smatch& m = *it;
        // Append text before this match
        out.append(text, last_pos, static_cast<std::size_t>(m.position()) - last_pos);

        const std::string& name = m[1].str();
        auto ai = attrs.find(name);
        if (ai != attrs.end()) {
            out += ai->second;
        } else if (missing_policy == "drop") {
            // drop: replace with empty string
        } else {
            if (missing_policy == "warn") {
                std::cerr << "asciiquack: WARNING: skipping reference to missing attribute: "
                          << name << "\n";
            }
            out += m[0].str();  // leave unknown references intact (skip / warn)
        }
        last_pos = static_cast<std::size_t>(m.position()) +
                   static_cast<std::size_t>(m.length());
    }
    out.append(text, last_pos);
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// 4. Typographic replacements
// ─────────────────────────────────────────────────────────────────────────────

/// Apply typographic replacements:
///   --          →  em-dash (&#8212;)
///   ...         →  ellipsis (&#8230;&#8203;)
///   (C)/(c)     →  © (&#169;)
///   (R)/(r)     →  ® (&#174;)
///   (TM)/(tm)   →  ™ (&#8482;)
///   '           →  right single quotation mark in certain contexts
[[nodiscard]] inline std::string sub_replacements(const std::string& text) {
    std::string out = text;

    // Em-dash: -- (but not --- or longer runs).
    // Asciidoctor converts -- to em-dash only when adjacent to whitespace:
    //   " -- "  (surrounded by spaces) → thin-space + em-dash + thin-space
    //   "-- "   at start of inline text (list continuation) → thin-space + em-dash + thin-space
    //   " --"   at end of inline text → thin-space + em-dash
    // This avoids falsely converting option prefixes like --help or write--.
    {
        // " -- " (space -- space) → thin-space + em-dash + thin-space
        static const aqrx::regex rx_spaced(R"( -- )",
                                           aqrx::ECMAScript | aqrx::optimize);
        out = aqrx::regex_replace(out, rx_spaced, "&#8201;&#8212;&#8201;");
        // "-- " at start of string (e.g. list-item continuation "-- body")
        // → thin-space + em-dash + thin-space, consuming the trailing space
        static const aqrx::regex rx_start(R"(^-- )",
                                          aqrx::ECMAScript | aqrx::optimize);
        out = aqrx::regex_replace(out, rx_start, "&#8201;&#8212;&#8201;");
        // " --" at end of string (space before -- at end of text)
        static const aqrx::regex rx_end(R"( --$)",
                                        aqrx::ECMAScript | aqrx::optimize);
        out = aqrx::regex_replace(out, rx_end, "&#8201;&#8212;");
    }
    // Ellipsis: ...
    {
        static const aqrx::regex rx(R"(\.\.\.)");
        out = aqrx::regex_replace(out, rx, "&#8230;&#8203;");
    }
    // Copyright: only uppercase (C) → ©   (Asciidoctor is case-sensitive here)
    {
        static const aqrx::regex rx(R"(\(C\))");
        out = aqrx::regex_replace(out, rx, "&#169;");
    }
    // Registered: only uppercase (R) → ®
    {
        static const aqrx::regex rx(R"(\(R\))");
        out = aqrx::regex_replace(out, rx, "&#174;");
    }
    // Trademark: only uppercase TM
    {
        static const aqrx::regex rx(R"(\(TM\))");
        out = aqrx::regex_replace(out, rx, "&#8482;");
    }
    // Arrow replacements (Asciidoctor typographic replacements):
    //   -> → &#8594; (→ right arrow)
    //   <- → &#8592; (← left arrow)
    //   => → &#8658; (⇒ double right arrow)
    //   <= → &#8656; (⇐ double left arrow)
    // Note: sub_specialchars runs before sub_replacements in the normal pipeline,
    // so by the time these patterns are applied '<' has been escaped to '&lt;' and
    // '>' to '&gt;'.  The regex patterns below match those HTML entities, which is
    // why stashed inline code (``...``) is already safe (it bypasses this step).
    {
        static const aqrx::regex rx_rarr(R"(\-&gt;)", aqrx::ECMAScript | aqrx::optimize);
        static const aqrx::regex rx_larr(R"(&lt;\-)", aqrx::ECMAScript | aqrx::optimize);
        static const aqrx::regex rx_rArr(R"(=&gt;)",  aqrx::ECMAScript | aqrx::optimize);
        static const aqrx::regex rx_lArr(R"(&lt;=)",  aqrx::ECMAScript | aqrx::optimize);
        out = aqrx::regex_replace(out, rx_rarr, "&#8594;");
        out = aqrx::regex_replace(out, rx_larr, "&#8592;");
        out = aqrx::regex_replace(out, rx_rArr, "&#8658;");
        out = aqrx::regex_replace(out, rx_lArr, "&#8656;");
    }
    // Smart apostrophe: word' or 'word
    {
        static const aqrx::regex rx(R"((\w)'(\w))",
                                   aqrx::ECMAScript | aqrx::optimize);
        out = aqrx::regex_replace(out, rx, "$1&#8217;$2");
    }

    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// 5. Inline macros
// ─────────────────────────────────────────────────────────────────────────────

/// Apply inline macro substitutions (links, images, xrefs, anchors).
[[nodiscard]] inline std::string sub_macros(const std::string& text) {
    std::string out = text;

    // Inline anchor: [[id]] or [[id, reftext]]
    {
        static const aqrx::regex rx(R"(\[\[([A-Za-z_:][A-Za-z0-9_\-.:]*)(?:,\s*([^\]]+))?\]\])",
                                   aqrx::ECMAScript | aqrx::optimize);
        out = aqrx::regex_replace(out, rx, R"(<a id="$1"></a>)");
    }

    // Xref: <<id>> or <<id,text>>
    {
        static const aqrx::regex rx(R"(&lt;&lt;([A-Za-z0-9_\-#/.:{]+?)(?:,\s*(.*?))?\s*&gt;&gt;)",
                                   aqrx::ECMAScript | aqrx::optimize);
        // Replace with a link; text defaults to the id.
        std::string after;
        {
            auto begin = aqrx::sregex_iterator(out.begin(), out.end(), rx);
            auto end   = aqrx::sregex_iterator{};
            std::size_t last = 0;
            for (auto it = begin; it != end; ++it) {
                const aqrx::smatch& m = *it;
                after.append(out, last, static_cast<std::size_t>(m.position()) - last);
                const std::string& id   = m[1].str();
                std::string        disp = m[2].matched ? m[2].str() : id;
                after += "<a href=\"#" + id + "\">" + disp + "</a>";
                last = static_cast<std::size_t>(m.position()) +
                       static_cast<std::size_t>(m.length());
            }
            after.append(out, last);
        }
        out = std::move(after);
    }

    // xref: macro form  xref:id[text]
    {
        static const aqrx::regex rx(R"(xref:([A-Za-z0-9_\-#/.:{]+)\[([^\]]*)\])",
                                   aqrx::ECMAScript | aqrx::optimize);
        std::string after;
        {
            auto begin = aqrx::sregex_iterator(out.begin(), out.end(), rx);
            auto end   = aqrx::sregex_iterator{};
            std::size_t last = 0;
            for (auto it = begin; it != end; ++it) {
                const aqrx::smatch& m = *it;
                after.append(out, last, static_cast<std::size_t>(m.position()) - last);
                const std::string& id   = m[1].str();
                std::string        disp = m[2].str().empty() ? id : m[2].str();
                after += "<a href=\"#" + id + "\">" + disp + "</a>";
                last = static_cast<std::size_t>(m.position()) +
                       static_cast<std::size_t>(m.length());
            }
            after.append(out, last);
        }
        out = std::move(after);
    }

    // Explicit link macro: link:url[text]
    // Matches any URL (absolute or relative), not just http/https.
    {
        static const aqrx::regex rx(R"(link:([^\[]+)\[([^\]]*)\])",
                                   aqrx::ECMAScript | aqrx::optimize);
        std::string after;
        {
            auto begin = aqrx::sregex_iterator(out.begin(), out.end(), rx);
            auto end   = aqrx::sregex_iterator{};
            std::size_t last = 0;
            for (auto it = begin; it != end; ++it) {
                const aqrx::smatch& m = *it;
                after.append(out, last, static_cast<std::size_t>(m.position()) - last);
                const std::string& url  = m[1].str();
                std::string        disp = m[2].str().empty() ? url : m[2].str();
                after += "<a href=\"" + url + "\">" + disp + "</a>";
                last = static_cast<std::size_t>(m.position()) +
                       static_cast<std::size_t>(m.length());
            }
            after.append(out, last);
        }
        out = std::move(after);
    }

    // Auto-link bare URLs: https://... or http://...
    // Avoid re-linking already-wrapped anchors by not matching inside href="..."
    // We use a simple approach: match URL not immediately inside a quote.
    {
        static const aqrx::regex rx(R"((https?://[^\s<>\[\]"]+))",
                                   aqrx::ECMAScript | aqrx::optimize);
        // Only replace if not already inside an href attribute
        std::string after;
        auto begin = aqrx::sregex_iterator(out.begin(), out.end(), rx);
        auto end   = aqrx::sregex_iterator{};
        std::size_t last = 0;
        for (auto it = begin; it != end; ++it) {
            const aqrx::smatch& m = *it;
            std::size_t match_pos = static_cast<std::size_t>(m.position());
            // Check that the character before this URL is not a double-quote
            // (which would mean we're inside an href="..." attribute)
            bool in_href = (match_pos > 0 && out[match_pos - 1] == '"');
            after.append(out, last, match_pos - last);
            if (in_href) {
                after += m[0].str();
            } else {
                after += "<a href=\"" + m[1].str() + "\">" + m[1].str() + "</a>";
            }
            last = match_pos + static_cast<std::size_t>(m.length());
        }
        after.append(out, last);
        out = std::move(after);
    }

    // Inline image: image:path[attrs]
    // The bracket content follows AsciiDoc attribute list syntax:
    //   [alt,width=N]    – first positional param is alt text
    //   [width=N]        – only named param; alt text defaults to empty
    // Named attributes (width=, height=) are emitted as HTML attributes.
    {
        static const aqrx::regex rx(R"(image:([^\[]+)\[([^\]]*)\])",
                                   aqrx::ECMAScript | aqrx::optimize);
        std::string after;
        auto begin = aqrx::sregex_iterator(out.begin(), out.end(), rx);
        auto end   = aqrx::sregex_iterator{};
        std::size_t last = 0;
        for (auto it = begin; it != end; ++it) {
            const aqrx::smatch& m = *it;
            after.append(out, last, static_cast<std::size_t>(m.position()) - last);

            const std::string& target  = m[1].str();
            const std::string& bracket = m[2].str();

            // Parse bracket: split on commas, first token without '=' is alt.
            std::string alt_text;
            std::string width_val;
            std::string height_val;
            {
                std::istringstream ss(bracket);
                std::string tok;
                bool first_tok = true;
                while (std::getline(ss, tok, ',')) {
                    // Trim leading/trailing whitespace
                    auto b = tok.find_first_not_of(" \t");
                    auto e = tok.find_last_not_of(" \t");
                    if (b == std::string::npos) { first_tok = false; continue; }
                    tok = tok.substr(b, e - b + 1);

                    auto eq = tok.find('=');
                    if (eq == std::string::npos) {
                        // Positional param
                        if (first_tok) { alt_text = tok; }
                    } else {
                        std::string key = tok.substr(0, eq);
                        std::string val = tok.substr(eq + 1);
                        if (key == "width")  { width_val  = val; }
                        if (key == "height") { height_val = val; }
                    }
                    first_tok = false;
                }
            }

            std::string html = "<span class=\"image\"><img src=\""
                             + target
                             + "\" alt=\""
                             + sub_specialchars(alt_text) + "\"";
            if (!width_val.empty())  { html += " width=\""  + width_val  + "\""; }
            if (!height_val.empty()) { html += " height=\"" + height_val + "\""; }
            html += "></span>";
            after += html;

            last = static_cast<std::size_t>(m.position()) +
                   static_cast<std::size_t>(m.length());
        }
        after.append(out, last);
        out = std::move(after);
    }

    // ── UI macros ─────────────────────────────────────────────────────────────

    // kbd:[key] or kbd:[key+key+…]
    {
        static const aqrx::regex rx(R"(kbd:\[([^\]]+)\])",
                                   aqrx::ECMAScript | aqrx::optimize);
        std::string after;
        auto begin = aqrx::sregex_iterator(out.begin(), out.end(), rx);
        auto end   = aqrx::sregex_iterator{};
        std::size_t last = 0;
        for (auto it = begin; it != end; ++it) {
            const aqrx::smatch& m = *it;
            after.append(out, last, static_cast<std::size_t>(m.position()) - last);
            // Split on '+' to render individual keys
            std::string keys_str = m[1].str();
            std::string keys_html;
            std::istringstream ks(keys_str);
            std::string key_part;
            bool first_key = true;
            while (std::getline(ks, key_part, '+')) {
                // trim whitespace
                while (!key_part.empty() && key_part.front() == ' ') { key_part.erase(0, 1); }
                while (!key_part.empty() && key_part.back()  == ' ') { key_part.pop_back(); }
                if (!first_key) { keys_html += "+"; }
                keys_html += "<kbd>" + key_part + "</kbd>";
                first_key = false;
            }
            after += "<span class=\"keyseq\">" + keys_html + "</span>";
            last = static_cast<std::size_t>(m.position()) + static_cast<std::size_t>(m.length());
        }
        after.append(out, last);
        out = std::move(after);
    }

    // btn:[label]
    {
        static const aqrx::regex rx(R"(btn:\[([^\]]+)\])",
                                   aqrx::ECMAScript | aqrx::optimize);
        out = aqrx::regex_replace(out, rx, R"(<b class="button">$1</b>)");
    }

    // menu:Menu[Item > SubItem]  or  menu:Menu[]
    {
        static const aqrx::regex rx(R"(menu:([^\[]+)\[([^\]]*)\])",
                                   aqrx::ECMAScript | aqrx::optimize);
        std::string after;
        auto begin = aqrx::sregex_iterator(out.begin(), out.end(), rx);
        auto end   = aqrx::sregex_iterator{};
        std::size_t last = 0;
        for (auto it = begin; it != end; ++it) {
            const aqrx::smatch& m = *it;
            after.append(out, last, static_cast<std::size_t>(m.position()) - last);
            std::string menu_str = m[1].str();
            // trim
            while (!menu_str.empty() && menu_str.back() == ' ') { menu_str.pop_back(); }
            std::string items_str = m[2].str();
            std::string html = "<span class=\"menuseq\"><span class=\"menu\">" + menu_str + "</span>";
            if (!items_str.empty()) {
                // Split on '>' or ','
                std::istringstream is(items_str);
                std::string part;
                while (std::getline(is, part, '>')) {
                    while (!part.empty() && part.front() == ' ') { part.erase(0, 1); }
                    while (!part.empty() && part.back()  == ' ') { part.pop_back(); }
                    if (!part.empty()) {
                        html += "<span class=\"caret\">&#8250;</span>"
                                "<span class=\"menuitem\">" + part + "</span>";
                    }
                }
            }
            html += "</span>";
            after += html;
            last = static_cast<std::size_t>(m.position()) + static_cast<std::size_t>(m.length());
        }
        after.append(out, last);
        out = std::move(after);
    }

    // ── Inline stem / math macros ─────────────────────────────────────────────
    // stem:[expr]      → \(expr\)
    // latexmath:[expr] → \(expr\)
    // asciimath:[expr] → `expr`  (rendered by MathJax ASCIIMath)
    if (out.find("stem:") != std::string::npos ||
        out.find("latexmath:") != std::string::npos ||
        out.find("asciimath:") != std::string::npos) {
        static const aqrx::regex math_rx(
            R"((stem|latexmath|asciimath):\[([^\]]*)\])",
            aqrx::ECMAScript | aqrx::optimize);
        std::string after;
        auto begin = aqrx::sregex_iterator(out.begin(), out.end(), math_rx);
        auto end   = aqrx::sregex_iterator{};
        std::size_t last = 0;
        for (auto it = begin; it != end; ++it) {
            const aqrx::smatch& m = *it;
            after.append(out, last, static_cast<std::size_t>(m.position()) - last);
            std::string kind = m[1].str();
            std::string expr = m[2].str();
            if (kind == "asciimath") {
                after += "`" + expr + "`";
            } else {
                // LaTeX inline: \(expr\)
                after += "\\(" + expr + "\\)";
            }
            last = static_cast<std::size_t>(m.position()) +
                   static_cast<std::size_t>(m.length());
        }
        after.append(out, last);
        out = std::move(after);
    }

    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// 5b. Footnote + counter macro substitutions (context-aware)
// ─────────────────────────────────────────────────────────────────────────────

/// Apply counter:/counter2: and footnote:/footnoteref: macros to @p text,
/// updating the mutable state in @p ctx.
///
/// Returns the transformed text:
///   - counter:name  →  incremented value (string) inserted inline
///   - counter2:name →  empty string (counter is incremented but not emitted)
///   - footnote:[text] →  inline superscript reference; text added to ctx.footnotes
[[nodiscard]] inline std::string sub_macros_with_ctx(
        const std::string& text,
        InlineContext& ctx)
{
    std::string out = text;

    // ── counter: / counter2: ─────────────────────────────────────────────────
    // Format:  counter:name[initial]  or  counter2:name[initial]
    // counter: returns the (incremented) value; counter2: does not return it.
    if (out.find("counter") != std::string::npos && ctx.counters) {
        static const aqrx::regex rx(
            R"((counter2?):(\w[\w\-]*)(?:\[([^\]]*)\])?)",
            aqrx::ECMAScript | aqrx::optimize);
        std::string after;
        auto begin = aqrx::sregex_iterator(out.begin(), out.end(), rx);
        auto end   = aqrx::sregex_iterator{};
        std::size_t last = 0;
        for (auto it = begin; it != end; ++it) {
            const aqrx::smatch& m = *it;
            after.append(out, last, static_cast<std::size_t>(m.position()) - last);

            std::string kind    = m[1].str();  // "counter" or "counter2"
            std::string name    = m[2].str();
            std::string initial = m[3].matched ? m[3].str() : "1";

            // Determine if alpha or numeric
            bool alpha = (!initial.empty() &&
                          std::isalpha(static_cast<unsigned char>(initial[0])));
            auto cit = ctx.counters->find(name);
            int val;
            if (cit == ctx.counters->end()) {
                // Initialise
                if (alpha) {
                    val = static_cast<int>(
                        std::tolower(static_cast<unsigned char>(initial[0])) - 'a' + 1);
                } else {
                    try { val = std::stoi(initial); } catch (...) { val = 1; }
                }
                (*ctx.counters)[name] = val;
            } else {
                ++cit->second;
                val = cit->second;
            }

            if (kind == "counter") {
                if (alpha) {
                    after += static_cast<char>('a' + (val - 1));
                } else {
                    after += std::to_string(val);
                }
            }
            // counter2: emits nothing

            last = static_cast<std::size_t>(m.position()) +
                   static_cast<std::size_t>(m.length());
        }
        after.append(out, last);
        out = std::move(after);
    }

    // ── footnote:[text] / footnoteref:[id,text] ───────────────────────────────
    if (out.find("footnote") != std::string::npos && ctx.footnotes) {
        // footnote:[body text]
        static const aqrx::regex fn_rx(
            R"(footnote(?:ref)?:\[([^\]]*)\])",
            aqrx::ECMAScript | aqrx::optimize);
        std::string after;
        auto begin = aqrx::sregex_iterator(out.begin(), out.end(), fn_rx);
        auto end   = aqrx::sregex_iterator{};
        std::size_t last = 0;

        for (auto it = begin; it != end; ++it) {
            const aqrx::smatch& m = *it;
            after.append(out, last, static_cast<std::size_t>(m.position()) - last);

            std::string content = m[1].str();
            // Check for footnoteref format: id,text  or  id (back-reference)
            std::string fn_id;
            std::string fn_text;
            auto comma = content.find(',');
            if (comma != std::string::npos) {
                fn_id   = content.substr(0, comma);
                fn_text = content.substr(comma + 1);
                while (!fn_text.empty() && fn_text.front() == ' ') { fn_text.erase(0, 1); }
            } else {
                // May be a back-reference (id only, no text) or anonymous footnote
                fn_text = content;
            }

            // Look up existing footnote by id, or create a new one
            int fn_num = 0;
            if (!fn_id.empty()) {
                for (const auto& e : *ctx.footnotes) {
                    if (e.id == fn_id) { fn_num = e.number; break; }
                }
            }
            if (fn_num == 0) {
                fn_num = static_cast<int>(ctx.footnotes->size()) + 1;
                ctx.footnotes->push_back({fn_num, fn_id, fn_text});
            }

            // Inline marker
            after += "<sup class=\"footnote\">"
                     "<a id=\"_footnoteref_" + std::to_string(fn_num) + "\" "
                     "class=\"footnote\" "
                     "href=\"#_footnotedef_" + std::to_string(fn_num) + "\" "
                     "title=\"View footnote.\">" +
                     std::to_string(fn_num) + "</a></sup>";

            last = static_cast<std::size_t>(m.position()) +
                   static_cast<std::size_t>(m.length());
        }
        after.append(out, last);
        out = std::move(after);
    }

    return out;
}

/// Replace trailing " +" with <br>.
[[nodiscard]] inline std::string sub_post_replacements(const std::string& text) {
    static const aqrx::regex rx(R"( \+$)",
                                aqrx::ECMAScript | aqrx::optimize);
    return aqrx::regex_replace(text, rx, "<br>");
}

// ─────────────────────────────────────────────────────────────────────────────
// Combined pipelines
// ─────────────────────────────────────────────────────────────────────────────

/// Apply the "normal" substitution pipeline to inline text:
///   pass-extract → specialcharacters → quotes → attributes →
///   replacements → macros → post → pass-restore
/// Optionally accepts an @p ctx for counter: and footnote: macro processing.
[[nodiscard]] inline std::string apply_normal_subs(
        const std::string&                                    text,
        const std::unordered_map<std::string, std::string>&   attrs,
        InlineContext* ctx = nullptr)
{
    // Extract pass:[] regions first so they are protected from all other subs.
    std::vector<std::string> pass_stash;
    std::string s = extract_pass_macros(text, pass_stash);

    s = sub_specialchars(s);
    s = sub_quotes(s);
    s = sub_attributes(s, attrs);
    s = sub_replacements(s);
    s = sub_macros(s);
    if (ctx) { s = sub_macros_with_ctx(s, *ctx); }
    s = sub_post_replacements(s);

    // Restore any pass:[] regions that were extracted.
    if (!pass_stash.empty()) { s = restore_pass_macros(s, pass_stash); }
    return s;
}

/// Apply only special-character escaping (for verbatim / listing blocks).
[[nodiscard]] inline std::string apply_verbatim_subs(const std::string& text) {
    // Strip trailing whitespace from each line to match Asciidoctor's behaviour,
    // then apply special character escaping.
    std::string stripped;
    stripped.reserve(text.size());
    std::size_t line_start = 0;
    while (line_start <= text.size()) {
        std::size_t nl = text.find('\n', line_start);
        std::size_t line_end = (nl == std::string::npos) ? text.size() : nl;
        // Find last non-space character in this line
        std::size_t last_non_space = line_end;
        while (last_non_space > line_start &&
               (text[last_non_space - 1] == ' ' || text[last_non_space - 1] == '\t')) {
            --last_non_space;
        }
        stripped.append(text, line_start, last_non_space - line_start);
        if (nl != std::string::npos) {
            stripped += '\n';
            line_start = nl + 1;
        } else {
            break;
        }
    }
    return sub_specialchars(stripped);
}

/// Apply header-level subs: specialcharacters + attributes.
[[nodiscard]] inline std::string apply_header_subs(
        const std::string&                                    text,
        const std::unordered_map<std::string, std::string>&   attrs)
{
    std::string s = sub_specialchars(text);
    return sub_attributes(s, attrs);
}

// ─────────────────────────────────────────────────────────────────────────────
// ID generation helpers  (mirrors Asciidoctor's Section#generate_id)
// ─────────────────────────────────────────────────────────────────────────────

/// Convert a section title to a valid HTML id attribute value.
///
/// Algorithm:
///   1. Lower-case the title.
///   2. Strip any HTML tags.
///   3. Replace sequences of non-word / non-hyphen characters with the
///      id-separator (default "_").
///   4. Prepend the id-prefix   (default "_").
///   5. Strip leading/trailing separators.
[[nodiscard]] inline std::string generate_id(
        const std::string& title,
        const std::string& prefix    = "_",
        const std::string& separator = "_")
{
    // 1. Lowercase
    std::string id;
    id.reserve(title.size());
    for (char c : title) {
        id += static_cast<char>(
            (c >= 'A' && c <= 'Z') ? (c + ('a' - 'A')) : c);
    }

    // 2. Strip HTML tags
    {
        static const aqrx::regex tag_rx(R"(<[^>]+>)",
                                       aqrx::ECMAScript | aqrx::optimize);
        id = aqrx::regex_replace(id, tag_rx, "");
    }
    // 3. Strip HTML entities
    {
        static const aqrx::regex ent_rx(R"(&[^;]+;)",
                                       aqrx::ECMAScript | aqrx::optimize);
        id = aqrx::regex_replace(id, ent_rx, "");
    }

    // 4. Replace runs of unwanted characters with the separator
    std::string clean;
    clean.reserve(id.size());
    bool in_sep = false;
    for (char c : id) {
        bool ok = (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') ||
                  c == '-' || c == '_' || c == '.';
        if (ok) {
            clean += c;
            in_sep = false;
        } else {
            if (!in_sep && !clean.empty()) {
                clean += separator;
                in_sep = true;
            }
        }
    }
    // Trim trailing separator
    while (!clean.empty() && clean.back() == separator[0]) {
        clean.pop_back();
    }

    return prefix + clean;
}

} // namespace asciiquack
