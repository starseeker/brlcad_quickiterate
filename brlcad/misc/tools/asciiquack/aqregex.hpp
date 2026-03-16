/// @file aqregex.hpp
/// @brief Compile-time switchable regex backend for asciiquack.
///
/// Three backends, selected by CMake (see CMakeLists.txt):
///
///   AQREGEX_USE_PCRE2 + AQREGEX_PCRE2_SYSTEM
///       System-installed libpcre2-8 (includes JIT; fastest).
///       CMake: -DUSE_PCRE2=ON  (and libpcre2-dev is found)
///
///   AQREGEX_USE_PCRE2  (without AQREGEX_PCRE2_SYSTEM)
///       Embedded PCRE2 subset in vendor/pcre2/ (no JIT; ~3× faster than
///       std::regex; zero external dependency).
///       CMake: -DUSE_PCRE2=ON  (when libpcre2-dev is absent)
///              or -DUSE_SYSTEM_PCRE2=OFF  (to force embedded even when the
///              system library is present)
///
///   (neither)
///       std::regex fallback (slowest; always available).
///       CMake: -DUSE_PCRE2=OFF
///
/// RE2 is intentionally not provided as a drop-in backend because several
/// patterns in asciiquack rely on features RE2 omits for safety:
///   - Backreferences           e.g. ([-*_]) … \1
///   - Positive lookahead       e.g. (?=[^*\w]|$)
///   - Negative lookahead       e.g. (?!//[^/])
///
/// Public interface (namespace aqrx):
///   aqrx::regex            – compiled pattern
///   aqrx::smatch           – match result (groups + position/length)
///   aqrx::sregex_iterator  – forward iterator over all non-overlapping matches
///   aqrx::regex_replace()  – global substitution
///   aqrx::regex_match()    – full-string match
///   aqrx::ECMAScript       – flag constant (no-op in PCRE2 mode)
///   aqrx::optimize         – flag constant (enables JIT in system-PCRE2 mode)

#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// PCRE2 backend  (system library OR embedded vendor subset)
// ─────────────────────────────────────────────────────────────────────────────
#ifdef AQREGEX_USE_PCRE2

