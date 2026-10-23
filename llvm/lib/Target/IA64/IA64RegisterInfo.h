//===-- IA64RegisterInfo.h - IA64 Register Information Impl -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the IA64 implementation of the TargetRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_IA64_IA64REGISTERINFO_H
#define LLVM_LIB_TARGET_IA64_IA64REGISTERINFO_H

#include "llvm/CodeGen/TargetRegisterInfo.h"

#define GET_REGINFO_HEADER
#include "IA64GenRegisterInfo.inc"

namespace llvm {

class IA64RegisterInfo : public IA64GenRegisterInfo {
public:
  IA64RegisterInfo();

  const MCPhysReg *getCalleeSavedRegs(const MachineFunction *MF) const override;
  const uint32_t *getCallPreservedMask(const MachineFunction &MF,
                                       CallingConv::ID) const override;
  /// The registers preserved across a call to a returns_twice function.
  const uint32_t *getReturnsTwiceCallPreservedMask() const;
  BitVector getReservedRegs(const MachineFunction &MF) const override;
  bool isCallerPreservedPhysReg(MCRegister PhysReg,
                                const MachineFunction &MF) const override;

  /// A preserved register costs a spill and a fill on every call, against an
  /// entry frequency of 1 << 14. With this cost a value live across a call
  /// that rarely runs, such as the sqrt that sets errno after an inline one,
  /// is split around the call in a scratch register instead.
  unsigned getCSRCost() const override { return 20; }

  bool eliminateFrameIndex(MachineBasicBlock::iterator MI, int SPAdj,
                           unsigned FIOperandNum,
                           RegScavenger *RS = nullptr) const override;

  Register getFrameRegister(const MachineFunction &MF) const override;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_IA64_IA64REGISTERINFO_H
