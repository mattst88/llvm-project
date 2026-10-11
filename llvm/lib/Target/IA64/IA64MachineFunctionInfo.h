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

  // The local holding ar.unat on entry, if the function spills a general
  // register and so modifies it.
  Register SavedUNAT;

  // The local holding the incoming stack pointer, if SP moves after the
  // prologue.
  Register FramePointer;

  // The local holding SP as the prologue leaves it, if frame objects are
  // addressed from it while SP moves.
  Register BasePointer;

  // Where the arguments that have no names start.
  int VarArgsFrameIndex = 0;

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
  Register getSavedRP() const { return SavedRP; }
  void setSavedRP(Register Reg) { SavedRP = Reg; }
  Register getSavedUNAT() const { return SavedUNAT; }
  void setSavedUNAT(Register Reg) { SavedUNAT = Reg; }
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
