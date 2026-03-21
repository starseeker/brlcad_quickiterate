/* Stub: ARPACK backend not available in this build. */
#ifndef NL_ARPACK_EXT_H
#define NL_ARPACK_EXT_H
#include "nl.h"
#ifdef __cplusplus
extern "C" {
#endif
static inline NLboolean nlInitExtension_ARPACK(void) { return NL_FALSE; }
static inline NLboolean nlExtensionIsInitialized_ARPACK(void) { return NL_FALSE; }
#ifdef __cplusplus
}
#endif
#endif
