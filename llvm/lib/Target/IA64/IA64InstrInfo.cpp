//===-- IA64InstrInfo.cpp - IA64 Instruction Information ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "IA64InstrInfo.h"
#include "IA64Subtarget.h"
#include "MCTargetDesc/IA64MCTargetDesc.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/Support/ErrorHandling.h"

#define GET_INSTRINFO_CTOR_DTOR
#include "IA64GenInstrInfo.inc"

using namespace llvm;

IA64InstrInfo::IA64InstrInfo(const IA64Subtarget &STI)
    : IA64GenInstrInfo(STI, RI, IA64::ADJCALLSTACKDOWN, IA64::ADJCALLSTACKUP),
      RI() {}

void IA64InstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator MI,
                                const DebugLoc &DL, Register DestReg,
                                Register SrcReg, bool KillSrc,
                                bool RenamableDest, bool RenamableSrc) const {
  // GR copy: adds r1 = 0, r2, which is what the "mov" pseudo-op assembles
  // to.
  if (IA64::GRRegClass.contains(DestReg, SrcReg)) {
    BuildMI(MBB, MI, DL, get(IA64::ADDS), DestReg)
        .addImm(0)
        .addReg(SrcReg, getKillRegState(KillSrc))
        .addReg(IA64::P0);
    return;
  }
  report_fatal_error("IA64 cannot copy between these registers yet");
}
