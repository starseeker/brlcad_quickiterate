/// @file bench_asciiquack.cpp
/// @brief In-process benchmark for the asciiquack C++17 AsciiDoc processor.
///
/// Measures parse-and-convert throughput over many iterations so that
/// process-startup overhead does not distort the results.
///
/// Build (from repo root, after cmake --build build):
///   cmake --build build --target bench_asciiquack
///
/// Run:
///   ./build/bench_asciiquack [file] [iterations]
///   ./build/bench_asciiquack [directory] [iterations]
///
/// Defaults: benchmark/sample-data/mdbasics.adoc, 1000 iterations.
/// When a directory is given, all *.adoc files inside it (recursively)
/// are each converted once per iteration round, so the per-round cost
/// equals the cost of converting the whole corpus.

#include "document.hpp"
#include "html5.hpp"
#include "manpage.hpp"
#include "parser.hpp"
#include "reader.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ── Active backend description ────────────────────────────────────────────────
// Compose a one-line description of which optional backends are compiled in.
// This appears in benchmark output so results are self-documenting.
static std::string backend_label() {
    std::string label;
#ifdef AQREGEX_USE_PCRE2
#  ifdef AQREGEX_PCRE2_SYSTEM
    label += "system PCRE2 (JIT)";
#  else
    label += "embedded PCRE2 (no JIT)";
#  endif
#else
    label += "std::regex";
#endif

#ifdef ASCIIQUACK_SCANNER_PARSER
#  ifdef ASCIIQUACK_HAND_BLOCK_SCANNER
    label += " + hand-written block scanner";
#  else
    label += " + re2c block scanner";
#  endif
#endif
#ifdef ASCIIQUACK_USE_INLINE_SCANNER
    label += " + inline scanner";
#endif
    return label;
}

static std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        throw std::runtime_error("cannot open: " + path);
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Run one timed parse+convert of content using the appropriate backend.
static void convert_once(const std::string& content,
                         const std::string& path,
                         asciiquack::ParseOptions& opts)
{
    auto doc = asciiquack::Parser::parse_string(content, opts, path);
    if (doc->doctype() == "manpage") {
        (void)asciiquack::convert_to_manpage(*doc);
    } else {
        (void)asciiquack::convert_to_html5(*doc);
    }
}

int main(int argc, char* argv[]) {
    const std::string arg1 = (argc > 1) ? argv[1]
                                        : "benchmark/sample-data/mdbasics.adoc";
    const int n = (argc > 2) ? std::stoi(argv[2]) : 1000;

    asciiquack::ParseOptions opts;
    opts.safe_mode = asciiquack::SafeMode::Safe;
    opts.attributes["embedded"] = "";

    // ── Single-file mode ──────────────────────────────────────────────────────
    if (!fs::is_directory(arg1)) {
        std::string content;
        try {
            content = read_file(arg1);
        } catch (const std::exception& ex) {
            std::cerr << "bench_asciiquack: " << ex.what() << "\n";
            return 1;
        }

        // Warm-up
        for (int i = 0; i < 10; ++i) { convert_once(content, arg1, opts); }

        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < n; ++i) { convert_once(content, arg1, opts); }
        auto t1 = std::chrono::high_resolution_clock::now();

        double total_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        double avg_ms   = total_ms / n;

        std::cout << "asciiquack (C++17):\n";
        std::cout << "  backend    : " << backend_label() << "\n";
        std::cout << "  file       : " << arg1  << "\n";
        std::cout << "  iterations : " << n     << "\n";
        std::cout << "  total      : " << total_ms << " ms\n";
        std::cout << "  average    : " << avg_ms   << " ms/iter\n";
        std::cout << "  throughput : "
                  << static_cast<int>(1000.0 / avg_ms) << " conversions/sec\n";
        return 0;
    }

    // ── Directory / corpus mode ───────────────────────────────────────────────
    std::vector<std::string> paths;
    for (auto& entry : fs::recursive_directory_iterator(arg1)) {
        if (entry.path().extension() == ".adoc") {
            paths.push_back(entry.path().string());
        }
    }
    std::sort(paths.begin(), paths.end());

    if (paths.empty()) {
        std::cerr << "bench_asciiquack: no .adoc files found in " << arg1 << "\n";
        return 1;
    }

    // Pre-load all files into memory so disk I/O is not measured.
    struct FileEntry { std::string path; std::string content; };
    std::vector<FileEntry> corpus;
    corpus.reserve(paths.size());
    for (auto& p : paths) {
        try {
            corpus.push_back({p, read_file(p)});
        } catch (...) {}  // skip unreadable files
    }

    std::cout << "asciiquack corpus benchmark:\n";
    std::cout << "  backend    : " << backend_label() << "\n";
    std::cout << "  corpus     : " << arg1 << "\n";
    std::cout << "  files      : " << corpus.size() << " .adoc files\n";
    std::cout << "  iterations : " << n << " rounds (each round converts all files)\n";

    // Warm-up (2 rounds)
    for (int i = 0; i < 2; ++i) {
        for (auto& fe : corpus) { convert_once(fe.content, fe.path, opts); }
    }

    auto t0 = std::chrono::high_resolution_clock::now();
    long long total_conversions = 0;
    for (int i = 0; i < n; ++i) {
        for (auto& fe : corpus) {
            convert_once(fe.content, fe.path, opts);
            ++total_conversions;
        }
    }
    auto t1 = std::chrono::high_resolution_clock::now();

    double total_ms     = std::chrono::duration<double, std::milli>(t1 - t0).count();
    double avg_round_ms = total_ms / n;
    double avg_file_us  = total_ms * 1000.0 / static_cast<double>(total_conversions);

    std::cout << "  total      : " << static_cast<long long>(total_ms) << " ms\n";
    std::cout << "  per round  : " << static_cast<int>(avg_round_ms)   << " ms\n";
    std::cout << "  per file   : " << static_cast<int>(avg_file_us)    << " µs\n";
    std::cout << "  throughput : "
              << static_cast<int>(1000.0 / avg_file_us * 1000.0)
              << " files/sec\n";
    return 0;
}
