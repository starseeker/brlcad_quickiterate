/// @file minipdf.cpp
/// @brief Compilation unit for minipdf: struetype TrueType font parser
///        implementation and PdfImage loading (JPEG and PNG).
///
/// struetype.h is an stb-style single-header library: the implementation
/// is compiled in exactly one translation unit by defining
/// STRUETYPE_IMPLEMENTATION before including the header.
///
/// PdfImage::from_jpeg_file and from_png_file are defined here to keep
/// zlib and format-parsing code out of the header.

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

#ifdef MINIPDF_USE_ZLIB
#    include <zlib.h>
#endif

#include <algorithm>
#include <cstdint>
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
// PNG loading (requires zlib)
// ─────────────────────────────────────────────────────────────────────────────

#ifdef MINIPDF_USE_ZLIB

/// Holds the result of a successful PNG decode (always converted to RGB).
struct PngRgb {
    int width = 0, height = 0;
    std::vector<unsigned char> rgb;
};

// Apply PNG row filter for a single scanline.
static void png_unfilter_row(unsigned char filter,
                              unsigned char* row, int row_bytes, int bpp,
                              const unsigned char* prev) {
    auto paeth = [](int a, int b, int c) -> int {
        int p  = a + b - c;
        int pa = std::abs(p - a);
        int pb = std::abs(p - b);
        int pc = std::abs(p - c);
        if (pa <= pb && pa <= pc) { return a; }
        if (pb <= pc)             { return b; }
        return c;
    };

    switch (filter) {
        case 0: break;  // None – no change
        case 1:         // Sub
            for (int x = bpp; x < row_bytes; ++x) {
                row[x] = static_cast<unsigned char>(row[x] + row[x - bpp]);
            }
            break;
        case 2:         // Up
            for (int x = 0; x < row_bytes; ++x) {
                row[x] = static_cast<unsigned char>(row[x] + prev[x]);
            }
            break;
        case 3:         // Average
            for (int x = 0; x < row_bytes; ++x) {
                int l = (x >= bpp) ? static_cast<int>(row[x - bpp]) : 0;
                int u = static_cast<int>(prev[x]);
                row[x] = static_cast<unsigned char>(
                    row[x] + static_cast<unsigned char>((l + u) / 2));
            }
            break;
        case 4:         // Paeth
            for (int x = 0; x < row_bytes; ++x) {
                int a = (x >= bpp) ? static_cast<int>(row[x - bpp])  : 0;
                int b = static_cast<int>(prev[x]);
                int c = (x >= bpp) ? static_cast<int>(prev[x - bpp]) : 0;
                row[x] = static_cast<unsigned char>(
                    row[x] + static_cast<unsigned char>(paeth(a, b, c)));
            }
            break;
        default:
            break;  // Unknown filter – leave as-is
    }
}

