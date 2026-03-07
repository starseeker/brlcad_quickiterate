/* vendor/pcre2_impl.c
 * Single translation unit that instantiates the embedded PCRE2 implementation.
 * Exactly one .c/.cpp file in any build must include this (or do the same two
 * lines below).  All other files that need the PCRE2 API just #include
 * "vendor/pcre2_embed.h" without defining PCRE2_EMBED_IMPLEMENTATION.
 */
#define PCRE2_EMBED_IMPLEMENTATION
#include "pcre2_embed.h"
