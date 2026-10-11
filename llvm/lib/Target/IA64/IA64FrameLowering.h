//===-- IA64FrameLowering.h - Define frame lowering for IA64 ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_IA64_IA64FRAMELOWERING_H
#define LLVM_LIB_TARGET_IA64_IA64FRAMELOWERING_H

#include "llvm/CodeGen/TargetFrameLowering.h"

namespace llvm {

class IA64Subtarget;

class IA64FrameLowering : public TargetFrameLowering {
public:
  explicit IA64FrameLowering(const IA64Subtarget &STI);

  void emitPrologue(MachineFunction &MF, MachineBasicBlock &MBB) const override;
  void emitEpilogue(MachineFunction &MF, MachineBasicBlock &MBB) const override;

  StackOffset getFrameIndexReference(const MachineFunction &MF, int FI,
                                     Register &FrameReg) const override;

  // The bytes at sp that belong to the callee.
  static constexpr unsigned ScratchAreaSize = 16;

protected:
  bool hasFPImpl(const MachineFunction &MF) const override;

private:
  void adjustStackPointer(MachineBasicBlock &MBB,
                          MachineBasicBlock::iterator MBBI, const DebugLoc &DL,
                          int64_t Amount, MachineInstr::MIFlag Flag) const;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_IA64_IA64FRAMELOWERING_H
