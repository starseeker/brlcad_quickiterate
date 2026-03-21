/*
 * Stub nl_cuda.h - CUDA backend not available in this build.
 * Provides declarations to satisfy nl_amgcl.cpp compilation.
 */
#ifndef OPENNL_CUDA_EXT_H
#define OPENNL_CUDA_EXT_H
#include "nl.h"
#include "nl_blas.h"
#ifdef __cplusplus
extern "C" {
#endif
static inline NLboolean nlExtensionIsInitialized_CUDA(void) { return NL_FALSE; }
static inline NLMatrix nlCUDAMatrixNewFromCRSMatrix(NLMatrix M) { (void)M; return NULL; }
static inline NLMatrix nlCUDAJacobiPreconditionerNewFromCRSMatrix(NLMatrix M) { (void)M; return NULL; }
static inline void nlCUDAMatrixSpMV(
    NLMatrix M, const double* x, double* y, double alpha, double beta
) { (void)M;(void)x;(void)y;(void)alpha;(void)beta; }
static inline NLBlas_t nlCUDABlas(void) { return NULL; }
#ifdef __cplusplus
}
#endif
#endif
