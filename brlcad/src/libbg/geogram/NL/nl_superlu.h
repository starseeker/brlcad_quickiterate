/* Stub: SUPERLU backend not available in this build. */
#ifndef NL_SUPERLU_EXT_H
#define NL_SUPERLU_EXT_H
#include "nl.h"
#ifdef __cplusplus
extern "C" {
#endif
static inline NLMatrix nlMatrixFactorize_SUPERLU(NLMatrix M, NLenum solver) { (void)M;(void)solver; return NULL; }
static inline NLboolean nlInitExtension_SUPERLU(void) { return NL_FALSE; }
static inline NLboolean nlExtensionIsInitialized_SUPERLU(void) { return NL_FALSE; }
#ifdef __cplusplus
}
#endif
#endif
