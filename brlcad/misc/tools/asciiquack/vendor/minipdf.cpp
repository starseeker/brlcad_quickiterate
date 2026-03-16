/// @file minipdf.cpp
/// @brief Compilation unit for minipdf: struetype TrueType font parser
///        implementation and PdfImage loading (JPEG and PNG).
///
/// struetype.h is an stb-style single-header library: the implementation
/// is compiled in exactly one translation unit by defining
/// STRUETYPE_IMPLEMENTATION before including the header.
///
/// PdfImage::from_jpeg_file and from_png_file are defined here to keep
/// format-parsing code out of the header.
/// PNG decoding uses the bundled lodepng library (lodepng.h / lodepng.cpp).

// ── struetype implementation ──────────────────────────────────────────────────
// Suppress warnings from the third-party header that we cannot control.
#if defined(__GNUC__) || defined(__clang__)
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wconversion"
#    pragma GCC diagnostic ignored "-Wsign-conversion"
#    pragma GCC diagnostic ignored "-Wcast-align"
#    pragma GCC diagnostic ignored "-Wdouble-promotion"
#    pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#    pragma GCC diagnostic ignored "-Wold-style-cast"
#    pragma GCC diagnostic ignored "-Wformat-nonliteral"
#    pragma GCC diagnostic ignored "-Wshadow"
#    pragma GCC diagnostic ignored "-Wunused-function"
#endif

#define STRUETYPE_IMPLEMENTATION
#include "struetype.h"
// Prevent re-definition when minipdf.hpp subsequently re-includes struetype.h:
// the implementation code at the top of struetype.h is outside the header guard,
// so it would compile again unless we clear the macro first.
#undef STRUETYPE_IMPLEMENTATION

#if defined(__GNUC__) || defined(__clang__)
#    pragma GCC diagnostic pop
#endif

// ── PdfImage loading ──────────────────────────────────────────────────────────

#include "minipdf.hpp"
#include "lodepng.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

namespace minipdf {

// ─────────────────────────────────────────────────────────────────────────────
// JPEG loading
// ─────────────────────────────────────────────────────────────────────────────

// Parse JPEG markers to find a Start-Of-Frame segment and extract image
// dimensions and number of components.
static bool jpeg_sof_info(const unsigned char* data, std::size_t len,
                           int& out_w, int& out_h, int& out_ncomp) {
    // JPEG must start with SOI marker FF D8
    if (len < 4 || data[0] != 0xFF || data[1] != 0xD8) { return false; }

    std::size_t i = 2;
    while (i + 2 <= len) {
        if (data[i] != 0xFF) { return false; }  // lost marker sync
        unsigned char marker = data[i + 1];
        i += 2;

        // Markers that have no length field
        if (marker == 0xD8 || marker == 0xD9 ||
            (marker >= 0xD0 && marker <= 0xD7) || marker == 0x01) {
            continue;
        }
        if (i + 2 > len) { return false; }
        std::size_t seg_len =
            (static_cast<std::size_t>(data[i]) << 8) | data[i + 1];
        if (seg_len < 2 || i + seg_len > len) { return false; }

        // SOF0..SOF15 except DHT (C4), JPG (C8), DAC (CC)
        bool is_sof = marker >= 0xC0 && marker <= 0xCF &&
                      marker != 0xC4 && marker != 0xC8 && marker != 0xCC;
        if (is_sof && seg_len >= 9) {
            // precision(1 B), height(2 B), width(2 B), ncomp(1 B)
            out_h     = (static_cast<int>(data[i + 3]) << 8) | data[i + 4];
            out_w     = (static_cast<int>(data[i + 5]) << 8) | data[i + 6];
            out_ncomp = static_cast<int>(data[i + 7]);
            return out_w > 0 && out_h > 0;
        }
        i += seg_len;
    }
    return false;
}

std::shared_ptr<PdfImage> PdfImage::from_jpeg_file(const std::string& path) {
    if (path.empty()) { return nullptr; }

    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { return nullptr; }
    auto file_size = f.tellg();
    if (file_size < 4) { return nullptr; }

    std::vector<unsigned char> bytes(static_cast<std::size_t>(file_size));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(bytes.data()), file_size);
    if (!f) { return nullptr; }

    int w = 0, h = 0, ncomp = 0;
    if (!jpeg_sof_info(bytes.data(), bytes.size(), w, h, ncomp)) {
        return nullptr;
    }
    // Support grayscale (1) and RGB (3) JPEG only
    if (ncomp != 1 && ncomp != 3) { return nullptr; }

    auto img = std::shared_ptr<PdfImage>(new PdfImage());
    img->width_    = w;
    img->height_   = h;
    img->channels_ = ncomp;
    img->enc_      = Encoding::Dct;
    img->data_     = std::move(bytes);
    return img;
}

// ─────────────────────────────────────────────────────────────────────────────
// PNG loading (via bundled lodepng)
// ─────────────────────────────────────────────────────────────────────────────

std::shared_ptr<PdfImage> PdfImage::from_png_file(const std::string& path) {
    if (path.empty()) { return nullptr; }

    unsigned char* raw = nullptr;
    unsigned w = 0, h = 0;
    unsigned error = lodepng_decode24_file(&raw, &w, &h, path.c_str());
    if (error || !raw || w == 0 || h == 0) {
        free(raw);
        return nullptr;
    }

    std::size_t nbytes = static_cast<std::size_t>(w) *
                         static_cast<std::size_t>(h) * 3u;
    std::vector<unsigned char> rgb(raw, raw + nbytes);
    free(raw);

    auto img = std::shared_ptr<PdfImage>(new PdfImage());
    img->width_    = static_cast<int>(w);
    img->height_   = static_cast<int>(h);
    img->channels_ = 3;
    img->enc_      = Encoding::Raw;
    img->data_     = std::move(rgb);
    return img;
}

std::shared_ptr<PdfImage> PdfImage::from_file(const std::string& path) {
    if (path.empty()) { return nullptr; }
    // Peek at the magic bytes to choose the right loader
    std::ifstream f(path, std::ios::binary);
    if (!f) { return nullptr; }
    unsigned char magic[8] = {};
    f.read(reinterpret_cast<char*>(magic), sizeof(magic));
    const std::size_t nread = static_cast<std::size_t>(f.gcount());
    f.close();

    // JPEG: starts with FF D8
    if (nread >= 2 && magic[0] == 0xFF && magic[1] == 0xD8) {
        return from_jpeg_file(path);
    }
    // PNG: starts with 89 50 4E 47 0D 0A 1A 0A
    static const unsigned char PNG_SIG[8] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A
    };
    if (nread >= 8 && std::memcmp(magic, PNG_SIG, 8) == 0) {
        return from_png_file(path);
    }
    return nullptr;
}

} // namespace minipdf