#define PCRE2_CODE_UNIT_WIDTH 8
#ifdef AQREGEX_PCRE2_SYSTEM
#  include <pcre2.h>   // system-installed header
#else
#  include "vendor/pcre2_embed.h"   // embedded single-header amalgamation
#endif

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace aqrx {

// ── Flag constants (accepted for source-level compatibility; values ignored) ──
constexpr unsigned ECMAScript = 0u;
constexpr unsigned optimize   = 0u;

// ─────────────────────────────────────────────────────────────────────────────
// sub_match  – one capture group in a match result
// ─────────────────────────────────────────────────────────────────────────────
struct sub_match {
    std::string _str;
    bool        matched = false;

    const std::string& str() const noexcept { return _str; }
};

// ─────────────────────────────────────────────────────────────────────────────
// smatch  – result of a single match operation
// ─────────────────────────────────────────────────────────────────────────────
class smatch {
public:
    std::vector<sub_match> groups_;  ///< [0]=full match, [1..n]=captures
    std::size_t position_ = 0;
    std::size_t length_   = 0;

    std::size_t position() const noexcept { return position_; }
    std::size_t length()   const noexcept { return length_; }

    const sub_match& operator[](std::size_t i) const noexcept {
        static const sub_match empty{};
        return (i < groups_.size()) ? groups_[i] : empty;
    }

    bool empty() const noexcept { return groups_.empty(); }
};

// ─────────────────────────────────────────────────────────────────────────────
// regex  – RAII wrapper around a compiled PCRE2 pattern
// ─────────────────────────────────────────────────────────────────────────────
class regex {
public:
    regex() = default;

    /// Compile @p pattern.  @p flags is accepted for API compatibility but
    /// ignored; JIT is enabled automatically for the system-library backend.
    explicit regex(const std::string& pattern, unsigned /*flags*/ = 0) {
        compile(pattern.c_str());
    }
    explicit regex(const char* pattern, unsigned /*flags*/ = 0) {
        compile(pattern);
    }

    ~regex() { if (code_) { pcre2_code_free_8(code_); } }

    regex(const regex&)            = delete;
    regex& operator=(const regex&) = delete;

    regex(regex&& o) noexcept
        : code_(o.code_), capture_count_(o.capture_count_)
    { o.code_ = nullptr; o.capture_count_ = 0; }

    regex& operator=(regex&&) = delete;

    bool     valid()         const noexcept { return code_ != nullptr; }
    uint32_t capture_count() const noexcept { return capture_count_; }
    const pcre2_code_8* code() const noexcept { return code_; }

private:
    pcre2_code_8* code_          = nullptr;
    uint32_t      capture_count_ = 0;

    void compile(const char* pattern) {
        int        err    = 0;
        PCRE2_SIZE erroff = 0;
        code_ = pcre2_compile_8(
            reinterpret_cast<PCRE2_SPTR8>(pattern),
            PCRE2_ZERO_TERMINATED,
            0,           // no special flags; basic PCRE2 dialect
            &err, &erroff, nullptr);
        if (!code_) {
            char buf[256];
            pcre2_get_error_message_8(err,
                reinterpret_cast<PCRE2_UCHAR8*>(buf), sizeof(buf));
            throw std::runtime_error(
                std::string("aqrx::regex compile error: ") + buf +
                " at offset " + std::to_string(erroff) +
                " in pattern: " + pattern);
        }
        pcre2_pattern_info_8(code_, PCRE2_INFO_CAPTURECOUNT, &capture_count_);
#ifdef AQREGEX_PCRE2_SYSTEM
        // JIT-compile for speed.  Non-fatal: JIT may be unavailable on some
        // platforms; the interpreter fallback is used in that case.
        pcre2_jit_compile_8(code_, PCRE2_JIT_COMPLETE);
#endif
        // Embedded PCRE2 is built without SUPPORT_JIT so we skip the JIT call;
        // pcre2_jit_compile_8 would just return PCRE2_ERROR_JIT_BADOPTION.
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────
namespace detail {

/// Run a PCRE2 match and populate @p m on success.
/// @p extra_flags may include e.g. PCRE2_ANCHORED | PCRE2_ENDANCHORED.
inline bool do_match(const regex& rx,
                     std::string_view subject,
                     std::size_t offset,
                     smatch& m,
                     uint32_t extra_flags = 0)
{
    if (!rx.valid()) { return false; }

    pcre2_match_data_8* md =
        pcre2_match_data_create_from_pattern_8(rx.code(), nullptr);
    if (!md) { return false; }

    int rc = pcre2_match_8(
        rx.code(),
        reinterpret_cast<PCRE2_SPTR8>(subject.data()), subject.size(),
        offset,
        extra_flags,
        md, nullptr);

    if (rc < 0) {
        pcre2_match_data_free_8(md);
        return false;
    }

    PCRE2_SIZE* ov    = pcre2_get_ovector_pointer_8(md);
    uint32_t    total = rx.capture_count() + 1u;  // group 0 + all captures

    m.groups_.resize(total);
    m.position_ = static_cast<std::size_t>(ov[0]);
    m.length_   = static_cast<std::size_t>(ov[1] - ov[0]);

    for (uint32_t i = 0; i < total; ++i) {
        if (ov[2u * i] != PCRE2_UNSET) {
            m.groups_[i].matched = true;
            m.groups_[i]._str.assign(
                subject.data() + ov[2u * i],
                ov[2u * i + 1u] - ov[2u * i]);
        } else {
            m.groups_[i].matched = false;
            m.groups_[i]._str.clear();
        }
    }

    pcre2_match_data_free_8(md);
    return true;
}

} // namespace detail

// ─────────────────────────────────────────────────────────────────────────────
// regex_match  – full-string match
// ─────────────────────────────────────────────────────────────────────────────

/// Returns true iff @p rx matches the entirety of @p text.
/// On success @p m is populated with groups, position, and length.
inline bool regex_match(const std::string& text, smatch& m, const regex& rx) {
    std::string_view sv(text.data(), text.size());
    if (!detail::do_match(rx, sv, 0, m,
                          PCRE2_ANCHORED | PCRE2_ENDANCHORED)) {
        m.groups_.clear();
        m.position_ = 0;
        m.length_   = 0;
        return false;
    }
    // Verify the match covers the whole string.
    return (m.position_ == 0 && m.length_ == text.size());
}

/// Returns true iff @p rx matches the entirety of @p text (no match object).
inline bool regex_match(const std::string& text, const regex& rx) {
    smatch m;
    return regex_match(text, m, rx);
}

// ─────────────────────────────────────────────────────────────────────────────
// regex_replace  – global substitution
// ─────────────────────────────────────────────────────────────────────────────

/// Replace all non-overlapping matches of @p rx in @p text with @p replacement.
/// The replacement string uses PCRE2 extended format: $0 (full match),
/// $1, $2, … (captured groups) – compatible with std::regex $1, $2 syntax.
inline std::string regex_replace(const std::string& text,
                                 const regex&       rx,
                                 const std::string& replacement)
{
    if (!rx.valid()) { return text; }

    // Estimate output size; resize and retry with exponential growth if the
    // buffer is too small.  Some PCRE2 builds (e.g. 10.42) return PCRE2_UNSET
    // (UINT64_MAX) for outlen on PCRE2_ERROR_NOMEMORY rather than the required
    // size, so we must not rely on that hint being useful.
    std::size_t buf_size = text.size() * 2u + 64u;
    std::string out;
    out.resize(buf_size);
    PCRE2_SIZE outlen = static_cast<PCRE2_SIZE>(buf_size);

    int rc = pcre2_substitute_8(
        rx.code(),
        reinterpret_cast<PCRE2_SPTR8>(text.data()), text.size(),
        0,
        PCRE2_SUBSTITUTE_GLOBAL | PCRE2_SUBSTITUTE_EXTENDED,
        nullptr, nullptr,
        reinterpret_cast<PCRE2_SPTR8>(replacement.data()), replacement.size(),
        reinterpret_cast<PCRE2_UCHAR8*>(out.data()), &outlen);

    // Retry loop: grow the buffer exponentially until pcre2_substitute succeeds.
    // We cap at 16 doublings (~64× original size) to avoid unbounded growth.
    for (int attempt = 0; rc == PCRE2_ERROR_NOMEMORY && attempt < 16; ++attempt) {
        // If PCRE2 set outlen to a sensible (non-sentinel) required size, use
        // it directly; otherwise fall back to 4× growth.
        std::size_t new_size;
        if (outlen != PCRE2_UNSET && outlen > buf_size) {
            new_size = static_cast<std::size_t>(outlen);
        } else {
            new_size = buf_size * 4u;
        }
        buf_size = new_size;
        try {
            out.resize(buf_size);
        } catch (const std::length_error&) {
            return text;  // truly cannot allocate; return input unchanged
        }
        outlen = static_cast<PCRE2_SIZE>(buf_size);
        rc = pcre2_substitute_8(
            rx.code(),
            reinterpret_cast<PCRE2_SPTR8>(text.data()), text.size(),
            0,
            PCRE2_SUBSTITUTE_GLOBAL | PCRE2_SUBSTITUTE_EXTENDED,
            nullptr, nullptr,
            reinterpret_cast<PCRE2_SPTR8>(replacement.data()), replacement.size(),
            reinterpret_cast<PCRE2_UCHAR8*>(out.data()), &outlen);
    }

    if (rc < 0) { return text; }  // on unexpected error, return original

    out.resize(outlen);
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// sregex_iterator  – iterate over all non-overlapping matches in a string
// ─────────────────────────────────────────────────────────────────────────────

/// Forward iterator that yields an aqrx::smatch for every non-overlapping
/// occurrence of @p rx in the string range [begin, end).
///
/// The string must remain valid and unmodified for the iterator's lifetime.
class sregex_iterator {
public:
    /// End-of-sequence sentinel (default construction).
    sregex_iterator() = default;

    /// Begin iterator over the string range [begin, end).
    sregex_iterator(std::string::const_iterator begin,
                    std::string::const_iterator end,
                    const regex& rx)
    {
        if (begin == end) { return; }  // empty string → at end
        // Obtain a string_view over the original storage without copying.
        data_ = std::addressof(*begin);
        size_ = static_cast<std::size_t>(end - begin);
        rx_   = std::addressof(rx);
        advance(0);
    }

    const smatch& operator*()  const noexcept { return current_; }
    const smatch* operator->() const noexcept { return &current_; }

    sregex_iterator& operator++() {
        // Advance past the current match; handle zero-length match.
        std::size_t next = current_.position_ + current_.length_;
        if (current_.length_ == 0) { ++next; }
        advance(next);
        return *this;
    }

    bool operator==(const sregex_iterator& other) const noexcept {
        if (at_end_ && other.at_end_) { return true; }
        if (at_end_ != other.at_end_) { return false; }
        return (data_ == other.data_) &&
               (next_start_ == other.next_start_);
    }
    bool operator!=(const sregex_iterator& other) const noexcept {
        return !(*this == other);
    }

private:
    const char*  data_       = nullptr;
    std::size_t  size_       = 0;
    const regex* rx_         = nullptr;
    smatch       current_;
    std::size_t  next_start_ = 0;
    bool         at_end_     = true;

    void advance(std::size_t offset) {
        if (!rx_ || offset > size_) { at_end_ = true; return; }
        std::string_view sv(data_, size_);
        if (!detail::do_match(*rx_, sv, offset, current_)) {
            at_end_ = true;
            return;
        }
        at_end_     = false;
        next_start_ = current_.position_ + current_.length_;
    }
};

} // namespace aqrx

// ─────────────────────────────────────────────────────────────────────────────
// std::regex fallback
// ─────────────────────────────────────────────────────────────────────────────
#else  // !AQREGEX_USE_PCRE2

#include <regex>

namespace aqrx {

using regex           = std::regex;
using smatch          = std::smatch;
using sregex_iterator = std::sregex_iterator;

using std::regex_replace;
using std::regex_match;

// Expose flag constants under the aqrx namespace so callers can write
// aqrx::ECMAScript | aqrx::optimize without #if guards.
constexpr auto ECMAScript = std::regex::ECMAScript;
constexpr auto optimize   = std::regex::optimize;

} // namespace aqrx

#endif // AQREGEX_USE_PCRE2
