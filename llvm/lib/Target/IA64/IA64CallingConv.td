//===-- IA64CallingConv.td - IA64 calling conventions ------*- tablegen -*-===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The preserved register set of the IA-64 software conventions. The argument
// and return conventions arrive with their lowering.
//
//===----------------------------------------------------------------------===//

// The stacked registers a function keeps across a call are preserved by the
// register stack engine, not by the callee, so they are not listed here.
def CSR_IA64 : CalleeSavedRegs<(add (sequence "R%u", 4, 7),
                                    (sequence "F%u", 2, 5),
                                    (sequence "F%u", 16, 31),
                                    (sequence "B%u", 1, 5),
                                    (sequence "P%u", 1, 5),
                                    (sequence "P%u", 16, 63))>;
