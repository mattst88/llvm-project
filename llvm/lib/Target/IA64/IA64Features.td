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

// Tune for Madison rather than for Montecito and its successors: shifts,
// extracts, deposits, bit tests and popcnt issue only on I0, and a multimedia
// result reaches an address without a pipeline flush.
def TuneMadison
    : SubtargetFeature<"madison", "IsMadison", "true", "Tune for Madison">;

// Registers reserved from code generation. A kernel does not save most of the
// FP registers on entry and so must not use them, and a program may keep a
// global value in a register for its whole run. Stacked registers cannot be
// reserved, since they belong to each function. Of the branch registers,
// only the callee-saved ones can be reserved: the compiler uses them only for
// inline asm. Calls need the others.
foreach I = 2...31 in
  def FeatureReserveR#I
      : SubtargetFeature<"reserve-r"#I, "UserReservedRegister[IA64::R"#I#"]",
                         "true", "Reserve r"#I>;
foreach I = 2...127 in
  def FeatureReserveF#I
      : SubtargetFeature<"reserve-f"#I, "UserReservedRegister[IA64::F"#I#"]",
                         "true", "Reserve f"#I>;
foreach I = 1...5 in
  def FeatureReserveB#I
      : SubtargetFeature<"reserve-b"#I, "UserReservedRegister[IA64::B"#I#"]",
                         "true", "Reserve b"#I>;

// Not user-settable. The assembler sets it for the padding before an
// instruction that must start an instruction group, so that each bundle of
// the padding ends with a stop bit.
def FeatureStoppedPadding
    : SubtargetFeature<"stopped-padding", "HasStoppedPadding", "true",
                       "End each bundle of padding with a stop">;
