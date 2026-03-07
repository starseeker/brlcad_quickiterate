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
///   ./build/bench_asciiquack [file] [iterations]   (defaults: mdbasics.adoc, 1000)

#include "document.hpp"
#include "html5.hpp"
#include "parser.hpp"
#include "reader.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

static std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        throw std::runtime_error("cannot open: " + path);
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main(int argc, char* argv[]) {
    const std::string file = (argc > 1) ? argv[1]
                                        : "benchmark/sample-data/mdbasics.adoc";
    const int n = (argc > 2) ? std::stoi(argv[2]) : 1000;

    std::string content;
    try {
        content = read_file(file);
    } catch (const std::exception& ex) {
        std::cerr << "bench_asciiquack: " << ex.what() << "\n";
        return 1;
    }

    asciiquack::ParseOptions opts;
    opts.safe_mode = asciiquack::SafeMode::Safe;
    opts.attributes["embedded"] = "";

    // Warm-up (not timed)
    for (int i = 0; i < 10; ++i) {
        auto doc = asciiquack::Parser::parse_string(content, opts, file);
        (void)asciiquack::convert_to_html5(*doc);
    }

    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < n; ++i) {
        auto doc = asciiquack::Parser::parse_string(content, opts, file);
        (void)asciiquack::convert_to_html5(*doc);
    }
    auto t1 = std::chrono::high_resolution_clock::now();

    double total_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    double avg_ms   = total_ms / n;

    std::cout << "asciiquack (C++17):\n";
    std::cout << "  file       : " << file  << "\n";
    std::cout << "  iterations : " << n     << "\n";
    std::cout << "  total      : " << total_ms << " ms\n";
    std::cout << "  average    : " << avg_ms   << " ms/iter\n";
    std::cout << "  throughput : "
              << static_cast<int>(1000.0 / avg_ms)
              << " conversions/sec\n";
    return 0;
}
