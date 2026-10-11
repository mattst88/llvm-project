//===- IntrinsicsIA64.td - Defines IA-64 intrinsics --------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines all of the IA-64-specific intrinsics. An unwinder reads
// the caller frames' registers from the RSE backing store, and these give it
// what it needs to locate them.
//
//===----------------------------------------------------------------------===//

let TargetPrefix = "ia64" in {

// The backing store address at which this frame's registers would be saved.
def int_ia64_bsp : ClangBuiltin<"__builtin_ia64_bsp">,
                   Intrinsic<[llvm_ptr_ty], [], [IntrNoMem, IntrHasSideEffects]>;

// Flush the register stack frames of the callers to the backing store.
def int_ia64_flushrs : ClangBuiltin<"__builtin_ia64_flushrs">,
                       Intrinsic<[], [], []>;

} // TargetPrefix = "ia64"
