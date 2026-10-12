//===-- IA64RegisterBanks.td - Describe the IA64 banks -----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Values live in GRs or FRs, and getf/setf move them between the two. No
// value lives in a predicate: a compare writes a predicate pair, and its
// users come right after it.
//
//===----------------------------------------------------------------------===//

def GPRRegBank : RegisterBank<"GPR", [GR]>;
def FPRRegBank : RegisterBank<"FPR", [FR]>;
