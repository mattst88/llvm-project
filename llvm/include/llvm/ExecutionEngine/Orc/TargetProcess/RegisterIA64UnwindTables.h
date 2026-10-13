//===- RegisterIA64UnwindTables.h - Register IA-64 unwind tables -*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Support for registering the IA-64 unwind tables of JIT'd code with the
// process's unwinder.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_REGISTERIA64UNWINDTABLES_H
#define LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_REGISTERIA64UNWINDTABLES_H

#include "llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h"
#include "llvm/ExecutionEngine/Orc/Shared/WrapperFunctionUtils.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"

namespace llvm::orc {

/// Register the unwind table at \p Table with the unwinder. Its entries are
/// sorted by function address and are offsets from the start of \p Segment,
/// which covers all the memory holding the code they describe. \p GP is that
/// code's global pointer.
LLVM_ABI Error registerIA64UnwindTable(ExecutorAddrRange Table,
                                       ExecutorAddrRange Segment,
                                       ExecutorAddr GP);

/// Deregister a table previously passed to registerIA64UnwindTable.

LLVM_ABI Error deregisterIA64UnwindTable(ExecutorAddrRange Table);

} // namespace llvm::orc

extern "C" LLVM_ABI llvm::orc::shared::CWrapperFunctionBuffer
llvm_orc_registerIA64UnwindTableAllocAction(const char *ArgData,
                                            size_t ArgSize);

extern "C" LLVM_ABI llvm::orc::shared::CWrapperFunctionBuffer
llvm_orc_deregisterIA64UnwindTableAllocAction(const char *ArgData,
                                              size_t ArgSize);

#endif // LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_REGISTERIA64UNWINDTABLES_H
