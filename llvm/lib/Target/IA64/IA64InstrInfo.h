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
};
} // namespace IA64

namespace IA64CC {
// Compare conditions, integer then FP. Each is next to its inverse; for FP
// that is the one that differs in its result for unordered operands.
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
};

inline bool isFloatingPoint(CondCode CC) { return CC >= OEQ; }

inline CondCode getOpposite(CondCode CC) { return CondCode(CC ^ 1); }

// Whether a compare against the constant has an immediate form.
bool isLegalImmediate(CondCode CC, int64_t Imm);

// The parallel compare (or.andcm) testing CC against a register, if Imm is
// empty, or against the constant Imm; 0 if there is none. There are forms for
// equality against a register or an 8-bit immediate, and for ordering against
// zero, which also covers ordering against its neighbors 1 and -1. If CC has
// one, so does its opposite.
unsigned getParallelCompare(CondCode CC, std::optional<int64_t> Imm);
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

  // An operand's target flags are a single relocation specifier, never a
  // bitmask.
  std::pair<unsigned, unsigned>
  decomposeMachineOperandsTargetFlags(unsigned TF) const override {
    return {TF, 0};
  }
  ArrayRef<std::pair<unsigned, const char *>>
  getSerializableDirectMachineOperandTargetFlags() const override;

  bool isPredicated(const MachineInstr &MI) const override;

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

  bool useMachineCombiner() const override { return true; }
  bool isAssociativeAndCommutative(const MachineInstr &Inst,
                                   bool Invert) const override;
  std::optional<unsigned> getInverseOpcode(unsigned Opcode) const override;

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
