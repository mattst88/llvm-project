//===- IntrinsicsIA64.td - Defines IA-64 intrinsics --------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines all of the IA-64-specific intrinsics. An unwinder reads
// the caller frames' registers from the RSE backing store, and the first ones
// give it what it needs to locate them. The others speculate loads.
//
//===----------------------------------------------------------------------===//

let TargetPrefix = "ia64" in {

// The backing store address at which this frame's registers would be saved.
def int_ia64_bsp : ClangBuiltin<"__builtin_ia64_bsp">,
                   Intrinsic<[llvm_ptr_ty], [], [IntrNoMem, IntrHasSideEffects]>;

// Flush the register stack frames of the callers to the backing store.
def int_ia64_flushrs : ClangBuiltin<"__builtin_ia64_flushrs">,
                       Intrinsic<[], [], []>;

// Control-speculative loads (ld.s), zero-extended to 64 bits. A load that
// would fault, or one from a NaT address, gives NaT instead.
foreach S = ["1", "2", "4", "8"] in
def int_ia64_ld#S#_s
    : Intrinsic<[llvm_i64_ty], [llvm_ptr_ty],
                [IntrReadMem, IntrArgMemOnly, IntrWillReturn, IntrNoCallback,
                 IntrNoFree, IntrNoSync], "", [SDNPMemOperand]>;

// 1 if the value is NaT and 0 otherwise (tnat).
def int_ia64_tnat : Intrinsic<[llvm_i64_ty], [llvm_i64_ty],
                              [IntrNoMem, IntrWillReturn]>;

} // TargetPrefix = "ia64"
