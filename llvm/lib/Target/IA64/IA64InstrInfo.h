//===-- IA64InstrInfo.h - IA64 Instruction Information ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the IA64 implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_IA64_IA64INSTRINFO_H
#define LLVM_LIB_TARGET_IA64_IA64INSTRINFO_H

#include "IA64RegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

#define GET_INSTRINFO_HEADER
#include "IA64GenInstrInfo.inc"

namespace llvm {

class IA64Subtarget;

namespace IA64 {
// Machine instruction flags, which the assembly printer reads.
enum : uint8_t {
  // A stop follows the instruction.
  StopAfter = MachineInstr::TAsmComments,
  // A stop precedes the instruction, which IA64InsertStops records where it
  // needs one and IA64PackGroups keeps.
  StartsGroup = MachineInstr::TAsmComments << 1,
  // The instruction starts a cycle of a software-pipelined kernel, whose
  // schedule IA64InsertStops keeps by ending the group before it.
  StartsCycle = MachineInstr::TAsmComments << 2,
};

// Whether the opcode is a control-speculative integer load, ld.s.
bool isControlSpeculativeLoad(unsigned Opcode);
} // namespace IA64

namespace IA64CC {
// Compare conditions, integer then FP, then tests of the bit of the left
// operand that the right one, a constant, numbers, then tests of whether the
// left operand is NaT, the right one being 0, then the integer conditions
// again for compares of only the low 32 bits (cmp4). Each is next
// to its inverse; for FP that is the one that differs in its result for
// unordered operands.
enum CondCode : unsigned {
  EQ,
  NE,
  LT,
  GE,
  GT,
  LE,
  LTU,
  GEU,
  GTU,
  LEU,
  OEQ,
  UNE,
  OLT,
  UGE,
  OLE,
  UGT,
  OGT,
  ULE,
  OGE,
  ULT,
  UO,
  ORD,
  BitClear,
  BitSet,
  NotNaT,
  NaT,
  EQ4,
  NE4,
  LT4,
  GE4,
  GT4,
  LE4,
  LTU4,
  GEU4,
  GTU4,
  LEU4,
};

inline bool isFloatingPoint(CondCode CC) { return CC >= OEQ && CC <= ORD; }

inline bool isBitTest(CondCode CC) { return CC == BitClear || CC == BitSet; }

inline bool isNaTTest(CondCode CC) { return CC == NotNaT || CC == NaT; }

inline bool isWord(CondCode CC) { return CC >= EQ4; }

// The 32-bit form of an integer condition, and the 64-bit form of either.
inline CondCode getWord(CondCode CC) { return CondCode(CC - EQ + EQ4); }
inline CondCode getDoubleword(CondCode CC) {
  return isWord(CC) ? CondCode(CC - EQ4 + EQ) : CC;
}

inline CondCode getOpposite(CondCode CC) { return CondCode(CC ^ 1); }

// Whether a compare against the constant has an immediate form.
bool isLegalImmediate(CondCode CC, int64_t Imm);

// The parallel compare (or.andcm) testing CC against a register, if Imm is
// empty, or against the constant Imm; 0 if there is none. There are forms for
// equality against a register or an 8-bit immediate, for ordering against
// zero, which also covers ordering against its neighbors 1 and -1, and for
// bit tests. If CC has one, so does its opposite.
unsigned getParallelCompare(CondCode CC, std::optional<int64_t> Imm);

// The and-type compare testing CC, for the same conditions as
// getParallelCompare and for NaT tests; 0 if there is none. It clears its
// predicates if the condition does not hold or an operand is NaT.
unsigned getAndCompare(CondCode CC, std::optional<int64_t> Imm);
} // namespace IA64CC

class IA64InstrInfo : public IA64GenInstrInfo {
  const IA64RegisterInfo RI;

public:
  explicit IA64InstrInfo(const IA64Subtarget &STI);

  const IA64RegisterInfo &getRegisterInfo() const { return RI; }