static bool png_decode_to_rgb(const unsigned char* data, std::size_t len,
                               PngRgb& out) {
    static const unsigned char PNG_SIG[8] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A
    };
    if (len < 8 || std::memcmp(data, PNG_SIG, 8) != 0) { return false; }

    auto read_u32 = [](const unsigned char* p) -> uint32_t {
        return (static_cast<uint32_t>(p[0]) << 24) |
               (static_cast<uint32_t>(p[1]) << 16) |
               (static_cast<uint32_t>(p[2]) <<  8) |
                static_cast<uint32_t>(p[3]);
    };

    int width = 0, height = 0;
    int bit_depth = 0, color_type = 0;
    bool got_ihdr = false;
    std::vector<unsigned char> idat;

    std::size_t i = 8;
    while (i + 12 <= len) {
        uint32_t chunk_len  = read_u32(data + i);
        if (i + 12 + chunk_len > len) { break; }
        const char*          ctype = reinterpret_cast<const char*>(data + i + 4);
        const unsigned char* cdata = data + i + 8;

        if (std::memcmp(ctype, "IHDR", 4) == 0) {
            if (chunk_len < 13) { return false; }
            width      = static_cast<int>(read_u32(cdata));
            height     = static_cast<int>(read_u32(cdata + 4));
            bit_depth  = static_cast<int>(cdata[8]);
            color_type = static_cast<int>(cdata[9]);
            if (cdata[12] != 0) { return false; }  // interlaced not supported
            got_ihdr = true;
        } else if (std::memcmp(ctype, "IDAT", 4) == 0) {
            idat.insert(idat.end(), cdata, cdata + chunk_len);
        } else if (std::memcmp(ctype, "IEND", 4) == 0) {
            break;
        }
        i += 12 + chunk_len;
    }

    if (!got_ihdr || idat.empty() || width <= 0 || height <= 0) { return false; }
    if (bit_depth != 8) { return false; }

    int src_ch = 0;
    switch (color_type) {
        case 0: src_ch = 1; break;
        case 2: src_ch = 3; break;
        case 4: src_ch = 2; break;
        case 6: src_ch = 4; break;
        default: return false;
    }

    const int row_bytes = width * src_ch;
    const int stride    = row_bytes + 1;
    std::vector<unsigned char> raw(static_cast<std::size_t>(stride) *
                                    static_cast<std::size_t>(height));
    uLongf dest_len = static_cast<uLongf>(raw.size());
    if (uncompress(raw.data(), &dest_len,
                   idat.data(), static_cast<uLong>(idat.size())) != Z_OK) {
        return false;
    }

    out.width  = width;
    out.height = height;
    out.rgb.resize(static_cast<std::size_t>(width) *
                   static_cast<std::size_t>(height) * 3);

    std::vector<unsigned char> prev_row(static_cast<std::size_t>(row_bytes), 0);
    for (int y = 0; y < height; ++y) {
        unsigned char* row = raw.data() +
            static_cast<std::size_t>(y) * static_cast<std::size_t>(stride) + 1;
        unsigned char filt = raw[static_cast<std::size_t>(y) *
                                  static_cast<std::size_t>(stride)];
        png_unfilter_row(filt, row, row_bytes, src_ch, prev_row.data());
        std::copy(row, row + row_bytes, prev_row.begin());

        for (int x = 0; x < width; ++x) {
            std::size_t d  = static_cast<std::size_t>(y * width + x) * 3;
            const unsigned char* px = row + x * src_ch;
            switch (color_type) {
                case 0: out.rgb[d]=out.rgb[d+1]=out.rgb[d+2]=px[0]; break;
                case 2: out.rgb[d]=px[0]; out.rgb[d+1]=px[1]; out.rgb[d+2]=px[2]; break;
                case 4: out.rgb[d]=out.rgb[d+1]=out.rgb[d+2]=px[0]; break;
                case 6: out.rgb[d]=px[0]; out.rgb[d+1]=px[1]; out.rgb[d+2]=px[2]; break;
                default: break;
            }
        }
    }
    return true;
}

#endif  // MINIPDF_USE_ZLIB

std::shared_ptr<PdfImage> PdfImage::from_png_file(const std::string& path) {
#ifdef MINIPDF_USE_ZLIB
    if (path.empty()) { return nullptr; }
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { return nullptr; }
    auto file_size = f.tellg();
    if (file_size < 8) { return nullptr; }
    std::vector<unsigned char> bytes(static_cast<std::size_t>(file_size));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(bytes.data()), file_size);
    if (!f) { return nullptr; }

    PngRgb decoded;
    if (!png_decode_to_rgb(bytes.data(), bytes.size(), decoded)) {
        return nullptr;
    }
    auto img = std::shared_ptr<PdfImage>(new PdfImage());
    img->width_    = decoded.width;
    img->height_   = decoded.height;
    img->channels_ = 3;
    img->enc_      = Encoding::Raw;
    img->data_     = std::move(decoded.rgb);
    return img;
#else
    (void)path;
    return nullptr;
#endif
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
