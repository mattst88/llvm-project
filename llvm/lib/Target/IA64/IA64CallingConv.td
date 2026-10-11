//===-- IA64CallingConv.td - IA64 calling conventions ------*- tablegen -*-===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The calling convention of the Itanium Software Conventions and Runtime
// Architecture Guide, chapter 8.
//
//===----------------------------------------------------------------------===//

// The first eight argument slots are the caller's output registers, which the
// callee sees as its first input registers.
def CC_IA64 : CallingConv<[
  CCIfType<[i64], CCAssignToReg<[R32, R33, R34, R35, R36, R37, R38, R39]>>
]>;

// An integer result of up to four words comes back in r8 to r11.
def RetCC_IA64 : CallingConv<[
  CCIfType<[i64], CCAssignToReg<[R8, R9, R10, R11]>>
]>;

// Stacked registers are preserved across calls by the RSE, not by the
// callee, so they are listed in the call-preserved mask rather than here.
def CSR_IA64 : CalleeSavedRegs<(add (sequence "R%u", 4, 7),
                                    (sequence "F%u", 2, 5),
                                    (sequence "F%u", 16, 31),
                                    (sequence "B%u", 1, 5),
                                    (sequence "P%u", 1, 5),
                                    (sequence "P%u", 16, 63))>;

// The caller's inputs and locals are not visible to the callee.
def CSR_IA64_Call : CalleeSavedRegs<(add CSR_IA64,
                                         (sequence "R%u", 32, 119))>;
