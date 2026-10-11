//===-- IA64Patterns.td - IA-64 selection patterns ---------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// SelectionDAG selection patterns. The instruction definitions describe the
// ISA and carry no patterns. Every instruction's last operand is its
// qualifying predicate, which patterns omit to get p0 (always true).
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Target nodes
//===----------------------------------------------------------------------===//

// A return, glued to the copies into the return value registers.
def IA64retglue : SDNode<"IA64ISD::RET_GLUE", SDTypeProfile<0, 0, []>,
                         [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;

//===----------------------------------------------------------------------===//
// Returns
//===----------------------------------------------------------------------===//

// br.ret.sptk.many b0
def : Pat<(IA64retglue), (BR_RET B0, 0, 1, 0)>;

//===----------------------------------------------------------------------===//
// Integer arithmetic
//===----------------------------------------------------------------------===//

def : Pat<(add GR:$r2, GR:$r3), (ADD GR:$r2, GR:$r3)>;
def : Pat<(sub GR:$r2, GR:$r3), (SUB GR:$r2, GR:$r3)>;
def : Pat<(and GR:$r2, GR:$r3), (AND GR:$r2, GR:$r3)>;
def : Pat<(or GR:$r2, GR:$r3), (OR GR:$r2, GR:$r3)>;
def : Pat<(xor GR:$r2, GR:$r3), (XOR GR:$r2, GR:$r3)>;

// andcm computes r2 & ~r3.
def : Pat<(and GR:$r2, (not GR:$r3)), (ANDCM GR:$r2, GR:$r3)>;

// add can add an extra 1, and sub can subtract an extra 1, which is the same
// as adding the complement.
def : Pat<(add (add GR:$r2, GR:$r3), 1), (ADD1 GR:$r2, GR:$r3)>;
def : Pat<(add GR:$r2, (not GR:$r3)), (SUB1 GR:$r2, GR:$r3)>;