  void copyPhysReg(MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
                   const DebugLoc &DL, Register DestReg, Register SrcReg,
                   bool KillSrc, bool RenamableDest = false,
                   bool RenamableSrc = false) const override;

  void storeRegToStackSlot(
      MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, Register SrcReg,
      bool IsKill, int FrameIndex, const TargetRegisterClass *RC, Register VReg,
      MachineInstr::MIFlag Flags = MachineInstr::NoFlags) const override;
  void loadRegFromStackSlot(
      MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, Register DestReg,
      int FrameIndex, const TargetRegisterClass *RC, Register VReg,
      unsigned SubReg = 0,
      MachineInstr::MIFlag Flags = MachineInstr::NoFlags) const override;

  Register isLoadFromStackSlot(const MachineInstr &MI,
                               int &FrameIndex) const override;
  Register isStoreToStackSlot(const MachineInstr &MI,
                              int &FrameIndex) const override;

  // Put a constant in a register with the shortest instruction that can.
  void loadImmediate(MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
                     const DebugLoc &DL, Register Reg, int64_t Imm,
                     MachineInstr::MIFlag Flags = MachineInstr::NoFlags) const;

  bool expandPostRAPseudo(MachineInstr &MI) const override;

  const MachineOperand &getCalleeOperand(const MachineInstr &MI) const override;

  MCInst getNop() const override;
  bool isFunctionSafeToSplit(const MachineFunction &MF) const override;

  // An operand's target flags are a single relocation specifier, never a
  // bitmask.
  std::pair<unsigned, unsigned>
  decomposeMachineOperandsTargetFlags(unsigned TF) const override {
    return {TF, 0};
  }
  ArrayRef<std::pair<unsigned, const char *>>
  getSerializableDirectMachineOperandTargetFlags() const override;

  bool isPredicated(const MachineInstr &MI) const override;
  bool isAsCheapAsAMove(const MachineInstr &MI) const override;
  bool isSafeToSink(MachineInstr &MI, MachineBasicBlock *SuccToSinkTo,
                    MachineCycleInfo *CI) const override;
  bool shouldSink(const MachineInstr &MI) const override;

  bool isSchedulingBoundary(const MachineInstr &MI,
                            const MachineBasicBlock *MBB,
                            const MachineFunction &MF) const override;

  bool analyzeBranch(MachineBasicBlock &MBB, MachineBasicBlock *&TBB,
                     MachineBasicBlock *&FBB,
                     SmallVectorImpl<MachineOperand> &Cond,
                     bool AllowModify) const override;
  unsigned removeBranch(MachineBasicBlock &MBB,
                        int *BytesRemoved = nullptr) const override;
  unsigned insertBranch(MachineBasicBlock &MBB, MachineBasicBlock *TBB,
                        MachineBasicBlock *FBB, ArrayRef<MachineOperand> Cond,
                        const DebugLoc &DL,
                        int *BytesAdded = nullptr) const override;
  bool
  reverseBranchCondition(SmallVectorImpl<MachineOperand> &Cond) const override;
  std::unique_ptr<PipelinerLoopInfo>
  analyzeLoopForPipelining(MachineBasicBlock *LoopBB) const override;
  // The pipeliner compares the addresses of accesses in different iterations
  // by their base register and how much it steps each iteration.
  bool getMemOperandsWithOffsetWidth(
      const MachineInstr &MI, SmallVectorImpl<const MachineOperand *> &BaseOps,
      int64_t &Offset, bool &OffsetIsScalable, LocationSize &Width,
      const TargetRegisterInfo *TRI) const override;
  bool getIncrementValue(const MachineInstr &MI, int &Value) const override;

  // Copying a block into the end of its predecessors saves each of them a
  // taken branch, which on Itanium costs more than a few instructions.
  unsigned getTailDuplicateSize(CodeGenOptLevel OptLevel) const override {
    return 9;
  }

