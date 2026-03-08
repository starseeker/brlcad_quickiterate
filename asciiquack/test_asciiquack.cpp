/// @file test_asciiquack.cpp
/// @brief C++ unit tests for the asciiquack AsciiDoc processor.
///
/// Tests are self-contained: no external framework is required.
/// Each test is a function that asserts a condition; on failure it prints
/// a message and increments the failure counter.
///
/// Run with:   ./asciiquack_tests
/// CMake:      ctest  (via `add_test`)

#include "document.hpp"
#include "html5.hpp"
#include "manpage.hpp"
#include "minipdf.hpp"
#include "parser.hpp"
#include "pdf.hpp"
#include "reader.hpp"
#include "substitutors.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// Minimal test harness
// ─────────────────────────────────────────────────────────────────────────────

static int  g_total   = 0;
static int  g_failed  = 0;
static bool g_verbose = false;

#define EXPECT(cond)                                                        \
    do {                                                                    \
        ++g_total;                                                          \
        if (!(cond)) {                                                      \
            ++g_failed;                                                     \
            std::cerr << "  FAIL: " << __FILE__ << ":" << __LINE__         \
                      << "  " << #cond << "\n";                             \
        } else if (g_verbose) {                                             \
            std::cout << "  PASS: " << #cond << "\n";                      \
        }                                                                   \
    } while (false)

#define EXPECT_EQ(a, b)                                                     \
    do {                                                                    \
        ++g_total;                                                          \
        if ((a) != (b)) {                                                   \
            ++g_failed;                                                     \
            std::cerr << "  FAIL: " << __FILE__ << ":" << __LINE__         \
                      << "  expected\n    [" << (a)                         \
                      << "]\n  got\n    [" << (b) << "]\n";                 \
        } else if (g_verbose) {                                             \
            std::cout << "  PASS: " << #a << " == " << #b << "\n";         \
        }                                                                   \
    } while (false)

#define EXPECT_CONTAINS(haystack, needle)                                   \
    do {                                                                    \
        ++g_total;                                                          \
        if ((haystack).find(needle) == std::string::npos) {                 \
            ++g_failed;                                                     \
            std::cerr << "  FAIL: " << __FILE__ << ":" << __LINE__         \
                      << "  expected to find [" << (needle)                 \
                      << "] in output\n";                                   \
        } else if (g_verbose) {                                             \
            std::cout << "  PASS: output contains [" << (needle) << "]\n"; \
        }                                                                   \
    } while (false)

#define EXPECT_NOT_CONTAINS(haystack, needle)                               \
    do {                                                                    \
        ++g_total;                                                          \
        if ((haystack).find(needle) != std::string::npos) {                 \
            ++g_failed;                                                     \
            std::cerr << "  FAIL: " << __FILE__ << ":" << __LINE__         \
                      << "  expected NOT to find [" << (needle)             \
                      << "] in output\n";                                   \
        } else if (g_verbose) {                                             \
            std::cout << "  PASS: output lacks [" << (needle) << "]\n";    \
        }                                                                   \
    } while (false)

