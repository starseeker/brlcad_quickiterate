/// @file minipdf.cpp
/// @brief Compilation unit for the struetype TrueType font parser.
///
/// struetype.h is an stb-style single-header library: the implementation
/// is compiled in exactly one translation unit by defining
/// STRUETYPE_IMPLEMENTATION before including the header.
///
/// All other translation units that include struetype.h (via minipdf.hpp)
/// get only the declarations.

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

#if defined(__GNUC__) || defined(__clang__)
#    pragma GCC diagnostic pop
#endif
