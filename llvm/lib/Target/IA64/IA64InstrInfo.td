//===-- IA64InstrInfo.td - IA64 instruction definitions ----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the IA-64 instruction set.
//
//===----------------------------------------------------------------------===//

include "IA64Schedule.td"
include "IA64InstrFormats.td"

//===----------------------------------------------------------------------===//
// Call-sequence markers.
//===----------------------------------------------------------------------===//

def SDT_IA64CallSeqStart : SDCallSeqStart<[SDTCisVT<0, i64>,
                                            SDTCisVT<1, i64>]>;
def SDT_IA64CallSeqEnd   : SDCallSeqEnd<[SDTCisVT<0, i64>,
                                          SDTCisVT<1, i64>]>;

def callseq_start : SDNode<"ISD::CALLSEQ_START", SDT_IA64CallSeqStart,
                           [SDNPHasChain, SDNPOutGlue]>;
def callseq_end   : SDNode<"ISD::CALLSEQ_END", SDT_IA64CallSeqEnd,
                           [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;

def brtarget : Operand<OtherVT>;
def calltarget : Operand<i64>;

let Defs = [R12], Uses = [R12] in {
def ADJCALLSTACKDOWN : IA64Pseudo<(outs), (ins i64imm:$amt1, i64imm:$amt2),
                                   "#ADJCALLSTACKDOWN",
                                   [(callseq_start timm:$amt1, timm:$amt2)]>;
def ADJCALLSTACKUP   : IA64Pseudo<(outs), (ins i64imm:$amt1, i64imm:$amt2),
                                   "#ADJCALLSTACKUP",
                                   [(callseq_end timm:$amt1, timm:$amt2)]>;
}

// A direct call, together with the register holding this function's GP, which
// is restored after the call returns because the callee may be in another
// module and leave its own GP in r1. They are one instruction so that nothing
// can be scheduled between them. The save register is preserved across the
// call, which also keeps it from being coalesced with r1. An r0 save register
// means the callee is known to be in this module.
let isCall = 1, Defs = [B0, R1] in
let SchedRW = [WriteBr] in
def CALL : IA64Pseudo<(outs), (ins calltarget:$target, GRSave:$gp, variable_ops),
                      "#CALL", []>;

// A tail call. The callee receives its arguments in this function's input
// registers, and the register frame is resized to just those:
// "alloc r2 = ar.pfs, 0, 0, inputs, 0" and "br.cond.sptk.many target".
let isCall = 1, isTerminator = 1, isReturn = 1, isBarrier = 1 in
let SchedRW = [WriteBr] in
def TCRETURN : IA64Pseudo<(outs),
                          (ins calltarget:$target, i64imm:$inputs,
                               variable_ops),
                          "#TCRETURN", []>;
// An indirect tail call through a function descriptor whose entry point and
// GP have been loaded: "mov b6 = entry", "mov r1 = gp", the alloc, and
// "br.cond.sptk.many b6".
let isCall = 1, isTerminator = 1, isReturn = 1, isBarrier = 1,
    Defs = [B6, R1] in
let SchedRW = [WriteBr] in
def TCRETURN_IND : IA64Pseudo<(outs),
                              (ins GR:$entry, GR:$calleegp, i64imm:$inputs,
                                   variable_ops),
                              "#TCRETURN_IND", []>;

// An indirect call through a function descriptor. The entry point and GP have
// already been loaded from it; the entry point goes to b6 and the GP to r1.
let isCall = 1, Defs = [B0, B6, R1] in
let SchedRW = [WriteBr] in
def CALL_IND : IA64Pseudo<(outs),
                          (ins GR:$entry, GR:$calleegp, GRSave:$gp,
                               variable_ops),
                          "#CALL_IND", []>;

// A compare writes a predicate and its complement, and its user is an
// instruction predicated on one of them. Until register allocation the
// compare and its user are a single pseudo, so no predicate has to stay live
// and a condition can be inverted just by changing the condition code. The
// predicate pair is always p6/p7. The condition is an IA64CC::CondCode, and
// the second compare operand is a register or an immediate. The "f" forms
// compare FP registers.
let Defs = [P6, P7], hasSideEffects = 0 in {
let SchedRW = [WriteSelect] in
def SETCC  : IA64Pseudo<(outs GR:$r1), (ins i64imm:$cc, GR:$lhs, GR:$rhs),
                        "#SETCC", []>;
let SchedRW = [WriteSelect] in
def SETCCi : IA64Pseudo<(outs GR:$r1), (ins i64imm:$cc, GR:$lhs, i64imm:$rhs),
                        "#SETCCi", []>;
let Uses = [AR40], mayRaiseFPException = 1 in {
let SchedRW = [WriteFSelect] in
def SETCCf : IA64Pseudo<(outs GR:$r1), (ins i64imm:$cc, FR:$lhs, FR:$rhs),
                        "#SETCCf", []>;

// An unordered compare raises the invalid exception for the relational
// predicates, but for equality and ordered/unordered tests only on a signaling
// NaN. These are compares that raise it for no predicate (quiet) and for every
// predicate (signaling).
let SchedRW = [WriteFSelect] in
def SETCCfq : IA64Pseudo<(outs GR:$r1), (ins i64imm:$cc, FR:$lhs, FR:$rhs),
                         "#SETCCfq", []>;
let SchedRW = [WriteFSelect] in
def SETCCfs : IA64Pseudo<(outs GR:$r1), (ins i64imm:$cc, FR:$lhs, FR:$rhs),
                         "#SETCCfs", []>;
}

// 1 if the value is in one of the classes in the 9-bit fclass mask, else 0;
// or, with $none set, the inverse.
let Defs = [P6, P7], hasSideEffects = 0, SchedRW = [WriteFSelect] in
def SETFCLASS : IA64Pseudo<(outs GR:$r1),
                           (ins FR:$f2, i64imm:$fclass, i64imm:$none),
                           "#SETFCLASS", []>;

// 1 if all (SETCC_AND) or any (SETCC_OR) of two or more compares hold, else
// 0. The operands are a condition code, a register and a register or
// immediate for each compare. The first compare may be any; the others must
// be integer compares, and those with parallel forms accumulate into its
// predicates. The others are ordinary compares under the predicate still
// undecided. In the "f" forms the first compare is of FP registers.
let Defs = [P6, P7], hasSideEffects = 0 in {
let SchedRW = [WriteSelect] in {
def SETCC_AND : IA64Pseudo<(outs GR:$r1), (ins variable_ops), "#SETCC_AND",
                           []>;
def SETCC_OR  : IA64Pseudo<(outs GR:$r1), (ins variable_ops), "#SETCC_OR",
                           []>;
}
let Uses = [AR40], mayRaiseFPException = 1, SchedRW = [WriteFSelect] in {
def SETCC_ANDf : IA64Pseudo<(outs GR:$r1), (ins variable_ops), "#SETCC_ANDf",
                            []>;
def SETCC_ORf  : IA64Pseudo<(outs GR:$r1), (ins variable_ops), "#SETCC_ORf",
                            []>;
}
}

// Select: $r1 is tied to $false and overwritten with $true under the
// predicate.
let Constraints = "$false = $r1" in {
let SchedRW = [WriteSelect] in
def SELECT  : IA64Pseudo<(outs GR:$r1),
                         (ins GR:$false, GR:$true, i64imm:$cc, GR:$lhs,
                              GR:$rhs),
                         "#SELECT", []>;
let SchedRW = [WriteSelect] in
def SELECTi : IA64Pseudo<(outs GR:$r1),
                         (ins GR:$false, GR:$true, i64imm:$cc, GR:$lhs,
                              i64imm:$rhs),
                         "#SELECTi", []>;
let Uses = [AR40], mayRaiseFPException = 1 in
let SchedRW = [WriteFSelect] in
def SELECTf : IA64Pseudo<(outs GR:$r1),
                         (ins GR:$false, GR:$true, i64imm:$cc, FR:$lhs,
                              FR:$rhs),
                         "#SELECTf", []>;

// The same, selecting between FP registers.
let SchedRW = [WriteSelect] in
def SELECT_FR  : IA64Pseudo<(outs FR:$r1),
                            (ins FR:$false, FR:$true, i64imm:$cc, GR:$lhs,
                                 GR:$rhs),
                            "#SELECT_FR", []>;
let SchedRW = [WriteSelect] in
def SELECT_FRi : IA64Pseudo<(outs FR:$r1),
                            (ins FR:$false, FR:$true, i64imm:$cc, GR:$lhs,
                                 i64imm:$rhs),
                            "#SELECT_FRi", []>;
let Uses = [AR40], mayRaiseFPException = 1 in
let SchedRW = [WriteFSelect] in
def SELECT_FRf : IA64Pseudo<(outs FR:$r1),
                            (ins FR:$false, FR:$true, i64imm:$cc, FR:$lhs,
                                 FR:$rhs),
                            "#SELECT_FRf", []>;
}

// Select on all (AND) or any (OR) of two or more compares, whose operands
// follow $true as in SETCC_AND and SETCC_OR.
let Constraints = "$false = $r1", Defs = [P6, P7], hasSideEffects = 0 in {
let SchedRW = [WriteSelect] in {
def SELECT_AND    : IA64Pseudo<(outs GR:$r1),
                               (ins GR:$false, GR:$true, variable_ops),
                               "#SELECT_AND", []>;
def SELECT_OR     : IA64Pseudo<(outs GR:$r1),
                               (ins GR:$false, GR:$true, variable_ops),
                               "#SELECT_OR", []>;
def SELECT_FR_AND : IA64Pseudo<(outs FR:$r1),
                               (ins FR:$false, FR:$true, variable_ops),
                               "#SELECT_FR_AND", []>;
def SELECT_FR_OR  : IA64Pseudo<(outs FR:$r1),
                               (ins FR:$false, FR:$true, variable_ops),
                               "#SELECT_FR_OR", []>;
}
let Uses = [AR40], mayRaiseFPException = 1, SchedRW = [WriteFSelect] in {
def SELECT_ANDf    : IA64Pseudo<(outs GR:$r1),
                                (ins GR:$false, GR:$true, variable_ops),
                                "#SELECT_ANDf", []>;
def SELECT_ORf     : IA64Pseudo<(outs GR:$r1),
                                (ins GR:$false, GR:$true, variable_ops),
                                "#SELECT_ORf", []>;
def SELECT_FR_ANDf : IA64Pseudo<(outs FR:$r1),
                                (ins FR:$false, FR:$true, variable_ops),
                                "#SELECT_FR_ANDf", []>;
def SELECT_FR_ORf  : IA64Pseudo<(outs FR:$r1),
                                (ins FR:$false, FR:$true, variable_ops),
                                "#SELECT_FR_ORf", []>;
}
}

// A conditional branch, predicated on the compare. It is not expanded until
// blocks can no longer be moved.
let isBranch = 1, isTerminator = 1 in {
let SchedRW = [WriteBr] in
def BRCC  : IA64Pseudo<(outs),
                       (ins brtarget:$target, i64imm:$cc, GR:$lhs, GR:$rhs),
                       "#BRCC", []>;
let SchedRW = [WriteBr] in
def BRCCi : IA64Pseudo<(outs),
                       (ins brtarget:$target, i64imm:$cc, GR:$lhs,
                            i64imm:$rhs),
                       "#BRCCi", []>;
let Uses = [AR40], mayRaiseFPException = 1 in
let SchedRW = [WriteBr] in
def BRCCf : IA64Pseudo<(outs),
                       (ins brtarget:$target, i64imm:$cc, FR:$lhs, FR:$rhs),
                       "#BRCCf", []>;

// A branch taken if all of two or more integer compares hold, whose
// operands follow $target as in SETCC_AND. The compares are and-type ones
// but for those with no such form: the first goes first, and the others are
// ordinary compares predicated on the result so far. A compare of
// a NaT fails, which is the point: the compares stand in for NaT tests of
// what they read. Its condition has no inverse, so branch analysis leaves it
// alone.
let SchedRW = [WriteBr] in
def BRCC_AND : IA64Pseudo<(outs), (ins brtarget:$target, variable_ops),
                          "#BRCC_AND", []>;
}
}

// The end of a counted loop: br.cloop, which branches to $target and
// decrements ar.lc if ar.lc is not zero. With $exit set, the sense is
// reversed: the loop ends by branching to $target when ar.lc is zero, and
// otherwise ar.lc is decremented and execution continues. That has no
// single instruction; once blocks are placed it becomes a br.cloop to the
// block that follows and a branch to $target.
let isBranch = 1, isTerminator = 1, Uses = [AR65], Defs = [AR65] in
let SchedRW = [WriteBr] in
def BRLOOP : IA64Pseudo<(outs), (ins brtarget:$target, i64imm:$exit),
                        "#BRLOOP", []>;

// Start a counted loop: set ar.lc to one less than the number of times the
// loop runs. ar.lc is not allocatable, so this is a pseudo rather than a
// move to a register operand.
let hasSideEffects = 1, Defs = [AR65] in
let SchedRW = [WriteToLC] in
def SET_LC : IA64Pseudo<(outs), (ins GR:$lc), "#SET_LC", []>;

// An unconditional branch. Hardware branches are unconditional only by having
// p0 as their qualifying predicate, but the code generator needs a distinct
// opcode, so this stands for "br.cond" until it is emitted.
let isBranch = 1, isTerminator = 1, isBarrier = 1 in
let SchedRW = [WriteBr] in
def BRUNCOND : IA64Pseudo<(outs), (ins brtarget:$target), "#BRUNCOND", []>;

// An unconditional branch beyond the 16 MiB reach of "br":
// "brl.cond.sptk.few", whose displacement occupies a second slot of the
// bundle. Only branch relaxation creates it, and no later pass needs to know
// its target.
let isBranch = 1, isTerminator = 1, isBarrier = 1 in
let SchedRW = [WriteBr] in
def BRLONG : IA64Pseudo<(outs), (ins brtarget:$target), "#BRLONG", []>;

// An indirect branch through a GR, via b7: "mov b7 = r" and
// "br.cond.sptk.few b7". It is a single pseudo because the mov is not a
// terminator. Indirect calls go through b6, so an interpreter's dispatch
// does not overwrite the target a call site last used, and a call whose
// target is unchanged is not stalled waiting on its move.
let isBranch = 1, isTerminator = 1, isBarrier = 1, isIndirectBranch = 1,
    Defs = [B7] in
let SchedRW = [WriteBr] in
def BRIND : IA64Pseudo<(outs), (ins GR:$target), "#BRIND", []>;

// The address of the most recent dynamic stack allocation, which lies above
// the outgoing argument area: "adds r1 = size, sp", once that size is known.
// The frame address, i.e. the stack pointer on entry. The prologue picks the
// register that holds it.
let hasSideEffects = 0 in
def FRAMEADDR : IA64Pseudo<(outs GR:$r1), (ins), "#FRAMEADDR", []>;

// A $size-byte compare-and-exchange. The compare value must first be moved to
// ar.ccv, which is why this stays a single pseudo until register allocation.
let Defs = [AR32], mayLoad = 1, mayStore = 1, hasSideEffects = 1 in
let SchedRW = [WriteSem] in
def CMPXCHG : IA64Pseudo<(outs GR:$r1),
                         (ins GR:$r3, GR:$cmp, GR:$new, i64imm:$size),
                         "#CMPXCHG", []>;

// Read and write ar.fpsr. The register is reserved, so in a function that
// never writes it every read would look like the same value, even though a
// callee may have changed the rounding mode in between; hence these are
// side-effecting pseudos rather than plain moves.
let hasSideEffects = 1 in {
let Uses = [AR40] in
let SchedRW = [WriteFromAR] in
def GET_FPSR : IA64Pseudo<(outs GR:$r1), (ins), "#GET_FPSR", []>;
let Defs = [AR40] in
let SchedRW = [WriteToAR] in
def SET_FPSR : IA64Pseudo<(outs), (ins GR:$r2), "#SET_FPSR", []>;
}

// Single- and double-precision division. There is no divide instruction:
// frcpa approximates the divisor's reciprocal to fewer than 9 bits; its
// predicate output is clear when an operand is zero, infinite or NaN, in
// which case its result is the quotient. Each Newton-Raphson step doubles
// the number of correct bits; the steps use status field 1, which programs
// never modify or read the flags of, and its widened exponent range, which
// operands of these precisions cannot overflow. They produce a quotient q
// rounded to the target precision and a reciprocal y accurate to more than
// twice that. The remainder a - b * q is then exact, and
// q + (a - b * q) * y is closer to a / b than a / b is to any other rounding
// candidate, so the final step rounds once, in the program's rounding mode
// and raising the flags a true division would.
// The steps are expanded before register allocation, so that each gets
// registers of its own and the steps of separate divisions can interleave.
let hasSideEffects = 0, Uses = [AR40], mayRaiseFPException = 1,
    usesCustomInserter = 1, SchedRW = [WriteFDiv] in {
def FDIV_S : IA64Pseudo<(outs FR:$f1), (ins FRAddend:$a, FR:$b), "#FDIV_S",
                        []>;
def FDIV_D : IA64Pseudo<(outs FR:$f1), (ins FRAddend:$a, FR:$b), "#FDIV_D",
                        []>;
}

// The final, rounding step of a division: $f1 is tied to $old, frcpa's
// result, and overwritten with $f3 * $f4 + $f2 under frcpa's predicate $p.
let hasSideEffects = 0, Uses = [AR40], mayRaiseFPException = 1,
    Constraints = "$old = $f1", SchedRW = [WriteFMA] in {
def FMA_S_MERGE : IA64Pseudo<(outs FR:$f1),
                             (ins FR:$old, FR:$f3, FR:$f4, FR:$f2,
                                  sfield:$sf, PR:$p),
                             "#FMA_S_MERGE", []>;
def FMA_D_MERGE : IA64Pseudo<(outs FR:$f1),
                             (ins FR:$old, FR:$f3, FR:$f4, FR:$f2,
                                  sfield:$sf, PR:$p),
                             "#FMA_D_MERGE", []>;
}

// Division by a constant $b, given $y, its reciprocal rounded to double:
// frcpa for the special cases, then q = a * y, the remainder a - b * q and
// q + (a - b * q) * y, rounded once. Expanded before register allocation, as
// is FDIV_S.
let hasSideEffects = 0, Uses = [AR40], mayRaiseFPException = 1,
    usesCustomInserter = 1, SchedRW = [WriteFDivConst] in {
def FDIV_CONST_S : IA64Pseudo<(outs FR:$f1), (ins FRAddend:$a, FR:$b, FR:$y),
                              "#FDIV_CONST_S", []>;
def FDIV_CONST_D : IA64Pseudo<(outs FR:$f1), (ins FRAddend:$a, FR:$b, FR:$y),
                              "#FDIV_CONST_D", []>;
}

// Single- and double-precision square root. frsqrta approximates
// 1 / sqrt(b); its predicate output is clear when b is zero, negative,
// infinite or NaN, in which case its result is the square root. In single
// precision, the throughput-optimized sequence of Intel's "Divide, Square
// Root, and Remainder Algorithms for the IA-64 Architecture", which gcc also
// uses: a polynomial in e = 1 - b * y * y refines y, and g ~ sqrt(b) is
// rounded to single, so that its remainder is exact. $half is 0.5 and $c
// 0.375. In double precision, the latency-optimized sequence of the Itanium
// manual, volume 1, section 6.3.3.2, which glibc uses: with g = b * y,
// h = y / 2 and e = 1/2 - g * h, g and h are corrected by polynomials in e,
// and g is rounded to double and corrected once by the remainder b - g * g. $c1 to $c5 are 1.5, 2.5, 231/16, 63/8 and 35/8. The
// steps use status field 1, as do a division's, and only the last rounds in
// the program's mode and raises its flags.
let hasSideEffects = 0, Uses = [AR40], mayRaiseFPException = 1,
    usesCustomInserter = 1, SchedRW = [WriteFSqrt] in {
def FSQRT_S : IA64Pseudo<(outs FR:$f1), (ins FRAddend:$b, FRAddend:$half,
                                             FR:$c), "#FSQRT_S", []>;
def FSQRT_D : IA64Pseudo<(outs FR:$f1),
                         (ins FRAddend:$b, FRAddend:$half, FRAddend:$c1,
                              FR:$c2, FR:$c3, FRAddend:$c4, FRAddend:$c5),
                         "#FSQRT_D", []>;
}

// Signed and unsigned 64-bit division of the integers in the significands of
// $a and $b, leaving the quotient in the significand of $f1. The integers are
// converted to FP values, which they fit exactly, and divided much as above,
// with all of it in status field 1, whose flags programs do not see. Two
// steps after frcpa give a reciprocal accurate enough that the quotient,
// corrected once by its remainder, truncates to the integer quotient. This
// is the sequence of Intel's application note, which gcc also uses, but for
// the predicate: of integers, frcpa gives the quotient itself only when the
// dividend is 0, and then every step gives 0 too. The steps are expanded
// before register allocation, as are FDIV_S's.
let hasSideEffects = 0, Uses = [AR40], usesCustomInserter = 1,
    SchedRW = [WriteIDiv] in {
def SDIV_SIG : IA64Pseudo<(outs FR:$f1), (ins FR:$a, FR:$b), "#SDIV_SIG", []>;
def UDIV_SIG : IA64Pseudo<(outs FR:$f1), (ins FR:$a, FR:$b), "#UDIV_SIG", []>;
}

// The same for integers that fit in 32 bits, whose quotient needs less
// accuracy: two steps after frcpa, the second adding $c = 2^-34 so that the
// quotient does not fall short of an exact integer.
let hasSideEffects = 0, Uses = [AR40], usesCustomInserter = 1,
    SchedRW = [WriteIDiv32] in {
def SDIV32_SIG : IA64Pseudo<(outs FR:$f1), (ins FR:$a, FR:$b, FR:$c),
                            "#SDIV32_SIG", []>;
def UDIV32_SIG : IA64Pseudo<(outs FR:$f1), (ins FR:$a, FR:$b, FR:$c),
                            "#UDIV32_SIG", []>;
}

// Spills and reloads stay single pseudos until frame layout, so nothing can be
// scheduled between the address computation and the access. The address is
// computed in r2, or for a GR reload in the destination register. r2 is
// reserved for this and is not marked as defined, since a reload whose result
// is unused must look dead to be deleted.
let hasSideEffects = 0 in {
let mayStore = 1 in {
let SchedRW = [WriteStore] in
def SPILL_GR : IA64Pseudo<(outs), (ins GR:$src, i64imm:$fi), "#SPILL_GR", []>;
let SchedRW = [WriteFStore] in
def SPILL_FR : IA64Pseudo<(outs), (ins FR:$src, i64imm:$fi), "#SPILL_FR", []>;
let SchedRW = [WriteStore] in
def SPILL_PR : IA64Pseudo<(outs), (ins PRValue:$src, i64imm:$fi), "#SPILL_PR",
                          []>;
}
let mayLoad = 1 in {
let SchedRW = [WriteLoad] in
def RELOAD_GR : IA64Pseudo<(outs GR:$dst), (ins i64imm:$fi), "#RELOAD_GR", []>;
let SchedRW = [WriteFLoad] in
def RELOAD_FR : IA64Pseudo<(outs FR:$dst), (ins i64imm:$fi), "#RELOAD_FR", []>;
let SchedRW = [WriteLoad] in
def RELOAD_PR : IA64Pseudo<(outs PRValue:$dst), (ins i64imm:$fi), "#RELOAD_PR",
                           []>;
}
}

// A load moved ahead of branches and stores (SPEC_LD), and the check left in
// its place (CHECK_LD), which writes the register the early load wrote. $opc
// is the opcode of the plain load and $pair matches the two up. They become
// ld.sa and ld.c.clr once nothing can come between them, or, if something
// did, a copy of zero and the plain load.
let mayLoad = 1, hasSideEffects = 0 in {
let SchedRW = [WriteLoad, ReadAddr] in {
def SPEC_LD_GR : IA64Pseudo<(outs GR:$r1),
                            (ins GR:$r3, i64imm:$opc, i64imm:$pair),
                            "#SPEC_LD_GR", []>;
let Constraints = "$old = $r1" in
def CHECK_LD_GR : IA64Pseudo<(outs GR:$r1),
                             (ins GR:$r3, GR:$old, i64imm:$opc, i64imm:$pair),
                             "#CHECK_LD_GR", []>;
}
let SchedRW = [WriteFLoad, ReadAddr] in {
def SPEC_LD_FR : IA64Pseudo<(outs FR:$f1),
                            (ins GR:$r3, i64imm:$opc, i64imm:$pair),
                            "#SPEC_LD_FR", []>;
let Constraints = "$old = $f1" in
def CHECK_LD_FR : IA64Pseudo<(outs FR:$f1),
                             (ins GR:$r3, FR:$old, i64imm:$opc, i64imm:$pair),
                             "#CHECK_LD_FR", []>;
}
}

let Uses = [R12] in
def ADJDYNALLOC : IA64Pseudo<(outs GR:$r1), (ins), "#ADJDYNALLOC", []>;
let hasSideEffects = 0 in
def DYNAREAOFFSET : IA64Pseudo<(outs GR:$r1), (ins), "#DYNAREAOFFSET", []>;

//===----------------------------------------------------------------------===//
// Integer ALU
//===----------------------------------------------------------------------===//

def ADD      : A1<0, 0, "add">;
def ADD1     : A1<0, 1, "add", "$r1 = $r2, $r3, 1">;
def SUB1     : A1<1, 0, "sub", "$r1 = $r2, $r3, 1">;
def SUB      : A1<1, 1, "sub">;
def ADDP4    : A1<2, 0, "addp4">;
def AND      : A1<3, 0, "and">;
def ANDCM    : A1<3, 1, "andcm">;
def OR       : A1<3, 2, "or">;
def XOR      : A1<3, 3, "xor">;

def SHLADD   : A2<4, "shladd">;
def SHLADDP4 : A2<6, "shladdp4">;

def SUBi     : A3<9, 1, "sub">;
def ANDi     : A3<0xb, 0, "and">;
def ANDCMi   : A3<0xb, 1, "andcm">;
def ORi      : A3<0xb, 2, "or">;
def XORi     : A3<0xb, 3, "xor">;

def ADDS     : A4<2, "adds">;
def ADDP4i   : A4<3, "addp4">;

// With an r0 base, this materializes a 22-bit constant.
let isReMaterializable = 1 in
def ADDL     : A5<"addl">;

//===----------------------------------------------------------------------===//
// Integer compare
//===----------------------------------------------------------------------===//

// The three major opcodes each hold one relation, selected by ta and c.
class CmpRel<string name, bits<4> opcode, bit ta_, bit c_> {
  string Name = name;
  bits<4> Opcode = opcode;
  bit ta = ta_;
  bit c = c_;
}

defvar CmpRels = [
  CmpRel<"lt",          0xc, 0, 0>, CmpRel<"ltu",         0xd, 0, 0>,
  CmpRel<"eq",          0xe, 0, 0>, CmpRel<"lt.unc",      0xc, 0, 1>,
  CmpRel<"ltu.unc",     0xd, 0, 1>, CmpRel<"eq.unc",      0xe, 0, 1>,
  CmpRel<"eq.and",      0xc, 1, 0>, CmpRel<"eq.or",       0xd, 1, 0>,
  CmpRel<"eq.or.andcm", 0xe, 1, 0>, CmpRel<"ne.and",      0xc, 1, 1>,
  CmpRel<"ne.or",       0xd, 1, 1>, CmpRel<"ne.or.andcm", 0xe, 1, 1>
];

// The parallel compares against zero.
defvar CmpZeroRels = [
  CmpRel<"gt.and",      0xc, 0, 0>, CmpRel<"gt.or",       0xd, 0, 0>,
  CmpRel<"gt.or.andcm", 0xe, 0, 0>, CmpRel<"le.and",      0xc, 0, 1>,
  CmpRel<"le.or",       0xd, 0, 1>, CmpRel<"le.or.andcm", 0xe, 0, 1>,
  CmpRel<"ge.and",      0xc, 1, 0>, CmpRel<"ge.or",       0xd, 1, 0>,
  CmpRel<"ge.or.andcm", 0xe, 1, 0>, CmpRel<"lt.and",      0xc, 1, 1>,
  CmpRel<"lt.or",       0xd, 1, 1>, CmpRel<"lt.or.andcm", 0xe, 1, 1>
];

// A parallel compare writes its targets only for one outcome of the relation
// and otherwise leaves them unchanged, which the operands do not express.
multiclass Cmp<string mnemonic, bits<2> x2, bits<2> x2imm> {
  foreach R = CmpRels in {
    defvar Suffix = !toupper(!subst(".", "_", R.Name));
    let hasSideEffects = R.ta in {
    def _#Suffix : A6<R.Opcode, x2, R.ta, R.c, mnemonic#"."#R.Name>;
    def _#Suffix#i : A8<R.Opcode, x2imm, R.ta, R.c, mnemonic#"."#R.Name>;
    }
  }
  let hasSideEffects = 1 in
  foreach R = CmpZeroRels in
    def _#!toupper(!subst(".", "_", R.Name))#z
        : A7<R.Opcode, x2, R.ta, R.c, mnemonic#"."#R.Name>;
}
defm CMP  : Cmp<"cmp", 0, 2>;
defm CMP4 : Cmp<"cmp4", 1, 3>;

//===----------------------------------------------------------------------===//
// Multimedia ALU
//===----------------------------------------------------------------------===//

// Saturation completers and their x2b values.
class MMSat<string name, bits<2> x2b> {
  string Name = name;
  bits<2> X2b = x2b;
}
foreach S = [MMSat<"", 0>, MMSat<".sss", 1>, MMSat<".uuu", 2>,
             MMSat<".uus", 3>] in {
  defvar Suffix = !toupper(!subst(".", "_", S.Name));
  def PADD1#Suffix : A9<0, 0, 0, S.X2b, "padd1"#S.Name>;
  def PADD2#Suffix : A9<0, 1, 0, S.X2b, "padd2"#S.Name>;
  def PSUB1#Suffix : A9<0, 0, 1, S.X2b, "psub1"#S.Name>;
  def PSUB2#Suffix : A9<0, 1, 1, S.X2b, "psub2"#S.Name>;
}
def PADD4     : A9<1, 0, 0, 0, "padd4">;
def PSUB4     : A9<1, 0, 1, 0, "psub4">;
def PAVG1     : A9<0, 0, 2, 2, "pavg1">;
def PAVG1_RAZ : A9<0, 0, 2, 3, "pavg1.raz">;
def PAVG2     : A9<0, 1, 2, 2, "pavg2">;
def PAVG2_RAZ : A9<0, 1, 2, 3, "pavg2.raz">;
def PAVGSUB1  : A9<0, 0, 3, 2, "pavgsub1">;
def PAVGSUB2  : A9<0, 1, 3, 2, "pavgsub2">;
def PCMP1_EQ  : A9<0, 0, 9, 0, "pcmp1.eq">;
def PCMP1_GT  : A9<0, 0, 9, 1, "pcmp1.gt">;
def PCMP2_EQ  : A9<0, 1, 9, 0, "pcmp2.eq">;
def PCMP2_GT  : A9<0, 1, 9, 1, "pcmp2.gt">;
def PCMP4_EQ  : A9<1, 0, 9, 0, "pcmp4.eq">;
def PCMP4_GT  : A9<1, 0, 9, 1, "pcmp4.gt">;

def PSHLADD2  : A10<4, "pshladd2">;
def PSHRADD2  : A10<6, "pshradd2">;

//===----------------------------------------------------------------------===//
// Multimedia multiply, mix, pack and shift
//===----------------------------------------------------------------------===//

def PMPYSHR2   : I1<3, "pmpyshr2">;
def PMPYSHR2_U : I1<1, "pmpyshr2.u">;

def MPY4      : I2<1, 0, 0, 3, 1, "mpy4">;
def MPYSHL4   : I2<1, 0, 0, 3, 3, "mpyshl4">;
let SchedRW = [WriteMMMul, ReadMM, ReadMM] in {
def PMPY2_R   : I2<0, 2, 1, 3, 1, "pmpy2.r">;
def PMPY2_L   : I2<0, 2, 1, 3, 3, "pmpy2.l">;
}
def MIX1_R    : I2<0, 2, 0, 2, 0, "mix1.r">;
def MIX2_R    : I2<0, 2, 1, 2, 0, "mix2.r">;
def MIX4_R    : I2<1, 2, 0, 2, 0, "mix4.r">;
def MIX1_L    : I2<0, 2, 0, 2, 2, "mix1.l">;
def MIX2_L    : I2<0, 2, 1, 2, 2, "mix2.l">;
def MIX4_L    : I2<1, 2, 0, 2, 2, "mix4.l">;
def PACK2_USS : I2<0, 2, 1, 0, 0, "pack2.uss">;
def PACK2_SSS : I2<0, 2, 1, 0, 2, "pack2.sss">;
def PACK4_SSS : I2<1, 2, 0, 0, 2, "pack4.sss">;
def UNPACK1_H : I2<0, 2, 0, 1, 0, "unpack1.h">;
def UNPACK2_H : I2<0, 2, 1, 1, 0, "unpack2.h">;
def UNPACK4_H : I2<1, 2, 0, 1, 0, "unpack4.h">;
def UNPACK1_L : I2<0, 2, 0, 1, 2, "unpack1.l">;
def UNPACK2_L : I2<0, 2, 1, 1, 2, "unpack2.l">;
def UNPACK4_L : I2<1, 2, 0, 1, 2, "unpack4.l">;
def PMIN1_U   : I2<0, 2, 0, 0, 1, "pmin1.u">;
def PMAX1_U   : I2<0, 2, 0, 1, 1, "pmax1.u">;
def PMIN2     : I2<0, 2, 1, 0, 3, "pmin2">;
def PMAX2     : I2<0, 2, 1, 1, 3, "pmax2">;
def PSAD1     : I2<0, 2, 0, 2, 3, "psad1">;

def MUX1 : I3<"mux1">;
def MUX2 : I4<"mux2">;

def PSHR2    : I5<0, 1, 2, "pshr2">;
def PSHR4    : I5<1, 0, 2, "pshr4">;
def SHR      : I5<1, 1, 2, "shr">;
def PSHR2_U  : I5<0, 1, 0, "pshr2.u">;
def PSHR4_U  : I5<1, 0, 0, "pshr4.u">;
def SHR_U    : I5<1, 1, 0, "shr.u">;
def PSHR2i   : I6<0, 1, 3, "pshr2">;
def PSHR4i   : I6<1, 0, 3, "pshr4">;
def PSHR2_Ui : I6<0, 1, 1, "pshr2.u">;
def PSHR4_Ui : I6<1, 0, 1, "pshr4.u">;
def PSHL2    : I7<0, 1, "pshl2">;
def PSHL4    : I7<1, 0, "pshl4">;
def SHL      : I7<1, 1, "shl">;
def PSHL2i   : I8<0, 1, "pshl2">;
def PSHL4i   : I8<1, 0, "pshl4">;

def POPCNT : I9<2, "popcnt">;
def CLZ    : I9<3, "clz">;

//===----------------------------------------------------------------------===//
// Shift pair, extract and deposit
//===----------------------------------------------------------------------===//

def SHRP   : I10<"shrp">;
def EXTR_U : I11<0, "extr.u">;
def EXTR   : I11<1, "extr">;
def DEP_Z  : I12<"dep.z">;
def DEP_Zi : I13<"dep.z">;
def DEPi   : I14<"dep">;
def DEP    : I15<"dep">;

//===----------------------------------------------------------------------===//
// Bit, NaT and feature tests
//===----------------------------------------------------------------------===//

class TestRel<string name, bit tb_, bit ta_, bit c_> {
  string Name = name;
  bit tb = tb_;
  bit ta = ta_;
  bit c = c_;
}
foreach R = [TestRel<"z",          0, 0, 0>, TestRel<"z.unc",       0, 0, 1>,
             TestRel<"z.and",      1, 0, 0>, TestRel<"nz.and",      1, 0, 1>,
             TestRel<"z.or",       0, 1, 0>, TestRel<"nz.or",       0, 1, 1>,
             TestRel<"z.or.andcm", 1, 1, 0>, TestRel<"nz.or.andcm", 1, 1, 1>]
    in {
  defvar Suffix = !toupper(!subst(".", "_", R.Name));
  let hasSideEffects = !or(R.tb, R.ta) in {
  def TBIT_#Suffix : I16<R.tb, R.ta, R.c, "tbit."#R.Name>;
  def TNAT_#Suffix : I17<R.tb, R.ta, R.c, "tnat."#R.Name>;
  }
  def TF_#Suffix   : I30<R.tb, R.ta, R.c, "tf."#R.Name>;
}

//===----------------------------------------------------------------------===//
// I-unit miscellany
//===----------------------------------------------------------------------===//

def NOP_I   : I18<0, "nop.i">;
def HINT_I  : I18<1, "hint.i">;
def BREAK_I : I19<0, "break.i">;
def CHK_S_I : I20<"chk.s.i">;

// The whether hint predicts the branch that uses the register; the importance
// hint (.imp) says whether to prefetch the target instruction stream now.
class MovBRHint<string name, bits<2> wh_, bit ih_> {
  string Name = name;
  bits<2> wh = wh_;
  bit ih = ih_;
}
foreach H = [MovBRHint<"",      1, 0>, MovBRHint<".imp",      1, 1>,
             MovBRHint<".sptk", 0, 0>, MovBRHint<".sptk.imp", 0, 1>,
             MovBRHint<".dptk", 2, 0>, MovBRHint<".dptk.imp", 2, 1>] in {
  defvar Suffix = !toupper(!subst(".", "_", H.Name));
  let hasSideEffects = 0 in {
  def MOV_BR#Suffix     : I21<0, H.wh, H.ih, "mov"#H.Name>;
  def MOV_RET_BR#Suffix : I21<1, H.wh, H.ih, "mov.ret"#H.Name>;
  }
}

def MOV_FROM_BR : I22<"mov">;
def MOV_PR      : I23<"mov">;
def MOV_PR_ROT  : I24<"mov">;
def MOV_FROM_IP : I25<0x30, "mov", "ip">;
def MOV_FROM_PR : I25<0x33, "mov", "pr">;
def MOV_I_AR    : I26<"mov.i">;
def MOV_I_ARi   : I27<"mov.i">;
def MOV_I_FROM_AR : I28<"mov.i">;

def ZXT1   : I29<0x10, "zxt1">;
def ZXT2   : I29<0x11, "zxt2">;
def ZXT4   : I29<0x12, "zxt4">;
def SXT1   : I29<0x14, "sxt1">;
def SXT2   : I29<0x15, "sxt2">;
def SXT4   : I29<0x16, "sxt4">;
def CZX1_L : I29<0x18, "czx1.l">;
def CZX2_L : I29<0x19, "czx2.l">;
def CZX1_R : I29<0x1c, "czx1.r">;
def CZX2_R : I29<0x1d, "czx2.r">;

//===----------------------------------------------------------------------===//
// Memory access
//===----------------------------------------------------------------------===//

// Load kinds: plain, control-speculative (.s), data-speculative (.a), both
// (.sa), check loads that clear or keep the ALAT entry (.c.clr, .c.nc), and
// the bias and ordered forms (.bias, .acq, .c.clr.acq). The low two bits of
// x6 encode the size and the rest the kind.
class MemKind<string name, int enc> {
  string Name = name;
  int Code = enc;
  // Whether the load allocates or checks an ALAT entry.
  bit ALAT = !or(!ge(enc, 8), !eq(!and(enc, 6), 2));
}
class MemSize<string name, int enc> {
  string Name = name;
  int Code = enc;
}
defvar IntSizes = [MemSize<"1", 0>, MemSize<"2", 1>, MemSize<"4", 2>,
                   MemSize<"8", 3>];
defvar FPSizes = [MemSize<"e", 0>, MemSize<"8", 1>, MemSize<"s", 2>,
                  MemSize<"d", 3>];
defvar FPLoadKinds = [MemKind<"", 0>, MemKind<".s", 1>, MemKind<".a", 2>,
                      MemKind<".sa", 3>, MemKind<".c.clr", 8>,
                      MemKind<".c.nc", 9>];
defvar IntLoadKinds = !listconcat(FPLoadKinds,
                                  [MemKind<".bias", 4>, MemKind<".acq", 5>,
                                   MemKind<".c.clr.acq", 10>]);

let mayLoad = 1 in
multiclass IntLoad<bits<6> x6, string mnemonic> {
  def "" : M1<x6, mnemonic>;
  def _r : M2<x6, mnemonic>;
  def _i : M3<x6, mnemonic>;
}
foreach K = IntLoadKinds in
  foreach S = IntSizes in
    let hasSideEffects = K.ALAT in
    defm LD#S.Name#!toupper(!subst(".", "_", K.Name))
        : IntLoad<!add(!shl(K.Code, 2), S.Code), "ld"#S.Name#K.Name>;
// ld8.fill restores a register and its NaT bit saved by st8.spill. The NaT
// bit is held in ar.unat in between.
let Uses = [AR36] in
defm LD8_FILL : IntLoad<0x1b, "ld8.fill">;
let mayLoad = 1 in
def LD8_MOV : M1Mov<0x03, "ld8.mov">;
let mayLoad = 1 in {
def LD16     : M1_16<0x28, "ld16">;
def LD16_ACQ : M1_16<0x2c, "ld16.acq">;
}

let mayStore = 1 in
multiclass IntStore<bits<6> x6, string mnemonic> {
  def "" : M4<x6, mnemonic>;
  def _i : M5<x6, mnemonic>;
}
foreach S = IntSizes in {
  defm ST#S.Name       : IntStore<!add(0x30, S.Code), "st"#S.Name>;
  defm ST#S.Name#_REL  : IntStore<!add(0x34, S.Code), "st"#S.Name#".rel">;
}
let Defs = [AR36] in
defm ST8_SPILL : IntStore<0x3b, "st8.spill">;
let mayStore = 1 in {
def ST16     : M4_16<0x30, "st16">;
def ST16_REL : M4_16<0x34, "st16.rel">;
}

let mayLoad = 1 in
multiclass FPLoad<bits<6> x6, string mnemonic> {
  def "" : M6<x6, mnemonic>;
  def _r : M7<x6, mnemonic>;
  def _i : M8<x6, mnemonic>;
}
foreach K = FPLoadKinds in
  foreach S = FPSizes in
    let hasSideEffects = K.ALAT in
    defm LDF#!toupper(S.Name)#!toupper(!subst(".", "_", K.Name))
        : FPLoad<!add(!shl(K.Code, 2), S.Code), "ldf"#S.Name#K.Name>;
defm LDF_FILL : FPLoad<0x1b, "ldf.fill">;

let mayStore = 1 in
multiclass FPStore<bits<6> x6, string mnemonic> {
  def "" : M9<x6, mnemonic>;
  def _i : M10<x6, mnemonic>;
}
foreach S = FPSizes in
  defm STF#!toupper(S.Name) : FPStore<!add(0x30, S.Code), "stf"#S.Name>;
defm STF_SPILL : FPStore<0x3b, "stf.spill">;

// FP pair loads fill two registers from adjacent memory. There is no
// extended-precision form.
let mayLoad = 1 in
foreach K = FPLoadKinds in
  foreach S = !tail(FPSizes) in {
    defvar Name = "LDFP"#!toupper(S.Name)#!toupper(!subst(".", "_", K.Name));
    let hasSideEffects = K.ALAT in {
    def Name    : M11<!add(!shl(K.Code, 2), S.Code), "ldfp"#S.Name#K.Name>;
    def Name#_i : M12<!add(!shl(K.Code, 2), S.Code), "ldfp"#S.Name#K.Name,
                      !if(!eq(S.Name, "s"), 8, 16)>;
    }
  }

multiclass Fetch<bits<6> x6, string mnemonic, bit hasCount = 0> {
  def "" : M13<x6, mnemonic, hasCount>;
  def _r : M14<x6, mnemonic>;
  def _i : M15<x6, mnemonic>;
}
// A prefetch changes no memory, but is kept as one that may write it, so that
// nothing removes it for writing no register.
let mayLoad = 1, mayStore = 1, hasSideEffects = 0 in {
defm LFETCH            : Fetch<0x2c, "lfetch", /*hasCount=*/1>;
defm LFETCH_EXCL       : Fetch<0x2d, "lfetch.excl">;
defm LFETCH_FAULT      : Fetch<0x2e, "lfetch.fault">;
defm LFETCH_FAULT_EXCL : Fetch<0x2f, "lfetch.fault.excl">;
def LFETCH_COUNT : M13Count<0x2c, "lfetch.count">;
}

let mayLoad = 1, mayStore = 1 in {
foreach S = IntSizes in {
  let Uses = [AR32] in {
  def CMPXCHG#S.Name#_ACQ : M16<!add(0x00, S.Code), "cmpxchg"#S.Name#".acq",
                                ", ar.ccv">;
  def CMPXCHG#S.Name#_REL : M16<!add(0x04, S.Code), "cmpxchg"#S.Name#".rel",
                                ", ar.ccv">;
  }
  def XCHG#S.Name         : M16<!add(0x08, S.Code), "xchg"#S.Name>;
}
def CMP8XCHG16_ACQ : M16<0x20, "cmp8xchg16.acq", ", ar.csd, ar.ccv">;
def CMP8XCHG16_REL : M16<0x24, "cmp8xchg16.rel", ", ar.csd, ar.ccv">;

def FETCHADD4_ACQ : M17<0x12, "fetchadd4.acq">;
def FETCHADD8_ACQ : M17<0x13, "fetchadd8.acq">;
def FETCHADD4_REL : M17<0x16, "fetchadd4.rel">;
def FETCHADD8_REL : M17<0x17, "fetchadd8.rel">;
}

//===----------------------------------------------------------------------===//
// Transfers between the general and floating-point registers
//===----------------------------------------------------------------------===//

def SETF_SIG : M18<0x1c, "setf.sig">;
def SETF_EXP : M18<0x1d, "setf.exp">;
def SETF_S   : M18<0x1e, "setf.s">;
def SETF_D   : M18<0x1f, "setf.d">;
def GETF_SIG : M19<0x1c, "getf.sig">;
def GETF_EXP : M19<0x1d, "getf.exp">;
def GETF_S   : M19<0x1e, "getf.s">;
def GETF_D   : M19<0x1f, "getf.d">;

//===----------------------------------------------------------------------===//
// M-unit miscellany
//===----------------------------------------------------------------------===//

def CHK_S_M      : M20<"chk.s.m">;
def CHK_S_F      : M21<"chk.s">;
def CHK_A_NC     : M22<4, "chk.a.nc">;
def CHK_A_CLR    : M22<5, "chk.a.clr">;
def CHK_A_NC_F   : M23<6, "chk.a.nc">;
def CHK_A_CLR_F  : M23<7, "chk.a.clr">;

def INVALA   : M24<0, 1, "invala">;
def FWB      : M24<0, 2, "fwb">;
def MF       : M24<2, 2, "mf">;
def MF_A     : M24<3, 2, "mf.a">;
def SRLZ_D   : M24<0, 3, "srlz.d">;
def SRLZ_I   : M24<1, 3, "srlz.i">;
def SYNC_I   : M24<3, 3, "sync.i">;
// flushrs writes the dirty stacked registers of earlier frames to the backing
// store, updating ar.bspstore and ar.rnat.
let hasSideEffects = 1, Defs = [AR18, AR19] in
def FLUSHRS  : M25<0xc, "flushrs">;
def LOADRS   : M25<0xa, "loadrs">;
def INVALA_E   : M26<"invala.e">;
def INVALA_E_F : M27<"invala.e">;
def FC       : M28<0, "fc">;
def FC_I     : M28<1, "fc.i">;

def MOV_M_AR      : M29<"mov.m">;
def MOV_M_ARi     : M30<"mov.m">;
def MOV_M_FROM_AR : M31<"mov.m">;
def MOV_CR        : M32<"mov">;
def MOV_FROM_CR   : M33<"mov">;
let Uses = [AR64] in
def ALLOC         : M34<"alloc">;
def MOV_PSR_L       : M35<0x2d, "mov", "psr.l">;
def MOV_PSR_UM      : M35<0x29, "mov", "psr.um">;
def MOV_FROM_PSR    : M36<0x25, "mov", "psr">;
def MOV_FROM_PSR_UM : M36<0x21, "mov", "psr.um">;

def BREAK_M : M37<0, "break.m">;

// llvm.trap uses gcc's break immediate, which Linux reports as SIGILL;
// llvm.debugtrap uses gdb's breakpoint immediate, reported as SIGTRAP.
def : Pat<(trap), (BREAK_M 0)>;
def : Pat<(debugtrap), (BREAK_M 0xccccc)>;
def NOP_M   : M48<0, "nop.m">;
def HINT_M  : M48Hint<"hint.m">;

def PROBE_R   : M38<0x38, "probe.r">;
def PROBE_W   : M38<0x39, "probe.w">;
def PROBE_Ri  : M39<0x18, "probe.r">;
def PROBE_Wi  : M39<0x19, "probe.w">;
def PROBE_RW_FAULT : M40<0x31, "probe.rw.fault">;
def PROBE_R_FAULT  : M40<0x32, "probe.r.fault">;
def PROBE_W_FAULT  : M40<0x33, "probe.w.fault">;

def ITC_D : M41<0x2e, "itc.d">;
def ITC_I : M41<0x2f, "itc.i">;

// The indirect register files: region, data and instruction breakpoint,
// protection key, performance monitor configuration and data, and the
// read-only CPUID registers. The model-specific registers are also an
// indirect file, with implementation-defined contents.
def MOV_RR  : M42<0x00, "mov", "rr">;
def MOV_DBR : M42<0x01, "mov", "dbr">;
def MOV_IBR : M42<0x02, "mov", "ibr">;
def MOV_PKR : M42<0x03, "mov", "pkr">;
def MOV_PMC : M42<0x04, "mov", "pmc">;
def MOV_PMD : M42<0x05, "mov", "pmd">;
def MOV_MSR : M42<0x06, "mov", "msr">;
def ITR_D   : M42<0x0e, "itr.d", "dtr">;
def ITR_I   : M42<0x0f, "itr.i", "itr">;
def MOV_FROM_RR    : M43<0x10, "mov", "rr">;
def MOV_FROM_DBR   : M43<0x11, "mov", "dbr">;
def MOV_FROM_IBR   : M43<0x12, "mov", "ibr">;
def MOV_FROM_PKR   : M43<0x13, "mov", "pkr">;
def MOV_FROM_PMC   : M43<0x14, "mov", "pmc">;
def MOV_FROM_PMD   : M43<0x15, "mov", "pmd">;
def MOV_FROM_MSR   : M43<0x16, "mov", "msr">;
def MOV_FROM_CPUID : M43<0x17, "mov", "cpuid">;

// The data access hint registers, written from an immediate, and the
// read-only DAHR stack.
def MOV_DAHR      : M50<"mov">;
def MOV_FROM_DAHR : M43<0x20, "mov", "dahr">;

def SUM : M44<4, "sum">;
def RUM : M44<5, "rum">;
def SSM : M44<6, "ssm">;
def RSM : M44<7, "rsm">;

def PTC_L  : M45<0x09, "ptc.l">;
def PTC_G  : M45<0x0a, "ptc.g">;
def PTC_GA : M45<0x0b, "ptc.ga">;
def PTR_D  : M45<0x0c, "ptr.d">;
def PTR_I  : M45<0x0d, "ptr.i">;
def THASH  : M46<0x1a, "thash">;
def TTAG   : M46<0x1b, "ttag">;
def TPA    : M46<0x1e, "tpa">;
def TAK    : M46<0x1f, "tak">;
def PTC_E  : M47<"ptc.e">;

//===----------------------------------------------------------------------===//
// F-unit instructions
//===----------------------------------------------------------------------===//

// Fused multiply-add at the status field's precision, single, double, and
// parallel single.
multiclass FMulAdd<bits<4> op, string name> {
  def "" : F1<op, 0, "f"#name>;
  def _S : F1<op, 1, "f"#name#".s">;
  def _D : F1<!add(op, 1), 0, "f"#name#".d">;
  // The same instructions for an explicit fadd that must not be turned into
  // a multiply: their addend cannot be f0.
  let isCodeGenOnly = 1 in {
    def _SUM : F1<op, 0, "f"#name, FRAddend>;
    def _S_SUM : F1<op, 1, "f"#name#".s", FRAddend>;
    def _D_SUM : F1<!add(op, 1), 0, "f"#name#".d", FRAddend>;
  }
}
defm FMA  : FMulAdd<0x8, "ma">;
defm FMS  : FMulAdd<0xa, "ms">;
defm FNMA : FMulAdd<0xc, "nma">;
def FPMA  : F1<0x9, 1, "fpma">;
def FPMS  : F1<0xb, 1, "fpms">;
def FPNMA : F1<0xd, 1, "fpnma">;

def XMA_L  : F2<0, "xma.l">;
def XMA_HU : F2<2, "xma.hu">;
def XMA_H  : F2<3, "xma.h">;
def FSELECT : F3<"fselect">;

multiclass FCompare<bit rb, bit ra, string name> {
  def "" : F4<rb, ra, 0, "fcmp."#name>;
  def _UNC : F4<rb, ra, 1, "fcmp."#name#".unc">;
}
defm FCMP_EQ    : FCompare<0, 0, "eq">;
defm FCMP_LT    : FCompare<1, 0, "lt">;
defm FCMP_LE    : FCompare<0, 1, "le">;
defm FCMP_UNORD : FCompare<1, 1, "unord">;

def FCLASS_M     : F5<0, "fclass.m">;
def FCLASS_M_UNC : F5<1, "fclass.m.unc">;

def FRCPA    : F6<0, "frcpa">;
def FPRCPA   : F6<1, "fprcpa">;
def FRSQRTA  : F7<0, "frsqrta">;
def FPRSQRTA : F7<1, "fprsqrta">;

multiclass FMinMax<bits<4> op, string prefix> {
  def MIN  : F8<op, 0x14, prefix#"min">;
  def MAX  : F8<op, 0x15, prefix#"max">;
  def AMIN : F8<op, 0x16, prefix#"amin">;
  def AMAX : F8<op, 0x17, prefix#"amax">;
}
defm F  : FMinMax<0, "f">;
defm FP : FMinMax<1, "fp">;

def FPCMP_EQ    : F8<1, 0x30, "fpcmp.eq">;
def FPCMP_LT    : F8<1, 0x31, "fpcmp.lt">;
def FPCMP_LE    : F8<1, 0x32, "fpcmp.le">;
def FPCMP_UNORD : F8<1, 0x33, "fpcmp.unord">;
def FPCMP_NEQ   : F8<1, 0x34, "fpcmp.neq">;
def FPCMP_NLT   : F8<1, 0x35, "fpcmp.nlt">;
def FPCMP_NLE   : F8<1, 0x36, "fpcmp.nle">;
def FPCMP_ORD   : F8<1, 0x37, "fpcmp.ord">;

def FMERGE_S   : F9<0, 0x10, "fmerge.s">;
def FMERGE_NS  : F9<0, 0x11, "fmerge.ns">;
def FMERGE_SE  : F9<0, 0x12, "fmerge.se">;
def FPMERGE_S  : F9<1, 0x10, "fpmerge.s">;
def FPMERGE_NS : F9<1, 0x11, "fpmerge.ns">;
def FPMERGE_SE : F9<1, 0x12, "fpmerge.se">;
def FPACK      : F9<0, 0x28, "fpack">;
def FAND       : F9<0, 0x2c, "fand">;
def FANDCM     : F9<0, 0x2d, "fandcm">;
def FOR        : F9<0, 0x2e, "for">;
def FXOR       : F9<0, 0x2f, "fxor">;
def FSWAP      : F9<0, 0x34, "fswap">;
def FSWAP_NL   : F9<0, 0x35, "fswap.nl">;
def FSWAP_NR   : F9<0, 0x36, "fswap.nr">;
def FMIX_LR    : F9<0, 0x39, "fmix.lr">;
def FMIX_R     : F9<0, 0x3a, "fmix.r">;
def FMIX_L     : F9<0, 0x3b, "fmix.l">;
def FSXT_R     : F9<0, 0x3c, "fsxt.r">;
def FSXT_L     : F9<0, 0x3d, "fsxt.l">;

multiclass FToFixed<bits<4> op, string prefix> {
  def FX        : F10<op, 0x18, prefix#"cvt.fx">;
  def FXU       : F10<op, 0x19, prefix#"cvt.fxu">;
  def FX_TRUNC  : F10<op, 0x1a, prefix#"cvt.fx.trunc">;
  def FXU_TRUNC : F10<op, 0x1b, prefix#"cvt.fxu.trunc">;
}
defm FCVT_  : FToFixed<0, "f">;
defm FPCVT_ : FToFixed<1, "fp">;
def FCVT_XF : F11<"fcvt.xf">;

// These write ar.fpsr: a status field's controls and its flags.
let Defs = [AR40] in {
def FSETC : F12<"fsetc">;
def FCLRF : F13<"fclrf">;
}
def FCHKF : F14<"fchkf">;

def BREAK_F : F15<0, "break.f">;
def NOP_F   : F16<0, "nop.f">;
def HINT_F  : F16<1, "hint.f">;

//===----------------------------------------------------------------------===//
// B-unit instructions
//===----------------------------------------------------------------------===//

def BR_COND  : B1<0, "br.cond">;
// The same branch, used for tail calls.
let isCodeGenOnly = 1, isBranch = 0, isCall = 1, isReturn = 1,
    isBarrier = 1 in
def BR_TAIL  : B1<0, "br.cond">;
// Loop branches, these two and the counted ones, must be in the last slot of
// a bundle.
let LastSlot = 1 in {
def BR_WEXIT : B1<2, "br.wexit">;
def BR_WTOP  : B1<3, "br.wtop">;
}
let Uses = [AR65], Defs = [AR65] in
def BR_CLOOP : B2<5, "br.cloop">;
def BR_CEXIT : B2<6, "br.cexit">;
let Uses = [AR65, AR66], Defs = [AR65, AR66] in
def BR_CTOP  : B2<7, "br.ctop">;
// A call saves the frame marker in ar.pfs.
let Defs = [AR64] in
def BR_CALL  : B3<"br.call">;

let isBranch = 1, isIndirectBranch = 1 in {
  def BR_COND_B : B4<0x20, 0, "br.cond">;
  def BR_IA     : B4<0x20, 1, "br.ia">;
}
// The indirect branch of a computed goto or a jump table, which BRIND
// becomes before stops are inserted.
let isCodeGenOnly = 1, isBranch = 1, isIndirectBranch = 1, isBarrier = 1 in
def BR_IND_B : B4<0x20, 0, "br.cond">;
// The indirect branch, used for tail calls.
let isCodeGenOnly = 1, isCall = 1, isReturn = 1, isBarrier = 1 in
def BR_TAIL_B : B4<0x20, 0, "br.cond">;
let isReturn = 1, isBarrier = 1 in
def BR_RET    : B4<0x21, 4, "br.ret">;
let Defs = [AR64] in
def BR_CALL_B : B5<"br.call">;

// Branch predict hints for IP-relative and indirect branches: the whether
// hint and the importance hint.
class PredictHint<string name, bits<2> wh, bit ih> {
  string Name = name;
  bits<2> Wh = wh;
  bit Ih = ih;
}
foreach H = [PredictHint<"sptk", 0, 0>, PredictHint<"loop", 1, 0>,
             PredictHint<"dptk", 2, 0>, PredictHint<"exit", 3, 0>,
             PredictHint<"sptk.imp", 0, 1>, PredictHint<"loop.imp", 1, 1>,
             PredictHint<"dptk.imp", 2, 1>, PredictHint<"exit.imp", 3, 1>] in
def BRP_#!toupper(!subst(".", "_", H.Name)) : B6<H.Wh, H.Ih, "brp."#H.Name>;
foreach H = [PredictHint<"sptk", 0, 0>, PredictHint<"dptk", 2, 0>,
             PredictHint<"sptk.imp", 0, 1>, PredictHint<"dptk.imp", 2, 1>] in {
  def BRP_B_#!toupper(!subst(".", "_", H.Name))
      : B7<0x10, H.Wh, H.Ih, "brp."#H.Name>;
  def BRP_RET_#!toupper(!subst(".", "_", H.Name))
      : B7<0x11, H.Wh, H.Ih, "brp.ret."#H.Name>;
}

// Each of these must be last in its instruction group, and therefore in its
// bundle, since a stop can follow a B slot only at the end of a bundle.
let LastSlot = 1 in {
def COVER     : B8<0x02, "cover">;
def CLRRRB    : B8<0x04, "clrrrb">;
def CLRRRB_PR : B8<0x05, "clrrrb.pr">;
def RFI       : B8<0x08, "rfi">;
def BSW_0     : B8<0x0c, "bsw.0">;
def BSW_1     : B8<0x0d, "bsw.1">;
}
def EPC       : B8<0x10, "epc">;
def VMSW_0    : B8<0x18, "vmsw.0">;
def VMSW_1    : B8<0x19, "vmsw.1">;

def BREAK_B : B9<0, 0, "break.b">;
def NOP_B   : B9<2, 0, "nop.b">;
def HINT_B  : B9<2, 1, "hint.b">;

//===----------------------------------------------------------------------===//
// X-unit instructions
//===----------------------------------------------------------------------===//

let isReMaterializable = 1, isMoveImm = 1 in
def MOVL : X2<"movl">;

def BRL_COND : X3<0, "brl.cond">;
let Defs = [AR64] in
def BRL_CALL : X4<"brl.call">;

def BREAK_X : X1<"break.x">;
def NOP_X   : X5<0, "nop.x">;
def HINT_X  : X5<1, "hint.x">;

//===----------------------------------------------------------------------===//
// Pseudo-ops
//===----------------------------------------------------------------------===//

// Instructions that more than one unit can execute. Application register
// moves go to whichever unit owns the register.
def NOP   : IA64AnyUnit<(outs), (ins uimm21:$imm), "nop", "$imm">;
def HINT  : IA64AnyUnit<(outs), (ins uimm21:$imm), "hint", "$imm">;
let SchedRW = [WriteSys] in
def BREAK : IA64AnyUnit<(outs), (ins uimm21:$imm), "break", "$imm">;
let SchedRW = [WriteChk] in
def CHK_S : IA64AnyUnit<(outs), (ins GR:$r2, target25m:$target), "chk.s",
                        "$r2, $target">;
let SchedRW = [WriteToAR] in
def MOV_AR      : IA64AnyUnit<(outs AR:$ar3), (ins GR:$r2), "mov",
                              "$ar3 = $r2">;
let SchedRW = [WriteToAR] in
def MOV_ARi     : IA64AnyUnit<(outs AR:$ar3), (ins simm8:$imm), "mov",
                              "$ar3 = $imm">;
let SchedRW = [WriteFromAR] in
def MOV_FROM_AR : IA64AnyUnit<(outs GR:$r1), (ins AR:$ar3), "mov",
                              "$r1 = $ar3">;
// An epilogue's move back to ar.pfs or ar.lc. What reads those registers
// names them, unlike what the other application registers control, so this
// has no side effects and may be scheduled among the epilogue's block.
let SchedRW = [WriteToAR], hasSideEffects = 0, isCodeGenOnly = 1 in
def MOV_AR_RESTORE : IA64AnyUnit<(outs AR:$ar3), (ins GR:$r2), "mov",
                                 "$ar3 = $r2">;

include "IA64InstrAliases.td"
