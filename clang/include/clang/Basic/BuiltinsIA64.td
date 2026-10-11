//===--- BuiltinsIA64.td - IA-64 Builtin function database -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the IA-64-specific builtin function database. Each
// builtin has the name of an llvm.ia64.* intrinsic and is lowered to it.
//
//===----------------------------------------------------------------------===//

include "clang/Basic/BuiltinsBase.td"

class IA64Builtin<string prototype> : TargetBuiltin {
  let Spellings = [NAME];
  let Prototype = prototype;
  let Attributes = [NoThrow];
}

// Where in the backing store the registers of this frame would be saved.
def __builtin_ia64_bsp : IA64Builtin<"void*()">;

// Save the registers of the frames above this one in the backing store.
def __builtin_ia64_flushrs : IA64Builtin<"void()">;
