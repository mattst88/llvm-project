//===-- IA64MachineFunctionInfo.h - IA64 function information ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_IA64_IA64MACHINEFUNCTIONINFO_H
#define LLVM_LIB_TARGET_IA64_IA64MACHINEFUNCTIONINFO_H

#include "llvm/CodeGen/MachineFunction.h"

namespace llvm {

class IA64MachineFunctionInfo : public MachineFunctionInfo {
  // Locals where the prologue saves ar.pfs and the return address, for the
  // epilogue to restore.
  Register SavedPFS;
  Register SavedRP;

  // Whether the function writes ar.pfs after its alloc, and so must restore
  // the value the alloc saved.
  bool RestoresPFS = false;

  // The local holding ar.unat on entry, if the function spills a general
  // register and so modifies it.
  Register SavedUNAT;

  // Other caller state the function modifies and must restore: the
  // predicates, ar.lc, and which of b1-b5. The prologue saves each in a
  // local.
  bool ChangesPR = false;
  bool ChangesLC = false;
  unsigned ChangedBRs = 0;
  Register SavedPR;
  Register SavedLC;
  Register SavedBR[5];

  // The local holding the incoming stack pointer, if SP moves after the
  // prologue.
  Register FramePointer;

  // The local holding SP as the prologue leaves it, if frame objects are
  // addressed from it while SP moves.
  Register BasePointer;

  // Frame index of the first variadic argument, and whether any variadic
  // arguments were passed in registers and spilled there.
  int VarArgsFrameIndex = 0;
  bool SpillsVarArgs = false;

  // The sret pointer passed by the caller, which the function returns.
  Register SRetReg;

public:
  IA64MachineFunctionInfo(const Function &F, const TargetSubtargetInfo *STI) {}

  MachineFunctionInfo *
  clone(BumpPtrAllocator &Allocator, MachineFunction &DestMF,
        const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB)
      const override {
    return DestMF.cloneInfo<IA64MachineFunctionInfo>(*this);
  }

  Register getSavedPFS() const { return SavedPFS; }
  void setSavedPFS(Register Reg) { SavedPFS = Reg; }
  bool restoresPFS() const { return RestoresPFS; }
  void setRestoresPFS(bool Restores) { RestoresPFS = Restores; }
  Register getSavedRP() const { return SavedRP; }
  void setSavedRP(Register Reg) { SavedRP = Reg; }
  bool spillsVarArgs() const { return SpillsVarArgs; }
  void setSpillsVarArgs(bool Spills) { SpillsVarArgs = Spills; }
  Register getSavedUNAT() const { return SavedUNAT; }
  void setSavedUNAT(Register Reg) { SavedUNAT = Reg; }
  bool changesPR() const { return ChangesPR; }
  void setChangesPR(bool Changes) { ChangesPR = Changes; }
  bool changesLC() const { return ChangesLC; }
  void setChangesLC(bool Changes) { ChangesLC = Changes; }
  unsigned getChangedBRs() const { return ChangedBRs; }
  void setChangedBRs(unsigned Mask) { ChangedBRs = Mask; }
  Register getSavedPR() const { return SavedPR; }
  void setSavedPR(Register Reg) { SavedPR = Reg; }
  Register getSavedLC() const { return SavedLC; }
  void setSavedLC(Register Reg) { SavedLC = Reg; }
  Register getSavedBR(unsigned I) const { return SavedBR[I]; }
  void setSavedBR(unsigned I, Register Reg) { SavedBR[I] = Reg; }
  Register getFramePointer() const { return FramePointer; }
  void setFramePointer(Register Reg) { FramePointer = Reg; }
  Register getBasePointer() const { return BasePointer; }
  void setBasePointer(Register Reg) { BasePointer = Reg; }
  Register getSRetReg() const { return SRetReg; }
  void setSRetReg(Register Reg) { SRetReg = Reg; }
  int getVarArgsFrameIndex() const { return VarArgsFrameIndex; }
  void setVarArgsFrameIndex(int FI) { VarArgsFrameIndex = FI; }
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_IA64_IA64MACHINEFUNCTIONINFO_H
