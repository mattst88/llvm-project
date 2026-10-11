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
// The conditions of an integer compare. Each is next to its opposite.
enum CondCode : unsigned { EQ, NE, LT, GE, GT, LE, LTU, GEU, GTU, LEU };

inline CondCode getOpposite(CondCode CC) { return CondCode(CC ^ 1); }

// Whether a compare against the constant has an immediate form.
bool isLegalImmediate(CondCode CC, int64_t Imm);
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

  bool expandPostRAPseudo(MachineInstr &MI) const override;

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

  /// Compare \p LHS with \p RHS, a register or a constant, before \p I.
  /// It leaves p6 set if \p CC holds and p7 if it does not.
  void emitCompare(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                   const DebugLoc &DL, IA64CC::CondCode CC, Register LHS,
                   const MachineOperand &RHS) const;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_IA64_IA64INSTRINFO_H
