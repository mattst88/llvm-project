//===-- IA64Features.td - IA64 subtarget features ----------*- tablegen -*-===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// IA-64 subtarget features. None of them enables instructions: every
// instruction the backend emits so far exists on all Itanium processors.
//
//===----------------------------------------------------------------------===//

// The program is a single module with a single GP, so a direct call returns
// with GP unchanged. An indirect call still loads GP from the function
// descriptor, which is where external callers expect it.
def FeatureConstantGP
    : SubtargetFeature<"constant-gp", "HasConstantGP", "true",
                       "The global pointer is the same in every function">;

// The registers that code is told to leave alone. A kernel does not save
// most of the floating-point registers when it is entered and so must not
// use them, and a program may keep a value of its own in a register for as
// long as it runs. The stacked registers are not among these: they are a
// function's own.
foreach I = 2...31 in
  def FeatureReserveR#I
      : SubtargetFeature<"reserve-r"#I, "UserReservedRegister[IA64::R"#I#"]",
                         "true", "Reserve r"#I>;
foreach I = 2...127 in
  def FeatureReserveF#I
      : SubtargetFeature<"reserve-f"#I, "UserReservedRegister[IA64::F"#I#"]",
                         "true", "Reserve f"#I>;