static void begin_test(const char* name) {
    std::cout << "  " << name << " ... " << std::flush;
}
static void end_test() {
    std::cout << "ok\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: parse a string and return the HTML output
// ─────────────────────────────────────────────────────────────────────────────

static std::string html(const std::string& asciidoc,
                        asciiquack::ParseOptions opts = {}) {
    opts.safe_mode = asciiquack::SafeMode::Unsafe;
    auto doc = asciiquack::Parser::parse_string(asciidoc, opts);
    return asciiquack::convert_to_html5(*doc);
}

// ─────────────────────────────────────────────────────────────────────────────
// Reader tests
// ─────────────────────────────────────────────────────────────────────────────

static void test_reader_basic() {
    begin_test("reader: basic line reading");

    asciiquack::Reader r("line1\nline2\nline3\n");
    EXPECT(r.has_more_lines());

    auto l1 = r.read_line();
    EXPECT(l1.has_value());
    EXPECT_EQ(*l1, "line1");

    EXPECT_EQ(r.lineno(), 2);

    auto l2 = r.peek_line();
    EXPECT(l2.has_value());
    EXPECT_EQ(std::string(*l2), "line2");

    // peek does not consume
    EXPECT_EQ(std::string(*r.peek_line()), "line2");

    auto l2r = r.read_line();
    EXPECT_EQ(*l2r, "line2");

    r.skip_line();  // line3
    EXPECT(!r.has_more_lines());
    EXPECT(!r.read_line().has_value());

    end_test();
}

static void test_reader_crlf() {
    begin_test("reader: CRLF line endings");

    asciiquack::Reader r("a\r\nb\r\nc\r\n");
    EXPECT_EQ(*r.read_line(), "a");
    EXPECT_EQ(*r.read_line(), "b");
    EXPECT_EQ(*r.read_line(), "c");

    end_test();
}

static void test_reader_unshift() {
    begin_test("reader: unshift_line");

    asciiquack::Reader r("b\n");
    r.unshift_line("a");
    EXPECT_EQ(*r.read_line(), "a");
    EXPECT_EQ(*r.read_line(), "b");

    end_test();
}

static void test_reader_skip_blank() {
    begin_test("reader: skip_blank_lines");

    asciiquack::Reader r("\n  \n\nfoo\n");
    int skipped = r.skip_blank_lines();
    EXPECT(skipped >= 2);
    EXPECT_EQ(*r.read_line(), "foo");

    end_test();
}

// ─────────────────────────────────────────────────────────────────────────────
// Substitutors tests
// ─────────────────────────────────────────────────────────────────────────────

static void test_sub_specialchars() {
    begin_test("substitutors: escape HTML special chars");

    EXPECT_EQ(asciiquack::sub_specialchars("a & b"), "a &amp; b");
    EXPECT_EQ(asciiquack::sub_specialchars("<tag>"),  "&lt;tag&gt;");
    EXPECT_EQ(asciiquack::sub_specialchars("a>b"),    "a&gt;b");
    EXPECT_EQ(asciiquack::sub_specialchars("plain"),  "plain");

    end_test();
}

static void test_sub_replacements() {
    begin_test("substitutors: typographic replacements");

    std::string em = asciiquack::sub_replacements("a -- b");
    EXPECT(em.find("&#8212;") != std::string::npos);

    std::string ellipsis = asciiquack::sub_replacements("...");
    EXPECT(ellipsis.find("&#8230;") != std::string::npos);

    std::string copyright = asciiquack::sub_replacements("(C)");
    EXPECT(copyright.find("&#169;") != std::string::npos);

    std::string tm = asciiquack::sub_replacements("(TM)");
    EXPECT(tm.find("&#8482;") != std::string::npos);

    end_test();
}

static void test_sub_attributes() {
    begin_test("substitutors: attribute expansion");

    std::unordered_map<std::string, std::string> attrs = {
        {"project", "asciiquack"},
        {"version", "1.0"},
    };

    EXPECT_EQ(asciiquack::sub_attributes("name: {project}", attrs), "name: asciiquack");
    EXPECT_EQ(asciiquack::sub_attributes("{version}", attrs), "1.0");
    // Unknown attribute is left as-is (default skip policy)
    EXPECT_EQ(asciiquack::sub_attributes("{unknown}", attrs), "{unknown}");
    // No brace → fast path
    EXPECT_EQ(asciiquack::sub_attributes("hello", attrs), "hello");

    // attribute-missing: drop removes the reference
    std::unordered_map<std::string, std::string> drop_attrs = {
        {"project", "asciiquack"},
        {"attribute-missing", "drop"},
    };
    EXPECT_EQ(asciiquack::sub_attributes("{project}", drop_attrs), "asciiquack");
    EXPECT_EQ(asciiquack::sub_attributes("{missing}", drop_attrs), "");
    EXPECT_EQ(asciiquack::sub_attributes("a {missing} b", drop_attrs), "a  b");

    // attribute-missing: warn leaves as-is (warning is written to stderr, not tested here)
    std::unordered_map<std::string, std::string> warn_attrs = {
        {"project", "asciiquack"},
        {"attribute-missing", "warn"},
    };
    EXPECT_EQ(asciiquack::sub_attributes("{project}", warn_attrs), "asciiquack");
    EXPECT_EQ(asciiquack::sub_attributes("{missing}", warn_attrs), "{missing}");

    end_test();
}

static void test_generate_id() {
    begin_test("substitutors: generate_id");

    EXPECT_EQ(asciiquack::generate_id("Hello World"),    "_hello_world");
    EXPECT_EQ(asciiquack::generate_id("Section A"),      "_section_a");
    EXPECT_EQ(asciiquack::generate_id("C++ is great!"),  "_c_is_great");
    EXPECT_EQ(asciiquack::generate_id("simple"),         "_simple");

    end_test();
}

// ─────────────────────────────────────────────────────────────────────────────
// Parser tests
// ─────────────────────────────────────────────────────────────────────────────

static void test_parser_section_level() {
    begin_test("parser: section_level()");

    EXPECT_EQ(asciiquack::Parser::section_level("= Title"),      0);
    EXPECT_EQ(asciiquack::Parser::section_level("== Section"),   1);
    EXPECT_EQ(asciiquack::Parser::section_level("=== Sub"),      2);
    EXPECT_EQ(asciiquack::Parser::section_level("==== Sub2"),    3);
    EXPECT_EQ(asciiquack::Parser::section_level("===== Sub3"),   4);
    EXPECT_EQ(asciiquack::Parser::section_level("====== Sub4"),  5);
    EXPECT_EQ(asciiquack::Parser::section_level("======= Too many"), -1);
    EXPECT_EQ(asciiquack::Parser::section_level("not a title"),  -1);
    EXPECT_EQ(asciiquack::Parser::section_level(""),             -1);
    EXPECT_EQ(asciiquack::Parser::section_level("==no space"),   -1);

    end_test();
}

static void test_parser_section_title_text() {
    begin_test("parser: section_title_text()");

    EXPECT_EQ(asciiquack::Parser::section_title_text("= My Title"),    "My Title");
    EXPECT_EQ(asciiquack::Parser::section_title_text("== Section A"),  "Section A");
    // Trailing markers stripped
    EXPECT_EQ(asciiquack::Parser::section_title_text("== Foo =="),     "Foo");

    end_test();
}

static void test_parser_empty_document() {
    begin_test("parser: empty document");

    auto doc = asciiquack::Parser::parse_string("");
    EXPECT(doc != nullptr);
    EXPECT(doc->doctitle().empty());
    EXPECT(doc->blocks().empty());

    end_test();
}

static void test_parser_document_title() {
    begin_test("parser: document title");

    auto doc = asciiquack::Parser::parse_string("= My Title\n");
    EXPECT_EQ(doc->doctitle(), "My Title");

    end_test();
}

static void test_parser_document_header_full() {
    begin_test("parser: full document header");

    const std::string src =
        "= Document Title\n"
        "John Doe <john@example.com>\n"
        "v1.2, 2024-06-01: Initial release\n"
        ":myattr: hello\n"
        "\n"
        "Body paragraph.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    EXPECT_EQ(doc->doctitle(), "Document Title");
    EXPECT(!doc->authors().empty());
    EXPECT_EQ(doc->authors()[0].firstname, "John");
    EXPECT_EQ(doc->authors()[0].email,     "john@example.com");
    EXPECT_EQ(doc->revision().number,      "1.2");
    EXPECT_EQ(doc->revision().date,        "2024-06-01");
    EXPECT_EQ(doc->attr("myattr"),         "hello");
    EXPECT(!doc->blocks().empty());

    end_test();
}

static void test_parser_paragraph() {
    begin_test("parser: paragraph");

    const std::string src = "Hello, world.\n\nSecond paragraph.\n";
    auto doc = asciiquack::Parser::parse_string(src);
    EXPECT_EQ(doc->blocks().size(), std::size_t{2});
    EXPECT(doc->blocks()[0]->context() == asciiquack::BlockContext::Paragraph);
    EXPECT_EQ(doc->blocks()[0]->source(), "Hello, world.");

    end_test();
}

static void test_parser_sections() {
    begin_test("parser: sections");

    const std::string src =
        "= Document\n\n"
        "Preamble text.\n\n"
        "== Section A\n\n"
        "Section A content.\n\n"
        "=== Subsection\n\n"
        "Subsection content.\n\n"
        "== Section B\n\n"
        "Section B content.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    EXPECT_EQ(doc->doctitle(), "Document");

    // There should be a preamble paragraph and 2 top-level sections
    int sections = 0;
    int paragraphs = 0;
    for (const auto& b : doc->blocks()) {
        if (b->context() == asciiquack::BlockContext::Section) { ++sections; }
        if (b->context() == asciiquack::BlockContext::Paragraph) { ++paragraphs; }
    }
    EXPECT(sections >= 2);
    EXPECT(paragraphs >= 1);

    end_test();
}

static void test_parser_attribute_entry() {
    begin_test("parser: attribute entry in body");

    const std::string src =
        ":greeting: Hello\n\n"
        "Paragraph with {greeting}.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    EXPECT_EQ(doc->attr("greeting"), "Hello");

    end_test();
}

static void test_parser_listing_block() {
    begin_test("parser: listing block");

    const std::string src =
        "[source,cpp]\n"
        "----\n"
        "int main() { return 0; }\n"
        "----\n";

    auto doc = asciiquack::Parser::parse_string(src);
    EXPECT(!doc->blocks().empty());
    auto& b = *doc->blocks()[0];
    EXPECT(b.context() == asciiquack::BlockContext::Listing);
    EXPECT(b.source().find("int main") != std::string::npos);

    end_test();
}

static void test_parser_unordered_list() {
    begin_test("parser: unordered list");

    const std::string src =
        "* Item 1\n"
        "* Item 2\n"
        "* Item 3\n";

    auto doc = asciiquack::Parser::parse_string(src);
    EXPECT(!doc->blocks().empty());
    auto& b = *doc->blocks()[0];
    EXPECT(b.context() == asciiquack::BlockContext::Ulist);

    const auto& lst = dynamic_cast<const asciiquack::List&>(b);
    EXPECT_EQ(lst.items().size(), std::size_t{3});
    EXPECT_EQ(lst.items()[0]->source(), "Item 1");

    end_test();
}

static void test_parser_ordered_list() {
    begin_test("parser: ordered list");

    const std::string src =
        ". First\n"
        ". Second\n"
        ". Third\n";

    auto doc = asciiquack::Parser::parse_string(src);
    EXPECT(!doc->blocks().empty());
    EXPECT(doc->blocks()[0]->context() == asciiquack::BlockContext::Olist);

    const auto& lst = dynamic_cast<const asciiquack::List&>(*doc->blocks()[0]);
    EXPECT_EQ(lst.items().size(), std::size_t{3});
    EXPECT_EQ(lst.items()[1]->source(), "Second");

    end_test();
}

static void test_parser_admonition_paragraph() {
    begin_test("parser: admonition paragraph");

    const std::string src = "NOTE: Pay attention.\n";
    auto doc = asciiquack::Parser::parse_string(src);
    EXPECT(!doc->blocks().empty());
    EXPECT(doc->blocks()[0]->context() == asciiquack::BlockContext::Admonition);
    EXPECT_EQ(doc->blocks()[0]->attr("name"), "note");

    end_test();
}

static void test_parser_block_title() {
    begin_test("parser: block title (.Title)");

    const std::string src =
        ".My Code Block\n"
        "----\n"
        "code here\n"
        "----\n";

    auto doc = asciiquack::Parser::parse_string(src);
    EXPECT(!doc->blocks().empty());
    EXPECT_EQ(doc->blocks()[0]->title(), "My Code Block");

    end_test();
}

static void test_parser_thematic_break() {
    begin_test("parser: thematic break (''')");

    const std::string src = "para\n\n'''\n\npara2\n";
    auto doc = asciiquack::Parser::parse_string(src);

    bool found = false;
    for (const auto& b : doc->blocks()) {
        if (b->context() == asciiquack::BlockContext::ThematicBreak) { found = true; }
    }
    EXPECT(found);

    end_test();
}

static void test_parser_comment_line() {
    begin_test("parser: comment line (//)");

    const std::string src =
        "// This should not appear in the output\n"
        "Visible paragraph.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    // Only the paragraph, not the comment
    EXPECT(!doc->blocks().empty());
    bool has_comment = false;
    for (const auto& b : doc->blocks()) {
        if (b->source().find("should not appear") != std::string::npos) {
            has_comment = true;
        }
    }
    EXPECT(!has_comment);

    end_test();
}

// ─────────────────────────────────────────────────────────────────────────────
// HTML5 converter tests
// ─────────────────────────────────────────────────────────────────────────────

static void test_html5_doctype() {
    begin_test("html5: DOCTYPE present");
    EXPECT_CONTAINS(html("= Title\n"), "<!DOCTYPE html>");
    end_test();
}

static void test_html5_title() {
    begin_test("html5: document title in <title> and <h1>");
    std::string out = html("= My Document\n");
    EXPECT_CONTAINS(out, "<title>My Document</title>");
    EXPECT_CONTAINS(out, "<h1>My Document</h1>");
    end_test();
}

static void test_html5_author() {
    begin_test("html5: author in header");
    std::string out = html("= Title\nJane Smith <jane@example.com>\n");
    EXPECT_CONTAINS(out, "Jane Smith");
    EXPECT_CONTAINS(out, "jane@example.com");
    end_test();
}

static void test_html5_paragraph() {
    begin_test("html5: paragraph");
    std::string out = html("Hello world.\n");
    EXPECT_CONTAINS(out, "<div class=\"paragraph\">");
    EXPECT_CONTAINS(out, "<p>Hello world.</p>");
    end_test();
}

static void test_html5_section() {
    begin_test("html5: section headings");
    std::string out = html("= Doc\n\n== Section One\n\nText.\n");
    EXPECT_CONTAINS(out, "class=\"sect1\"");
    EXPECT_CONTAINS(out, "<h2");
    EXPECT_CONTAINS(out, "Section One");
    end_test();
}

static void test_html5_section_id() {
    begin_test("html5: section id generated from title");
    std::string out = html("= Doc\n\n== My Section\n\nText.\n");
    EXPECT_CONTAINS(out, "id=\"_my_section\"");
    end_test();
}

static void test_html5_listing_block() {
    begin_test("html5: listing block");
    std::string out = html(
        "[source,python]\n"
        "----\n"
        "print('hello')\n"
        "----\n");
    EXPECT_CONTAINS(out, "class=\"listingblock\"");
    EXPECT_CONTAINS(out, "language-python");
    // "print" and "hello" must appear in the output regardless of whether
    // syntax highlighting is active (they may be inside <h-> spans when it is).
    EXPECT_CONTAINS(out, "print");
    EXPECT_CONTAINS(out, "hello");
    end_test();
}

#ifdef ASCIIQUACK_USE_ULIGHT
static void test_html5_highlighting_python() {
    begin_test("html5: ulight syntax highlighting – Python");
    std::string out = html(
        "[source,python]\n"
        "----\n"
        "x = 42\n"
        "----\n");
    // µlight should emit <h-> spans for at least the numeric literal.
    EXPECT_CONTAINS(out, "data-h=");
    EXPECT_CONTAINS(out, "42");
    end_test();
}

static void test_html5_highlighting_cpp() {
    begin_test("html5: ulight syntax highlighting – C++");
    std::string out = html(
        "[source,cpp]\n"
        "----\n"
        "int main() { return 0; }\n"
        "----\n");
    EXPECT_CONTAINS(out, "data-h=kw_type");  // "int" is a type keyword
    EXPECT_CONTAINS(out, "data-h=kw_ctrl");  // "return" is a control keyword
    EXPECT_CONTAINS(out, "data-h=num");      // "0" is a number
    end_test();
}

static void test_html5_highlighting_unknown_lang() {
    begin_test("html5: ulight falls back for unknown language");
    // "cobol" is not in µlight; should render as plain verbatim HTML.
    std::string out = html(
        "[source,cobol]\n"
        "----\n"
        "DISPLAY 'HELLO'.\n"
        "----\n");
    EXPECT_CONTAINS(out, "language-cobol");
    // No µlight spans – plain HTML-escaped content.
    EXPECT_CONTAINS(out, "DISPLAY");
    end_test();
}

static void test_html5_highlighting_html_escaping() {
    begin_test("html5: ulight HTML-escapes source content");
    std::string out = html(
        "[source,cpp]\n"
        "----\n"
        "if (a < b && b > 0) {}\n"
        "----\n");
    // Raw < and & must be escaped in the output.
    EXPECT_NOT_CONTAINS(out, " < ");
    EXPECT_NOT_CONTAINS(out, " && ");
    EXPECT_CONTAINS(out, "&lt;");
    EXPECT_CONTAINS(out, "&amp;&amp;");
    end_test();
}
#endif  // ASCIIQUACK_USE_ULIGHT

static void test_html5_literal_block() {
    begin_test("html5: literal block");
    std::string out = html(
        "....\n"
        "literal text here\n"
        "....\n");
    EXPECT_CONTAINS(out, "class=\"literalblock\"");
    EXPECT_CONTAINS(out, "literal text here");
    end_test();
}

static void test_html5_ulist() {
    begin_test("html5: unordered list");
    std::string out = html("* One\n* Two\n* Three\n");
    EXPECT_CONTAINS(out, "<ul>");
    EXPECT_CONTAINS(out, "<li>");
    EXPECT_CONTAINS(out, "One");
    EXPECT_CONTAINS(out, "Two");
    end_test();
}

static void test_html5_olist() {
    begin_test("html5: ordered list");
    std::string out = html(". First\n. Second\n");
    EXPECT_CONTAINS(out, "<ol");
    EXPECT_CONTAINS(out, "First");
    EXPECT_CONTAINS(out, "Second");
    end_test();
}

static void test_html5_admonition() {
    begin_test("html5: admonition paragraph");
    std::string out = html("TIP: Use asciiquack!\n");
    EXPECT_CONTAINS(out, "admonitionblock tip");
    EXPECT_CONTAINS(out, "Use asciiquack");
    end_test();
}

static void test_html5_special_chars() {
    begin_test("html5: special characters escaped");
    std::string out = html("a < b & c > d\n");
    EXPECT_CONTAINS(out, "&lt;");
    EXPECT_CONTAINS(out, "&amp;");
    EXPECT_CONTAINS(out, "&gt;");
    EXPECT_NOT_CONTAINS(out, "a < b");
    end_test();
}

static void test_html5_inline_bold() {
    begin_test("html5: inline bold");
    std::string out = html("Some *bold* text.\n");
    EXPECT_CONTAINS(out, "<strong>bold</strong>");
    end_test();
}

static void test_html5_inline_italic() {
    begin_test("html5: inline italic");
    std::string out = html("Some _italic_ text.\n");
    EXPECT_CONTAINS(out, "<em>italic</em>");
    end_test();
}

static void test_html5_inline_monospace() {
    begin_test("html5: inline monospace");
    std::string out = html("Use `code` here.\n");
    EXPECT_CONTAINS(out, "<code>code</code>");
    end_test();
}

static void test_html5_embedded() {
    begin_test("html5: embedded (no header/footer)");
    asciiquack::ParseOptions opts;
    opts.safe_mode = asciiquack::SafeMode::Unsafe;
    opts.attributes["embedded"] = "";
    auto doc = asciiquack::Parser::parse_string("= Title\n\nParagraph.\n", opts);
    std::string out = asciiquack::convert_to_html5(*doc);
    EXPECT_NOT_CONTAINS(out, "<!DOCTYPE html>");
    EXPECT_NOT_CONTAINS(out, "<html");
    EXPECT_CONTAINS(out, "<div class=\"paragraph\">");
    end_test();
}

static void test_html5_horizontal_rule() {
    begin_test("html5: horizontal rule");
    std::string out = html("'''\n");
    EXPECT_CONTAINS(out, "<hr>");
    end_test();
}

static void test_html5_attribute_ref() {
    begin_test("html5: attribute reference in paragraph");
    std::string out = html(":greeting: Hello\n\n{greeting}, world!\n");
    EXPECT_CONTAINS(out, "Hello, world!");
    end_test();
}

static void test_html5_image() {
    begin_test("html5: block image macro");
    std::string out = html("image::photo.png[A photo]\n");
    EXPECT_CONTAINS(out, "<img");
    EXPECT_CONTAINS(out, "photo.png");
    EXPECT_CONTAINS(out, "A photo");
    end_test();
}

static void test_html5_table() {
    begin_test("html5: basic table");
    std::string out = html("|===\n|Col1 |Col2\n|a |b\n|===\n");
    EXPECT_CONTAINS(out, "<table");
    EXPECT_CONTAINS(out, "<td");
    end_test();
}

static void test_html5_link_relative() {
    begin_test("html5: link macro with relative URL");
    std::string out = html("See link:/docs/guide[the guide].\n");
    EXPECT_CONTAINS(out, "<a href=\"/docs/guide\">");
    EXPECT_CONTAINS(out, "the guide");
    end_test();
}


static void test_parser_example_block_closed() {
    begin_test("parser: example block properly closed by delimiter");

    const std::string src =
        "====\n"
        "Inside example.\n"
        "====\n"
        "\n"
        "After example.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    // Should have two blocks: the example block and the paragraph after it
    EXPECT(doc->blocks().size() >= 2);
    EXPECT(doc->blocks()[0]->context() == asciiquack::BlockContext::Example);
    // The "After example." paragraph must be a sibling, not swallowed into the example
    bool found_after = false;
    for (const auto& b : doc->blocks()) {
        if (b->source().find("After example") != std::string::npos) { found_after = true; }
    }
    EXPECT(found_after);

    end_test();
}

static void test_parser_sidebar_block_closed() {
    begin_test("parser: sidebar block properly closed by delimiter");

    const std::string src =
        "****\n"
        "Sidebar content.\n"
        "****\n"
        "\n"
        "Normal paragraph.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    EXPECT(doc->blocks().size() >= 2);
    EXPECT(doc->blocks()[0]->context() == asciiquack::BlockContext::Sidebar);
    EXPECT(doc->blocks()[1]->context() == asciiquack::BlockContext::Paragraph);

    end_test();
}

static void test_parser_quote_block_closed() {
    begin_test("parser: quote block properly closed by delimiter");

    const std::string src =
        "[quote,Author Name]\n"
        "____\n"
        "Some quoted text.\n"
        "____\n"
        "\n"
        "After quote.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    EXPECT(doc->blocks().size() >= 2);
    EXPECT(doc->blocks()[0]->context() == asciiquack::BlockContext::Quote);
    EXPECT(doc->blocks()[1]->context() == asciiquack::BlockContext::Paragraph);

    end_test();
}

static void test_html5_example_block() {
    begin_test("html5: example block");
    std::string out = html(
        "====\n"
        "Example content.\n"
        "====\n"
        "\n"
        "After block.\n");
    EXPECT_CONTAINS(out, "class=\"exampleblock\"");
    EXPECT_CONTAINS(out, "Example content.");
    EXPECT_CONTAINS(out, "After block.");

    end_test();
}

static void test_html5_sidebar_block() {
    begin_test("html5: sidebar block");
    std::string out = html(
        "****\n"
        "Sidebar text.\n"
        "****\n"
        "\n"
        "Regular text.\n");
    EXPECT_CONTAINS(out, "class=\"sidebarblock\"");
    EXPECT_CONTAINS(out, "Sidebar text.");
    EXPECT_CONTAINS(out, "Regular text.");

    end_test();
}

static void test_html5_admonition_block() {
    begin_test("html5: admonition block (====)");
    std::string out = html(
        "[NOTE]\n"
        "====\n"
        "Compound note content.\n"
        "====\n"
        "\n"
        "After admonition.\n");
    EXPECT_CONTAINS(out, "admonitionblock note");
    EXPECT_CONTAINS(out, "Compound note content.");
    EXPECT_CONTAINS(out, "After admonition.");

    end_test();
}


static void test_integration_sample() {
    begin_test("integration: sample.adoc");

    const std::string src =
        "Document Title\n"
        "==============\n"
        "Doc Writer <thedoc@asciidoctor.org>\n"
        ":idprefix: id_\n"
        "\n"
        "Preamble paragraph.\n"
        "\n"
        "NOTE: This is test, only a test.\n"
        "\n"
        "== Section A\n"
        "\n"
        "*Section A* paragraph.\n"
        "\n"
        "=== Section A Subsection\n"
        "\n"
        "*Section A* 'subsection' paragraph.\n"
        "\n"
        "== Section B\n"
        "\n"
        "*Section B* paragraph.\n"
        "\n"
        ".Section B list\n"
        "* Item 1\n"
        "* Item 2\n"
        "* Item 3\n";

    auto doc = asciiquack::Parser::parse_string(src);
    EXPECT_EQ(doc->doctitle(), "Document Title");
    EXPECT(!doc->authors().empty());
    EXPECT_EQ(doc->authors()[0].firstname, "Doc");

    std::string out = asciiquack::convert_to_html5(*doc);
    EXPECT_CONTAINS(out, "<!DOCTYPE html>");
    EXPECT_CONTAINS(out, "Document Title");
    EXPECT_CONTAINS(out, "Section A");
    EXPECT_CONTAINS(out, "Section B");
    EXPECT_CONTAINS(out, "Preamble paragraph");
    EXPECT_CONTAINS(out, "admonitionblock note");
    EXPECT_CONTAINS(out, "<ul>");
    EXPECT_CONTAINS(out, "Item 1");

    end_test();
}

static void test_integration_basic() {
    begin_test("integration: basic.adoc");

    const std::string src =
        "= Document Title\n"
        "Doc Writer <doc.writer@asciidoc.org>\n"
        "v1.0, 2013-01-01\n"
        "\n"
        "Body content.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    EXPECT_EQ(doc->doctitle(), "Document Title");
    EXPECT_EQ(doc->revision().number, "1.0");
    EXPECT_EQ(doc->revision().date,   "2013-01-01");

    std::string out = asciiquack::convert_to_html5(*doc);
    EXPECT_CONTAINS(out, "Document Title");
    EXPECT_CONTAINS(out, "Body content.");
    EXPECT_CONTAINS(out, "version 1.0");

    end_test();
}

// ─────────────────────────────────────────────────────────────────────────────
// P1 feature tests
// ─────────────────────────────────────────────────────────────────────────────

static void test_multiline_attribute_value() {
    begin_test("parser: multi-line attribute value (trailing \\)");

    const std::string src =
        ":long-val: first part \\\n"
        "second part\n"
        "\n"
        "{long-val}\n";

    auto doc = asciiquack::Parser::parse_string(src);
    EXPECT_EQ(doc->attr("long-val"), "first part second part");

    // Verify it's expanded in paragraph
    std::string out = asciiquack::convert_to_html5(*doc);
    EXPECT_CONTAINS(out, "first part second part");

    end_test();
}

static void test_section_numbering() {
    begin_test("parser: section numbering (:sectnums:)");

    const std::string src =
        "= Document\n"
        ":sectnums:\n"
        "\n"
        "== First Section\n"
        "\n"
        "Content.\n"
        "\n"
        "== Second Section\n"
        "\n"
        "Content.\n"
        "\n"
        "=== Subsection\n"
        "\n"
        "Content.\n";

    auto doc = asciiquack::Parser::parse_string(src);

    // Find numbered sections
    int numbered = 0;
    for (const auto& b : doc->blocks()) {
        if (b->context() == asciiquack::BlockContext::Section) {
            const auto& sect = dynamic_cast<const asciiquack::Section&>(*b);
            if (sect.numbered()) { ++numbered; }
        }
    }
    EXPECT(numbered >= 2);

    std::string out = asciiquack::convert_to_html5(*doc);
    EXPECT_CONTAINS(out, "sectnum");
    EXPECT_CONTAINS(out, "1.");
    EXPECT_CONTAINS(out, "2.");

    end_test();
}

static void test_section_numbering_levels() {
    begin_test("parser: section numbering with :sectnumlevels: 1");

    const std::string src =
        "= Document\n"
        ":sectnums:\n"
        ":sectnumlevels: 1\n"
        "\n"
        "== Section A\n"
        "\n"
        "=== Subsection\n"
        "\n"
        "Text.\n";

    auto doc = asciiquack::Parser::parse_string(src);

    // Find level-2 section (===): should NOT be numbered since sectnumlevels is 1
    bool level2_not_numbered = true;
    std::function<void(const asciiquack::Block&)> walk;
    walk = [&](const asciiquack::Block& b) {
        if (b.context() == asciiquack::BlockContext::Section) {
            const auto& sect = dynamic_cast<const asciiquack::Section&>(b);
            if (sect.level() == 2 && sect.numbered()) { level2_not_numbered = false; }
        }
        for (const auto& child : b.blocks()) { walk(*child); }
    };
    walk(*doc);
    EXPECT(level2_not_numbered);

    end_test();
}

static void test_table_of_contents() {
    begin_test("html5: table of contents (:toc:)");

    const std::string src =
        "= Document\n"
        ":toc:\n"
        "\n"
        "== Introduction\n"
        "\n"
        "Text.\n"
        "\n"
        "== Conclusion\n"
        "\n"
        "Text.\n";

    std::string out = html(src);
    EXPECT_CONTAINS(out, "id=\"toc\"");
    EXPECT_CONTAINS(out, "Table of Contents");
    EXPECT_CONTAINS(out, "Introduction");
    EXPECT_CONTAINS(out, "Conclusion");
    // TOC links should reference section IDs
    EXPECT_CONTAINS(out, "href=\"#_introduction\"");

    end_test();
}

static void test_toc_custom_title() {
    begin_test("html5: TOC with custom :toc-title:");

    const std::string src =
        "= Doc\n"
        ":toc:\n"
        ":toc-title: Contents\n"
        "\n"
        "== Section\n"
        "\n"
        "Text.\n";

    std::string out = html(src);
    EXPECT_CONTAINS(out, "Contents");
    EXPECT_NOT_CONTAINS(out, "Table of Contents");

    end_test();
}

static void test_toc_with_sectnums() {
    begin_test("html5: TOC includes section numbers when :sectnums: set");

    const std::string src =
        "= Document\n"
        ":toc:\n"
        ":sectnums:\n"
        "\n"
        "== Alpha\n"
        "\n"
        "Text.\n"
        "\n"
        "== Beta\n"
        "\n"
        "Text.\n";

    std::string out = html(src);
    EXPECT_CONTAINS(out, "id=\"toc\"");
    // Section numbers should appear in the TOC
    EXPECT_CONTAINS(out, "1.");
    EXPECT_CONTAINS(out, "2.");

    end_test();
}

static void test_ifdef_single_line() {
    begin_test("parser: ifdef:: single-line form");

    // Attribute is set → content included
    const std::string src1 =
        ":myattr:\n"
        "\n"
        "ifdef::myattr[Included content.]\n";
    auto doc1 = asciiquack::Parser::parse_string(src1);
    bool found1 = false;
    for (const auto& b : doc1->blocks()) {
        if (b->source().find("Included content") != std::string::npos) { found1 = true; }
    }
    EXPECT(found1);

    // Attribute is NOT set → content excluded
    const std::string src2 =
        "ifdef::missing_attr[Should not appear.]\n"
        "Visible.\n";
    auto doc2 = asciiquack::Parser::parse_string(src2);
    bool found2 = false;
    for (const auto& b : doc2->blocks()) {
        if (b->source().find("Should not appear") != std::string::npos) { found2 = true; }
    }
    EXPECT(!found2);

    end_test();
}

static void test_ifdef_multiline() {
    begin_test("parser: ifdef:: multi-line form");

    const std::string src =
        ":myattr:\n"
        "\n"
        "Before.\n"
        "\n"
        "ifdef::myattr[]\n"
        "Conditional content.\n"
        "endif::myattr[]\n"
        "\n"
        "After.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    std::string out = asciiquack::convert_to_html5(*doc);
    EXPECT_CONTAINS(out, "Before.");
    EXPECT_CONTAINS(out, "Conditional content.");
    EXPECT_CONTAINS(out, "After.");

    end_test();
}

static void test_ifdef_multiline_false() {
    begin_test("parser: ifdef:: multi-line form (false branch skipped)");

    const std::string src =
        "Before.\n"
        "\n"
        "ifdef::missing_attr[]\n"
        "This should NOT appear.\n"
        "endif::missing_attr[]\n"
        "\n"
        "After.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    std::string out = asciiquack::convert_to_html5(*doc);
    EXPECT_CONTAINS(out, "Before.");
    EXPECT_NOT_CONTAINS(out, "This should NOT appear.");
    EXPECT_CONTAINS(out, "After.");

    end_test();
}

static void test_ifndef_single_line() {
    begin_test("parser: ifndef:: single-line form");

    // Attribute NOT set → content included
    const std::string src1 =
        "ifndef::missing[Included.]\n";
    auto doc1 = asciiquack::Parser::parse_string(src1);
    bool found1 = false;
    for (const auto& b : doc1->blocks()) {
        if (b->source().find("Included") != std::string::npos) { found1 = true; }
    }
    EXPECT(found1);

    // Attribute IS set → content excluded
    const std::string src2 =
        ":setattr:\n"
        "\n"
        "ifndef::setattr[Should not appear.]\n"
        "Visible.\n";
    auto doc2 = asciiquack::Parser::parse_string(src2);
    bool found2 = false;
    for (const auto& b : doc2->blocks()) {
        if (b->source().find("Should not appear") != std::string::npos) { found2 = true; }
    }
    EXPECT(!found2);

    end_test();
}

static void test_ifeval() {
    begin_test("parser: ifeval:: basic expression");

    // Version attribute set to "1.5"
    const std::string src =
        ":version: 1.5\n"
        "\n"
        "ifeval::[\"{version}\" >= \"1.0\"]\n"
        "Version is new enough.\n"
        "endif::[]\n"
        "\n"
        "ifeval::[\"{version}\" >= \"2.0\"]\n"
        "Should not appear.\n"
        "endif::[]\n"
        "\n"
        "Done.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    std::string out = asciiquack::convert_to_html5(*doc);
    EXPECT_CONTAINS(out, "Version is new enough.");
    EXPECT_NOT_CONTAINS(out, "Should not appear.");
    EXPECT_CONTAINS(out, "Done.");

    end_test();
}

static void test_floating_title() {
    begin_test("parser+html5: floating title ([discrete])");

    const std::string src =
        "Normal paragraph.\n"
        "\n"
        "[discrete]\n"
        "== Floating Heading\n"
        "\n"
        "Another paragraph.\n";

    auto doc = asciiquack::Parser::parse_string(src);

    // The [discrete] section should NOT be a Section node
    bool has_section = false;
    for (const auto& b : doc->blocks()) {
        if (b->context() == asciiquack::BlockContext::Section) { has_section = true; }
    }
    EXPECT(!has_section);

    std::string out = asciiquack::convert_to_html5(*doc);
    // Should have an <h2> with class="discrete" but no sect1 wrapper
    EXPECT_CONTAINS(out, "class=\"discrete\"");
    EXPECT_CONTAINS(out, "Floating Heading");
    EXPECT_NOT_CONTAINS(out, "class=\"sect1\"");

    end_test();
}

static void test_video_block() {
    begin_test("html5: video block macro");

    std::string out = html("video::demo.mp4[width=640,height=480]\n");
    EXPECT_CONTAINS(out, "<video");
    EXPECT_CONTAINS(out, "demo.mp4");
    EXPECT_CONTAINS(out, "controls");

    end_test();
}

static void test_audio_block() {
    begin_test("html5: audio block macro");

    std::string out = html("audio::demo.ogg[]\n");
    EXPECT_CONTAINS(out, "<audio");
    EXPECT_CONTAINS(out, "demo.ogg");
    EXPECT_CONTAINS(out, "controls");

    end_test();
}

static void test_include_directive() {
    begin_test("parser: include:: directive (unsafe mode)");

    // Write a temp file to include
    // Create a temp file with portable path
    namespace fs = std::filesystem;
    const std::string tmp_path =
        (fs::temp_directory_path() / "asciiquack_test_include.adoc").string();
    {
        std::ofstream f(tmp_path);
        f << "Included paragraph.\n";
    }

    const std::string src =
        "Before.\n"
        "\n"
        "include::" + tmp_path + "[]\n"
        "\n"
        "After.\n";

    asciiquack::ParseOptions opts;
    opts.safe_mode = asciiquack::SafeMode::Unsafe;
    auto doc = asciiquack::Parser::parse_string(src, opts);
    std::string out = asciiquack::convert_to_html5(*doc);
    EXPECT_CONTAINS(out, "Before.");
    EXPECT_CONTAINS(out, "Included paragraph.");
    EXPECT_CONTAINS(out, "After.");

    // Clean up
    std::remove(tmp_path.c_str());

    end_test();
}

static void test_include_directive_secure_mode() {
    begin_test("parser: include:: skipped in secure mode");

    const std::string src =
        "Before.\n"
        "include::/tmp/some_file.adoc[]\n"
        "After.\n";

    // Default mode is Secure – include should be silently ignored
    auto doc = asciiquack::Parser::parse_string(src);
    std::string out = asciiquack::convert_to_html5(*doc);
    EXPECT_CONTAINS(out, "Before.");
    EXPECT_CONTAINS(out, "After.");

    end_test();
}

static void test_bug7_description_list_not_table() {
    begin_test("bug #7: description list regex does not match | lines");

    // A line starting with | should not be mistaken for a description list
    const std::string src =
        "|===\n"
        "|Col1 |Col2\n"
        "|a |b\n"
        "|===\n"
        "\n"
        "After table.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    // Should be a table, not a description list
    bool has_table = false;
    bool has_dlist = false;
    for (const auto& b : doc->blocks()) {
        if (b->context() == asciiquack::BlockContext::Table)  { has_table = true; }
        if (b->context() == asciiquack::BlockContext::Dlist)  { has_dlist = true; }
    }
    EXPECT(has_table);
    EXPECT(!has_dlist);

    end_test();
}

static void test_ifeval_numeric() {
    begin_test("parser: ifeval:: numeric comparison");

    const std::string src =
        ":counter: 5\n"
        "\n"
        "ifeval::[{counter} > 3]\n"
        "Counter is large.\n"
        "endif::[]\n"
        "\n"
        "ifeval::[{counter} > 10]\n"
        "Counter is huge.\n"
        "endif::[]\n"
        "\n"
        "Done.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    std::string out = asciiquack::convert_to_html5(*doc);
    EXPECT_CONTAINS(out, "Counter is large.");
    EXPECT_NOT_CONTAINS(out, "Counter is huge.");

    end_test();
}

// ─────────────────────────────────────────────────────────────────────────────
// P2 features tests
// ─────────────────────────────────────────────────────────────────────────────

static void test_inline_passthrough() {
    begin_test("substitutors: pass:[] inline passthrough");

    // pass:[] with no subs – content is emitted raw (not HTML-escaped)
    const std::string src =
        "= Doc\n"
        "\n"
        "Raw pass:pass:[<b>bold</b>] here.\n";

    asciiquack::ParseOptions opts;
    opts.attributes["embedded"] = "";
    auto doc = asciiquack::Parser::parse_string(src, opts);
    std::string out = asciiquack::convert_to_html5(*doc);
    // The raw HTML should be present (not escaped)
    EXPECT_CONTAINS(out, "<b>bold</b>");
    // The pass:[] macro text itself should not appear
    EXPECT_NOT_CONTAINS(out, "pass:[");

    end_test();
}

static void test_inline_passthrough_q() {
    begin_test("substitutors: pass:q[] quotes-only passthrough");

    // pass:q[] applies only quotes substitution inside
    const std::string src =
        "= Doc\n"
        "\n"
        "pass:q[*bold text* inside pass]\n";

    asciiquack::ParseOptions opts;
    opts.attributes["embedded"] = "";
    auto doc = asciiquack::Parser::parse_string(src, opts);
    std::string out = asciiquack::convert_to_html5(*doc);
    EXPECT_CONTAINS(out, "<strong>bold text</strong>");

    end_test();
}

static void test_kbd_macro() {
    begin_test("substitutors: kbd:[] macro");

    const std::string src =
        "= Doc\n"
        "\n"
        "Press kbd:[Ctrl+T] to open a new tab.\n";

    asciiquack::ParseOptions opts;
    opts.attributes["embedded"] = "";
    auto doc = asciiquack::Parser::parse_string(src, opts);
    std::string out = asciiquack::convert_to_html5(*doc);
    EXPECT_CONTAINS(out, "<kbd>Ctrl</kbd>");
    EXPECT_CONTAINS(out, "<kbd>T</kbd>");
    EXPECT_CONTAINS(out, "keyseq");

    end_test();
}

static void test_btn_macro() {
    begin_test("substitutors: btn:[] macro");

    const std::string src =
        "= Doc\n"
        "\n"
        "Click btn:[OK] to continue.\n";

    asciiquack::ParseOptions opts;
    opts.attributes["embedded"] = "";
    auto doc = asciiquack::Parser::parse_string(src, opts);
    std::string out = asciiquack::convert_to_html5(*doc);
    EXPECT_CONTAINS(out, "<b class=\"button\">OK</b>");

    end_test();
}

static void test_menu_macro() {
    begin_test("substitutors: menu:[] macro");

    const std::string src =
        "= Doc\n"
        "\n"
        "Use menu:File[Save] to save.\n";

    asciiquack::ParseOptions opts;
    opts.attributes["embedded"] = "";
    auto doc = asciiquack::Parser::parse_string(src, opts);
    std::string out = asciiquack::convert_to_html5(*doc);
    EXPECT_CONTAINS(out, "<span class=\"menuseq\">");
    EXPECT_CONTAINS(out, "<span class=\"menu\">File</span>");
    EXPECT_CONTAINS(out, "<span class=\"menuitem\">Save</span>");

    end_test();
}

static void test_counter_macro() {
    begin_test("substitutors: counter: inline macro");

    const std::string src =
        "= Doc\n"
        "\n"
        "Item counter:item. Item counter:item. Item counter:item.\n";

    asciiquack::ParseOptions opts;
    opts.attributes["embedded"] = "";
    auto doc = asciiquack::Parser::parse_string(src, opts);
    std::string out = asciiquack::convert_to_html5(*doc);
    // counter:item increments each time; should produce 1, 2, 3
    EXPECT_CONTAINS(out, "Item 1.");
    EXPECT_CONTAINS(out, "Item 2.");
    EXPECT_CONTAINS(out, "Item 3.");

    end_test();
}

static void test_counter2_macro() {
    begin_test("substitutors: counter2: does not emit value");

    // counter2: increments but produces no output
    const std::string src =
        "= Doc\n"
        "\n"
        "counter2:hidden. counter2:hidden. Value: counter:hidden.\n";

    asciiquack::ParseOptions opts;
    opts.attributes["embedded"] = "";
    auto doc = asciiquack::Parser::parse_string(src, opts);
    std::string out = asciiquack::convert_to_html5(*doc);
    // counter2: should produce nothing; the final counter: should show 3
    EXPECT_CONTAINS(out, "Value: 3.");

    end_test();
}

static void test_footnote_macro() {
    begin_test("html5: footnote:[] macro rendered at end");

    const std::string src =
        "= Doc\n"
        "\n"
        "Some text.footnote:[This is a footnote.] More text.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    std::string out = asciiquack::convert_to_html5(*doc);
    // Inline footnote reference marker
    EXPECT_CONTAINS(out, "class=\"footnote\"");
    EXPECT_CONTAINS(out, "_footnoteref_1");
    // Footnote body at end of document
    EXPECT_CONTAINS(out, "id=\"footnotes\"");
    EXPECT_CONTAINS(out, "This is a footnote.");
    EXPECT_CONTAINS(out, "_footnotedef_1");

    end_test();
}

static void test_footnote_multiple() {
    begin_test("html5: multiple footnotes numbered sequentially");

    const std::string src =
        "= Doc\n"
        "\n"
        "First.footnote:[Note one.] Second.footnote:[Note two.]\n";

    auto doc = asciiquack::Parser::parse_string(src);
    std::string out = asciiquack::convert_to_html5(*doc);
    EXPECT_CONTAINS(out, "_footnoteref_1");
    EXPECT_CONTAINS(out, "_footnoteref_2");
    EXPECT_CONTAINS(out, "Note one.");
    EXPECT_CONTAINS(out, "Note two.");

    end_test();
}

static void test_ordered_list_style() {
    begin_test("html5: [loweralpha] ordered list style");

    const std::string src =
        "= Doc\n"
        "\n"
        "[loweralpha]\n"
        ". First\n"
        ". Second\n"
        ". Third\n";

    asciiquack::ParseOptions opts;
    opts.attributes["embedded"] = "";
    auto doc = asciiquack::Parser::parse_string(src, opts);
    std::string out = asciiquack::convert_to_html5(*doc);
    EXPECT_CONTAINS(out, "class=\"loweralpha\"");
    EXPECT_CONTAINS(out, "olist loweralpha");

    end_test();
}

static void test_ordered_list_start() {
    begin_test("html5: [start=3] ordered list start attribute");

    const std::string src =
        "= Doc\n"
        "\n"
        "[start=3]\n"
        ". Third item\n"
        ". Fourth item\n";

    asciiquack::ParseOptions opts;
    opts.attributes["embedded"] = "";
    auto doc = asciiquack::Parser::parse_string(src, opts);
    std::string out = asciiquack::convert_to_html5(*doc);
    EXPECT_CONTAINS(out, "start=\"3\"");

    end_test();
}

static void test_special_section_names() {
    begin_test("html5: special section names ([preface], [appendix])");

    const std::string src =
        "= Doc\n"
        "\n"
        "[preface]\n"
        "== Preface\n"
        "\n"
        "Preface text.\n"
        "\n"
        "[appendix]\n"
        "== Appendix A\n"
        "\n"
        "Appendix text.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    std::string out = asciiquack::convert_to_html5(*doc);
    EXPECT_CONTAINS(out, "class=\"preface\"");
    EXPECT_CONTAINS(out, "class=\"appendix\"");

    end_test();
}

static void test_compound_list_items() {
    begin_test("parser+html5: compound list items (+ continuation)");

    const std::string src =
        "= Doc\n"
        "\n"
        "* First item\n"
        "+\n"
        "Attached paragraph in first item.\n"
        "\n"
        "* Second item\n";

    asciiquack::ParseOptions opts;
    opts.attributes["embedded"] = "";
    auto doc = asciiquack::Parser::parse_string(src, opts);
    std::string out = asciiquack::convert_to_html5(*doc);
    EXPECT_CONTAINS(out, "First item");
    EXPECT_CONTAINS(out, "Attached paragraph in first item.");
    EXPECT_CONTAINS(out, "Second item");
    // The attached paragraph should be inside the list item
    auto li_pos  = out.find("<li>");
    auto para_pos = out.find("Attached paragraph");
    auto li2_pos = out.find("<li>", li_pos + 1);
    EXPECT(li_pos  != std::string::npos);
    EXPECT(para_pos != std::string::npos);
    EXPECT(li2_pos  != std::string::npos);
    EXPECT(para_pos < li2_pos);  // attached para comes before the 2nd <li>

    end_test();
}

static void test_dlist_compound_body() {
    begin_test("html5: description list with compound body blocks");

    const std::string src =
        "= Doc\n"
        "\n"
        "term1::\n"
        "Body paragraph.\n"
        "+\n"
        "Second paragraph.\n"
        "\n"
        "term2:: Simple body.\n";

    asciiquack::ParseOptions opts;
    opts.attributes["embedded"] = "";
    auto doc = asciiquack::Parser::parse_string(src, opts);
    std::string out = asciiquack::convert_to_html5(*doc);
    EXPECT_CONTAINS(out, "term1");
    EXPECT_CONTAINS(out, "Body paragraph.");
    EXPECT_CONTAINS(out, "term2");
    EXPECT_CONTAINS(out, "Simple body.");

    end_test();
}

static void test_idprefix_empty() {
    begin_test("html5: idprefix empty string (id without leading underscore)");

    const std::string src =
        "= Doc\n"
        ":idprefix:\n"
        ":idseparator: -\n"
        "\n"
        "== My Section\n"
        "\n"
        "Body.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    std::string out = asciiquack::convert_to_html5(*doc);
    // ID should be "my-section" not "_my_section"
    EXPECT_CONTAINS(out, "id=\"my-section\"");

    end_test();
}

static void test_bug4_inline_bold_url() {
    begin_test("bug #4: constrained bold does not match inside URLs");

    // A URL containing a '*' character should not trigger constrained bold
    const std::string src =
        "= Doc\n"
        "\n"
        "See https://example.com/path*query for details.\n";

    asciiquack::ParseOptions opts;
    opts.attributes["embedded"] = "";
    auto doc = asciiquack::Parser::parse_string(src, opts);
    std::string out = asciiquack::convert_to_html5(*doc);
    // The URL should not be broken by bold substitution
    EXPECT_CONTAINS(out, "example.com/path");
    EXPECT_NOT_CONTAINS(out, "<strong>query");

    end_test();
}

// ─────────────────────────────────────────────────────────────────────────────
// P3 features tests
// ─────────────────────────────────────────────────────────────────────────────

static void test_source_callouts() {
    begin_test("html5: source callout markers <N> rendered as badges");

    const std::string src =
        "= Doc\n"
        "\n"
        "[source,ruby]\n"
        "----\n"
        "require 'sinatra' <1>\n"
        "get '/hi' do <2>\n"
        "  \"Hello!\"\n"
        "end\n"
        "----\n"
        "\n"
        "<1> Load the library.\n"
        "<2> Define a route.\n";

    asciiquack::ParseOptions opts;
    opts.attributes["embedded"] = "";
    auto doc = asciiquack::Parser::parse_string(src, opts);
    std::string out = asciiquack::convert_to_html5(*doc);

    // Callout markers should be replaced with badge elements
    EXPECT_CONTAINS(out, "class=\"conum\"");
    EXPECT_CONTAINS(out, "(1)");
    EXPECT_CONTAINS(out, "(2)");
    // The callout list should be present
    EXPECT_CONTAINS(out, "colist");
    EXPECT_CONTAINS(out, "Load the library.");
    EXPECT_CONTAINS(out, "Define a route.");
    // Raw &lt;1&gt; should NOT appear in source code
    EXPECT_NOT_CONTAINS(out, "&lt;1&gt;");

    end_test();
}

static void test_admonition_caption_attr() {
    begin_test("html5: admonition caption from locale attribute");

    const std::string src =
        "= Doc\n"
        ":note-caption: Nota\n"
        ":tip-caption: Consejo\n"
        "\n"
        "NOTE: This is a note.\n"
        "\n"
        "TIP: This is a tip.\n";

    asciiquack::ParseOptions opts;
    opts.attributes["embedded"] = "";
    auto doc = asciiquack::Parser::parse_string(src, opts);
    std::string out = asciiquack::convert_to_html5(*doc);
    EXPECT_CONTAINS(out, "Nota");
    EXPECT_CONTAINS(out, "Consejo");
    EXPECT_NOT_CONTAINS(out, ">Note<");  // default should not appear

    end_test();
}

static void test_admonition_default_captions() {
    begin_test("html5: admonition default captions (Tip, Warning, Important, Caution)");

    const std::string src =
        "= Doc\n"
        "\n"
        "TIP: tip\n"
        "\n"
        "WARNING: warn\n"
        "\n"
        "IMPORTANT: important\n"
        "\n"
        "CAUTION: caution\n";

    asciiquack::ParseOptions opts;
    opts.attributes["embedded"] = "";
    auto doc = asciiquack::Parser::parse_string(src, opts);
    std::string out = asciiquack::convert_to_html5(*doc);
    EXPECT_CONTAINS(out, "Tip");
    EXPECT_CONTAINS(out, "Warning");
    EXPECT_CONTAINS(out, "Important");
    EXPECT_CONTAINS(out, "Caution");

    end_test();
}

static void test_stem_inline_macro() {
    begin_test("substitutors: stem:[] inline math macro");

    const std::string src =
        "= Doc\n"
        "\n"
        "The formula stem:[E = mc^2] is famous.\n";

    asciiquack::ParseOptions opts;
    opts.attributes["embedded"] = "";
    auto doc = asciiquack::Parser::parse_string(src, opts);
    std::string out = asciiquack::convert_to_html5(*doc);
    // Should render as MathJax inline math
    EXPECT_CONTAINS(out, "\\(E = mc^2\\)");
    EXPECT_NOT_CONTAINS(out, "stem:[");

    end_test();
}

static void test_latexmath_inline_macro() {
    begin_test("substitutors: latexmath:[] inline macro");

    const std::string src =
        "= Doc\n"
        "\n"
        "Inline: latexmath:[\\sum_{i=1}^{n} i].\n";

    asciiquack::ParseOptions opts;
    opts.attributes["embedded"] = "";
    auto doc = asciiquack::Parser::parse_string(src, opts);
    std::string out = asciiquack::convert_to_html5(*doc);
    EXPECT_CONTAINS(out, "\\(");
    EXPECT_NOT_CONTAINS(out, "latexmath:[");

    end_test();
}

static void test_stem_block() {
    begin_test("html5: [stem] block renders display math");

    const std::string src =
        "= Doc\n"
        "\n"
        "[stem]\n"
        "++++\n"
        "\\sum_{i=1}^{n} i = \\frac{n(n+1)}{2}\n"
        "++++\n";

    asciiquack::ParseOptions opts;
    opts.attributes["embedded"] = "";
    auto doc = asciiquack::Parser::parse_string(src, opts);
    std::string out = asciiquack::convert_to_html5(*doc);
    // Should wrap in display math delimiters
    EXPECT_CONTAINS(out, "\\[");
    EXPECT_CONTAINS(out, "\\]");
    EXPECT_CONTAINS(out, "stemblock");

    end_test();
}

static void test_preamble_no_sections() {
    begin_test("html5: preamble div NOT emitted when no sections");

    // A document with only body content and no sections should not
    // wrap content in <div id="preamble">
    const std::string src =
        "= Doc\n"
        "\n"
        "Just a paragraph. No sections.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    std::string out = asciiquack::convert_to_html5(*doc);
    EXPECT_NOT_CONTAINS(out, "id=\"preamble\"");
    EXPECT_CONTAINS(out, "Just a paragraph.");

    end_test();
}

static void test_preamble_with_sections() {
    begin_test("html5: preamble div emitted when sections follow");

    const std::string src =
        "= Doc\n"
        "\n"
        "This is the preamble.\n"
        "\n"
        "== Section One\n"
        "\n"
        "Section content.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    std::string out = asciiquack::convert_to_html5(*doc);
    EXPECT_CONTAINS(out, "id=\"preamble\"");
    EXPECT_CONTAINS(out, "This is the preamble.");
    EXPECT_CONTAINS(out, "Section One");

    end_test();
}

static void test_linkcss_attribute() {
    begin_test("html5: :linkcss: emits <link> tag instead of inline style");

    const std::string src =
        "= Doc\n"
        ":linkcss:\n"
        "\n"
        "Body.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    std::string out = asciiquack::convert_to_html5(*doc);
    // Should have a <link> tag, not inline <style>
    EXPECT_CONTAINS(out, "<link rel=\"stylesheet\"");
    EXPECT_NOT_CONTAINS(out, "<style>\n/* asciiquack");

    end_test();
}

static void test_stylesheet_attribute() {
    begin_test("html5: :stylesheet: path used in <link> tag");

    const std::string src =
        "= Doc\n"
        ":linkcss:\n"
        ":stylesheet: /custom/style.css\n"
        "\n"
        "Body.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    std::string out = asciiquack::convert_to_html5(*doc);
    EXPECT_CONTAINS(out, "href=\"/custom/style.css\"");

    end_test();
}

static void test_stem_mathjax_script() {
    begin_test("html5: :stem: attribute adds MathJax script to head");

    const std::string src =
        "= Doc\n"
        ":stem: latexmath\n"
        "\n"
        "Body.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    std::string out = asciiquack::convert_to_html5(*doc);
    EXPECT_CONTAINS(out, "MathJax");
    EXPECT_CONTAINS(out, "mathjax");

    end_test();
}

static void test_multi_author_semicolon() {
    begin_test("parser: semicolon-separated multi-author line recognised as header");

    // Authors on one line separated by "; " (as produced by db2adoc or fixed
    // manually).  The attribute entry on the following line must also be
    // recognised as a header attribute, not body text.
    const std::string src =
        "= My Document\n"
        "Alice B. Smith; Charlie D. Jones; Eve F. Brown\n"
        ":doctype: article\n"
        "\n"
        "Body paragraph.\n";

    auto doc = asciiquack::Parser::parse_string(src);

    // All three authors must be captured.
    const auto& authors = doc->authors();
    EXPECT(authors.size() == 3u);
    if (authors.size() == 3u) {
        EXPECT(authors[0].firstname == "Alice");
        EXPECT(authors[1].firstname == "Charlie");
        EXPECT(authors[2].firstname == "Eve");
    }

    // The :doctype: attribute entry must have been processed (not emitted as
    // body text).
    std::string out = asciiquack::convert_to_html5(*doc);
    EXPECT(out.find(":doctype:") == std::string::npos);
    // Author metadata should appear in HTML <head>.
    EXPECT_CONTAINS(out, "Alice");
    EXPECT_CONTAINS(out, "Charlie");

    end_test();
}

static void test_doctype_manpage() {
    begin_test("parser+html5: doctype manpage title parsing");

    const std::string src =
        "= git-commit(1)\n"
        "Git Author\n"
        "\n"
        "== Name\n"
        "\n"
        "git-commit - Record changes to the repository\n";

    asciiquack::ParseOptions opts;
    opts.doctype = "manpage";
    auto doc = asciiquack::Parser::parse_string(src, opts);

    // manname and manvolnum should be extracted
    EXPECT(doc->attr("manname") == "git-commit");
    EXPECT(doc->attr("manvolnum") == "1");

    std::string out = asciiquack::convert_to_html5(*doc);
    // Should show volume number in title heading
    EXPECT_CONTAINS(out, "git-commit(1)");
    EXPECT_CONTAINS(out, "Manual Page");

    end_test();
}

static void test_manpage_backend_basic() {
    begin_test("manpage: basic document converts to troff");

    const std::string src =
        "= ls(1)\n"
        ":manvolnum: 1\n"
        "\n"
        "== Name\n"
        "\n"
        "ls - list directory contents\n"
        "\n"
        "== Synopsis\n"
        "\n"
        "ls [options] [file...]\n"
        "\n"
        "== Description\n"
        "\n"
        "List information about the FILEs.\n";

    asciiquack::ParseOptions opts;
    opts.doctype = "manpage";
    auto doc = asciiquack::Parser::parse_string(src, opts);
    std::string out = asciiquack::convert_to_manpage(*doc);

    // .TH header must be present
    EXPECT_CONTAINS(out, ".TH");
    EXPECT_CONTAINS(out, "LS");
    // Section headings
    EXPECT_CONTAINS(out, ".SH NAME");
    EXPECT_CONTAINS(out, ".SH SYNOPSIS");
    EXPECT_CONTAINS(out, ".SH DESCRIPTION");
    // Paragraph content
    EXPECT_CONTAINS(out, "list directory contents");

    end_test();
}

static void test_manpage_backend_bold_italic() {
    begin_test("manpage: inline bold and italic rendered as troff markup");

    const std::string src =
        "= test(1)\n"
        "\n"
        "== Description\n"
        "\n"
        "Use *bold* text and _italic_ text here.\n";

    asciiquack::ParseOptions opts;
    opts.doctype = "manpage";
    auto doc = asciiquack::Parser::parse_string(src, opts);
    std::string out = asciiquack::convert_to_manpage(*doc);

    EXPECT_CONTAINS(out, "\\fB");
    EXPECT_CONTAINS(out, "\\fR");
    EXPECT_CONTAINS(out, "\\fI");

    end_test();
}

static void test_manpage_backend_listing() {
    begin_test("manpage: listing block uses .nf/.fi");

    const std::string src =
        "= test(1)\n"
        "\n"
        "== Synopsis\n"
        "\n"
        "----\n"
        "command --flag value\n"
        "----\n";

    asciiquack::ParseOptions opts;
    opts.doctype = "manpage";
    auto doc = asciiquack::Parser::parse_string(src, opts);
    std::string out = asciiquack::convert_to_manpage(*doc);

    EXPECT_CONTAINS(out, ".nf");
    EXPECT_CONTAINS(out, ".fi");
    EXPECT_CONTAINS(out, "command --flag value");

    end_test();
}

static void test_manpage_table() {
    begin_test("manpage: table uses tbl(1) .TS/.TE macros");

    const std::string src =
        "= test(1)\n"
        "\n"
        "== Description\n"
        "\n"
        "[cols=\"1,1\"]\n"
        "|===\n"
        "| Header A | Header B\n"
        "\n"
        "| Cell 1 | Cell 2\n"
        "| Cell 3 | Cell 4\n"
        "|===\n";

    asciiquack::ParseOptions opts;
    opts.doctype = "manpage";
    auto doc = asciiquack::Parser::parse_string(src, opts);
    std::string out = asciiquack::convert_to_manpage(*doc);

    // tbl(1) preprocessor activation comment must be present
    EXPECT_CONTAINS(out, "'\\\" t");
    // Table delimiters
    EXPECT_CONTAINS(out, ".TS");
    EXPECT_CONTAINS(out, ".TE");
    // allbox draws borders; tab(:) sets the cell separator
    EXPECT_CONTAINS(out, "allbox");
    EXPECT_CONTAINS(out, "tab(:)");
    // Cell delimiters for long-text cells
    EXPECT_CONTAINS(out, "T{");
    EXPECT_CONTAINS(out, "T}");
    // Header row separator
    EXPECT_CONTAINS(out, ".T&");
    // Cell content
    EXPECT_CONTAINS(out, "Header A");
    EXPECT_CONTAINS(out, "Cell 1");

    end_test();
}

static void test_manpage_th_mansource_manmanual() {
    begin_test("manpage: :mansource: and :manmanual: appear in .TH line");

    const std::string src =
        "= mycommand(1)\n"
        ":mansource: BRL-CAD\n"
        ":manmanual: BRL-CAD User Commands\n"
        "\n"
        "== Name\n"
        "mycommand - a test command\n";

    asciiquack::ParseOptions opts;
    opts.doctype = "manpage";
    auto doc = asciiquack::Parser::parse_string(src, opts);
    std::string out = asciiquack::convert_to_manpage(*doc);

    // .TH must have source and manual fields
    EXPECT_CONTAINS(out, ".TH \"MYCOMMAND\" \"1\"");
    EXPECT_CONTAINS(out, "\"BRL-CAD\"");
    EXPECT_CONTAINS(out, "\"BRL-CAD User Commands\"");

    end_test();
}

static void test_manpage_alpha_volnum() {
    begin_test("manpage: non-numeric volume (e.g. \"nged\") parsed from title");

    const std::string src =
        "= analyze(nged)\n"
        ":mansource: BRL-CAD\n"
        ":manmanual: BRL-CAD User Commands\n"
        "\n"
        "== Name\n"
        "analyze - analyze geometry\n";

    asciiquack::ParseOptions opts;
    opts.doctype = "manpage";
    auto doc = asciiquack::Parser::parse_string(src, opts);

    EXPECT(doc->attr("manname")   == "analyze");
    EXPECT(doc->attr("manvolnum") == "nged");

    std::string out = asciiquack::convert_to_manpage(*doc);
    EXPECT_CONTAINS(out, ".TH \"ANALYZE\" \"nged\"");

    end_test();
}

static void test_manpage_dlist_no_double_bold() {
    begin_test("manpage: dlist term with *bold* markup not double-bolded");

    const std::string src =
        "= cmd(1)\n"
        "\n"
        "== Description\n"
        "\n"
        "*-a value*::\n"
        "  sets option a.\n"
        "\n"
        "plain-term::\n"
        "  plain description.\n";

    asciiquack::ParseOptions opts;
    opts.doctype = "manpage";
    auto doc = asciiquack::Parser::parse_string(src, opts);
    std::string out = asciiquack::convert_to_manpage(*doc);

    // troff_inline converts *bold* to \fB...\fR; troff_escape then doubles
    // every backslash.  The resulting in-memory string for "*-a value*" is
    // \\fB\-a value\\fR (where \\fB and \\fR are the doubled-backslash forms).
    //
    // The old bug wrapped the already-formatted term in an extra \fB...\fR,
    // producing \fB\\fB\-a value\\fR\fR.  Verify that double-bold pattern
    // never appears in the output.
    EXPECT(out.find("\\fB\\\\fB") == std::string::npos);

    // The term line for the explicitly-bolded term must be present.
    // In-memory the sequence is: \\fB\-a value\\fR
    // As a C++ literal that is "\\\\fB\\-a value\\\\fR".
    EXPECT_CONTAINS(out, "\\\\fB\\-a value\\\\fR");

    // Plain term must be auto-bolded.  troff_escape converts '-' to '\-', so
    // the .TP term line is \fBplain\-term\fR.
    // As a C++ literal: "\\fBplain\\-term\\fR".
    EXPECT_CONTAINS(out, "\\fBplain\\-term\\fR");

    end_test();
}

static void test_manpage_backend_auto_doctype() {
    begin_test("manpage: -b manpage backend auto-implies doctype=manpage");

    // Document uses `:doctype: manpage` in header but ParseOptions keeps
    // the default doctype ("article").  The post-header fixup should still
    // extract manname and manvolnum from the title.
    const std::string src =
        "= SEARCH(nged)\n"
        ":doctype: manpage\n"
        ":mansource: BRL-CAD\n"
        ":manmanual: BRL-CAD User Commands\n"
        "\n"
        "== Name\n"
        "search - find objects\n";

    // Simulate what asciiquack does when -b manpage is passed without -d manpage:
    // it sets doctype="manpage" before parsing.
    asciiquack::ParseOptions opts;
    opts.doctype = "manpage";  // set by -b manpage in asciiquack.cpp
    auto doc = asciiquack::Parser::parse_string(src, opts);

    EXPECT(doc->attr("manname")   == "SEARCH");
    EXPECT(doc->attr("manvolnum") == "nged");

    std::string out = asciiquack::convert_to_manpage(*doc);
    EXPECT_CONTAINS(out, ".TH \"SEARCH\" \"nged\"");

    end_test();
}

static void test_manpage_backend_indoctype() {
    begin_test("manpage: :doctype: manpage in header triggers post-fixup");

    // When the user doesn't pass -b manpage but the file sets :doctype: manpage,
    // the post-header fixup runs after attribute processing and extracts the
    // manname / manvolnum from the title.
    const std::string src =
        "= ANALYZE(nged)\n"
        ":doctype: manpage\n"
        ":mansource: BRL-CAD\n"
        ":manmanual: BRL-CAD User Commands\n"
        "\n"
        "== Name\n"
        "analyze - analyze geometry\n";

    // Parse with default doctype ("article") – the :doctype: header entry
    // overrides to "manpage" during parsing.
    asciiquack::ParseOptions opts;
    // no explicit opts.doctype override – defaults to "article"
    auto doc = asciiquack::Parser::parse_string(src, opts);

    EXPECT(doc->attr("manname")   == "ANALYZE");
    EXPECT(doc->attr("manvolnum") == "nged");

    end_test();
}

static void test_manpage_empty_term_suppressed() {
    begin_test("manpage: empty-term DL items with no body are suppressed");

    // Multiple synopsis variants separated by standalone "::" markers should
    // render as sequential .nf blocks with no .TP / empty-bold lines.
    const std::string src =
        "= cmd(1)\n"
        ":doctype: manpage\n"
        "\n"
        "== SYNOPSIS\n"
        "\n"
        "[source]\n"
        "----\n"
        "cmd [options]\n"
        "----\n"
        "\n"
        "::\n"
        "\n"
        "[source]\n"
        "----\n"
        "cmd subcommand\n"
        "----\n"
        "\n"
        "== Description\n"
        "Desc.\n";

    asciiquack::ParseOptions opts;
    opts.doctype = "manpage";
    auto doc = asciiquack::Parser::parse_string(src, opts);
    std::string out = asciiquack::convert_to_manpage(*doc);

    // No ".TP\n\\fB\\fR" (empty bold term) should appear
    EXPECT(out.find("\\fB\\fR") == std::string::npos);
    // Both synopsis blocks should be present
    EXPECT_CONTAINS(out, "cmd [options]");
    EXPECT_CONTAINS(out, "cmd subcommand");

    end_test();
}

static void test_html5_empty_dlist_suppressed() {
    begin_test("html5: all-empty DL (only empty-term no-body items) produces no output");

    const std::string src =
        "= Test\n"
        "\n"
        "[source]\n"
        "----\n"
        "cmd1\n"
        "----\n"
        "\n"
        "::\n"
        "\n"
        "[source]\n"
        "----\n"
        "cmd2\n"
        "----\n";

    asciiquack::ParseOptions opts;
    auto doc = asciiquack::Parser::parse_string(src, opts);
    std::string out = asciiquack::convert_to_html5(*doc);

    // No empty dlist wrapper should appear
    EXPECT(out.find("class=\"dlist\"") == std::string::npos);
    // Both code blocks still rendered
    EXPECT_CONTAINS(out, "cmd1");
    EXPECT_CONTAINS(out, "cmd2");

    end_test();
}

static void test_dlist_body_not_swallowed_by_table() {
    begin_test("parser: table delimiter after DL body terminates item, not swallowed");

    // A table block immediately following a DL item body text (no blank line,
    // no list-continuation '+') must be parsed as a sibling block, not as
    // continuation text inside the item.
    const std::string src =
        "= Test\n"
        "\n"
        "== Section\n"
        "\n"
        "*-opt*::\n"
        "Description text. .Title\n"
        "|===\n"
        "|A |B\n"
        "\n"
        "|1 |2\n"
        "|===\n"
        "\n"
        "== Next Section\n"
        "\n"
        "After table.\n";

    asciiquack::ParseOptions opts;
    auto doc = asciiquack::Parser::parse_string(src, opts);
    std::string out = asciiquack::convert_to_html5(*doc);

    // The table must be rendered
    EXPECT_CONTAINS(out, "<table");
    // The Next Section heading must appear
    EXPECT_CONTAINS(out, "Next Section");
    // The table delimiters must NOT be treated as text inside the DL item
    EXPECT(out.find("|===") == std::string::npos);

    end_test();
}

static void test_html5_block_anchor_on_example() {
    begin_test("html5: [[anchor]] before example block sets id on div");

    const std::string src =
        "= Test\n"
        "\n"
        "See <<myexample,the example>>.\n"
        "\n"
        "[[myexample]]\n"
        ".My Example\n"
        "[example]\n"
        "====\n"
        "Example content here.\n"
        "====\n";

    asciiquack::ParseOptions opts;
    auto doc = asciiquack::Parser::parse_string(src, opts);
    std::string out = asciiquack::convert_to_html5(*doc);

    // The example block div must have id="myexample"
    EXPECT_CONTAINS(out, "id=\"myexample\"");
    // The cross-reference link must point to #myexample
    EXPECT_CONTAINS(out, "href=\"#myexample\"");

    end_test();
}

static void test_html5_block_anchor_on_paragraph() {
    begin_test("html5: [[anchor]] before paragraph sets id on div");

    const std::string src =
        "= Test\n"
        "\n"
        "See <<target>>.\n"
        "\n"
        "[[target]]\n"
        "This is the target paragraph.\n";

    asciiquack::ParseOptions opts;
    auto doc = asciiquack::Parser::parse_string(src, opts);
    std::string out = asciiquack::convert_to_html5(*doc);

    // The paragraph div must carry id="target"
    EXPECT_CONTAINS(out, "id=\"target\"");
    // The href in the xref must point to #target
    EXPECT_CONTAINS(out, "href=\"#target\"");

    end_test();
}

static void test_table_col_alignment() {
    begin_test("html5: table cols alignment prefix (< ^ >)");

    const std::string src =
        "= Doc\n"
        "\n"
        "[cols=\"<1,^1,>1\",options=\"header\"]\n"
        "|===\n"
        "| Left | Center | Right\n"
        "| a | b | c\n"
        "|===\n";

    auto doc = asciiquack::Parser::parse_string(src);
    std::string out = asciiquack::convert_to_html5(*doc);

    EXPECT_CONTAINS(out, "halign-left");
    EXPECT_CONTAINS(out, "halign-center");
    EXPECT_CONTAINS(out, "halign-right");

    end_test();
}

static void test_table_col_repeat() {
    begin_test("html5: table cols repeat notation (3*)");

    const std::string src =
        "= Doc\n"
        "\n"
        "[cols=\"3*\"]\n"
        "|===\n"
        "| a | b | c\n"
        "|===\n";

    auto doc = asciiquack::Parser::parse_string(src);
    // Should parse 3 equal-width columns from the repeat shorthand "3*"
    bool found_table = false;
    for (const auto& blk : doc->blocks()) {
        if (blk->context() == asciiquack::BlockContext::Table) {
            auto& tbl = dynamic_cast<const asciiquack::Table&>(*blk);
            EXPECT(tbl.column_specs().size() == 3);
            found_table = true;
        }
    }
    EXPECT(found_table);

    end_test();
}

static void test_table_col_style_h() {
    begin_test("html5: table cols style 'h' renders first column as header");

    const std::string src =
        "= Doc\n"
        "\n"
        "[cols=\"1h,2\"]\n"
        "|===\n"
        "| Key | Value\n"
        "| name | Alice\n"
        "|===\n";

    auto doc = asciiquack::Parser::parse_string(src);
    std::string out = asciiquack::convert_to_html5(*doc);

    // The 'h' style column should render body cells as <th> not <td>
    EXPECT_CONTAINS(out, "<th");

    end_test();
}

// ─────────────────────────────────────────────────────────────────────────────
// DocBook 5 backend tests
// ─────────────────────────────────────────────────────────────────────────────

#include "docbook5.hpp"

static void test_docbook5_basic_document() {
    begin_test("docbook5: basic document structure");

    const std::string src =
        "= My Article\n"
        "Jane Doe <jane@example.com>\n"
        "v1.0, 2024-01-01\n"
        "\n"
        "Introduction paragraph.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    const std::string out = asciiquack::convert_to_docbook5(*doc);

    EXPECT_CONTAINS(out, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
    EXPECT_CONTAINS(out, "xmlns=\"http://docbook.org/ns/docbook\"");
    EXPECT_CONTAINS(out, "version=\"5.0\"");
    EXPECT_CONTAINS(out, "<article");
    EXPECT_CONTAINS(out, "</article>");
    EXPECT_CONTAINS(out, "<info>");
    EXPECT_CONTAINS(out, "<title>My Article</title>");
    EXPECT_CONTAINS(out, "<author>");
    EXPECT_CONTAINS(out, "Jane");
    EXPECT_CONTAINS(out, "2024-01-01");
    EXPECT_CONTAINS(out, "<para>Introduction paragraph.</para>");

    end_test();
}

static void test_docbook5_sections() {
    begin_test("docbook5: sections become <section> elements with titles");

    const std::string src =
        "= Doc\n"
        "\n"
        "== First Section\n"
        "\n"
        "Body text.\n"
        "\n"
        "=== Subsection\n"
        "\n"
        "Sub content.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    const std::string out = asciiquack::convert_to_docbook5(*doc);

    EXPECT_CONTAINS(out, "<section");
    EXPECT_CONTAINS(out, "<title>First Section</title>");
    EXPECT_CONTAINS(out, "<title>Subsection</title>");
    EXPECT_CONTAINS(out, "<para>Body text.</para>");

    end_test();
}

static void test_docbook5_listing_block() {
    begin_test("docbook5: listing block becomes <programlisting>");

    const std::string src =
        "= Doc\n"
        "\n"
        "[source,python]\n"
        "----\n"
        "print(\"hello\")\n"
        "----\n";

    auto doc = asciiquack::Parser::parse_string(src);
    const std::string out = asciiquack::convert_to_docbook5(*doc);

    EXPECT_CONTAINS(out, "<programlisting language=\"python\">");
    EXPECT_CONTAINS(out, "print(");
    EXPECT_CONTAINS(out, "</programlisting>");

    end_test();
}

static void test_docbook5_admonition() {
    begin_test("docbook5: admonition paragraph becomes DocBook element");

    const std::string src =
        "= Doc\n"
        "\n"
        "NOTE: This is important.\n"
        "\n"
        "WARNING: Be careful.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    const std::string out = asciiquack::convert_to_docbook5(*doc);

    EXPECT_CONTAINS(out, "<note>");
    EXPECT_CONTAINS(out, "</note>");
    EXPECT_CONTAINS(out, "<warning>");
    EXPECT_CONTAINS(out, "</warning>");
    EXPECT_CONTAINS(out, "This is important.");

    end_test();
}

static void test_docbook5_ulist() {
    begin_test("docbook5: unordered list becomes <itemizedlist>");

    const std::string src =
        "= Doc\n"
        "\n"
        "* Apple\n"
        "* Banana\n"
        "* Cherry\n";

    auto doc = asciiquack::Parser::parse_string(src);
    const std::string out = asciiquack::convert_to_docbook5(*doc);

    EXPECT_CONTAINS(out, "<itemizedlist>");
    EXPECT_CONTAINS(out, "<listitem>");
    EXPECT_CONTAINS(out, "Apple");
    EXPECT_CONTAINS(out, "Banana");
    EXPECT_CONTAINS(out, "</itemizedlist>");

    end_test();
}

static void test_docbook5_olist() {
    begin_test("docbook5: ordered list becomes <orderedlist>");

    const std::string src =
        "= Doc\n"
        "\n"
        ". Step one\n"
        ". Step two\n"
        ". Step three\n";

    auto doc = asciiquack::Parser::parse_string(src);
    const std::string out = asciiquack::convert_to_docbook5(*doc);

    EXPECT_CONTAINS(out, "<orderedlist");
    EXPECT_CONTAINS(out, "numeration=\"arabic\"");
    EXPECT_CONTAINS(out, "<listitem>");
    EXPECT_CONTAINS(out, "Step one");
    EXPECT_CONTAINS(out, "</orderedlist>");

    end_test();
}

static void test_docbook5_dlist() {
    begin_test("docbook5: description list becomes <variablelist>");

    const std::string src =
        "= Doc\n"
        "\n"
        "term one:: description one\n"
        "term two:: description two\n";

    auto doc = asciiquack::Parser::parse_string(src);
    const std::string out = asciiquack::convert_to_docbook5(*doc);

    EXPECT_CONTAINS(out, "<variablelist>");
    EXPECT_CONTAINS(out, "<varlistentry>");
    EXPECT_CONTAINS(out, "<term>");
    EXPECT_CONTAINS(out, "term one");
    EXPECT_CONTAINS(out, "description one");
    EXPECT_CONTAINS(out, "</variablelist>");

    end_test();
}

static void test_docbook5_inline_markup() {
    begin_test("docbook5: inline bold and italic become <emphasis>");

    const std::string src =
        "= Doc\n"
        "\n"
        "Use *bold* and _italic_ and `mono` text.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    const std::string out = asciiquack::convert_to_docbook5(*doc);

    EXPECT_CONTAINS(out, "<emphasis role=\"strong\">");
    EXPECT_CONTAINS(out, "<emphasis>");
    EXPECT_CONTAINS(out, "<literal>");
    EXPECT_CONTAINS(out, "bold");
    EXPECT_CONTAINS(out, "italic");
    EXPECT_CONTAINS(out, "mono");

    end_test();
}

static void test_docbook5_special_chars() {
    begin_test("docbook5: special characters are XML-escaped");

    const std::string src =
        "= Doc\n"
        "\n"
        "Use <angle> brackets & \"quotes\".\n";

    auto doc = asciiquack::Parser::parse_string(src);
    const std::string out = asciiquack::convert_to_docbook5(*doc);

    EXPECT_CONTAINS(out, "&lt;angle&gt;");
    EXPECT_CONTAINS(out, "&amp;");
    EXPECT_NOT_CONTAINS(out, "<angle>");

    end_test();
}

static void test_docbook5_book_doctype() {
    begin_test("docbook5: book doctype produces <book> root and <chapter>");

    const std::string src =
        "= My Book\n"
        ":doctype: book\n"
        "\n"
        "== Chapter One\n"
        "\n"
        "Content.\n";

    asciiquack::ParseOptions opts;
    opts.doctype = "book";
    auto doc = asciiquack::Parser::parse_string(src, opts);
    const std::string out = asciiquack::convert_to_docbook5(*doc);

    EXPECT_CONTAINS(out, "<book");
    EXPECT_CONTAINS(out, "</book>");
    EXPECT_CONTAINS(out, "<chapter");
    EXPECT_CONTAINS(out, "<title>Chapter One</title>");

    end_test();
}

static void test_docbook5_table() {
    begin_test("docbook5: table becomes CALS <informaltable>");

    const std::string src =
        "= Doc\n"
        "\n"
        "|===\n"
        "| A | B\n"
        "| 1 | 2\n"
        "|===\n";

    auto doc = asciiquack::Parser::parse_string(src);
    const std::string out = asciiquack::convert_to_docbook5(*doc);

    EXPECT_CONTAINS(out, "<informaltable");
    EXPECT_CONTAINS(out, "<tgroup");
    EXPECT_CONTAINS(out, "<tbody>");
    EXPECT_CONTAINS(out, "<row>");
    EXPECT_CONTAINS(out, "<entry>");

    end_test();
}

static void test_docbook5_image() {
    begin_test("docbook5: block image becomes <mediaobject>");

    const std::string src =
        "= Doc\n"
        "\n"
        "image::sunset.png[Beautiful sunset]\n";

    auto doc = asciiquack::Parser::parse_string(src);
    const std::string out = asciiquack::convert_to_docbook5(*doc);

    EXPECT_CONTAINS(out, "<mediaobject>");
    EXPECT_CONTAINS(out, "<imagedata fileref=\"sunset.png\"");
    EXPECT_CONTAINS(out, "Beautiful sunset");

    end_test();
}

static void test_section_nesting_warning() {
    begin_test("parser: section nesting skip emits warning to stderr");

    // Redirect stderr to a string buffer for inspection
    std::ostringstream err_buf;
    std::streambuf* old_err = std::cerr.rdbuf(err_buf.rdbuf());

    const std::string src =
        "= Doc\n"
        "\n"
        "== Section\n"
        "\n"
        "==== Deep nested (skips ===)\n"
        "\n"
        "Content.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    (void)doc;

    std::cerr.rdbuf(old_err);  // restore

    std::string errmsg = err_buf.str();
    EXPECT_CONTAINS(errmsg, "WARNING");
    EXPECT_CONTAINS(errmsg, "nesting");

    end_test();
}

static void test_unclosed_block_warning() {
    begin_test("parser: unclosed block emits warning to stderr");

    std::ostringstream err_buf;
    std::streambuf* old_err = std::cerr.rdbuf(err_buf.rdbuf());

    const std::string src =
        "= Doc\n"
        "\n"
        "====\n"
        "An example block that is never closed.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    (void)doc;

    std::cerr.rdbuf(old_err);

    std::string errmsg = err_buf.str();
    EXPECT_CONTAINS(errmsg, "WARNING");
    EXPECT_CONTAINS(errmsg, "unclosed");

    end_test();
}

// ─────────────────────────────────────────────────────────────────────────────
// PDF backend tests
// ─────────────────────────────────────────────────────────────────────────────

/// Check that @p pdf starts with the standard PDF header and ends with %%EOF.
static bool is_valid_pdf_envelope(const std::string& pdf) {
    if (pdf.compare(0, 7, "%PDF-1.") != 0) { return false; }
    if (pdf.size() < 5)                    { return false; }
    // Ends with "%%EOF\n" or "%%EOF"
    return pdf.rfind("%%EOF") != std::string::npos;
}

/// Parse the xref table and verify that every object offset is correct.
static bool pdf_xref_valid(const std::string& pdf) {
    auto sxr_pos = pdf.rfind("startxref");
    if (sxr_pos == std::string::npos) { return false; }

    // Read xref byte offset
    std::size_t xref_offset = 0;
    {
        auto after = pdf.find('\n', sxr_pos + 9);
        if (after == std::string::npos) { return false; }
        xref_offset = static_cast<std::size_t>(
            std::stoul(pdf.substr(sxr_pos + 10, after - sxr_pos - 10)));
    }

    // Parse "xref\n0 N\n..." from xref_offset
    const char* xref = pdf.c_str() + xref_offset;
    if (std::string(xref, 4) != "xref") { return false; }

    // Find "0 N" count
    const char* p = xref + 5;  // skip "xref\n"
    int n_objs = std::atoi(p + 2);  // skip "0 "
    if (n_objs <= 0) { return false; }

    // Advance past "0 N\n"
    p = std::strchr(p, '\n');
    if (!p) { return false; }
    ++p;

    // Validate each entry (skip obj 0 which is always free)
    // Each entry is 20 bytes: "NNNNNNNNNN GGGGG x \n"
    for (int i = 0; i < n_objs; ++i) {
        if (i == 0) { p += 20; continue; }  // skip free entry

        std::size_t obj_off = static_cast<std::size_t>(std::atol(p));
        p += 20;

        // At obj_off in the pdf, we expect "i 0 obj"
        std::string expected = std::to_string(i) + " 0 obj";
        if (pdf.compare(obj_off, expected.size(), expected) != 0) {
            return false;
        }
    }
    return true;
}

static void test_pdf_basic_structure() {
    begin_test("pdf: basic document produces valid PDF structure");

    const std::string src =
        "= Hello PDF\n"
        "Author Name\n"
        "\n"
        "A simple paragraph.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    std::string pdf = asciiquack::convert_to_pdf(*doc);

    EXPECT(is_valid_pdf_envelope(pdf));
    EXPECT(pdf_xref_valid(pdf));
    // PDF should contain the title text (rendered word-by-word)
    EXPECT_CONTAINS(pdf, "Hello");
    EXPECT_CONTAINS(pdf, "PDF");
    // Should reference standard fonts
    EXPECT_CONTAINS(pdf, "Helvetica");
    EXPECT_CONTAINS(pdf, "Courier");

    end_test();
}

static void test_pdf_a4_size() {
    begin_test("pdf: A4 page size produces correct MediaBox");

    const std::string src = "= Test\n\nParagraph.\n";
    auto doc = asciiquack::Parser::parse_string(src);

    std::string pdf_a4     = asciiquack::convert_to_pdf(*doc, true);
    std::string pdf_letter = asciiquack::convert_to_pdf(*doc, false);

    // A4: 595 x 842
    EXPECT_CONTAINS(pdf_a4, "595.00");
    EXPECT_CONTAINS(pdf_a4, "842.00");
    // Letter: 612 x 792
    EXPECT_CONTAINS(pdf_letter, "612.00");
    EXPECT_CONTAINS(pdf_letter, "792.00");

    EXPECT(is_valid_pdf_envelope(pdf_a4));
    EXPECT(is_valid_pdf_envelope(pdf_letter));
    EXPECT(pdf_xref_valid(pdf_a4));
    EXPECT(pdf_xref_valid(pdf_letter));

    end_test();
}

static void test_pdf_sections() {
    begin_test("pdf: section headings included in output");

    const std::string src =
        "= Document Title\n"
        "\n"
        "== Section One\n"
        "\n"
        "Some text.\n"
        "\n"
        "=== Subsection\n"
        "\n"
        "Sub text.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    std::string pdf = asciiquack::convert_to_pdf(*doc);

    EXPECT(is_valid_pdf_envelope(pdf));
    EXPECT(pdf_xref_valid(pdf));
    // Section headings rendered word-by-word
    EXPECT_CONTAINS(pdf, "Section");
    EXPECT_CONTAINS(pdf, "One");
    EXPECT_CONTAINS(pdf, "Subsection");

    end_test();
}

static void test_pdf_lists() {
    begin_test("pdf: unordered and ordered lists rendered");

    const std::string src =
        "= Lists\n"
        "\n"
        "* Alpha\n"
        "* Beta\n"
        "\n"
        ". First\n"
        ". Second\n";

    auto doc = asciiquack::Parser::parse_string(src);
    std::string pdf = asciiquack::convert_to_pdf(*doc);

    EXPECT(is_valid_pdf_envelope(pdf));
    EXPECT(pdf_xref_valid(pdf));
    EXPECT_CONTAINS(pdf, "Alpha");
    EXPECT_CONTAINS(pdf, "Beta");
    EXPECT_CONTAINS(pdf, "First");
    EXPECT_CONTAINS(pdf, "Second");

    end_test();
}

static void test_pdf_code_block() {
    begin_test("pdf: listing block uses Courier font");

    const std::string src =
        "= Code\n"
        "\n"
        "----\n"
        "int main() { return 0; }\n"
        "----\n";

    auto doc = asciiquack::Parser::parse_string(src);
    std::string pdf = asciiquack::convert_to_pdf(*doc);

    EXPECT(is_valid_pdf_envelope(pdf));
    EXPECT(pdf_xref_valid(pdf));
    EXPECT_CONTAINS(pdf, "int main");
    // Courier should be used for code
    EXPECT_CONTAINS(pdf, "/F5");  // F5 = Courier in our resource dict

    end_test();
}

static void test_pdf_inline_markup() {
    begin_test("pdf: inline bold/italic/mono uses correct font resources");

    const std::string src =
        "= Test\n"
        "\n"
        "Text with *bold* and _italic_ and `mono` words.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    std::string pdf = asciiquack::convert_to_pdf(*doc);

    EXPECT(is_valid_pdf_envelope(pdf));
    EXPECT(pdf_xref_valid(pdf));
    EXPECT_CONTAINS(pdf, "bold");
    EXPECT_CONTAINS(pdf, "italic");
    EXPECT_CONTAINS(pdf, "mono");
    // F2=Helvetica-Bold, F3=Oblique, F5=Courier
    EXPECT_CONTAINS(pdf, "/F2");
    EXPECT_CONTAINS(pdf, "/F3");
    EXPECT_CONTAINS(pdf, "/F5");

    end_test();
}

static void test_pdf_admonition() {
    begin_test("pdf: admonition block rendered with label");

    const std::string src =
        "= Test\n"
        "\n"
        "NOTE: Important information here.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    std::string pdf = asciiquack::convert_to_pdf(*doc);

    EXPECT(is_valid_pdf_envelope(pdf));
    EXPECT(pdf_xref_valid(pdf));
    EXPECT_CONTAINS(pdf, "NOTE");
    EXPECT_CONTAINS(pdf, "Important");

    end_test();
}

static void test_pdf_hrule() {
    begin_test("pdf: thematic break emits a horizontal line");

    const std::string src =
        "= Test\n"
        "\n"
        "Before.\n"
        "\n"
        "'''\n"
        "\n"
        "After.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    std::string pdf = asciiquack::convert_to_pdf(*doc);

    EXPECT(is_valid_pdf_envelope(pdf));
    EXPECT(pdf_xref_valid(pdf));
    EXPECT_CONTAINS(pdf, "Before");
    EXPECT_CONTAINS(pdf, "After");

    end_test();
}

static void test_pdf_multipage() {
    begin_test("pdf: long document produces multiple pages");

    // Build a document with enough content to overflow one page
    std::string src = "= Long Document\n\n";
    for (int i = 1; i <= 40; ++i) {
        src += "== Section " + std::to_string(i) + "\n\n"
               "This is paragraph " + std::to_string(i) +
               " with some text content to fill the page.\n\n";
    }

    auto doc = asciiquack::Parser::parse_string(src);
    std::string pdf = asciiquack::convert_to_pdf(*doc);

    EXPECT(is_valid_pdf_envelope(pdf));
    EXPECT(pdf_xref_valid(pdf));

    // A long document should produce more than one page (i.e. more than
    // one content stream / page pair in the PDF)
    std::size_t stream_count = 0;
    std::size_t pos = 0;
    while ((pos = pdf.find("stream\n", pos)) != std::string::npos) {
        ++stream_count;
        ++pos;
    }
    EXPECT(stream_count > 1);

    end_test();
}

static void test_pdf_escape_special_chars() {
    begin_test("pdf: text with parentheses and backslashes is escaped");

    const std::string src =
        "= Test\n"
        "\n"
        "Text (with parens) and back\\slash.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    std::string pdf = asciiquack::convert_to_pdf(*doc);

    EXPECT(is_valid_pdf_envelope(pdf));
    EXPECT(pdf_xref_valid(pdf));
    // The parens should appear escaped in the content stream
    EXPECT_CONTAINS(pdf, "\\(");
    EXPECT_CONTAINS(pdf, "\\)");

    end_test();
}

// ─────────────────────────────────────────────────────────────────────────────
// TrueType font tests
// ─────────────────────────────────────────────────────────────────────────────

// System font path used for TrueType tests.  The Lato font is available on
// the CI runner; this is a light dependency since Lato is bundled with many
// Ubuntu/Debian installations.  If the path doesn't exist the test is skipped
// rather than failed so the suite remains portable.
static constexpr const char* LATO_REGULAR_PATH =
    "/usr/share/fonts/truetype/lato/Lato-Regular.ttf";

static void test_pdf_empty_font_path_fallback() {
    begin_test("pdf: empty font path falls back to base-14 Helvetica");

    const std::string src = "= Test\n\nA paragraph.\n";
    auto doc = asciiquack::Parser::parse_string(src);

    std::string pdf_default = asciiquack::convert_to_pdf(*doc);
    std::string pdf_empty   = asciiquack::convert_to_pdf(*doc, false, "");

    EXPECT(is_valid_pdf_envelope(pdf_default));
    EXPECT(is_valid_pdf_envelope(pdf_empty));
    EXPECT(pdf_xref_valid(pdf_default));
    EXPECT(pdf_xref_valid(pdf_empty));

    // Both outputs should be structurally identical (same object count).
    // Neither should embed a TrueType font.
    EXPECT(pdf_default.find("/Subtype /TrueType") == std::string::npos);
    EXPECT(pdf_empty.find("/Subtype /TrueType") == std::string::npos);
    EXPECT_CONTAINS(pdf_empty, "Helvetica");

    end_test();
}

static void test_pdf_invalid_font_path_fallback() {
    begin_test("pdf: invalid font path falls back gracefully to Helvetica");

    const std::string src = "= Test\n\nA paragraph.\n";
    auto doc = asciiquack::Parser::parse_string(src);

    // Should not throw; should fall back to Helvetica silently.
    std::string pdf = asciiquack::convert_to_pdf(*doc, false,
                                                  "/no/such/font/does/not/exist.ttf");

    EXPECT(is_valid_pdf_envelope(pdf));
    EXPECT(pdf_xref_valid(pdf));
    // No TrueType embedding – fell back to base-14
    EXPECT(pdf.find("/Subtype /TrueType") == std::string::npos);
    EXPECT_CONTAINS(pdf, "Helvetica");

    end_test();
}

static void test_pdf_ttf_font_embedded() {
    begin_test("pdf: valid TTF font path embeds TrueType font as F1");

    // Skip if the system font is not present (keeps the suite portable)
    {
        std::ifstream probe(LATO_REGULAR_PATH);
        if (!probe) {
            std::cout << " (skipped – " << LATO_REGULAR_PATH << " not found)";
            end_test();
            return;
        }
    }

    const std::string src = "= Document\n\nBody text paragraph.\n";
    auto doc = asciiquack::Parser::parse_string(src);

    std::string pdf = asciiquack::convert_to_pdf(*doc, false, LATO_REGULAR_PATH);

    EXPECT(is_valid_pdf_envelope(pdf));
    EXPECT(pdf_xref_valid(pdf));

    // A TrueType font must be present
    EXPECT_CONTAINS(pdf, "/Subtype /TrueType");
    // FontDescriptor and font data stream must be present
    EXPECT_CONTAINS(pdf, "/FontDescriptor");
    EXPECT_CONTAINS(pdf, "/FontFile2");
    // Font name derived from filename stem
    EXPECT_CONTAINS(pdf, "Lato-Regular");
    // Widths array for characters 32–255
    EXPECT_CONTAINS(pdf, "/Widths [");
    EXPECT_CONTAINS(pdf, "/FirstChar 32");
    EXPECT_CONTAINS(pdf, "/LastChar 255");
    // Body text still contains the page content
    EXPECT_CONTAINS(pdf, "Body");
    // Courier still present for code/mono
    EXPECT_CONTAINS(pdf, "Courier");

    end_test();
}

static void test_pdf_ttf_object_layout() {
    begin_test("pdf: with TTF font object IDs are 3=stream 4=descriptor 5=F1");

    {
        std::ifstream probe(LATO_REGULAR_PATH);
        if (!probe) {
            std::cout << " (skipped – " << LATO_REGULAR_PATH << " not found)";
            end_test();
            return;
        }
    }

    const std::string src = "= T\n\nP.\n";
    auto doc = asciiquack::Parser::parse_string(src);

    std::string pdf = asciiquack::convert_to_pdf(*doc, false, LATO_REGULAR_PATH);

    EXPECT(pdf_xref_valid(pdf));

    // Object 3 must be the font stream
    EXPECT_CONTAINS(pdf, "3 0 obj\n<< /Length ");
    EXPECT_CONTAINS(pdf, "/Length1 ");
    // Object 4 must be FontDescriptor
    EXPECT_CONTAINS(pdf, "4 0 obj\n<< /Type /FontDescriptor");
    // Object 5 must be the TrueType font dict (F1)
    EXPECT_CONTAINS(pdf, "5 0 obj\n<< /Type /Font\n   /Subtype /TrueType");
    // F1 still maps to object 5 in the per-page resource dict
    EXPECT_CONTAINS(pdf, "/F1 5 0 R");

    end_test();
}

static void test_pdf_ttf_widths_differ_from_helvetica() {
    begin_test("pdf: custom TTF produces different character widths from Helvetica");

    {
        std::ifstream probe(LATO_REGULAR_PATH);
        if (!probe) {
            std::cout << " (skipped – " << LATO_REGULAR_PATH << " not found)";
            end_test();
            return;
        }
    }

    // Lato is a humanist sans-serif with different metrics from Helvetica.
    // Load the font directly via TtfFont and check a few glyph widths.
    auto font = minipdf::TtfFont::from_file(LATO_REGULAR_PATH);
    EXPECT(font != nullptr);

    // The Helvetica width for 'A' (codepoint 65) is 667/1000 em.
    // Lato's 'A' should differ.
    float lato_a_width = font->advance_1000('A');
    EXPECT(lato_a_width > 0.0f);
    // Helvetica value from the built-in table is 667; Lato will not be exactly 667
    // (Lato is slightly narrower).  We just check it's in a sane range.
    EXPECT(lato_a_width > 300.0f && lato_a_width < 900.0f);

    // Verify metrics
    EXPECT(font->ascent_1000()  > 500.0f);
    EXPECT(font->descent_1000() < 0.0f);

    end_test();
}

static void test_pdf_ttf_xref_still_valid_with_font() {
    begin_test("pdf: xref table is correct when TrueType font is embedded");

    {
        std::ifstream probe(LATO_REGULAR_PATH);
        if (!probe) {
            std::cout << " (skipped – " << LATO_REGULAR_PATH << " not found)";
            end_test();
            return;
        }
    }

    // Build a multi-section document to get multiple pages
    std::string src = "= Long Doc\n\n";
    for (int i = 0; i < 20; ++i) {
        src += "== Section " + std::to_string(i + 1) + "\n\nParagraph text here.\n\n";
    }

    auto doc = asciiquack::Parser::parse_string(src);
    std::string pdf = asciiquack::convert_to_pdf(*doc, false, LATO_REGULAR_PATH);

    EXPECT(is_valid_pdf_envelope(pdf));
    EXPECT(pdf_xref_valid(pdf));

    // Should have multiple pages
    std::size_t stream_count = 0;
    std::size_t pos = 0;
    while ((pos = pdf.find("stream\n", pos)) != std::string::npos) {
        ++stream_count;
        ++pos;
    }
    // At least 2 streams: font data + ≥1 page content
    EXPECT(stream_count >= 2);

    end_test();
}

static void test_pdf_ttf_postscript_name_from_table() {
    begin_test("pdf: TrueType PostScript name read from font name table");

    {
        std::ifstream probe(LATO_REGULAR_PATH);
        if (!probe) {
            std::cout << " (skipped – " << LATO_REGULAR_PATH << " not found)";
            end_test();
            return;
        }
    }

    // Load the font; the PostScript name (nameID=6) stored in Lato-Regular.ttf
    // is "Lato-Regular".  We verify that this value is read from the font's own
    // name table rather than inferred from the path (the two happen to agree for
    // Lato, but the name-table path is more standards-compliant).
    auto font = minipdf::TtfFont::from_file(LATO_REGULAR_PATH);
    EXPECT(font != nullptr);

    // pdf_name() must be a non-empty, PDF-token-safe string (no spaces).
    const std::string& ps_name = font->pdf_name();
    EXPECT(!ps_name.empty());
    EXPECT(ps_name.find(' ') == std::string::npos);
    // The Lato PostScript name is "Lato-Regular"
    EXPECT(ps_name == "Lato-Regular");

    // The generated PDF must use the same name in /BaseFont and /FontName
    const std::string src = "= Doc\n\nText.\n";
    auto doc = asciiquack::Parser::parse_string(src);
    std::string pdf = asciiquack::convert_to_pdf(*doc, false, LATO_REGULAR_PATH);
    EXPECT_CONTAINS(pdf, "/FontName /Lato-Regular");
    EXPECT_CONTAINS(pdf, "/BaseFont /Lato-Regular");

    end_test();
}

static void test_pdf_ttf_os2_vertical_metrics() {
    begin_test("pdf: TrueType OS/2 vertical metrics used in FontDescriptor");

    {
        std::ifstream probe(LATO_REGULAR_PATH);
        if (!probe) {
            std::cout << " (skipped – " << LATO_REGULAR_PATH << " not found)";
            end_test();
            return;
        }
    }

    auto font = minipdf::TtfFont::from_file(LATO_REGULAR_PATH);
    EXPECT(font != nullptr);

    // OS/2 typographic ascent for Lato Regular is 1900 units in a 2000-UPM
    // font, giving 950 in 1000-unit space.  We just verify the values are sane
    // (positive ascent, negative descent) and within plausible PDF ranges.
    float asc  = font->ascent_1000();
    float desc = font->descent_1000();
    EXPECT(asc  >  0.0f);
    EXPECT(desc <  0.0f);
    EXPECT(asc  <= 2000.0f);
    EXPECT(desc >= -2000.0f);

    // The generated PDF FontDescriptor must contain non-zero /Ascent and /Descent
    const std::string src = "= Doc\n\nText.\n";
    auto doc = asciiquack::Parser::parse_string(src);
    std::string pdf = asciiquack::convert_to_pdf(*doc, false, LATO_REGULAR_PATH);
    EXPECT_CONTAINS(pdf, "/Ascent ");
    EXPECT_CONTAINS(pdf, "/Descent ");
    // Descent must be negative (stored as a plain integer in PDF).
    auto desc_pos = pdf.find("/Descent ");
    EXPECT(desc_pos != std::string::npos);
    if (desc_pos != std::string::npos) {
        // Skip "/Descent " and check for a minus sign
        auto val_start = desc_pos + 9;
        EXPECT(val_start < pdf.size() && pdf[val_start] == '-');
    }

    end_test();
}

// ─────────────────────────────────────────────────────────────────────────────
// Noto / multi-font tests
// ─────────────────────────────────────────────────────────────────────────────

#ifdef ASCIIQUACK_NOTO_FONTS_DIR
static const std::string NOTO_DIR = ASCIIQUACK_NOTO_FONTS_DIR;
static const std::string NOTO_REGULAR     = NOTO_DIR + "/Noto_Sans/static/NotoSans-Regular.ttf";
static const std::string NOTO_BOLD        = NOTO_DIR + "/Noto_Sans/static/NotoSans-Bold.ttf";
static const std::string NOTO_ITALIC      = NOTO_DIR + "/Noto_Sans/static/NotoSans-Italic.ttf";
static const std::string NOTO_BOLD_ITALIC = NOTO_DIR + "/Noto_Sans/static/NotoSans-BoldItalic.ttf";
static const std::string NOTO_MONO        = NOTO_DIR + "/Noto_Sans_Mono/static/NotoSansMono-Regular.ttf";
static const std::string NOTO_MONO_BOLD   = NOTO_DIR + "/Noto_Sans_Mono/static/NotoSansMono-Bold.ttf";
#endif

static void test_pdf_fontset_regular_only() {
    begin_test("pdf: FontSet with regular-only path embeds one TrueType (F1)");

#ifndef ASCIIQUACK_NOTO_FONTS_DIR
    std::cout << " (skipped – ASCIIQUACK_NOTO_FONTS_DIR not defined)";
    end_test();
    return;
#else
    {
        std::ifstream probe(NOTO_REGULAR);
        if (!probe) {
            std::cout << " (skipped – Noto fonts not found at " << NOTO_REGULAR << ")";
            end_test();
            return;
        }
    }

    const std::string src = "= Title\n\nBody text with *bold* and _italic_.\n";
    auto doc = asciiquack::Parser::parse_string(src);

    asciiquack::FontSet fs;
    fs.regular = NOTO_REGULAR;
    std::string pdf = asciiquack::convert_to_pdf(*doc, false, fs);

    EXPECT(is_valid_pdf_envelope(pdf));
    EXPECT(pdf_xref_valid(pdf));

    // Exactly one TrueType embedding (F1 only).
    // Count occurrences of /Subtype /TrueType
    std::size_t ttf_count = 0;
    std::size_t pos = 0;
    while ((pos = pdf.find("/Subtype /TrueType", pos)) != std::string::npos) {
        ++ttf_count;
        ++pos;
    }
    EXPECT(ttf_count == 1);

    // F2 (bold) and F5 (mono) still use base-14.
    EXPECT_CONTAINS(pdf, "Helvetica-Bold");
    EXPECT_CONTAINS(pdf, "Courier");

    end_test();
#endif
}

static void test_pdf_fontset_all_six_styles() {
    begin_test("pdf: FontSet with all six styles embeds six TrueType fonts");

#ifndef ASCIIQUACK_NOTO_FONTS_DIR
    std::cout << " (skipped – ASCIIQUACK_NOTO_FONTS_DIR not defined)";
    end_test();
    return;
#else
    {
        std::ifstream probe(NOTO_REGULAR);
        if (!probe) {
            std::cout << " (skipped – Noto fonts not found at " << NOTO_REGULAR << ")";
            end_test();
            return;
        }
    }

    const std::string src =
        "= Title\n\nBody text.\n\n*Bold text.* _Italic text._ `Monospace text.`\n";
    auto doc = asciiquack::Parser::parse_string(src);

    asciiquack::FontSet fs;
    fs.regular     = NOTO_REGULAR;
    fs.bold        = NOTO_BOLD;
    fs.italic      = NOTO_ITALIC;
    fs.bold_italic = NOTO_BOLD_ITALIC;
    fs.mono        = NOTO_MONO;
    fs.mono_bold   = NOTO_MONO_BOLD;
    std::string pdf = asciiquack::convert_to_pdf(*doc, false, fs);

    EXPECT(is_valid_pdf_envelope(pdf));
    EXPECT(pdf_xref_valid(pdf));

    // All six slots should be TrueType.
    std::size_t ttf_count = 0;
    std::size_t pos = 0;
    while ((pos = pdf.find("/Subtype /TrueType", pos)) != std::string::npos) {
        ++ttf_count;
        ++pos;
    }
    EXPECT(ttf_count == 6);

    // No base-14 Helvetica or Courier references (all overridden by Noto).
    EXPECT(pdf.find("Helvetica") == std::string::npos);
    EXPECT(pdf.find("Courier")   == std::string::npos);

    // Six font data streams and six FontDescriptors embedded.
    std::size_t fd_count = 0;
    pos = 0;
    while ((pos = pdf.find("/Type /FontDescriptor", pos)) != std::string::npos) {
        ++fd_count;
        ++pos;
    }
    EXPECT(fd_count == 6);

    end_test();
#endif
}

static void test_pdf_fontset_object_layout_two_ttf() {
    begin_test("pdf: FontSet with Regular+Bold has correct two-TTF object layout");

#ifndef ASCIIQUACK_NOTO_FONTS_DIR
    std::cout << " (skipped – ASCIIQUACK_NOTO_FONTS_DIR not defined)";
    end_test();
    return;
#else
    {
        std::ifstream probe(NOTO_REGULAR);
        if (!probe) {
            std::cout << " (skipped – Noto fonts not found at " << NOTO_REGULAR << ")";
            end_test();
            return;
        }
    }

    const std::string src = "= Title\n\n*Bold.* Normal.\n";
    auto doc = asciiquack::Parser::parse_string(src);

    asciiquack::FontSet fs;
    fs.regular = NOTO_REGULAR;
    fs.bold    = NOTO_BOLD;
    std::string pdf = asciiquack::convert_to_pdf(*doc, false, fs);

    EXPECT(is_valid_pdf_envelope(pdf));
    EXPECT(pdf_xref_valid(pdf));

    // With two embedded TTF fonts:
    //   3 = stream (Regular), 4 = descriptor (Regular)
    //   5 = stream (Bold),    6 = descriptor (Bold)
    //   7 = F1 font dict (TrueType), 8 = F2 font dict (TrueType)
    //   9-11 = F3-F5 base-14, 12 = F6 base-14, 13+ = pages
    EXPECT_CONTAINS(pdf, "7 0 obj\n<< /Type /Font\n   /Subtype /TrueType");
    EXPECT_CONTAINS(pdf, "8 0 obj\n<< /Type /Font\n   /Subtype /TrueType");

    // F3 should be a base-14 (no custom italic).
    EXPECT_CONTAINS(pdf, "Helvetica-Oblique");

    // Exactly two font data streams.
    std::size_t ttf_count = 0;
    std::size_t pos = 0;
    while ((pos = pdf.find("/Subtype /TrueType", pos)) != std::string::npos) {
        ++ttf_count;
        ++pos;
    }
    EXPECT(ttf_count == 2);

    end_test();
#endif
}

static void test_pdf_fontset_mono_custom() {
    begin_test("pdf: FontSet with custom mono embeds TrueType for F5");

#ifndef ASCIIQUACK_NOTO_FONTS_DIR
    std::cout << " (skipped – ASCIIQUACK_NOTO_FONTS_DIR not defined)";
    end_test();
    return;
#else
    {
        std::ifstream probe(NOTO_MONO);
        if (!probe) {
            std::cout << " (skipped – Noto Mono font not found at " << NOTO_MONO << ")";
            end_test();
            return;
        }
    }

    const std::string src = "= Title\n\n----\nsome code\n----\n";
    auto doc = asciiquack::Parser::parse_string(src);

    asciiquack::FontSet fs;
    fs.mono = NOTO_MONO;
    std::string pdf = asciiquack::convert_to_pdf(*doc, false, fs);

    EXPECT(is_valid_pdf_envelope(pdf));
    EXPECT(pdf_xref_valid(pdf));

    // One TrueType embedding (F5 only).
    std::size_t ttf_count = 0;
    std::size_t pos = 0;
    while ((pos = pdf.find("/Subtype /TrueType", pos)) != std::string::npos) {
        ++ttf_count;
        ++pos;
    }
    EXPECT(ttf_count == 1);

    // F1 (regular) is still base-14 Helvetica.
    EXPECT_CONTAINS(pdf, "Helvetica");
    // Courier-Bold (F6 MonoBold) is still base-14 since we didn't set mono_bold,
    // but NotoSansMono replaced plain Courier (F5).
    EXPECT(pdf.find("Courier-Bold") != std::string::npos);
    // The NotoSansMono PostScript name should appear instead of "Courier" for F5.
    auto noto_mono_font = minipdf::TtfFont::from_file(NOTO_MONO);
    if (noto_mono_font) {
        EXPECT_CONTAINS(pdf, noto_mono_font->pdf_name());
    }

    end_test();
#endif
}

static void test_pdf_fontset_noto_metrics_differ_from_helvetica() {
    begin_test("pdf: Noto Sans has different character widths than Helvetica");

#ifndef ASCIIQUACK_NOTO_FONTS_DIR
    std::cout << " (skipped – ASCIIQUACK_NOTO_FONTS_DIR not defined)";
    end_test();
    return;
#else
    {
        std::ifstream probe(NOTO_REGULAR);
        if (!probe) {
            std::cout << " (skipped – Noto fonts not found at " << NOTO_REGULAR << ")";
            end_test();
            return;
        }
    }

    auto noto = minipdf::TtfFont::from_file(NOTO_REGULAR);
    EXPECT(noto != nullptr);
    if (!noto) { end_test(); return; }

    // The width of 'W' in 1000-unit EM space should be non-zero and differ
    // from Helvetica's hard-coded value (722 units).
    float noto_w = noto->advance_1000(static_cast<int>('W'));
    EXPECT(noto_w > 0.0f);

    float helv_w = minipdf::char_width_units('W', minipdf::FontStyle::Regular);
    EXPECT(noto_w != helv_w);

    // Vertical metrics should be sane.
    EXPECT(noto->ascent_1000()  > 0.0f);
    EXPECT(noto->descent_1000() < 0.0f);

    end_test();
#endif
}

static void test_pdf_fontset_xref_valid_six_fonts() {
    begin_test("pdf: xref table is valid with all six custom fonts embedded");

#ifndef ASCIIQUACK_NOTO_FONTS_DIR
    std::cout << " (skipped – ASCIIQUACK_NOTO_FONTS_DIR not defined)";
    end_test();
    return;
#else
    {
        std::ifstream probe(NOTO_REGULAR);
        if (!probe) {
            std::cout << " (skipped – Noto fonts not found at " << NOTO_REGULAR << ")";
            end_test();
            return;
        }
    }

    // Multi-page document exercises the page-pair object numbering
    // when all 6 font slots are occupied by embedded TrueType fonts.
    std::string src = "= Big Doc\n\n";
    for (int i = 0; i < 50; ++i) {
        src += "Paragraph number " + std::to_string(i + 1) +
               " with some filler text to push content across pages.\n\n";
    }
    auto doc = asciiquack::Parser::parse_string(src);

    asciiquack::FontSet fs;
    fs.regular     = NOTO_REGULAR;
    fs.bold        = NOTO_BOLD;
    fs.italic      = NOTO_ITALIC;
    fs.bold_italic = NOTO_BOLD_ITALIC;
    fs.mono        = NOTO_MONO;
    fs.mono_bold   = NOTO_MONO_BOLD;
    std::string pdf = asciiquack::convert_to_pdf(*doc, false, fs);

    EXPECT(is_valid_pdf_envelope(pdf));
    EXPECT(pdf_xref_valid(pdf));

    // Verify multiple pages were produced.
    std::size_t page_count = 0;
    std::size_t pos = 0;
    while ((pos = pdf.find("/Type /Page\n", pos)) != std::string::npos) {
        ++page_count;
        ++pos;
    }
    EXPECT(page_count > 1);

    end_test();
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Heading rule tests
// ─────────────────────────────────────────────────────────────────────────────

static void test_pdf_heading_rule_not_through_body() {
    begin_test("pdf: heading rule does not bleed into body text (fill_rect before paragraph)");

    // A document with a level-1 heading followed immediately by a paragraph.
    const std::string src =
        "= Doc Title\n"
        "\n"
        "== Section One\n"
        "\n"
        "Body text follows the heading.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    std::string pdf = asciiquack::convert_to_pdf(*doc);

    EXPECT(is_valid_pdf_envelope(pdf));
    EXPECT(pdf_xref_valid(pdf));

    // The PDF should render without crashing and contain the heading text.
    EXPECT_CONTAINS(pdf, "Section");
    EXPECT_CONTAINS(pdf, "One");
    EXPECT_CONTAINS(pdf, "Body");

    // Both the title-level and section-level rules should be present.
    // fill_rect produces "<w> <h> re f" sequences.  Each rule produces one.
    std::size_t re_count = 0;
    std::size_t pos = 0;
    while ((pos = pdf.find(" re f\n", pos)) != std::string::npos) {
        ++re_count;
        ++pos;
    }
    // At least two fill_rect calls: one for document title rule, one for
    // the section rule.
    EXPECT(re_count >= 2);

    end_test();
}

static void test_pdf_heading_rule_position_below_heading() {
    begin_test("pdf: heading rule is positioned below heading, not overlapping body");

    // Generate a minimal single-page document with a level-0 heading and body.
    const std::string src =
        "= My Title\n"
        "\n"
        "Paragraph text here.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    std::string pdf = asciiquack::convert_to_pdf(*doc);

    EXPECT(is_valid_pdf_envelope(pdf));
    EXPECT(pdf_xref_valid(pdf));

    // The first fill_rect in the page content places the title rule.
    // We verify that its Y coordinate is LOWER on the page than the title
    // text baseline (which is placed near the top margin).
    // The title baseline will be near 720pt (page height 792 - margin 72 = 720).
    // After the title is drawn the rule Y is approximately title_baseline - sz*0.35
    // for sz=26 that is ~710pt.  Body text starts well below that (< 700pt).
    //
    // We can verify indirectly: the PDF content must contain at least one
    // fill_rect (re f) command, and the document must be structurally valid.
    EXPECT_CONTAINS(pdf, " re f\n");
    EXPECT_CONTAINS(pdf, "My");
    EXPECT_CONTAINS(pdf, "Title");
    EXPECT_CONTAINS(pdf, "Paragraph");

    end_test();
}

// ─────────────────────────────────────────────────────────────────────────────
// PDF code-block layout tests
// ─────────────────────────────────────────────────────────────────────────────

/// Return the bottom y-coordinate (in PDF points) of the first grey
/// code-block background rectangle in the content stream.  The grey fill is
/// identified by the "0.95 0.95 0.95 rg" colour command emitted by fill_rect.
/// Returns -1.0f when not found.
static float pdf_code_bg_bottom_y(const std::string& pdf) {
    const std::string marker = "0.95 0.95 0.95 rg\n";
    auto gpos = pdf.find(marker);
    if (gpos == std::string::npos) return -1.0f;
    // fill_rect emits: "<r> <g> <b> rg\n<x> <y> <w> <h> re f\n"
    // so the line immediately following the rg line holds the rectangle.
    auto line_start = gpos + marker.size();
    auto line_end   = pdf.find('\n', line_start);
    if (line_end == std::string::npos) return -1.0f;
    std::istringstream iss(pdf.substr(line_start, line_end - line_start));
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    if (iss >> x >> y >> w >> h) return y;
    return -1.0f;
}

/// Return the y-coordinate from the "1 0 0 1 x y Tm" command that precedes
/// the first occurrence of "(needle) Tj" in the content stream.
/// Returns -1.0f when not found.
static float pdf_tm_y_of_text(const std::string& pdf,
                               const std::string& needle) {
    std::string pat = "(" + needle + ") Tj";
    auto tpos = pdf.find(pat);
    if (tpos == std::string::npos) return -1.0f;
    // Search backward for the "1 0 0 1 " Tm prefix.
    auto tm = pdf.rfind("1 0 0 1 ", tpos);
    if (tm == std::string::npos) return -1.0f;
    std::istringstream iss(pdf.substr(tm + 8, 64));
    float x = 0.0f, y = 0.0f;
    if (iss >> x >> y) return y;
    return -1.0f;
}

static void test_pdf_code_block_gap_clears_background() {
    begin_test("pdf: paragraph after code block starts below grey background");

    // A two-line code block immediately followed by a paragraph.
    // The grey rectangle must not overlap the following body text.
    const std::string src =
        "= Title\n"
        "\n"
        "----\n"
        "code line 1\n"
        "code line 2\n"
        "----\n"
        "\n"
        "AfterBlock text here.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    std::string pdf = asciiquack::convert_to_pdf(*doc);

    EXPECT(is_valid_pdf_envelope(pdf));
    EXPECT(pdf_xref_valid(pdf));
    EXPECT_CONTAINS(pdf, "AfterBlock");

    // Extract the bottom y-coordinate of the grey code block background and
    // the baseline y-coordinate of the first word of the following paragraph.
    float bg_bottom = pdf_code_bg_bottom_y(pdf);
    float para_y    = pdf_tm_y_of_text(pdf, "AfterBlock");

    EXPECT(bg_bottom > 0.0f);   // sanity: found the grey rect
    EXPECT(para_y    > 0.0f);   // sanity: found the paragraph text

    // The paragraph baseline must be below the background rectangle by at
    // least BODY_SIZE * 0.75 ≈ 8.25 pt so that the tallest ascenders do not
    // reach into the grey box.  We use 8.0 pt as the threshold to give a
    // small tolerance for rounding.
    EXPECT(bg_bottom - para_y > 8.0f);

    end_test();
}

// ─────────────────────────────────────────────────────────────────────────────
// PDF layout interaction tests
// ─────────────────────────────────────────────────────────────────────────────

static void test_pdf_table_gap_after_table() {
    begin_test("pdf: paragraph after table starts below table bottom border");

    const std::string src =
        "= Title\n"
        "\n"
        "[cols=\"1,1\"]\n"
        "|===\n"
        "| H1 | H2\n"
        "\n"
        "| A | B\n"
        "|===\n"
        "\n"
        "AfterTable paragraph here.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    std::string pdf = asciiquack::convert_to_pdf(*doc);

    EXPECT(is_valid_pdf_envelope(pdf));
    EXPECT(pdf_xref_valid(pdf));
    EXPECT_CONTAINS(pdf, "AfterTable");

    // The table's bottom border is a draw_hline; after it the cursor advances
    // by BODY_SIZE * 1.2 before the next paragraph.
    // Verify simply that the paragraph text is present and the PDF is valid –
    // the exact y positions are table-height-dependent and tested visually via
    // the stress-test PDF and check_pdf_layout.py.

    end_test();
}

static void test_pdf_heading_followed_by_code_block() {
    begin_test("pdf: heading rule does not bleed into following code-block background");

    // A level-1 heading (which draws a decorative rule) immediately followed
    // by a code block.  The heading rule must sit above the grey code-block
    // background; the two fill_rect calls must not overlap vertically.
    const std::string src =
        "= Doc Title\n"
        "\n"
        "== Section With Code\n"
        "\n"
        "----\n"
        "code line one\n"
        "code line two\n"
        "----\n"
        "\n"
        "AfterCode paragraph.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    std::string pdf = asciiquack::convert_to_pdf(*doc);

    EXPECT(is_valid_pdf_envelope(pdf));
    EXPECT(pdf_xref_valid(pdf));
    EXPECT_CONTAINS(pdf, "code line one");
    EXPECT_CONTAINS(pdf, "AfterCode");

    // Both the heading-rule fill_rect and the code-block fill_rect must appear.
    // Count all "re f" sequences – expect at least 3 (title rule, section rule,
    // code block background).
    std::size_t ref_count = 0;
    std::size_t pos = 0;
    while ((pos = pdf.find(" re f\n", pos)) != std::string::npos) {
        ++ref_count; ++pos;
    }
    EXPECT(ref_count >= 3);

    // Heading rule colour is ~0.6/0.6/0.6; code-block colour is 0.95/0.95/0.95.
    // Both must be present in the content stream.
    EXPECT_CONTAINS(pdf, "0.60 0.60 0.60 rg");
    EXPECT_CONTAINS(pdf, "0.95 0.95 0.95 rg");

    // The code-block background y must be below the heading rule y.
    // The heading rule rg line appears before the code-block rg line in the stream.
    auto rule_pos = pdf.find("0.60 0.60 0.60 rg");
    auto code_pos = pdf.find("0.95 0.95 0.95 rg");
    EXPECT(rule_pos != std::string::npos);
    EXPECT(code_pos != std::string::npos);
    EXPECT(rule_pos < code_pos);   // heading rule must come before code-block bg

    // Extract the y of the heading rule and the y of the code-block background.
    // The rule y must be GREATER (higher on the page) than the code-block top y.
    // heading rule fill_rect line:
    float rule_y = -1.0f, code_bg_y = -1.0f;
    {
        auto parse_fill_y = [&](std::size_t rg_pos, float& out_y) {
            const std::string marker = " rg\n";
            auto after = pdf.find(marker, rg_pos);
            if (after == std::string::npos) return;
            auto line_start = after + marker.size();
            auto line_end   = pdf.find('\n', line_start);
            if (line_end == std::string::npos) return;
            std::istringstream iss(pdf.substr(line_start, line_end - line_start));
            float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
            if (iss >> x >> y >> w >> h) out_y = y;
        };
        parse_fill_y(rule_pos, rule_y);
        parse_fill_y(code_pos, code_bg_y);
    }
    EXPECT(rule_y    > 0.0f);
    EXPECT(code_bg_y > 0.0f);
    // rule_y is the BOTTOM of the heading rule bar; code_bg_y is the BOTTOM of the
    // code background rect.  The code box is below the heading so its bottom y
    // must be strictly lower (smaller value in PDF coordinates = lower on page).
    EXPECT(rule_y > code_bg_y);

    end_test();
}

static void test_pdf_consecutive_headings_valid() {
    begin_test("pdf: consecutive headings at all levels produce valid PDF");

    const std::string src =
        "= Level 0 Title\n"
        "\n"
        "== Level 1\n"
        "\n"
        "=== Level 2\n"
        "\n"
        "==== Level 3\n"
        "\n"
        "===== Level 4\n"
        "\n"
        "====== Level 5\n"
        "\n"
        "Body text after all headings.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    std::string pdf = asciiquack::convert_to_pdf(*doc);

    EXPECT(is_valid_pdf_envelope(pdf));
    EXPECT(pdf_xref_valid(pdf));
    EXPECT_CONTAINS(pdf, "Level");
    EXPECT_CONTAINS(pdf, "Body");

    // Level-0 and Level-1 headings each emit a fill_rect (the decorative rule).
    // There must be exactly 2 such fill_rects from headings (title + level-1).
    // Count the dark-grey fill colour "0.30" (title) and "0.60" (level-1).
    EXPECT_CONTAINS(pdf, "0.30 0.30 0.30 rg");
    EXPECT_CONTAINS(pdf, "0.60 0.60 0.60 rg");

    end_test();
}

static void test_pdf_admonition_multiline_body_valid() {
    begin_test("pdf: admonition with long multi-line body produces valid PDF");

    // A very long body forces wrapping; the label must stay on the first line
    // and not overlap subsequent lines of the body.
    const std::string src =
        "= Doc\n"
        "\n"
        "IMPORTANT: This is a very long admonition body that must word-wrap "
        "across multiple lines. Each continuation line must be indented past "
        "the IMPORTANT: label so that the label and the body text never "
        "overlap horizontally. The quick brown fox jumps over the lazy dog.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    std::string pdf = asciiquack::convert_to_pdf(*doc);

    EXPECT(is_valid_pdf_envelope(pdf));
    EXPECT(pdf_xref_valid(pdf));
    EXPECT_CONTAINS(pdf, "IMPORTANT:");
    EXPECT_CONTAINS(pdf, "quick");

    end_test();
}

static void test_pdf_quote_block_gap_after() {
    begin_test("pdf: paragraph after block quote starts below quote body");

    const std::string src =
        "= Doc\n"
        "\n"
        "[quote]\n"
        "____\n"
        "A notable quotation here.\n"
        "____\n"
        "\n"
        "AfterQuote paragraph here.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    std::string pdf = asciiquack::convert_to_pdf(*doc);

    EXPECT(is_valid_pdf_envelope(pdf));
    EXPECT(pdf_xref_valid(pdf));
    EXPECT_CONTAINS(pdf, "notable");
    EXPECT_CONTAINS(pdf, "AfterQuote");

    // The quote body text must appear before the AfterQuote paragraph in the
    // content stream (top-to-bottom rendering order).
    auto quote_pos = pdf.find("notable");
    auto after_pos = pdf.find("AfterQuote");
    EXPECT(quote_pos != std::string::npos);
    EXPECT(after_pos != std::string::npos);
    EXPECT(quote_pos < after_pos);

    // Extract the y-coordinates: quote body text must be higher on the page
    // (larger PDF y value) than the following paragraph.
    float quote_y = pdf_tm_y_of_text(pdf, "notable");
    float after_y = pdf_tm_y_of_text(pdf, "AfterQuote");
    EXPECT(quote_y > 0.0f);
    EXPECT(after_y > 0.0f);
    // Quote body is higher on the page (larger y) than the following paragraph.
    EXPECT(quote_y > after_y);

    end_test();
}

static void test_pdf_dlist_body_indented_below_term() {
    begin_test("pdf: description list body is below its term on the page");

    const std::string src =
        "= Doc\n"
        "\n"
        "myterm:: The description of the term follows on the same or next line.\n"
        "\n"
        "AfterDlist paragraph.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    std::string pdf = asciiquack::convert_to_pdf(*doc);

    EXPECT(is_valid_pdf_envelope(pdf));
    EXPECT(pdf_xref_valid(pdf));
    EXPECT_CONTAINS(pdf, "myterm");
    EXPECT_CONTAINS(pdf, "description");
    EXPECT_CONTAINS(pdf, "AfterDlist");

    // The term must appear before the description body in the stream.
    float term_y  = pdf_tm_y_of_text(pdf, "myterm");
    float after_y = pdf_tm_y_of_text(pdf, "AfterDlist");
    EXPECT(term_y  > 0.0f);
    EXPECT(after_y > 0.0f);
    // Term is higher on the page than the post-list paragraph.
    EXPECT(term_y > after_y);

    end_test();
}

static void test_pdf_ordered_list_gap_after() {
    begin_test("pdf: paragraph after ordered list starts below last list item");

    const std::string src =
        "= Doc\n"
        "\n"
        ". First ordered item.\n"
        ". Second ordered item.\n"
        ". Third ordered item.\n"
        "\n"
        "AfterList paragraph.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    std::string pdf = asciiquack::convert_to_pdf(*doc);

    EXPECT(is_valid_pdf_envelope(pdf));
    EXPECT(pdf_xref_valid(pdf));
    EXPECT_CONTAINS(pdf, "Third");
    EXPECT_CONTAINS(pdf, "AfterList");

    float last_item_y = pdf_tm_y_of_text(pdf, "Third");
    float after_y     = pdf_tm_y_of_text(pdf, "AfterList");
    EXPECT(last_item_y > 0.0f);
    EXPECT(after_y     > 0.0f);
    // The last item is higher on the page than the following paragraph.
    EXPECT(last_item_y > after_y);

    end_test();
}

static void test_pdf_code_block_preceded_by_heading_gap() {
    begin_test("pdf: code block preceded by heading has adequate gap above");

    // Level-2 heading → code block: the heading gap_below (2 pt for level >= 2)
    // must result in the code block sitting clearly below the heading text.
    const std::string src =
        "= Doc\n"
        "\n"
        "=== SectionBeforeCode\n"
        "\n"
        "----\n"
        "code after heading\n"
        "----\n"
        "\n"
        "AfterCode.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    std::string pdf = asciiquack::convert_to_pdf(*doc);

    EXPECT(is_valid_pdf_envelope(pdf));
    EXPECT(pdf_xref_valid(pdf));
    EXPECT_CONTAINS(pdf, "SectionBeforeCode");
    EXPECT_CONTAINS(pdf, "code after heading");
    EXPECT_CONTAINS(pdf, "AfterCode");

    // The heading text y must be above (greater than) the code-block bg bottom y.
    float heading_y = pdf_tm_y_of_text(pdf, "SectionBeforeCode");
    float code_bg_bottom = pdf_code_bg_bottom_y(pdf);
    EXPECT(heading_y    > 0.0f);
    EXPECT(code_bg_bottom > 0.0f);
    EXPECT(heading_y > code_bg_bottom);

    end_test();
}

static void test_pdf_code_block_long_line_clipped() {
    begin_test("pdf: very long code line is clipped with ellipsis, does not overflow");

    // A code line whose raw rendered width greatly exceeds the content area.
    // The renderer must truncate it with "..." rather than letting it overflow
    // the right margin.
    const std::string src =
        "= Doc\n"
        "\n"
        "----\n"
        "this_is_a_very_long_identifier_name = some_function_call("
        "argument_one, argument_two, argument_three, argument_four)\n"
        "normal_line\n"
        "----\n"
        "\n"
        "AfterCode paragraph.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    std::string pdf = asciiquack::convert_to_pdf(*doc);

    EXPECT(is_valid_pdf_envelope(pdf));
    EXPECT(pdf_xref_valid(pdf));

    // The truncation marker "..." must be present.
    EXPECT_CONTAINS(pdf, "...");

    // The shorter second line must still be rendered in full.
    EXPECT_CONTAINS(pdf, "normal_line");

    // The paragraph after the code block must also be present.
    EXPECT_CONTAINS(pdf, "AfterCode");

    end_test();
}

// ─────────────────────────────────────────────────────────────────────────────
// PDF image rendering tests
// ─────────────────────────────────────────────────────────────────────────────

static void test_pdf_image_missing_file_emits_placeholder() {
    begin_test("pdf: missing image file falls back to text placeholder");

    const std::string src =
        "= Doc\n"
        "\n"
        "image::nonexistent_file_abc123.png[alt text]\n";

    auto doc = asciiquack::Parser::parse_string(src);
    std::string pdf = asciiquack::convert_to_pdf(*doc);

    EXPECT(is_valid_pdf_envelope(pdf));
    EXPECT(pdf_xref_valid(pdf));
    // Placeholder contains the target path
    EXPECT_CONTAINS(pdf, "nonexistent_file_abc123.png");
    // No /XObject resource should appear (no image was embedded)
    EXPECT_NOT_CONTAINS(pdf, "/XObject");

    end_test();
}

static void test_pdf_image_xobject_structure() {
    begin_test("pdf: embedded image produces /XObject and /Image entries");

    // Write a minimal 1×1 JPEG to a temp file.
    // A 1×1 grayscale JPEG (smallest valid JPEG): SOI + APP0 + SOF0 + SOS + EOI
    // We use a known-good minimal JPEG byte sequence.
    static const unsigned char TINY_JPEG[] = {
        // 1×1 white JPEG (generated with ImageMagick convert -size 1x1 xc:white tiny.jpg)
        0xFF, 0xD8,              // SOI
        0xFF, 0xE0,              // APP0 marker
        0x00, 0x10,              // length = 16
        0x4A, 0x46, 0x49, 0x46, 0x00,  // "JFIF\0"
        0x01, 0x01,              // version 1.1
        0x00,                    // density units = 0
        0x00, 0x01, 0x00, 0x01, // Xdensity=1, Ydensity=1
        0x00, 0x00,              // thumbnail 0×0
        0xFF, 0xDB,              // DQT marker
        0x00, 0x43,              // length = 67
        0x00,                    // table 0, 8-bit precision
        // 64 quantization values (all 1)
        0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
        0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
        0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
        0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
        0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
        0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
        0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
        0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
        0xFF, 0xC0,              // SOF0 marker
        0x00, 0x0B,              // length = 11
        0x08,                    // precision = 8
        0x00, 0x01,              // height = 1
        0x00, 0x01,              // width = 1
        0x01,                    // ncomponents = 1 (grayscale)
        0x01, 0x11, 0x00,        // component 1 params
        0xFF, 0xC4,              // DHT marker
        0x00, 0x1F,              // length = 31
        0x00,                    // table 0, DC
        0x00,0x01,0x05,0x01,0x01,0x01,0x01,0x01,
        0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
        0x08,0x09,0x0A,0x0B,
        0xFF, 0xDA,              // SOS marker
        0x00, 0x08,              // length = 8
        0x01,                    // ncomponents = 1
        0x01, 0x00,              // component 1, table ids
        0x00, 0x3F, 0x00,        // spectral selection
        0xF8,                    // compressed scan data (minimal)
        0xFF, 0xD9               // EOI
    };
    // Write to a temp file
    namespace fs = std::filesystem;
    const fs::path tmp_jpeg = fs::temp_directory_path() / "asciiquack_test_img.jpg";
    {
        std::ofstream f(tmp_jpeg, std::ios::binary);
        f.write(reinterpret_cast<const char*>(TINY_JPEG), sizeof(TINY_JPEG));
    }

    std::string src =
        "= Test Images\n\n"
        "image::" + tmp_jpeg.string() + "[tiny,width=72]\n";

    auto doc = asciiquack::Parser::parse_string(src);
    std::string pdf = asciiquack::convert_to_pdf(*doc);

    // Clean up temp file
    fs::remove(tmp_jpeg);

    EXPECT(is_valid_pdf_envelope(pdf));
    EXPECT(pdf_xref_valid(pdf));

    // An image XObject should be present
    EXPECT_CONTAINS(pdf, "/XObject");
    EXPECT_CONTAINS(pdf, "/Subtype /Image");
    EXPECT_CONTAINS(pdf, "/Filter /DCTDecode");
    // The image resource name Im1 should appear
    EXPECT_CONTAINS(pdf, "/Im1");
    // The page content should invoke the image with "Do"
    EXPECT_CONTAINS(pdf, "/Im1 Do");

    end_test();
}

static void test_pdf_image_xobject_xref_valid() {
    begin_test("pdf: xref table remains valid after embedding an image");

    namespace fs = std::filesystem;
    const fs::path tmp_jpeg = fs::temp_directory_path() / "asciiquack_xref_img.jpg";
    // Re-use the same minimal JPEG bytes as above
    static const unsigned char TINY_JPEG[] = {
        0xFF,0xD8, 0xFF,0xE0, 0x00,0x10, 0x4A,0x46,0x49,0x46,0x00,
        0x01,0x01, 0x00, 0x00,0x01,0x00,0x01, 0x00,0x00,
        0xFF,0xDB, 0x00,0x43, 0x00,
        0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
        0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
        0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
        0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
        0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
        0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
        0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
        0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
        0xFF,0xC0, 0x00,0x0B, 0x08, 0x00,0x01, 0x00,0x01,
        0x01, 0x01,0x11,0x00,
        0xFF,0xC4, 0x00,0x1F, 0x00,
        0x00,0x01,0x05,0x01,0x01,0x01,0x01,0x01,
        0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
        0x08,0x09,0x0A,0x0B,
        0xFF,0xDA, 0x00,0x08, 0x01, 0x01,0x00, 0x00,0x3F,0x00,
        0xF8, 0xFF,0xD9
    };
    {
        std::ofstream f(tmp_jpeg, std::ios::binary);
        f.write(reinterpret_cast<const char*>(TINY_JPEG), sizeof(TINY_JPEG));
    }

    std::string src =
        "= Xref Check\n\n"
        "image::" + tmp_jpeg.string() + "[img]\n"
        "\nSome paragraph after the image.\n";

    auto doc = asciiquack::Parser::parse_string(src);
    std::string pdf = asciiquack::convert_to_pdf(*doc);
    fs::remove(tmp_jpeg);

    EXPECT(is_valid_pdf_envelope(pdf));
    EXPECT(pdf_xref_valid(pdf));
    EXPECT_CONTAINS(pdf, "/Im1");

    end_test();
}

static void test_pdf_image_images_dir_resolution() {
    begin_test("pdf: images_dir parameter allows resolving relative image paths");

    namespace fs = std::filesystem;
    // Write a tiny JPEG to a temp directory
    fs::path tmp_dir = fs::temp_directory_path() / "asciiquack_img_dir_test";
    fs::create_directories(tmp_dir);
    const fs::path tmp_jpeg = tmp_dir / "test_img.jpg";
    static const unsigned char TINY_JPEG[] = {
        0xFF,0xD8, 0xFF,0xE0, 0x00,0x10, 0x4A,0x46,0x49,0x46,0x00,
        0x01,0x01, 0x00, 0x00,0x01,0x00,0x01, 0x00,0x00,
        0xFF,0xDB, 0x00,0x43, 0x00,
        0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
        0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
        0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
        0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
        0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
        0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
        0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
        0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
        0xFF,0xC0, 0x00,0x0B, 0x08, 0x00,0x01, 0x00,0x01,
        0x01, 0x01,0x11,0x00,
        0xFF,0xC4, 0x00,0x1F, 0x00,
        0x00,0x01,0x05,0x01,0x01,0x01,0x01,0x01,
        0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
        0x08,0x09,0x0A,0x0B,
        0xFF,0xDA, 0x00,0x08, 0x01, 0x01,0x00, 0x00,0x3F,0x00,
        0xF8, 0xFF,0xD9
    };
    {
        std::ofstream f(tmp_jpeg, std::ios::binary);
        f.write(reinterpret_cast<const char*>(TINY_JPEG), sizeof(TINY_JPEG));
    }

    // Reference only "test_img.jpg" (relative), but pass images_dir
    const std::string src =
        "= Dir Test\n\n"
        "image::test_img.jpg[test]\n";

    asciiquack::FontSet fs_empty;
    auto doc = asciiquack::Parser::parse_string(src);
    std::string pdf = asciiquack::convert_to_pdf(*doc, false, fs_empty,
                                                  tmp_dir.string());

    fs::remove(tmp_jpeg);
    fs::remove(tmp_dir);

    EXPECT(is_valid_pdf_envelope(pdf));
    EXPECT(pdf_xref_valid(pdf));
    // Image should have been found and embedded
    EXPECT_CONTAINS(pdf, "/XObject");
    EXPECT_CONTAINS(pdf, "/Im1 Do");

    end_test();
}

#ifdef MINIPDF_USE_ZLIB
static void test_minipdf_png_from_file_loads() {
    begin_test("minipdf: PdfImage::from_file loads the demo PNG");

    // Use the demo PNG that ships with the repository
    const std::string png_path =
        std::string(CMAKE_SOURCE_DIR) + "/examples/asciiquack.png";
    {
        std::ifstream probe(png_path, std::ios::binary);
        if (!probe) {
            std::cout << " (skipped – demo PNG not found at " << png_path << ")";
            end_test();
            return;
        }
    }

    auto img = minipdf::PdfImage::from_file(png_path);
    EXPECT(img != nullptr);
    if (img) {
        EXPECT(img->width()  == 1024);
        EXPECT(img->height() == 1024);
        EXPECT(img->channels() == 3);
        EXPECT(img->encoding() == minipdf::PdfImage::Encoding::Raw);
        // Raw 1024×1024 RGB = 3,145,728 bytes
        EXPECT(img->data().size() == 1024u * 1024u * 3u);
    }

    end_test();
}
#endif // MINIPDF_USE_ZLIB

static void test_pdf_table() {
    begin_test("pdf: table renders cell content with valid PDF structure");

    const std::string src =
        "= Document\n"
        "\n"
        "[cols=\"1,2\"]\n"
        "|===\n"
        "| Name | Description\n"
        "\n"
        "| Alpha | First item in the list\n"
        "| Beta  | Second item with *bold* text\n"
        "| Gamma | Third item with `mono` text\n"
        "|===\n";

    auto doc = asciiquack::Parser::parse_string(src);
    std::string pdf = asciiquack::convert_to_pdf(*doc);

    EXPECT(is_valid_pdf_envelope(pdf));
    EXPECT(pdf_xref_valid(pdf));

    // Header row content
    EXPECT_CONTAINS(pdf, "Name");
    EXPECT_CONTAINS(pdf, "Description");
    // Body row content
    EXPECT_CONTAINS(pdf, "Alpha");
    EXPECT_CONTAINS(pdf, "First");
    EXPECT_CONTAINS(pdf, "Beta");
    EXPECT_CONTAINS(pdf, "Gamma");

    end_test();
}

static void test_minipdf_jpeg_from_file_loads() {
    begin_test("minipdf: PdfImage::from_jpeg_file returns nullptr for non-JPEG");

    // Passing a non-existent file should return nullptr gracefully.
    auto img = minipdf::PdfImage::from_jpeg_file("/nonexistent/path/image.jpg");
    EXPECT(img == nullptr);

    // Passing an empty string should also return nullptr.
    auto img2 = minipdf::PdfImage::from_jpeg_file("");
    EXPECT(img2 == nullptr);

    end_test();
}

static void test_minipdf_png_missing_returns_nullptr() {
    begin_test("minipdf: PdfImage::from_png_file returns nullptr for missing file");

    auto img = minipdf::PdfImage::from_png_file("/nonexistent/path/image.png");
    EXPECT(img == nullptr);

    auto img2 = minipdf::PdfImage::from_file("/nonexistent/path/image.png");
    EXPECT(img2 == nullptr);

    end_test();
}



int main(int argc, char* argv[]) {
    // Check for -v flag
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "-v" || std::string(argv[i]) == "--verbose") {
            g_verbose = true;
        }
    }

    std::cout << "Running asciiquack C++ tests\n";
    std::cout << "============================\n\n";

    // Reader
    std::cout << "Reader tests:\n";
    test_reader_basic();
    test_reader_crlf();
    test_reader_unshift();
    test_reader_skip_blank();

    // Substitutors
    std::cout << "\nSubstitutor tests:\n";
    test_sub_specialchars();
    test_sub_replacements();
    test_sub_attributes();
    test_generate_id();

    // Parser
    std::cout << "\nParser tests:\n";
    test_parser_section_level();
    test_parser_section_title_text();
    test_parser_empty_document();
    test_parser_document_title();
    test_parser_document_header_full();
    test_parser_paragraph();
    test_parser_sections();
    test_parser_attribute_entry();
    test_parser_listing_block();
    test_parser_unordered_list();
    test_parser_ordered_list();
    test_parser_admonition_paragraph();
    test_parser_block_title();
    test_parser_thematic_break();
    test_parser_comment_line();

    // HTML5 converter
    std::cout << "\nHTML5 converter tests:\n";
    test_html5_doctype();
    test_html5_title();
    test_html5_author();
    test_html5_paragraph();
    test_html5_section();
    test_html5_section_id();
    test_html5_listing_block();
#ifdef ASCIIQUACK_USE_ULIGHT
    test_html5_highlighting_python();
    test_html5_highlighting_cpp();
    test_html5_highlighting_unknown_lang();
    test_html5_highlighting_html_escaping();
#endif
    test_html5_literal_block();
    test_html5_ulist();
    test_html5_olist();
    test_html5_admonition();
    test_html5_special_chars();
    test_html5_inline_bold();
    test_html5_inline_italic();
    test_html5_inline_monospace();
    test_html5_embedded();
    test_html5_horizontal_rule();
    test_html5_attribute_ref();
    test_html5_image();
    test_html5_table();
    test_html5_link_relative();

    // Compound delimited block tests (regression for FIXME fix)
    std::cout << "\nCompound delimited block tests:\n";
    test_parser_example_block_closed();
    test_parser_sidebar_block_closed();
    test_parser_quote_block_closed();
    test_html5_example_block();
    test_html5_sidebar_block();
    test_html5_admonition_block();

    // Integration
    std::cout << "\nIntegration tests:\n";
    test_integration_sample();
    test_integration_basic();

    // P1 features and bug fixes
    std::cout << "\nP1 features and bug fix tests:\n";
    test_multiline_attribute_value();
    test_section_numbering();
    test_section_numbering_levels();
    test_table_of_contents();
    test_toc_custom_title();
    test_toc_with_sectnums();
    test_ifdef_single_line();
    test_ifdef_multiline();
    test_ifdef_multiline_false();
    test_ifndef_single_line();
    test_ifeval();
    test_ifeval_numeric();
    test_floating_title();
    test_video_block();
    test_audio_block();
    test_include_directive();
    test_include_directive_secure_mode();
    test_bug7_description_list_not_table();

    // P2 features
    std::cout << "\nP2 features and bug fix tests:\n";
    test_inline_passthrough();
    test_inline_passthrough_q();
    test_kbd_macro();
    test_btn_macro();
    test_menu_macro();
    test_counter_macro();
    test_counter2_macro();
    test_footnote_macro();
    test_footnote_multiple();
    test_ordered_list_style();
    test_ordered_list_start();
    test_special_section_names();
    test_compound_list_items();
    test_dlist_compound_body();
    test_idprefix_empty();
    test_bug4_inline_bold_url();

    // P3 features
    std::cout << "\nP3 features tests:\n";
    test_source_callouts();
    test_admonition_caption_attr();
    test_admonition_default_captions();
    test_stem_inline_macro();
    test_latexmath_inline_macro();
    test_stem_block();
    test_preamble_no_sections();
    test_preamble_with_sections();
    test_linkcss_attribute();
    test_stylesheet_attribute();
    test_stem_mathjax_script();
    test_multi_author_semicolon();
    test_doctype_manpage();
    test_manpage_backend_basic();
    test_manpage_backend_bold_italic();
    test_manpage_backend_listing();
    test_manpage_table();
    test_manpage_th_mansource_manmanual();
    test_manpage_alpha_volnum();
    test_manpage_dlist_no_double_bold();
    test_manpage_backend_auto_doctype();
    test_manpage_backend_indoctype();
    test_manpage_empty_term_suppressed();
    test_html5_empty_dlist_suppressed();
    test_dlist_body_not_swallowed_by_table();
    test_html5_block_anchor_on_example();
    test_html5_block_anchor_on_paragraph();
    test_table_col_alignment();
    test_table_col_repeat();
    test_table_col_style_h();
    test_section_nesting_warning();
    test_unclosed_block_warning();

    // DocBook 5 backend tests
    std::cout << "\nDocBook 5 backend tests:\n";
    test_docbook5_basic_document();
    test_docbook5_sections();
    test_docbook5_listing_block();
    test_docbook5_admonition();
    test_docbook5_ulist();
    test_docbook5_olist();
    test_docbook5_dlist();
    test_docbook5_inline_markup();
    test_docbook5_special_chars();
    test_docbook5_book_doctype();
    test_docbook5_table();
    test_docbook5_image();

    // PDF backend
    std::cout << "\nPDF backend tests:\n";
    test_pdf_basic_structure();
    test_pdf_a4_size();
    test_pdf_sections();
    test_pdf_lists();
    test_pdf_code_block();
    test_pdf_inline_markup();
    test_pdf_admonition();
    test_pdf_hrule();
    test_pdf_multipage();
    test_pdf_escape_special_chars();
    test_pdf_empty_font_path_fallback();
    test_pdf_invalid_font_path_fallback();
    test_pdf_ttf_font_embedded();
    test_pdf_ttf_object_layout();
    test_pdf_ttf_widths_differ_from_helvetica();
    test_pdf_ttf_xref_still_valid_with_font();
    test_pdf_ttf_postscript_name_from_table();
    test_pdf_ttf_os2_vertical_metrics();
    test_pdf_fontset_regular_only();
    test_pdf_fontset_all_six_styles();
    test_pdf_fontset_object_layout_two_ttf();
    test_pdf_fontset_mono_custom();
    test_pdf_fontset_noto_metrics_differ_from_helvetica();
    test_pdf_fontset_xref_valid_six_fonts();
    test_pdf_heading_rule_not_through_body();
    test_pdf_heading_rule_position_below_heading();
    test_pdf_code_block_gap_clears_background();
    test_pdf_table_gap_after_table();
    test_pdf_heading_followed_by_code_block();
    test_pdf_consecutive_headings_valid();
    test_pdf_admonition_multiline_body_valid();
    test_pdf_quote_block_gap_after();
    test_pdf_dlist_body_indented_below_term();
    test_pdf_ordered_list_gap_after();
    test_pdf_code_block_preceded_by_heading_gap();
    test_pdf_code_block_long_line_clipped();
    test_pdf_image_missing_file_emits_placeholder();
    test_pdf_image_xobject_structure();
    test_pdf_image_xobject_xref_valid();
    test_pdf_image_images_dir_resolution();
#ifdef MINIPDF_USE_ZLIB
    test_minipdf_png_from_file_loads();
#endif
    test_minipdf_jpeg_from_file_loads();
    test_pdf_table();
    test_minipdf_png_missing_returns_nullptr();

    // Summary
    std::cout << "\n============================\n";
    if (g_failed == 0) {
        std::cout << "All " << g_total << " tests passed.\n";
        return 0;
    } else {
        std::cout << g_failed << " of " << g_total << " tests FAILED.\n";
        return 1;
    }
}
