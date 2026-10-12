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
let Entry = 1 in
def CC_IA64 : CallingConv<[
  // The static chain takes no slot. It goes in r15, where libgcc's
  // __ia64_trampoline puts it.
  CCIfNest<CCAssignToReg<[R15]>>,

  // Homogeneous floating-point aggregates (HFAs).
  CCIfByVal<CCCustom<"CC_IA64_ByVal">>,
  CCIfConsecutiveRegs<CCCustom<"CC_IA64_HFA">>,

  // An unnamed (varargs) FP argument is passed as its bits in the GR slot,
  // since the callee reads it from memory.
  CCIfArgVarArg<CCIfType<[f32, f64], CCBitConvertToType<i64>>>,

  CCIfType<[f80], CCCustom<"CC_IA64_Extended">>,

  // The sret pointer is not an argument and takes no slot. It goes in r8,
  // unless it is the result of a C++ function whose return type has a
  // non-trivial copy constructor or destructor.
  CCIfSRet<CCIfType<[i64], CCCustom<"CC_IA64_SRet">>>,

  // 16-byte aligned arguments start at an even slot.
  CCIfType<[i64], CCIfSplit<CCCustom<"CC_IA64_SkipOddSlot">>>,

  CCIfType<[i64], CCAssignToReg<[R32, R33, R34, R35, R36, R37, R38, R39]>>,

  // An FP argument still takes a slot, and while slots remain it goes in the
  // next of f8-f15 instead of the slot.
  CCIfType<[f32, f64], CCCustom<"CC_IA64_FP">>,

  // The rest go on the stack, above the scratch area.
  CCAssignToStack<8, 8>
]>;

// Integer results of up to four words are returned in r8-r11, and FP
// results, or HFAs of up to eight elements, in f8-f15.
let Entry = 1 in
def RetCC_IA64 : CallingConv<[
  CCIfType<[i64], CCAssignToReg<[R8, R9, R10, R11]>>,
  CCIfType<[f32, f64, f80],
           CCAssignToReg<[F8, F9, F10, F11, F12, F13, F14, F15]>>
]>;

// Stacked registers are preserved across calls by the RSE, not by the
// callee, so they are listed in the call-preserved mask rather than here.
//
// The unwinder computes the save location of memory-spilled registers
// solely from which ones are saved: GRs lowest, BRs above them, FRs above
// those, each kind in register number order, ending where the frame begins.
// The first register listed here gets the first slot, and slots are
// allocated downwards.
//
// The branch registers, predicates and ar.lc get no stack slot: a function
// that modifies one saves it in a local.
def CSR_IA64 : CalleeSavedRegs<(add (sequence "F%u", 31, 16),
                                    (sequence "F%u", 5, 2),
                                    (sequence "B%u", 5, 1),
                                    (sequence "R%u", 7, 4),
                                    (sequence "P%u", 1, 5),
                                    (sequence "P%u", 16, 63), AR65)>;

// The caller's inputs and locals are not visible to the callee.
def CSR_IA64_Call : CalleeSavedRegs<(add CSR_IA64,
                                         (sequence "R%u", 32, 119))>;

// A returns_twice function such as setjmp returns the second time with the
// caller's inputs and locals as last left, not as they were at the call. No
// value may be kept in one across such a call.
def CSR_IA64_ReturnsTwice : CalleeSavedRegs<(add CSR_IA64)>;
