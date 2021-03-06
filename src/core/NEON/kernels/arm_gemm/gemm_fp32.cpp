/*
 * Copyright (c) 2017-2020 Arm Limited.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include "arm_gemm.hpp"
#include "gemm_common.hpp"
#include "gemm_hybrid.hpp"
#include "gemm_implementation.hpp"
#include "gemm_interleaved.hpp"
#include "gemm_interleaved_pretransposed_2d.hpp"
#include "gemv_batched.hpp"
#include "gemv_pretransposed.hpp"

#include "kernels/a32_sgemm_8x6.hpp"
#include "kernels/a64_hybrid_fp32_mla_16x4.hpp"
#include "kernels/a64_hybrid_fp32_mla_4x8.hpp"
#include "kernels/a64_smallK_hybrid_fp32_mla_4x6.hpp"
#include "kernels/a64_smallK_hybrid_fp32_mla_4x8.hpp"
#include "kernels/a64_sgemm_12x8.hpp"
#include "kernels/a64_sgemv_pretransposed.hpp"

#include "kernels/sve_hybrid_fp32_mla_4VLx4.hpp"
#include "kernels/sve_hybrid_fp32_mmla_4VLx4.hpp"
#include "kernels/sve_interleaved_fp32_mla_3VLx8.hpp"
#include "kernels/sve_interleaved_fp32_mmla_3VLx8.hpp"
#include "kernels/sve_smallK_hybrid_fp32_mla_1VLx8.hpp"

namespace arm_gemm {

static const GemmImplementation<float, float> gemm_fp32_methods[] =
{
{
    GemmMethod::GEMV_BATCHED,
    "gemv_batched",
    [](const GemmArgs &args) { return (args._Msize==1) && (args._nbatches>1); },
    nullptr,
    [](const GemmArgs &args) { return new GemvBatched<float, float>(args); }
},
#ifdef __aarch64__
{
    GemmMethod::GEMV_PRETRANSPOSED,
    "sgemv_pretransposed",
    [](const GemmArgs &args) { return (args._Msize==1 && args._nbatches==1); },
    nullptr,
    [](const GemmArgs &args) { return new GemvPretransposed<sgemv_pretransposed, float, float>(args); }
},
#if defined(__ARM_FEATURE_SVE) && defined(MMLA_FP32)
{
    GemmMethod::GEMM_HYBRID,
    "hybrid_fp32_mmla_4VLx4",
    [](const GemmArgs &args) { return (args._Ksize >= 4); },
    [](const GemmArgs &args) { return ((args._Ksize <= 256) && (args._Nsize <= 256)) || ((args._nmulti > 1) && ((args._Msize / args._maxthreads) < 8)); },
    [](const GemmArgs &args) { return new GemmHybrid<hybrid_fp32_mmla_4VLx4, float, float>(args); }
},
{
    GemmMethod::GEMM_INTERLEAVED,
    "interleaved_fp32_mmla_3VLx8",
    [](const GemmArgs &args) { return (args._Ksize>4); },
    nullptr,
    [](const GemmArgs &args) { return new GemmInterleaved<interleaved_fp32_mmla_3VLx8, float, float>(args); }
},
#endif // __ARM_FEATURE_SVE && MMLA_FP32

#ifdef __ARM_FEATURE_SVE
// SVE smallk /  hybrid methods
{
    GemmMethod::GEMM_HYBRID,
    "smallK_hybrid_fp32_mla_1VLx8",
    [](const GemmArgs &args) { return (args._Ksize <= 24); },
    nullptr,
    [](const GemmArgs &args) { return new GemmHybrid<smallK_hybrid_fp32_mla_1VLx8, float, float>(args); }
},
{
    GemmMethod::GEMM_HYBRID,
    "hybrid_fp32_mla_4VLx4",
    [](const GemmArgs &args) { return (args._Ksize >= 4); },
    [](const GemmArgs &args) { return ((args._Ksize <= 256) && (args._Nsize <= 256)) || ((args._nmulti > 1) && ((args._Msize / args._maxthreads) < 8)); },
    [](const GemmArgs &args) { return new GemmHybrid<hybrid_fp32_mla_4VLx4, float, float>(args); }
},
#endif // __ARM_FEATURE_SVE

// NEON hybrid methods
{
    GemmMethod::GEMM_HYBRID,
    "smallK_hybrid_fp32_mla_4x8",
    [](const GemmArgs &args) { return (args._Ksize <= 8) && (args._Nsize % 4)==0; },
    nullptr,
    [](const GemmArgs &args) { return new GemmHybrid<smallK_hybrid_fp32_mla_4x8, float, float>(args); }
},
{
    GemmMethod::GEMM_HYBRID,
    "smallK_hybrid_fp32_mla_4x6",
    [](const GemmArgs &args) { return (args._Ksize > 8) && (args._Ksize <= 16) && (args._Nsize % 4)==0; },
    nullptr,
    [](const GemmArgs &args) { return new GemmHybrid<smallK_hybrid_fp32_mla_4x6, float, float>(args); }
},
{
    GemmMethod::GEMM_HYBRID,
    "hybrid_fp32_mla_4x8_normal",
    [](const GemmArgs &args) { return (args._Ksize >= 4); },
    [](const GemmArgs &args) { return (args._Nsize < 12); },
    [](const GemmArgs &args) { return new GemmHybrid<hybrid_fp32_mla_4x8, float, float>(args); }
},
GemmImplementation<float, float>::with_estimate(
    GemmMethod::GEMM_HYBRID,
    "hybrid_fp32_mla_16x4",
    [](const GemmArgs &args) { return (args._Ksize >= 4); },
    [](const GemmArgs &args) { return GemmHybrid<hybrid_fp32_mla_16x4, float, float>::estimate_cycles(args, hybrid_fp32_mla_16x4::get_performance_parameters(args._ci)); },
    [](const GemmArgs &args) { return new GemmHybrid<hybrid_fp32_mla_16x4, float, float>(args); }
),

#ifdef __ARM_FEATURE_SVE
{
    GemmMethod::GEMM_INTERLEAVED,
    "interleaved_fp32_mla_3VLx8",
    [](const GemmArgs &args) { return (args._Ksize>4); },
    nullptr,
    [](const GemmArgs &args) { return new GemmInterleaved<interleaved_fp32_mla_3VLx8, float, float>(args); }
},
#endif // __ARM_FEATURE_SVE
// Pretranposed, 2D split
GemmImplementation<float, float>::with_estimate(
    GemmMethod::GEMM_INTERLEAVED_2D,
    "sgemm_12x8_2d",
    nullptr,
    [](const GemmArgs &args) { return GemmInterleavedPretransposed2d<sgemm_12x8, float, float>::estimate_cycles(args, sgemm_12x8::get_performance_parameters(args._ci)); },
    [](const GemmArgs &args) { return new GemmInterleavedPretransposed2d<sgemm_12x8, float, float>(args); }
),
// 1D split (with pretransposed or not)
GemmImplementation<float, float>::with_estimate(
    GemmMethod::GEMM_INTERLEAVED,
    "sgemm_12x8_1d",
    nullptr,
    [](const GemmArgs &args) { return GemmInterleaved<sgemm_12x8, float, float>::estimate_cycles(args, sgemm_12x8::get_performance_parameters(args._ci)); },
    [](const GemmArgs &args) { return new GemmInterleaved<sgemm_12x8, float, float>(args); }
),
#endif // __aarch64__

#ifdef __arm__
{
    GemmMethod::GEMM_INTERLEAVED,
    "sgemm_8x6",
    nullptr,
    nullptr,
    [](const GemmArgs &args) { return new GemmInterleaved<sgemm_8x6, float, float>(args); }
},
#endif // __arm__
{
    GemmMethod::DEFAULT,
    "",
    nullptr,
    nullptr,
    nullptr
}
};

/* Templated function to return this list. */
template<>
const GemmImplementation<float, float> *gemm_implementation_list<float, float>() {
    return gemm_fp32_methods;
}

/* Explicitly instantiate the external functions for these types. */
template UniqueGemmCommon<float, float> gemm<float, float, Nothing>(const GemmArgs &args, const Nothing &);
template KernelDescription get_gemm_method<float, float, Nothing>(const GemmArgs &args, const Nothing &);
template std::vector<KernelDescription> get_compatible_kernels<float, float, Nothing> (const GemmArgs &args, const Nothing &);

} // namespace arm_gemm
