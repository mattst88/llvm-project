//===-- IA64Schedule.td - IA64 scheduling definitions ------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Scheduling classes. The instruction formats assign them, and each processor
// model gives their costs.
//
//===----------------------------------------------------------------------===//

// Integer instructions that either an I or an M unit runs.
def WriteALU   : SchedWrite; // Add, subtract, the logical operations, moves.
def WriteICmp  : SchedWrite; // Integer compare.
def WriteMMALU : SchedWrite; // Parallel add, subtract, average and compare.

// The second predicate written by a compare or test.
def WritePred2 : SchedWrite;

// Integer instructions that need an I unit.
def WriteMMMul    : SchedWrite; // Parallel multiply; population count.
def WriteMMShift  : SchedWrite; // Shift by a register; mix, pack, unpack.
def WriteMMShiftI : SchedWrite; // Parallel shift by a constant; mux.
def WriteShift    : SchedWrite; // Extract, deposit, shift right pair.
def WriteTBit     : SchedWrite; // Test a bit, a NaT bit or a feature.
def WriteExt      : SchedWrite; // Sign and zero extend; count zeroes.
def WriteToBR     : SchedWrite; // Move to a branch register.
def WriteFromBR   : SchedWrite; // Move from a branch register, or from ip.
def WriteToPR     : SchedWrite; // Move to the predicates.
def WriteFromPR   : SchedWrite; // Move from the predicates.
def WriteMovL     : SchedWrite; // Move of a 64-bit constant.

// Moves to and from the application registers, which take an I unit or an M
// unit as the register says.
def WriteToAR   : SchedWrite;
def WriteFromAR : SchedWrite;
def WriteToLC   : SchedWrite; // Set ar.lc for a counted loop.

// Memory.
def WriteLoad     : SchedWrite; // Integer load.
def WriteStore    : SchedWrite; // Integer store.
def WriteFLoad    : SchedWrite; // Floating-point load.
def WriteFLoad2   : SchedWrite; // Second register of a load pair.
def WriteFStore   : SchedWrite; // Floating-point store.
def WritePostInc  : SchedWrite; // Post-incremented base register.
def WritePrefetch : SchedWrite; // lfetch.
def WriteSem      : SchedWrite; // cmpxchg, xchg, fetchadd.
def WriteChk      : SchedWrite; // Speculation checks.
def WriteAlloc    : SchedWrite; // alloc.

// Moves between the general and the floating-point registers.
def WriteSetF : SchedWrite;
def WriteGetF : SchedWrite;

// Floating point.
def WriteFMA    : SchedWrite; // Multiply-add, and its aliases.
def WriteXMA    : SchedWrite; // Integer multiply and add.
def WriteFMisc  : SchedWrite; // Merge, min, max, the logical operations.
def WriteFCmp   : SchedWrite; // Compare and classify.
def WriteFPred2 : SchedWrite; // Second predicate of a compare or classify.
def WriteFRecip : SchedWrite; // Reciprocal approximation.
def WriteFRecipPred : SchedWrite; // The predicate that it writes.
def WriteFCvt   : SchedWrite; // Convert to and from integer.
def WriteFDiv   : SchedWrite; // The whole of a division.
def WriteIDiv   : SchedWrite; // Integer division, from significand to
                              // significand.
def WriteIDiv32 : SchedWrite; // The same, of integers that fit in 32 bits.

def WriteBr  : SchedWrite; // Branches, calls and returns.
def WriteNop : SchedWrite; // nop and hint.
def WriteSys : SchedWrite; // Everything a compiler does not schedule.

// A compare and the one or two predicated moves of a select.
def WriteSelect  : SchedWrite;
def WriteFSelect : SchedWrite;
