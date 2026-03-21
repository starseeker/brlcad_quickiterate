/* Stub: CHOLMOD backend not available in this build. */
#ifndef NL_CHOLMOD_EXT_H
#define NL_CHOLMOD_EXT_H
#include "nl.h"
#ifdef __cplusplus
extern "C" {
#endif
static inline NLMatrix nlMatrixFactorize_CHOLMOD(NLMatrix M, NLenum solver) { (void)M;(void)solver; return NULL; }
static inline NLboolean nlExtensionIsInitialized_CHOLMOD(void) { return NL_FALSE; }
#ifdef __cplusplus
}
#endif
#endif