  bool useMachineCombiner() const override { return true; }
  bool isAssociativeAndCommutative(const MachineInstr &Inst,
                                   bool Invert) const override;
  std::optional<unsigned> getInverseOpcode(unsigned Opcode) const override;
  bool getMachineCombinerPatterns(MachineInstr &Root,
                                  SmallVectorImpl<unsigned> &Patterns,
                                  bool DoRegPressureReduce) const override;
  CombinerObjective getCombinerObjective(unsigned Pattern) const override;
  void genAlternativeCodeSequence(
      MachineInstr &Root, unsigned Pattern,
      SmallVectorImpl<MachineInstr *> &InsInstrs,
      SmallVectorImpl<MachineInstr *> &DelInstrs,
      DenseMap<Register, unsigned> &InstrIdxForVirtReg) const override;

  unsigned getInstSizeInBytes(const MachineInstr &MI) const override;
  // Bundles are emitted whole, when full or ended, not as each instruction
  // is encoded, so the bytes emitted for an instruction do not reflect its
  // size.
  InstSizeVerifyMode
  getInstSizeVerifyMode(const MachineInstr &MI) const override {
    return InstSizeVerifyMode::NoVerify;
  }
  bool isBranchOffsetInRange(unsigned BranchOpc,
                             int64_t BrOffset) const override;
  MachineBasicBlock *getBranchDestBlock(const MachineInstr &MI) const override;
  void insertIndirectBranch(MachineBasicBlock &MBB,
                            MachineBasicBlock &NewDestBB,
                            MachineBasicBlock &RestoreBB, const DebugLoc &DL,
                            int64_t BrOffset, RegScavenger *RS) const override;

  /// Whether \p MI is a conditional branch on a compare result.
  static bool isConditionalBranch(const MachineInstr &MI);

  /// Whether \p Store, after \p Load in its block, overwrites some of what
  /// Load read into a general register, as far as alias analysis can tell. On
  /// Itanium 2 such a store cancels the fill of L1D that the load started,
  /// unless an instruction that reads the loaded value issued in a group before
  /// the store's.
  static bool cancelsFill(const MachineInstr &Load, const MachineInstr &Store,
                          AAResults *AA);

  /// How an FP compare treats unordered operands: as the instruction for its
  /// condition does, raising invalid only for a signaling NaN, or always
  /// raising invalid.
  enum CompareKind { PlainCompare, QuietCompare, SignalingCompare };

  /// Compare \p LHS with \p RHS, a register or a constant, before \p I.
  /// It leaves p6 set if \p CC holds and p7 if it does not.
  void emitCompare(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                   const DebugLoc &DL, IA64CC::CondCode CC,
                   const MachineOperand &LHS, const MachineOperand &RHS,
                   CompareKind Kind = PlainCompare) const;

  /// Compare \p LHS with \p RHS with a parallel compare before \p I: if
  /// \p CC holds, \p Set is set and \p Clear is cleared, and otherwise both
  /// are left as they are. Returns false and emits nothing if no parallel
  /// compare tests \p CC on these operands.
  bool emitParallelCompare(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator I, const DebugLoc &DL,
                           IA64CC::CondCode CC, const MachineOperand &LHS,
                           const MachineOperand &RHS, Register Set,
                           Register Clear) const;

  /// Compare \p LHS with \p RHS with an and-type compare before \p I,
  /// which clears \p Pred unless \p CC holds. Returns false and emits
  /// nothing if no and-type compare tests \p CC on these operands.
  bool emitAndCompare(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                      const DebugLoc &DL, IA64CC::CondCode CC,
                      const MachineOperand &LHS, const MachineOperand &RHS,
                      Register Pred) const;

  /// Emit parallel compares before \p MI for the compares whose operands
  /// start at operand \p First of \p MI, combining them into the predicates
  /// of a compare already emitted: with \p And, p6 stays set only if all of
  /// them hold, and otherwise it becomes set if any of them does. p7 stays
  /// the complement of p6.
  void emitChainCompares(MachineBasicBlock &MBB, MachineInstr &MI,
                         const DebugLoc &DL, unsigned First, bool And) const;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_IA64_IA64INSTRINFO_H
