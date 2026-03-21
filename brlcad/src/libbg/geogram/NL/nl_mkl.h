/* Stub: MKL backend not available in this build. */
#ifndef NL_MKL_EXT_H
#define NL_MKL_EXT_H
#include "nl.h"
#include "nl_matrix.h"
#ifdef __cplusplus
extern "C" {
#endif
static inline NLboolean nlExtensionIsInitialized_MKL(void) { return NL_FALSE; }
static NLMultMatrixVectorFunc NLMultMatrixVector_MKL = NULL;
static inline NLMatrix nlMKLMatrixNewFromSparseMatrix(NLSparseMatrix* M) { (void)M; return NULL; }
static inline NLMatrix nlMKLMatrixNewFromCRSMatrix(NLCRSMatrix* M) { (void)M; return NULL; }
#ifdef __cplusplus
}
#endif
#endif
